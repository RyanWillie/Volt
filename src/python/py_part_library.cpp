#include "py_part_library.hpp"

#include "binding_diagnostic_conversions.hpp"
#include "binding_part_definition_conversions.hpp"

#include <map>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>

#include <volt/core/errors.hpp>
#include <volt/io/parts/part_definition_writer.hpp>
#include <volt/io/parts/part_library_bundle.hpp>

namespace volt::python {
namespace {

[[nodiscard]] std::string asset_key(volt::PartAssetKind kind, const std::string &key) {
    return std::to_string(static_cast<int>(kind)) + ":" + key;
}

[[nodiscard]] volt::PartAssetKind asset_kind_from_string(const std::string &kind) {
    if (kind == "schematic") {
        return volt::PartAssetKind::Schematic;
    }
    if (kind == "footprint") {
        return volt::PartAssetKind::Footprint;
    }
    if (kind == "model_3d") {
        return volt::PartAssetKind::Model3D;
    }
    throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                    "Part asset kind is unsupported"};
}

[[nodiscard]] std::string asset_kind_name(volt::PartAssetKind kind) {
    switch (kind) {
    case volt::PartAssetKind::Schematic:
        return "schematic";
    case volt::PartAssetKind::Footprint:
        return "footprint";
    case volt::PartAssetKind::Model3D:
        return "model_3d";
    case volt::PartAssetKind::Simulation:
        return "simulation";
    case volt::PartAssetKind::Evidence:
        return "evidence";
    case volt::PartAssetKind::Licence:
        return "licence";
    case volt::PartAssetKind::Provenance:
        return "provenance";
    }
    throw volt::KernelLogicError{volt::ErrorCode::InvalidArgument,
                                 "Part asset kind is unsupported"};
}

[[nodiscard]] std::string bundle_role_name(volt::io::PartLibraryBundleEntryRole role) {
    switch (role) {
    case volt::io::PartLibraryBundleEntryRole::ComponentDefinition:
        return "component_definition";
    case volt::io::PartLibraryBundleEntryRole::PartDefinition:
        return "part_definition";
    case volt::io::PartLibraryBundleEntryRole::LibraryPartReference:
        return "library_part_reference";
    case volt::io::PartLibraryBundleEntryRole::Symbol:
        return "symbol";
    case volt::io::PartLibraryBundleEntryRole::Footprint:
        return "footprint";
    case volt::io::PartLibraryBundleEntryRole::Model3D:
        return "model_3d";
    case volt::io::PartLibraryBundleEntryRole::Simulation:
        return "simulation";
    case volt::io::PartLibraryBundleEntryRole::Evidence:
        return "evidence";
    case volt::io::PartLibraryBundleEntryRole::Licence:
        return "licence";
    case volt::io::PartLibraryBundleEntryRole::Provenance:
        return "provenance";
    }
    throw volt::KernelLogicError{volt::ErrorCode::InvalidArgument,
                                 "PartLibraryBundle entry role is unsupported"};
}

class PayloadAssetResolver final : public volt::PartAssetResolver {
  public:
    void add(const py::dict &asset) {
        const auto kind = asset_kind_from_string(required_string_field(asset, "kind"));
        const auto key = required_string_field(asset, "key");
        const auto bytes = static_cast<std::string>(py::cast<py::bytes>(asset["bytes"]));
        const auto [match, inserted] = assets_.try_emplace(asset_key(kind, key), bytes);
        if (!inserted && match->second != bytes) {
            throw volt::KernelLogicError{volt::ErrorCode::CrossReferenceViolation,
                                         "Part asset key resolves to conflicting bytes"};
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

struct BuiltLibrary {
    volt::PartLibraryBuilder builder;
    PayloadAssetResolver resolver;
    volt::PartLibrary library;
    std::optional<volt::io::PartLibraryBundle> selected_bundle;
    std::map<std::string, volt::ComponentSpec> component_specs;
};

[[nodiscard]] BuiltLibrary build_library(const std::string &namespace_name,
                                         const std::string &version, const py::list &parts,
                                         bool selected_bundle) {
    auto builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{namespace_name, version, volt::PartLibrarySchemaVersion::V1}};
    auto resolver = PayloadAssetResolver{};
    auto specs = std::map<std::string, volt::ComponentSpec>{};
    auto component_digests = std::map<std::string, volt::ContentHash>{};
    auto selected = std::vector<volt::PartKey>{};

    for (const auto item : parts) {
        const auto payload = py::cast<py::dict>(item);
        auto lowered = lower_part_definition_from_dict(payload);
        const auto component_key = lowered.component.contract().key().value();
        const auto digest = lowered.component.content_identity();
        const auto existing = component_digests.find(component_key);
        if (existing == component_digests.end()) {
            builder.add_component(lowered.component_spec);
            component_digests.emplace(component_key, digest);
        } else if (existing->second != digest) {
            throw volt::KernelLogicError{volt::ErrorCode::CrossReferenceViolation,
                                         "ComponentKey resolves to conflicting component content"};
        }
        const auto part_key = lowered.part.identity().name();
        selected.emplace_back(part_key);
        specs.emplace(part_key, std::move(lowered.component_spec));
        builder.add_part(std::move(lowered.part));
        for (const auto asset_item : required_list_field(payload, "assets")) {
            resolver.add(py::cast<py::dict>(asset_item));
        }
    }
    auto bundle = std::optional<volt::io::PartLibraryBundle>{};
    if (selected_bundle) {
        bundle.emplace(volt::io::PartLibraryBundle::build(builder, selected, resolver));
    }
    auto library = builder.build(resolver);
    return BuiltLibrary{std::move(builder), std::move(resolver), std::move(library),
                        std::move(bundle), std::move(specs)};
}

} // namespace

struct PyPartLibrary::State {
    explicit State(BuiltLibrary built)
        : builder{std::move(built.builder)}, resolver{std::move(built.resolver)},
          library{std::move(built.library)}, selected_bundle{std::move(built.selected_bundle)},
          component_specs{std::move(built.component_specs)} {}

