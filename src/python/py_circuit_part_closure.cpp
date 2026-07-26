#include "py_circuit_part_closure.hpp"

#include <map>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/updates.hpp>
#include <volt/core/errors.hpp>

namespace volt::python {
namespace {

constexpr auto authored_library_namespace = "volt.python.design";
constexpr auto authored_library_version = "1";

[[nodiscard]] volt::ComponentContractSpec contract_spec(const volt::ComponentContract &contract) {
    return volt::ComponentContractSpec{contract.key(),
                                       contract.pin_keys(),
                                       contract.framed_pins(),
                                       contract.relations(),
                                       contract.supply_domains(),
                                       contract.feature_schemas(),
                                       contract.feature_bindings()};
}

[[nodiscard]] std::string asset_key(volt::PartAssetKind kind, const std::string &key) {
    return std::to_string(static_cast<unsigned int>(kind)) + ":" + key;
}

class AuthoredAssetResolver final : public volt::PartAssetResolver {
  public:
    void add(volt::PartAssetKind kind, const std::string &key, const std::string &bytes) {
        const auto [match, inserted] = assets_.try_emplace(asset_key(kind, key), bytes);
        if (!inserted && match->second != bytes) {
            throw volt::KernelLogicError{volt::ErrorCode::CrossReferenceViolation,
                                         "Authored part asset key has conflicting bytes"};
        }
    }

    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &reference) const override {
        const auto match = assets_.find(asset_key(reference.kind(), reference.key()));
        if (match == assets_.end()) {
            return std::nullopt;
        }
        return match->second;
    }

  private:
    std::map<std::string, std::string> assets_;
};

} // namespace

volt::ComponentSpec component_spec(const volt::Circuit &circuit,
                                   volt::ComponentDefId definition_id) {
    const auto &definition = circuit.get(definition_id);
    auto pins = std::vector<volt::PinSpec>{};
    pins.reserve(definition.pins().size());
    for (const auto pin_id : definition.pins()) {
        const auto &pin = circuit.get(pin_id);
        auto attributes = std::vector<volt::ElectricalAttributeAssignment>{};
        attributes.reserve(pin.electrical_attributes().size());
        for (const auto &[name, value] : pin.electrical_attributes().entries()) {
            attributes.push_back(volt::ElectricalAttributeAssignment{
                volt::ElectricalAttributeSpec{name, volt::ElectricalAttributeOwner::PinSpec,
                                              volt::ElectricalAttributeKind::Constraint,
                                              value.dimension()},
                value});
        }
        pins.push_back(volt::PinSpec{pin.name(), pin.number(), pin.connection_requirement(),
                                     pin.terminal_kind(), pin.direction(), pin.signal_domain(),
                                     pin.drive_kind(), pin.polarity(), std::move(attributes)});
    }
    return volt::ComponentSpec{
        .name = definition.name(),
        .pins = std::move(pins),
        .properties = definition.properties(),
        .source = definition.source(),
        .schematic_symbols = definition.schematic_symbols(),
        .contract = definition.contract().explicitly_authored()
                        ? std::optional{contract_spec(definition.contract())}
                        : std::nullopt,
    };
}

std::optional<std::string>
PyCircuitPartClosure::symbol(const volt::SchematicSymbolReference &reference) const {
    const auto key = "symbol:" + reference.name() + "@" + reference.variant();
    const auto entry = std::ranges::find_if(bundle_->entries(), [&](const auto &candidate) {
        return candidate.role() == volt::io::PartLibraryBundleEntryRole::Symbol &&
               candidate.source_key() == std::optional{key};
    });
    if (entry == bundle_->entries().end()) {
        return std::nullopt;
    }
    const auto bytes = bundle_->asset(
        volt::PartAssetReference{volt::PartAssetKind::Schematic, key, entry->digest()});
    return bytes.has_value() ? std::optional<std::string>{*bytes} : std::nullopt;
}

