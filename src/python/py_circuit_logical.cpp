#include "py_circuit.hpp"

#include "binding_diagnostic_conversions.hpp"
#include "binding_part_definition_conversions.hpp"
#include "binding_pcb_conversions.hpp"
#include "py_circuit_logical_helpers.hpp"
#include "py_part_library.hpp"

#include <map>
#include <ranges>
#include <set>
#include <string>
#include <utility>

#include <volt/authoring/component_library.hpp>
#include <volt/circuit/bom/bom.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/updates.hpp>
#include <volt/io/bom/bom_writer.hpp>
#include <volt/io/parts/footprint_asset.hpp>
#include <volt/io/parts/part_library_bundle.hpp>

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

[[nodiscard]] volt::ComponentSpec component_spec(const volt::Circuit &circuit,
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

[[nodiscard]] volt::ComponentContractSpec
python_standard_contract(const volt::ContentHash &standard_digest, std::size_t pin_count) {
    auto pin_keys = std::vector<volt::PinKey>{};
    pin_keys.reserve(pin_count);
    for (std::size_t index = 0; index < pin_count; ++index) {
        pin_keys.emplace_back("pin/" + std::to_string(index));
    }
    return volt::ComponentContractSpec{
        .key = volt::ComponentKey{"volt.python.component/" + standard_digest.value()},
        .pin_keys = std::move(pin_keys),
    };
}

[[nodiscard]] std::size_t define_python_component(volt::Circuit &circuit,
                                                  const volt::authoring::ComponentSpec &source) {
    auto temporary = volt::Circuit{};
    const auto temporary_id = volt::authoring::define_component(temporary, source);
    auto spec = component_spec(temporary, temporary_id);
    spec.contract =
        python_standard_contract(temporary.get(temporary_id).content_identity(), spec.pins.size());
    return circuit.define_component(std::move(spec)).index();
}

[[nodiscard]] std::optional<volt::PartFootprintPolygon>
part_polygon(const std::optional<volt::FootprintPolygon> &polygon) {
    if (!polygon.has_value()) {
        return std::nullopt;
    }
    auto points = std::vector<volt::PartFootprintPoint>{};
    points.reserve(polygon->vertices().size());
    for (const auto &point : polygon->vertices()) {
        points.emplace_back(point.x_mm(), point.y_mm());
    }
    return volt::PartFootprintPolygon{std::move(points)};
}

[[nodiscard]] volt::PartFootprintMarkingKind part_marking_kind(volt::FootprintMarkingKind kind) {
    switch (kind) {
    case volt::FootprintMarkingKind::Silkscreen:
        return volt::PartFootprintMarkingKind::Silkscreen;
    case volt::FootprintMarkingKind::Polarity:
        return volt::PartFootprintMarkingKind::Polarity;
    case volt::FootprintMarkingKind::PinOne:
        return volt::PartFootprintMarkingKind::PinOne;
    }
    throw volt::KernelLogicError{volt::ErrorCode::InvalidState,
                                 "Footprint marking kind is unsupported"};
}

[[nodiscard]] std::vector<volt::PartFootprintMarking>
part_markings(const volt::FootprintDefinition &footprint) {
    auto result = std::vector<volt::PartFootprintMarking>{};
    result.reserve(footprint.markings().size());
    for (const auto &marking : footprint.markings()) {
        result.emplace_back(part_marking_kind(marking.kind()),
                            *part_polygon(std::optional{marking.polygon()}));
    }
    return result;
}

[[nodiscard]] std::vector<volt::PartFootprintPad>
part_footprint_pads(const volt::FootprintDefinition &footprint) {
    auto result = std::vector<volt::PartFootprintPad>{};
    result.reserve(footprint.pads().size());
    for (const auto &pad : footprint.pads()) {
        if (!pad.mechanical_role().has_value()) {
            result.emplace_back(pad.label(), pad.position().x_mm(), pad.position().y_mm(),
                                pad.size().width_mm(), pad.size().height_mm());
        } else if (*pad.mechanical_role() == volt::FootprintPadMechanicalRole::Thermal) {
            result.emplace_back(pad.label(), pad.position().x_mm(), pad.position().y_mm(),
                                pad.size().width_mm(), pad.size().height_mm(),
                                volt::PartFootprintPadRole::Thermal);
        } else {
            result.emplace_back(pad.label(), pad.position().x_mm(), pad.position().y_mm(),
                                pad.size().width_mm(), pad.size().height_mm(),
                                volt::PartFootprintPadRole::Mechanical);
        }
    }
    return result;
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

std::size_t PyCircuit::define_resistor() {
    return define_python_component(circuit_, volt::authoring::resistor());
}

std::size_t PyCircuit::define_capacitor() {
    return define_python_component(circuit_, volt::authoring::capacitor());
}

std::size_t PyCircuit::define_polarized_capacitor() {
    return define_python_component(circuit_, volt::authoring::polarized_capacitor());
}

std::size_t PyCircuit::define_inductor() {
    return define_python_component(circuit_, volt::authoring::inductor());
}

std::size_t PyCircuit::define_diode() {
    return define_python_component(circuit_, volt::authoring::diode());
}

std::size_t PyCircuit::define_led() {
    return define_python_component(circuit_, volt::authoring::led());
}

std::size_t PyCircuit::define_switch_spst() {
    return define_python_component(circuit_, volt::authoring::switch_spst());
}

std::size_t PyCircuit::define_crystal_2pin() {
    return define_python_component(circuit_, volt::authoring::crystal_2pin());
}

std::size_t PyCircuit::define_test_point() {
    return define_python_component(circuit_, volt::authoring::test_point());
}

std::size_t PyCircuit::define_connector_1x01() {
    return define_python_component(circuit_, volt::authoring::connector_1x01());
}

std::size_t PyCircuit::define_connector_1x02() {
    return define_python_component(circuit_, volt::authoring::connector_1x02());
}

std::size_t PyCircuit::define_connector_1x03() {
    return define_python_component(circuit_, volt::authoring::connector_1x03());
}

std::size_t PyCircuit::define_regulator_3pin() {
    return define_python_component(circuit_, volt::authoring::regulator_3pin());
}

std::size_t PyCircuit::define_op_amp_5pin() {
    return define_python_component(circuit_, volt::authoring::op_amp_5pin());
}

std::size_t PyCircuit::define_component(const std::string &name, const py::list &pins,
                                        const py::dict &properties,
                                        const std::string &source_namespace,
                                        const std::string &source_name,
                                        const std::string &source_version,
                                        const py::list &schematic_symbols, py::object contract) {
    auto source = std::optional<volt::DefinitionSource>{};
    const auto wants_source =
        !source_namespace.empty() || !source_name.empty() || !source_version.empty();
    if (wants_source) {
        if (source_namespace.empty() || source_name.empty() || source_version.empty()) {
            throw py::value_error{
                "define_component source must include namespace, name, and version"};
        }
        source = volt::DefinitionSource{source_namespace, source_name, source_version};
    }

    auto spec = volt::ComponentSpec{
        .name = name,
        .pins = component_pin_specs_from_list(pins),
        .properties = properties_from_dict(properties),
        .source = source,
        .schematic_symbols = schematic_symbol_references_from_list(schematic_symbols),
        .contract =
            contract.is_none()
                ? std::nullopt
                : std::optional<volt::ComponentContractSpec>{component_contract_spec_from_dict(
                      py::cast<py::dict>(contract))},
    };
    if (!spec.contract.has_value()) {
        auto standard = volt::Circuit{};
        const auto standard_id = standard.define_component(spec);
        spec.contract = python_standard_contract(standard.get(standard_id).content_identity(),
                                                 spec.pins.size());
    }
    auto temporary = volt::Circuit{};
    const auto prospective = temporary.define_component(spec);
    const auto digest = temporary.get(prospective).content_identity();
    for (std::size_t index = 0; index < circuit_.all<volt::ComponentDefId>().size(); ++index) {
        if (circuit_.get(volt::ComponentDefId{index}).content_identity() == digest) {
            return index;
        }
    }
    return circuit_.define_component(std::move(spec)).index();
}

std::size_t PyCircuit::define_library_part(const PyPartLibrary &library,
                                           const std::string &part_key) {
    const auto reference = library.require(part_key);
    const auto &part = library.resolver().resolve(reference);
    for (std::size_t index = 0; index < circuit_.all<volt::ComponentDefId>().size(); ++index) {
        const auto definition = volt::ComponentDefId{index};
        if (circuit_.get(definition).content_identity() == part.implemented_component()) {
            return index;
        }
    }
    const auto definition = circuit_.define_component(library.component_spec(part_key));
    if (circuit_.get(definition).content_identity() != part.implemented_component()) {
        throw volt::KernelLogicError{volt::ErrorCode::CrossReferenceViolation,
                                     "Python lowering changed the exact component contract"};
    }
    return definition.index();
}

void PyCircuit::select_library_part(std::size_t component, const PyPartLibrary &library,
                                    const std::string &part_key) {
    const auto reference = library.require(part_key);
    const auto owner = library.bundle_owner();
    auto prospective = circuit_;
    for (std::size_t index = 0; index < prospective.all<volt::ComponentId>().size(); ++index) {
        const auto &existing = prospective.get(volt::ComponentId{index});
        if (existing.selected_library_part_ref().has_value()) {
            static_cast<void>(owner->resolve(*existing.selected_library_part_ref()));
        }
    }
    prospective.update(component_id(component), volt::SelectLibraryPart{*owner, reference});
    circuit_ = std::move(prospective);
    selected_part_bundle_ = owner;
}

py::list PyCircuit::validate_selected_part_erc(const PyPartLibrary &library) const {
    return diagnostics_to_list(volt::validate_selected_part_erc(circuit_, library.resolver()));
}

std::size_t PyCircuit::add_net(const std::string &name, const std::string &kind) {
    return circuit_
        .add_net(volt::NetSpec{.name = volt::NetName{name}, .kind = parse_net_kind(kind)})
        .index();
}

std::size_t PyCircuit::add_net_class(const std::string &name, const py::dict &options) {
    auto net_class = volt::NetClass{volt::NetClassName{name}};

    const auto current = optional_double_field(options, "current");
    if (current.has_value()) {
        const auto temperature_rise = optional_double_field(options, "temp_rise").value_or(10.0);
        const auto copper_weight = optional_double_field(options, "copper_weight").value_or(1.0);
        const auto environment = optional_string_field(options, "environment").value_or("external");
        net_class.derive_track_width(volt::ipc2221_trace_width_from_current_mm(
            current.value(), temperature_rise, copper_weight,
            parse_trace_environment(environment)));
    }

    const auto voltage = optional_double_field(options, "voltage");
    const auto dielectric_height = optional_double_field(options, "dielectric_height");
    if (voltage.has_value() && dielectric_height.has_value()) {
        throw py::value_error{"Specify only one derived net-class clearance source per net class"};
    }
    if (voltage.has_value()) {
        net_class.derive_copper_clearance(
            volt::ipc2221_external_voltage_clearance_mm(voltage.value()));
    }
    if (dielectric_height.has_value()) {
        const auto rule = optional_string_field(options, "spacing_rule").value_or("microstrip_2h");
        net_class.derive_copper_clearance(volt::dielectric_height_spacing_mm(
            dielectric_height.value(), parse_dielectric_spacing_rule(rule)));
    }

    if (const auto track_width = optional_double_field(options, "track_width")) {
        net_class.set_track_width_mm(track_width.value());
    }
    const auto via_drill = optional_double_field(options, "via_drill");
    const auto via_diameter = optional_double_field(options, "via_diameter");
    if (via_drill.has_value() != via_diameter.has_value()) {
        throw py::value_error{"Specify both via_drill and via_diameter for net-class via sizing"};
    }
    if (via_drill.has_value()) {
        net_class.set_via_size_mm(via_drill.value(), via_diameter.value());
    }
    if (const auto clearance = optional_double_field(options, "clearance")) {
        net_class.set_copper_clearance_mm(clearance.value());
    }
    if (const auto priority = optional_double_field(options, "priority")) {
        net_class.set_priority(static_cast<int>(priority.value()));
    }
    if (const auto default_kind = optional_string_field(options, "default_for")) {
        net_class.set_default_for_net_kind(parse_net_kind(default_kind.value()));
    }
    if (const auto layer_scope = optional_string_field(options, "layer_scope")) {
        net_class.set_layer_scope(parse_net_class_layer_scope(layer_scope.value()));
    }

    return circuit_.define_net_class(volt::NetClassSpec{std::move(net_class)}).index();
}

void PyCircuit::assign_net_class(const std::vector<std::size_t> &nets, std::size_t net_class) {
    const auto target_class = volt::NetClassId{net_class};
    static_cast<void>(circuit_.get(target_class));

    auto targets = std::vector<volt::NetId>{};
    targets.reserve(nets.size());
    for (const auto net : nets) {
        const auto target = net_id(net);
        static_cast<void>(circuit_.get(target));
        targets.push_back(target);
    }

    for (const auto target : targets) {
        circuit_.update(target, volt::AssignNetClass{target_class});
    }
}

py::dict PyCircuit::net_class_info(std::size_t net_class) const {
    const auto id = volt::NetClassId{net_class};
    const auto &rule = circuit_.get(id);
    auto result = py::dict{};
    result["index"] = id.index();
    result["name"] = rule.name().value();
    result["track_width_mm"] =
        rule.track_width_mm().has_value() ? py::cast(rule.track_width_mm().value()) : py::none{};
    result["copper_clearance_mm"] = rule.copper_clearance_mm().has_value()
                                        ? py::cast(rule.copper_clearance_mm().value())
                                        : py::none{};
    result["via_drill_mm"] =
        rule.via_drill_mm().has_value() ? py::cast(rule.via_drill_mm().value()) : py::none{};
    result["via_diameter_mm"] =
        rule.via_diameter_mm().has_value() ? py::cast(rule.via_diameter_mm().value()) : py::none{};
    if (rule.derived_track_width().has_value()) {
        result["derived_track_width"] = derived_rule_to_dict(rule.derived_track_width().value());
    } else {
        result["derived_track_width"] = py::none{};
    }
    if (rule.derived_copper_clearance().has_value()) {
        result["derived_copper_clearance"] =
            derived_rule_to_dict(rule.derived_copper_clearance().value());
    } else {
        result["derived_copper_clearance"] = py::none{};
    }
    return result;
}

py::list PyCircuit::net_refs() const {
    auto result = py::list{};
    for (std::size_t index = 0; index < circuit_.all<volt::NetId>().size(); ++index) {
        const auto id = volt::NetId{index};
        const auto &net = circuit_.get(id);
        auto item = py::dict{};
        item["index"] = id.index();
        item["name"] = net.name().value();
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::component_refs() const {
    auto result = py::list{};
    for (std::size_t index = 0; index < circuit_.all<volt::ComponentId>().size(); ++index) {
        const auto id = volt::ComponentId{index};
        const auto &component = circuit_.get(id);
        auto item = py::dict{};
        item["index"] = id.index();
        item["reference"] = component.reference().value();
        result.append(std::move(item));
    }
    return result;
}

void PyCircuit::select_authored_part(std::size_t component, const std::string &manufacturer,
                                     const std::string &part_number, const std::string &package,
                                     const py::dict &footprint_payload, const py::dict &pin_pads,
                                     std::optional<double> voltage_rating, py::object model_3d,
                                     py::object model_3d_bytes,
                                     py::object approved_alternate_mpns) {
    for (std::size_t index = 0; index < circuit_.all<volt::ComponentId>().size(); ++index) {
        if (circuit_.get(volt::ComponentId{index}).selected_library_part_ref().has_value() &&
            (selected_part_bundle_->identity().namespace_name() != authored_library_namespace ||
             selected_part_bundle_->identity().version() != authored_library_version)) {
            throw volt::KernelLogicError{
                volt::ErrorCode::InvalidState,
                "A Design cannot mix an external exact library closure with authored parts"};
        }
    }
    const auto component_handle = component_id(component);
    const auto &instance = circuit_.get(component_handle);
    const auto &definition = circuit_.get(instance.definition());
    const auto footprint = footprint_definition_from_dict(footprint_payload);
    const auto footprint_bytes = volt::io::write_footprint_asset(footprint);

    auto pads_by_pin = std::map<std::size_t, std::vector<std::string>>{};

    for (const auto item : pin_pads) {
        const auto key = string_from_pin_key(item.first);
        auto pin = std::optional<volt::PinId>{};
        if (py::isinstance<py::int_>(item.first)) {
            pin = queries::pin_by_number(circuit_, component_handle, key);
        } else {
            const auto matches = pins_by_name(component_handle, key);
            if (matches.size() > 1) {
                throw std::invalid_argument{"Component pin name is ambiguous"};
            }
            if (!matches.empty()) {
                pin = matches.front();
            }
        }
        if (!pin.has_value()) {
            throw std::out_of_range{"Component has no pin with that name or number"};
        }
        const auto pin_definition = circuit_.get(pin.value()).definition();
        const auto definition_match = std::ranges::find(definition.pins(), pin_definition);
        if (definition_match == definition.pins().end()) {
            throw volt::KernelLogicError{volt::ErrorCode::CrossReferenceViolation,
                                         "Selected part pin belongs to another component"};
        }
        const auto pin_index =
            static_cast<std::size_t>(definition_match - definition.pins().begin());
        const auto [unused, inserted] =
            pads_by_pin.emplace(pin_index, pad_labels_from_value(item.second));
        if (!inserted) {
            throw volt::KernelArgumentError{volt::ErrorCode::DuplicateName,
                                            "Selected part maps one logical pin more than once"};
        }
    }

    auto pin_terminal_mappings = std::vector<volt::PinPackageTerminalMapping>{};
    auto terminal_pad_mappings = std::vector<volt::PackageTerminalPadMapping>{};
    pin_terminal_mappings.reserve(definition.pins().size());
    terminal_pad_mappings.reserve(definition.pins().size());
    for (std::size_t index = 0; index < definition.pins().size(); ++index) {
        const auto mapped = pads_by_pin.find(index);
        if (mapped == pads_by_pin.end()) {
            throw volt::KernelArgumentError{
                volt::ErrorCode::CrossReferenceViolation,
                "Every selected component PinKey must map to footprint pads"};
        }
        const auto &pin = circuit_.get(definition.pins()[index]);
        auto pads = std::vector<volt::FootprintPadKey>{};
        pads.reserve(mapped->second.size());
        for (const auto &pad : mapped->second) {
            pads.emplace_back(pad);
        }
        pin_terminal_mappings.emplace_back(definition.contract().pin_keys()[index],
                                           std::vector{volt::PackageTerminalKey{pin.number()}});
        terminal_pad_mappings.emplace_back(volt::PackageTerminalKey{pin.number()}, std::move(pads));
    }

    auto records = std::vector<volt::ElectricalRecordSpec>{};
    if (voltage_rating.has_value()) {
        require_finite(*voltage_rating, "Selected-part voltage rating must be finite");
        if (definition.contract().pin_keys().size() != 2U) {
            throw volt::KernelArgumentError{
                volt::ErrorCode::InvalidArgument,
                "voltage_rating requires an explicitly oriented two-pin exact part"};
        }
        records.push_back(volt::voltage_record(
            volt::ElectricalSubject::directed_relation(volt::ElectricalPinIndex{0},
                                                       volt::ElectricalPinIndex{1}),
            volt::ElectricalMeaning::AbsoluteLimit,
            volt::ElectricalValue{volt::QuantityRange::maximum(
                volt::Quantity{volt::UnitDimension::Voltage, *voltage_rating})}));
    }

    auto model_reference = std::optional<volt::PartModel3DReference>{};
    auto assets = std::vector<AuthoredPartAsset>{};
    assets.push_back(AuthoredPartAsset{
        volt::PartAssetKind::Footprint,
        "footprint:" + footprint.ref().library() + "/" + footprint.ref().name(), footprint_bytes});
    if (model_3d.is_none() != model_3d_bytes.is_none()) {
        throw volt::KernelArgumentError{volt::ErrorCode::InvalidArgument,
                                        "3D model metadata and bytes must be supplied together"};
    }
    if (!model_3d.is_none()) {
        const auto metadata = part_model_3d_from_object(model_3d).value();
        const auto bytes = static_cast<std::string>(py::cast<py::bytes>(model_3d_bytes));
        model_reference = volt::PartModel3DReference{
            metadata.format(), metadata.file_name(), volt::sha256_content_hash(bytes),
            metadata.translation_mm(), metadata.rotation_deg()};
        assets.push_back(
            AuthoredPartAsset{volt::PartAssetKind::Model3D,
                              "model:" + metadata.format() + "/" + metadata.file_name(), bytes});
    }

    const auto key = volt::PartKey{"component-" + std::to_string(component_handle.index())};
    auto draft = AuthoredPartDraft{
        key, component_spec(circuit_, instance.definition()),
        volt::PartDefinition{
            definition,
            volt::PartIdentity{authored_library_namespace, key.value(), authored_library_version},
            volt::ElectricalRecordSet{definition.contract().pin_keys().size(), std::move(records)},
            std::move(pin_terminal_mappings),
            {},
            volt::PartProvenance{},
            {},
            volt::OrderablePart{
                volt::ManufacturerPart{manufacturer, part_number}, volt::PackageRef{package},
                volt::HashedFootprintReference{footprint.ref(),
                                               volt::sha256_content_hash(footprint_bytes)},
                part_footprint_pads(footprint), std::move(terminal_pad_mappings),
                strings_from_iterable(approved_alternate_mpns,
                                      "approved_alternate_mpns must be iterable"),
                std::move(model_reference), part_polygon(footprint.courtyard()),
                part_polygon(footprint.body()), part_polygon(footprint.fabrication_outline()),
                part_polygon(footprint.assembly_outline()), part_markings(footprint)}},
        std::move(assets)};

    auto prospective_drafts = std::map<std::size_t, AuthoredPartDraft>{};
    for (std::size_t index = 0; index < circuit_.all<volt::ComponentId>().size(); ++index) {
        const auto existing_component = volt::ComponentId{index};
        const auto &existing = circuit_.get(existing_component);
        if (existing_component == component_handle ||
            !existing.selected_library_part_ref().has_value()) {
            continue;
        }
        const auto &existing_part =
            selected_part_bundle_->resolve(*existing.selected_library_part_ref());
        auto existing_assets = std::vector<AuthoredPartAsset>{};
        for (const auto &reference : volt::part_asset_references(existing_part)) {
            const auto bytes = selected_part_bundle_->asset(reference);
            if (!bytes.has_value()) {
                throw volt::KernelRangeError{
                    volt::ErrorCode::UnknownEntity,
                    "Selected authored part asset is absent from its retained exact closure",
                    volt::EntityRef::component(existing_component)};
            }
            existing_assets.push_back(
                AuthoredPartAsset{reference.kind(), reference.key(), std::string{*bytes}});
        }
        prospective_drafts.emplace(
            index, AuthoredPartDraft{existing.selected_library_part_ref()->part_key(),
                                     component_spec(circuit_, existing.definition()), existing_part,
                                     std::move(existing_assets)});
    }
    prospective_drafts.insert_or_assign(component_handle.index(), std::move(draft));
    auto builder = volt::PartLibraryBuilder{volt::PartLibraryIdentity{
        authored_library_namespace, authored_library_version, volt::PartLibrarySchemaVersion::V1}};
    auto resolver = AuthoredAssetResolver{};
    auto component_digests = std::map<std::string, volt::ContentHash>{};
    auto selected = std::vector<volt::PartKey>{};
    for (const auto &[unused_component, candidate] : prospective_drafts) {
        auto component_check = volt::Circuit{};
        const auto definition_check = component_check.define_component(candidate.component);
        const auto &lowered = component_check.get(definition_check);
        const auto component_key = lowered.contract().key().value();
        const auto [existing, inserted] =
            component_digests.emplace(component_key, lowered.content_identity());
        if (inserted) {
            builder.add_component(candidate.component);
        } else if (existing->second != lowered.content_identity()) {
            throw volt::KernelLogicError{
                volt::ErrorCode::CrossReferenceViolation,
                "Authored component key resolves to conflicting component content"};
        }
        builder.add_part(candidate.part);
        selected.push_back(candidate.key);
        for (const auto &asset : candidate.assets) {
            resolver.add(asset.kind, asset.key, asset.bytes);
        }
    }
    auto bundle = std::make_shared<const volt::io::PartLibraryBundle>(
        volt::io::PartLibraryBundle::build(builder, selected, resolver));

    auto prospective_circuit = circuit_;
    for (const auto &[selected_component, candidate] : prospective_drafts) {
        const auto reference = bundle->require(candidate.key);
        prospective_circuit.update(volt::ComponentId{selected_component},
                                   volt::SelectLibraryPart{*bundle, reference});
    }
    circuit_ = std::move(prospective_circuit);
    selected_part_bundle_ = std::move(bundle);
}

void PyCircuit::set_component_quantity(std::size_t component, const std::string &name,
                                       const std::string &dimension_name, double value) {
    require_finite(value, "Electrical attribute quantities must be finite");
    const auto dimension = parse_dimension(dimension_name);
    circuit_.update(component_id(component),
                    volt::SetComponentElectricalAttribute{
                        component_quantity_spec(name, dimension),
                        volt::ElectricalAttributeValue{volt::Quantity{dimension, value}}});
}

void PyCircuit::set_component_percent_tolerance(std::size_t component, double value) {
    require_finite(value, "Tolerance values must be finite");
    circuit_.update(component_id(component),
                    volt::SetComponentElectricalAttribute{
                        component_quantity_spec("tolerance", volt::UnitDimension::Ratio),
                        volt::ElectricalAttributeValue{volt::Tolerance::percent(value)}});
}

void PyCircuit::set_net_quantity(std::size_t net, const std::string &name,
                                 const std::string &dimension_name, double value) {
    require_finite(value, "Electrical attribute quantities must be finite");
    const auto dimension = parse_dimension(dimension_name);
    circuit_.update(net_id(net),
                    volt::SetNetElectricalAttribute{
                        net_quantity_spec(name, dimension),
                        volt::ElectricalAttributeValue{volt::Quantity{dimension, value}}});
}

std::size_t PyCircuit::instantiate_ref(std::size_t definition, const std::string &reference,
                                       const py::dict &properties) {
    return volt::authoring::instantiate(circuit_, component_def_id(definition),
                                        volt::ReferenceDesignator{reference},
                                        properties_from_dict(properties))
        .index();
}

std::size_t PyCircuit::instantiate_auto(std::size_t definition, const std::string &prefix,
                                        const py::dict &properties) {
    return volt::authoring::instantiate(circuit_, component_def_id(definition), prefix,
                                        properties_from_dict(properties))
        .index();
}

std::size_t PyCircuit::pin_by_name(std::size_t component, const std::string &name) const {
    const auto matches = pins_by_name(component_id(component), name);
    if (matches.empty()) {
        throw std::out_of_range{"Component has no pin with that name"};
    }
    if (matches.size() > 1) {
        throw std::invalid_argument{"Component pin name is ambiguous"};
    }

    return matches.front().index();
}

std::size_t PyCircuit::pin_by_number(std::size_t component, const std::string &number) const {
    const auto pin = queries::pin_by_number(circuit_, component_id(component), number);
    if (!pin.has_value()) {
        throw std::out_of_range{"Component has no pin with that number"};
    }

    return pin.value().index();
}

std::size_t PyCircuit::pin_component(std::size_t pin) const {
    return circuit_.get(pin_id(pin)).component().index();
}

std::string PyCircuit::component_reference(std::size_t component) const {
    return circuit_.get(component_id(component)).reference().value();
}

py::list PyCircuit::pin_refs(std::size_t component) const {
    auto result = py::list{};
    for (const auto pin : queries::pins_for(circuit_, component_id(component))) {
        const auto &definition = circuit_.get(circuit_.get(pin).definition());
        auto item = py::dict{};
        item["index"] = pin.index();
        item["name"] = definition.name();
        item["number"] = definition.number();
        result.append(std::move(item));
    }
    return result;
}

std::optional<std::string> PyCircuit::component_schematic_symbol(std::size_t component,
                                                                 const std::string &variant) const {
    const auto component_handle = component_id(component);
    const auto &definition = circuit_.get(circuit_.get(component_handle).definition());
    for (const auto &symbol : definition.schematic_symbols()) {
        if (symbol.variant() == variant) {
            return symbol.name();
        }
    }
    return std::nullopt;
}

void PyCircuit::connect(std::size_t net, std::size_t pin) {
    circuit_.connect(net_id(net), pin_id(pin));
}

std::optional<std::size_t> PyCircuit::net_of(std::size_t pin) const {
    const auto net = queries::net_of(circuit_, pin_id(pin));
    if (!net.has_value()) {
        return std::nullopt;
    }
    return net.value().index();
}

py::list PyCircuit::net_pins(std::size_t net) const {
    auto result = py::list{};
    for (const auto pin : circuit_.get(net_id(net)).pins()) {
        result.append(pin.index());
    }
    return result;
}

void PyCircuit::mark_intentional_stub_net(std::size_t net) {
    circuit_.update(net_id(net), volt::MarkIntentionalStub{});
}

void PyCircuit::mark_intentional_no_connect_pin(std::size_t pin) {
    circuit_.mark_no_connect(pin_id(pin));
}

void PyCircuit::set_component_dnp(std::size_t component, bool dnp) {
    circuit_.update(component_id(component), volt::SetAssemblyIntent{.dnp = dnp});
}

void PyCircuit::set_component_selection_override(std::size_t component, bool selection_override) {
    circuit_.update(component_id(component),
                    volt::SetAssemblyIntent{.selection_override = selection_override});
}

std::vector<volt::PinId> PyCircuit::pins_by_name(volt::ComponentId component,
                                                 const std::string &name) const {
    auto result = std::vector<volt::PinId>{};
    for (const auto pin : queries::pins_for(circuit_, component)) {
        const auto definition = circuit_.get(pin).definition();
        if (circuit_.get(definition).name() == name) {
            result.push_back(pin);
        }
    }
    return result;
}

} // namespace volt::python
