#include "py_circuit.hpp"

#include "binding_diagnostic_conversions.hpp"
#include "binding_part_definition_conversions.hpp"
#include "binding_schematic_conversions.hpp"
#include "py_circuit_logical_helpers.hpp"
#include "py_circuit_part_closure.hpp"
#include "py_part_library.hpp"

#include <map>
#include <string>
#include <utility>

#include <volt/authoring/component_library.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/updates.hpp>
#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/io/schematic/schematic_writer.hpp>

namespace volt::python {
namespace {

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

[[nodiscard]] volt::ComponentSpec
python_component_spec(const volt::authoring::ComponentSpec &source) {
    auto temporary = volt::Circuit{};
    const auto temporary_id = volt::authoring::define_component(temporary, source);
    auto spec = component_spec(temporary, temporary_id);
    spec.contract =
        python_standard_contract(temporary.get(temporary_id).content_identity(), spec.pins.size());
    return spec;
}

} // namespace

std::size_t PyCircuit::define_builtin_component(const volt::authoring::ComponentSpec &source) {
    auto spec = python_component_spec(source);
    auto symbols = std::vector<PyCircuitPartClosure::AuthoredSymbolInput>{};
    for (const auto &reference : spec.schematic_symbols) {
        const auto symbol = built_in_symbol(reference.name());
        if (!symbol.has_value()) {
            throw volt::KernelRangeError{
                volt::ErrorCode::UnknownEntity,
                "Built-in component symbol has no authoritative native definition"};
        }
        symbols.emplace_back(reference, volt::io::write_symbol_definition(*symbol));
    }
    return selected_part_bundle_->commit_authored_definition(circuit_, std::move(spec), symbols)
        .index();
}

std::size_t PyCircuit::define_resistor() {
    return define_builtin_component(volt::authoring::resistor());
}

std::size_t PyCircuit::define_capacitor() {
    return define_builtin_component(volt::authoring::capacitor());
}

std::size_t PyCircuit::define_polarized_capacitor() {
    return define_builtin_component(volt::authoring::polarized_capacitor());
}

std::size_t PyCircuit::define_inductor() {
    return define_builtin_component(volt::authoring::inductor());
}

std::size_t PyCircuit::define_diode() { return define_builtin_component(volt::authoring::diode()); }

std::size_t PyCircuit::define_led() { return define_builtin_component(volt::authoring::led()); }

std::size_t PyCircuit::define_switch_spst() {
    return define_builtin_component(volt::authoring::switch_spst());
}

std::size_t PyCircuit::define_crystal_2pin() {
    return define_builtin_component(volt::authoring::crystal_2pin());
}

std::size_t PyCircuit::define_test_point() {
    return define_builtin_component(volt::authoring::test_point());
}

std::size_t PyCircuit::define_connector_1x01() {
    return define_builtin_component(volt::authoring::connector_1x01());
}

std::size_t PyCircuit::define_connector_1x02() {
    return define_builtin_component(volt::authoring::connector_1x02());
}

std::size_t PyCircuit::define_connector_1x03() {
    return define_builtin_component(volt::authoring::connector_1x03());
}

std::size_t PyCircuit::define_regulator_3pin() {
    return define_builtin_component(volt::authoring::regulator_3pin());
}

std::size_t PyCircuit::define_op_amp_5pin() {
    return define_builtin_component(volt::authoring::op_amp_5pin());
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
    auto symbol_assets = std::vector<std::pair<volt::SchematicSymbolReference, std::string>>{};
    symbol_assets.reserve(static_cast<std::size_t>(py::len(schematic_symbols)));
    for (const auto item : schematic_symbols) {
        const auto payload = py::cast<py::dict>(item);
        auto symbol = symbol_definition_from_dict(payload);
        const auto variant = optional_string_field(payload, "variant", "default");
        symbol_assets.emplace_back(volt::SchematicSymbolReference{symbol.name(), variant},
                                   volt::io::write_symbol_definition(symbol));
    }
    if (!spec.contract.has_value()) {
        auto standard = volt::Circuit{};
        const auto standard_id = standard.define_component(spec);
        spec.contract = python_standard_contract(standard.get(standard_id).content_identity(),
                                                 spec.pins.size());
    }
    return selected_part_bundle_
        ->commit_authored_definition(circuit_, std::move(spec), symbol_assets)
        .index();
}

std::optional<std::string>
PyCircuit::retained_symbol_asset(const volt::SchematicSymbolReference &reference) const {
    return selected_part_bundle_->symbol(reference);
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
    selected_part_bundle_->replace_bundle(owner);
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
