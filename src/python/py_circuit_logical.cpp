#include "py_circuit.hpp"

#include "py_circuit_helpers.hpp"

#include <volt/circuit/bom/bom.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/io/bom/bom_writer.hpp>

namespace volt::python {

std::size_t PyCircuit::define_resistor() {
    return volt::authoring::define_component(circuit_, volt::authoring::resistor()).index();
}

std::size_t PyCircuit::define_capacitor() {
    return volt::authoring::define_component(circuit_, volt::authoring::capacitor()).index();
}

std::size_t PyCircuit::define_polarized_capacitor() {
    return volt::authoring::define_component(circuit_, volt::authoring::polarized_capacitor())
        .index();
}

std::size_t PyCircuit::define_inductor() {
    return volt::authoring::define_component(circuit_, volt::authoring::inductor()).index();
}

std::size_t PyCircuit::define_diode() {
    return volt::authoring::define_component(circuit_, volt::authoring::diode()).index();
}

std::size_t PyCircuit::define_led() {
    return volt::authoring::define_component(circuit_, volt::authoring::led()).index();
}

std::size_t PyCircuit::define_switch_spst() {
    return volt::authoring::define_component(circuit_, volt::authoring::switch_spst()).index();
}

std::size_t PyCircuit::define_crystal_2pin() {
    return volt::authoring::define_component(circuit_, volt::authoring::crystal_2pin()).index();
}

std::size_t PyCircuit::define_test_point() {
    return volt::authoring::define_component(circuit_, volt::authoring::test_point()).index();
}

std::size_t PyCircuit::define_connector_1x01() {
    return volt::authoring::define_component(circuit_, volt::authoring::connector_1x01()).index();
}

std::size_t PyCircuit::define_connector_1x02() {
    return volt::authoring::define_component(circuit_, volt::authoring::connector_1x02()).index();
}

std::size_t PyCircuit::define_connector_1x03() {
    return volt::authoring::define_component(circuit_, volt::authoring::connector_1x03()).index();
}

std::size_t PyCircuit::define_regulator_3pin() {
    return volt::authoring::define_component(circuit_, volt::authoring::regulator_3pin()).index();
}

std::size_t PyCircuit::define_op_amp_5pin() {
    return volt::authoring::define_component(circuit_, volt::authoring::op_amp_5pin()).index();
}

std::size_t PyCircuit::define_component(const std::string &name, const py::list &pins,
                                        const py::dict &properties,
                                        const std::string &source_namespace,
                                        const std::string &source_name,
                                        const std::string &source_version,
                                        const py::list &schematic_symbols) {
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

    return volt::authoring::define_component(
               circuit_,
               volt::authoring::ComponentSpec{
                   name, pin_specs_from_list(pins), properties_from_dict(properties), source,
                   schematic_symbol_references_from_list(schematic_symbols)})
        .index();
}

std::size_t PyCircuit::add_net(const std::string &name, const std::string &kind) {
    return circuit_.add_net(volt::Net{volt::NetName{name}, parse_net_kind(kind)}).index();
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

    return circuit_.add_net_class(std::move(net_class)).index();
}

void PyCircuit::assign_net_class(std::size_t net, std::size_t net_class) {
    static_cast<void>(circuit_.assign_net_class(net_id(net), volt::NetClassId{net_class}));
}

py::dict PyCircuit::net_class_info(std::size_t net_class) const {
    const auto id = volt::NetClassId{net_class};
    const auto &rule = circuit_.net_class(id);
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
    for (std::size_t index = 0; index < circuit_.net_count(); ++index) {
        const auto id = volt::NetId{index};
        const auto &net = circuit_.net(id);
        auto item = py::dict{};
        item["index"] = id.index();
        item["name"] = net.name().value();
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::component_refs() const {
    auto result = py::list{};
    for (std::size_t index = 0; index < circuit_.component_count(); ++index) {
        const auto id = volt::ComponentId{index};
        const auto &component = circuit_.component(id);
        auto item = py::dict{};
        item["index"] = id.index();
        item["reference"] = component.reference().value();
        result.append(std::move(item));
    }
    return result;
}

py::object PyCircuit::component_selected_part_model_3d(std::size_t component) const {
    const auto component_handle = component_id(component);
    static_cast<void>(circuit_.component(component_handle));
    const auto &selected_part = circuit_.selected_physical_part(component_handle);
    if (!selected_part.has_value()) {
        return py::none{};
    }
    return part_model_3d_to_object(selected_part->model_3d());
}

void PyCircuit::select_physical_part(std::size_t component, const std::string &manufacturer,
                                     const std::string &part_number, const std::string &package,
                                     const std::string &footprint_library,
                                     const std::string &footprint_name, const py::dict &pin_pads,
                                     const py::dict &properties, py::object model_3d,
                                     py::object approved_alternate_mpns) {
    const auto component_handle = component_id(component);
    auto mappings = std::vector<volt::PinPadMapping>{};
    mappings.reserve(static_cast<std::size_t>(py::len(pin_pads)));

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
        const auto pin_definition = circuit_.pin(pin.value()).definition();
        for (const auto &pad : pad_labels_from_value(item.second)) {
            mappings.emplace_back(pin_definition, pad);
        }
    }

    circuit_.select_physical_part(
        component_handle,
        volt::PhysicalPart{
            volt::ManufacturerPart{manufacturer, part_number}, volt::PackageRef{package},
            volt::FootprintRef{footprint_library, footprint_name}, std::move(mappings),
            properties_from_dict(properties), part_model_3d_from_object(model_3d),
            strings_from_iterable(approved_alternate_mpns,
                                  "approved_alternate_mpns must be iterable")});
}

void PyCircuit::set_component_quantity(std::size_t component, const std::string &name,
                                       const std::string &dimension_name, double value) {
    require_finite(value, "Electrical attribute quantities must be finite");
    const auto dimension = parse_dimension(dimension_name);
    circuit_.set_component_electrical_attribute(
        component_id(component), component_quantity_spec(name, dimension),
        volt::ElectricalAttributeValue{volt::Quantity{dimension, value}});
}

void PyCircuit::set_component_percent_tolerance(std::size_t component, double value) {
    require_finite(value, "Tolerance values must be finite");
    circuit_.set_component_electrical_attribute(
        component_id(component), component_quantity_spec("tolerance", volt::UnitDimension::Ratio),
        volt::ElectricalAttributeValue{volt::Tolerance::percent(value)});
}

void PyCircuit::set_net_quantity(std::size_t net, const std::string &name,
                                 const std::string &dimension_name, double value) {
    require_finite(value, "Electrical attribute quantities must be finite");
    const auto dimension = parse_dimension(dimension_name);
    circuit_.set_net_electrical_attribute(
        net_id(net), net_quantity_spec(name, dimension),
        volt::ElectricalAttributeValue{volt::Quantity{dimension, value}});
}

void PyCircuit::select_generic_physical_part(std::size_t component) {
    const auto component_handle = component_id(component);
    const auto &definition =
        circuit_.component_definition(circuit_.component(component_handle).definition());
    auto mappings = std::vector<volt::PinPadMapping>{};
    mappings.reserve(definition.pins().size());
    for (std::size_t index = 0; index < definition.pins().size(); ++index) {
        mappings.emplace_back(definition.pins()[index], std::to_string(index + 1));
    }
    circuit_.select_physical_part(
        component_handle,
        volt::PhysicalPart{volt::ManufacturerPart{"Volt", "generic"},
                           volt::PackageRef{"unspecified"},
                           volt::FootprintRef{"volt.generic", "unspecified"}, std::move(mappings)});
}

void PyCircuit::set_selected_part_quantity(std::size_t component, const std::string &name,
                                           const std::string &dimension_name, double value) {
    require_finite(value, "Electrical attribute quantities must be finite");
    const auto dimension = parse_dimension(dimension_name);
    circuit_.set_selected_part_electrical_attribute(
        component_id(component), selected_part_quantity_spec(name, dimension),
        volt::ElectricalAttributeValue{volt::Quantity{dimension, value}});
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
    return circuit_.pin(pin_id(pin)).component().index();
}

std::string PyCircuit::component_reference(std::size_t component) const {
    return circuit_.component(component_id(component)).reference().value();
}

py::list PyCircuit::pin_refs(std::size_t component) const {
    auto result = py::list{};
    for (const auto pin : queries::pins_for(circuit_, component_id(component))) {
        const auto &definition = circuit_.pin_definition(circuit_.pin(pin).definition());
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
    const auto &definition =
        circuit_.component_definition(circuit_.component(component_handle).definition());
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
    for (const auto pin : circuit_.net(net_id(net)).pins()) {
        result.append(pin.index());
    }
    return result;
}

void PyCircuit::mark_intentional_stub_net(std::size_t net) {
    static_cast<void>(circuit_.mark_intentional_stub_net(net_id(net)));
}

void PyCircuit::mark_intentional_no_connect_pin(std::size_t pin) {
    static_cast<void>(circuit_.mark_intentional_no_connect_pin(pin_id(pin)));
}

void PyCircuit::set_component_dnp(std::size_t component, bool dnp) {
    circuit_.set_component_dnp(component_id(component), dnp);
}

void PyCircuit::set_component_selection_override(std::size_t component, bool selection_override) {
    circuit_.set_component_selection_override(component_id(component), selection_override);
}

std::vector<volt::PinId> PyCircuit::pins_by_name(volt::ComponentId component,
                                                 const std::string &name) const {
    auto result = std::vector<volt::PinId>{};
    for (const auto pin : queries::pins_for(circuit_, component)) {
        const auto definition = circuit_.pin(pin).definition();
        if (circuit_.pin_definition(definition).name() == name) {
            result.push_back(pin);
        }
    }
    return result;
}

} // namespace volt::python