    volt::PartLibraryBuilder builder;
    PayloadAssetResolver resolver;
    volt::PartLibrary library;
    std::optional<volt::io::PartLibraryBundle> selected_bundle;
    std::map<std::string, volt::ComponentSpec> component_specs;
};

PyPartLibrary::PyPartLibrary(std::string namespace_name, std::string version, const py::list &parts,
                             bool selected_bundle) {
    try {
        auto built = build_library(namespace_name, version, parts, selected_bundle);
        state_ = std::make_shared<State>(std::move(built));
    } catch (const volt::KernelError &) {
        throw;
    } catch (const std::invalid_argument &error) {
        throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument, error.what()};
    } catch (const std::out_of_range &error) {
        throw volt::KernelRangeError{volt::ErrorCode::UnknownEntity, error.what()};
    }
}

const volt::PartLibrary &PyPartLibrary::library() const noexcept { return state_->library; }

const volt::io::PartLibraryBundle &PyPartLibrary::bundle() const {
    if (!state_->selected_bundle.has_value()) {
        throw volt::KernelLogicError{volt::ErrorCode::InvalidState,
                                     "Part snapshot was not built as a selected P6 closure"};
    }
    return *state_->selected_bundle;
}

const volt::ExactPartResolver &PyPartLibrary::resolver() const noexcept {
    if (state_->selected_bundle.has_value()) {
        return *state_->selected_bundle;
    }
    return state_->library;
}

volt::LibraryPartRef PyPartLibrary::require(const std::string &part_key) const {
    const auto key = volt::PartKey{part_key};
    if (state_->selected_bundle.has_value()) {
        return state_->selected_bundle->require(key);
    }
    return state_->library.require(key);
}

std::shared_ptr<const volt::io::PartLibraryBundle> PyPartLibrary::bundle_owner() const {
    static_cast<void>(bundle());
    return {state_, &*state_->selected_bundle};
}

const volt::ComponentSpec &PyPartLibrary::component_spec(const std::string &part_key) const {
    const auto match = state_->component_specs.find(part_key);
    if (match == state_->component_specs.end()) {
        throw volt::KernelRangeError{volt::ErrorCode::UnknownEntity,
                                     "Part library has no requested component lowering"};
    }
    return match->second;
}

py::dict library_part_reference_to_dict(const volt::LibraryPartRef &reference) {
    auto result = py::dict{};
    result["library_namespace"] = reference.library_namespace();
    result["library_version"] = reference.library_version();
    result["part_key"] = reference.part_key().value();
    result["library_digest"] = reference.library_digest().value();
    result["part_digest"] = reference.part_digest().value();
    return result;
}

py::dict PyPartLibrary::part_result(const std::string &part_key) const {
    const auto reference = require(part_key);
    const auto &part = resolver().resolve(reference);
    auto result = py::dict{};
    const auto bytes = volt::io::write_part_definition(part);
    result["bytes"] = py::bytes{bytes};
    result["sha256"] = volt::io::part_definition_content_hash(part).value();
    result["component_sha256"] = part.implemented_component().value();
    result["diagnostics"] = diagnostics_to_list(volt::validate_part_lineup(part));
    result["exact_reference"] = exact_reference(part_key);
    return result;
}

py::dict PyPartLibrary::exact_reference(const std::string &part_key) const {
    return library_part_reference_to_dict(require(part_key));
}

std::string PyPartLibrary::digest() const { return resolver().reference_digest().value(); }

py::bytes PyPartLibrary::bundle_bytes() const {
    if (state_->selected_bundle.has_value()) {
        return py::bytes{state_->selected_bundle->bytes()};
    }
    auto selected = std::vector<volt::PartKey>{};
    selected.reserve(state_->library.parts().size());
    for (const auto &part : state_->library.parts()) {
        selected.emplace_back(part.identity().name());
    }
    const auto bundle =
        volt::io::PartLibraryBundle::build(state_->builder, selected, state_->resolver);
    return py::bytes{bundle.bytes()};
}

PyPartLibraryBundle::PyPartLibraryBundle(std::string bytes)
    : bundle_{volt::io::PartLibraryBundle::open(bytes)} {}

std::string PyPartLibraryBundle::digest() const { return bundle_.digest().value(); }

std::string PyPartLibraryBundle::library_digest() const { return bundle_.library_digest().value(); }

py::dict PyPartLibraryBundle::inspect() const {
    auto entries = py::list{};
    for (const auto &entry : bundle_.entries()) {
        auto item = py::dict{};
        item["id"] = entry.id();
        item["role"] = bundle_role_name(entry.role());
        item["path"] = entry.path();
        item["digest"] = entry.digest().value();
        item["semantic_identity"] = entry.semantic_identity().has_value()
                                        ? py::cast(entry.semantic_identity()->value())
                                        : py::none();
        item["source_key"] =
            entry.source_key().has_value() ? py::cast(*entry.source_key()) : py::none();
        auto dependencies = py::list{};
        for (const auto &dependency : entry.dependencies()) {
            dependencies.append(dependency);
        }
        item["dependencies"] = std::move(dependencies);
        entries.append(std::move(item));
    }
    auto result = py::dict{};
    result["format"] = std::string{volt::io::part_library_bundle_format_name()};
    result["schema_version"] = 1;
    result["producer_version"] = 1;
    result["bundle_digest"] = digest();
    result["library_digest"] = library_digest();
    auto library = py::dict{};
    library["namespace"] = bundle_.library().identity().namespace_name();
    library["version"] = bundle_.library().identity().version();
    result["library"] = std::move(library);
    result["entries"] = std::move(entries);
    return result;
}

py::list PyPartLibraryBundle::part_keys() const {
    auto result = py::list{};
    for (const auto &part : bundle_.library().parts()) {
        result.append(part.identity().name());
    }
    return result;
}

py::dict PyPartLibraryBundle::part_result(const std::string &part_key) const {
    const auto reference = bundle_.require(volt::PartKey{part_key});
    const auto &part = bundle_.resolve(reference);
    auto result = py::dict{};
    const auto bytes = volt::io::write_part_definition(part);
    result["bytes"] = py::bytes{bytes};
    result["sha256"] = volt::io::part_definition_content_hash(part).value();
    result["component_sha256"] = part.implemented_component().value();
    result["diagnostics"] = diagnostics_to_list(volt::validate_part_lineup(part));
    result["exact_reference"] = library_part_reference_to_dict(reference);
    return result;
}

py::list PyPartLibraryBundle::part_assets(const std::string &part_key) const {
    const auto reference = bundle_.require(volt::PartKey{part_key});
    const auto &part = bundle_.resolve(reference);
    auto result = py::list{};
    for (const auto &asset : volt::part_asset_references(part)) {
        const auto bytes = bundle_.asset(asset);
        if (!bytes.has_value()) {
            throw volt::KernelLogicError{volt::ErrorCode::UnknownEntity,
                                         "PartLibraryBundle is missing a required part asset"};
        }
        auto item = py::dict{};
        item["kind"] = asset_kind_name(asset.kind());
        item["key"] = asset.key();
        item["sha256"] = asset.digest().value();
        item["bytes"] = py::bytes{*bytes};
        result.append(std::move(item));
    }
    return result;
}

} // namespace volt::python