volt::ComponentDefId PyCircuitPartClosure::commit_authored_definition(
    volt::Circuit &circuit, volt::ComponentSpec definition,
    std::span<const AuthoredSymbolInput> additional_symbols) {
    auto candidate = volt::Circuit{};
    const auto candidate_id = candidate.define_component(definition);
    const auto &candidate_definition = candidate.get(candidate_id);
    for (std::size_t index = 0; index < circuit.all<volt::ComponentDefId>().size(); ++index) {
        const auto existing_id = volt::ComponentDefId{index};
        const auto &existing = circuit.get(existing_id);
        if (existing.content_identity() != candidate_definition.content_identity()) {
            continue;
        }
        if (existing.source().has_value()) {
            const auto retained = bundle().library().component(existing.contract().key());
            if (!retained.has_value() ||
                retained->get().content_identity() != existing.content_identity()) {
                throw volt::KernelLogicError{volt::ErrorCode::InvalidState,
                                             "A Design cannot add authored definitions to an "
                                             "external exact library closure"};
            }
        }
        for (const auto &[reference, bytes] : additional_symbols) {
            const auto retained = symbol(reference);
            if (retained.has_value() && *retained != bytes) {
                throw volt::KernelLogicError{
                    volt::ErrorCode::CrossReferenceViolation,
                    "Schematic symbol identity resolves to conflicting authoritative bytes"};
            }
        }
        return existing_id;
    }

    const auto &previous_bundle = bundle();
    auto selected_components = std::map<std::size_t, volt::PartKey>{};
    auto selected_parts = std::map<volt::PartKey, const volt::PartDefinition *>{};
    for (std::size_t index = 0; index < circuit.all<volt::ComponentId>().size(); ++index) {
        const auto &component = circuit.get(volt::ComponentId{index});
        if (!component.selected_library_part_ref().has_value()) {
            continue;
        }
        if (previous_bundle.identity().namespace_name() != authored_library_namespace ||
            previous_bundle.identity().version() != authored_library_version) {
            throw volt::KernelLogicError{
                volt::ErrorCode::InvalidState,
                "A Design cannot add authored definitions to an external exact library closure"};
        }
        const auto &reference = *component.selected_library_part_ref();
        const auto &part = previous_bundle.resolve(reference);
        selected_components.emplace(index, reference.part_key());
        selected_parts.try_emplace(reference.part_key(), &part);
    }

    auto builder = volt::PartLibraryBuilder{volt::PartLibraryIdentity{
        authored_library_namespace, authored_library_version, volt::PartLibrarySchemaVersion::V1}};
    auto resolver = AuthoredAssetResolver{};
    auto roots = std::vector<volt::ComponentKey>{};
    auto attachments = std::vector<volt::io::PartLibraryBundleComponentAttachment>{};
    auto admit_definition = [&](const volt::Circuit &owner, volt::ComponentDefId definition_id) {
        const auto &admitted_definition = owner.get(definition_id);
        builder.add_component(component_spec(owner, definition_id));
        if (!admitted_definition.source().has_value()) {
            return;
        }
        roots.push_back(admitted_definition.contract().key());
        for (const auto &symbol_reference : admitted_definition.schematic_symbols()) {
            if (symbol_reference.variant() != "default") {
                continue;
            }
            const auto additional =
                std::ranges::find_if(additional_symbols, [&](const auto &input) {
                    return input.first == symbol_reference;
                });
            const auto retained = symbol(symbol_reference);
            if (additional != additional_symbols.end() && retained.has_value() &&
                additional->second != *retained) {
                throw volt::KernelLogicError{
                    volt::ErrorCode::CrossReferenceViolation,
                    "Schematic symbol identity resolves to conflicting authoritative bytes"};
            }
            const auto bytes = additional != additional_symbols.end()
                                   ? std::optional<std::string>{additional->second}
                                   : retained;
            if (!bytes.has_value()) {
                throw volt::KernelRangeError{
                    volt::ErrorCode::UnknownEntity,
                    "External component definition has no authoritative default symbol bytes"};
            }
            const auto key = "symbol:" + symbol_reference.name() + "@" + symbol_reference.variant();
            resolver.add(volt::PartAssetKind::Schematic, key, *bytes);
            attachments.emplace_back(admitted_definition.contract().key(),
                                     volt::PartAssetReference{volt::PartAssetKind::Schematic, key,
                                                              volt::sha256_content_hash(*bytes)});
        }
    };
    for (std::size_t index = 0; index < circuit.all<volt::ComponentDefId>().size(); ++index) {
        admit_definition(circuit, volt::ComponentDefId{index});
    }
    admit_definition(candidate, candidate_id);

    auto selected = std::vector<volt::PartKey>{};
    selected.reserve(selected_parts.size());
    for (const auto &[key, part] : selected_parts) {
        builder.add_part(*part);
        selected.push_back(key);
        for (const auto &reference : volt::part_asset_references(*part)) {
            const auto bytes = previous_bundle.asset(reference);
            if (!bytes.has_value()) {
                throw volt::KernelRangeError{
                    volt::ErrorCode::UnknownEntity,
                    "Selected authored part asset is absent from its retained exact closure"};
            }
            resolver.add(reference.kind(), reference.key(), std::string{*bytes});
        }
    }

    auto rebuilt = std::make_shared<const volt::io::PartLibraryBundle>(
        volt::io::PartLibraryBundle::build_with_component_roots(builder, selected, roots, resolver,
                                                                {}, attachments));
    auto rebound = std::vector<std::pair<volt::ComponentId, volt::LibraryPartRef>>{};
    rebound.reserve(selected_components.size());
    for (const auto &[component, key] : selected_components) {
        rebound.emplace_back(volt::ComponentId{component}, rebuilt->require(key));
    }

    const auto committed = circuit.define_component(std::move(definition));
    for (const auto &[component, reference] : rebound) {
        circuit.update(component, volt::SelectLibraryPart{*rebuilt, reference});
    }
    bundle_ = std::move(rebuilt);
    return committed;
}

} // namespace volt::python
