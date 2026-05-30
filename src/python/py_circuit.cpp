#include "py_circuit.hpp"

#include "binding_pcb_conversions.hpp"

#include <volt/io/pcb_svg_writer.hpp>
#include <volt/io/pcb_writer.hpp>

namespace volt::python {

PyCircuit::PyCircuit() : circuit_{}, schematic_document_{circuit_} {}

namespace {

[[nodiscard]] std::optional<std::size_t> optional_index_from_py(py::handle value,
                                                                const char *message) {
    if (value.is_none()) {
        return std::nullopt;
    }
    try {
        return py::cast<std::size_t>(value);
    } catch (const py::cast_error &) {
        throw py::type_error{message};
    }
}

[[nodiscard]] volt::SchematicEndpoint schematic_endpoint_from_tuple(const py::tuple &endpoint) {
    if (py::len(endpoint) != 4U) {
        throw py::value_error{"Schematic endpoint payloads must contain x, y, pin, and port net"};
    }

    const auto x = py::cast<double>(endpoint[0]);
    const auto y = py::cast<double>(endpoint[1]);
    require_finite(x, "Schematic coordinates must be finite");
    require_finite(y, "Schematic coordinates must be finite");

    const auto pin = optional_index_from_py(endpoint[2], "Schematic endpoint pins must be indexes");
    const auto port_net =
        optional_index_from_py(endpoint[3], "Schematic endpoint port nets must be indexes");
    if (pin.has_value() && port_net.has_value()) {
        throw py::value_error{"Schematic endpoints cannot reference both a pin and a port net"};
    }

    const auto point = volt::Point{x, y};
    if (pin.has_value()) {
        return volt::SchematicEndpoint{point, pin_id(pin.value())};
    }
    if (port_net.has_value()) {
        return volt::SchematicEndpoint::port(point, net_id(port_net.value()));
    }
    return volt::SchematicEndpoint{point};
}

[[nodiscard]] std::vector<volt::SchematicEndpoint>
schematic_endpoints_from_list(const py::list &endpoints) {
    auto result = std::vector<volt::SchematicEndpoint>{};
    result.reserve(static_cast<std::size_t>(py::len(endpoints)));
    for (const auto item : endpoints) {
        result.push_back(schematic_endpoint_from_tuple(py::cast<py::tuple>(item)));
    }
    return result;
}

[[nodiscard]] py::tuple schematic_entity_result(std::size_t index, volt::NetId net) {
    return py::make_tuple(index, net.index());
}

[[noreturn]] void raise_schematic_authoring_error(const std::invalid_argument &error) {
    throw py::value_error{error.what()};
}

} // namespace

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

void PyCircuit::select_physical_part(std::size_t component, const std::string &manufacturer,
                                     const std::string &part_number, const std::string &package,
                                     const std::string &footprint_library,
                                     const std::string &footprint_name, const py::dict &pin_pads,
                                     const py::dict &properties) {
    const auto component_handle = component_id(component);
    auto mappings = std::vector<volt::PinPadMapping>{};
    mappings.reserve(static_cast<std::size_t>(py::len(pin_pads)));

    for (const auto item : pin_pads) {
        const auto key = string_from_pin_key(item.first);
        const auto pad = py::cast<std::string>(item.second);
        auto pin = std::optional<volt::PinId>{};
        if (py::isinstance<py::int_>(item.first)) {
            pin = circuit_.pin_by_number(component_handle, key);
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
        mappings.emplace_back(circuit_.pin(pin.value()).definition(), pad);
    }

    circuit_.select_physical_part(
        component_handle,
        volt::PhysicalPart{volt::ManufacturerPart{manufacturer, part_number},
                           volt::PackageRef{package},
                           volt::FootprintRef{footprint_library, footprint_name},
                           std::move(mappings), properties_from_dict(properties)});
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
    const auto pin = circuit_.pin_by_number(component_id(component), number);
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
    for (const auto pin : circuit_.pins_for(component_id(component))) {
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
    const auto net = circuit_.net_of(pin_id(pin));
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

std::size_t PyCircuit::define_module(const std::string &name) {
    return circuit_.add_module_definition(volt::ModuleDefinition{volt::ModuleName{name}}).index();
}

std::size_t PyCircuit::add_template_net(std::size_t module, const std::string &name,
                                        const std::string &kind) {
    return circuit_
        .add_template_net(module_def_id(module),
                          volt::TemplateNetDefinition{volt::NetName{name}, parse_net_kind(kind)})
        .index();
}

std::size_t PyCircuit::add_port(std::size_t module, const std::string &name,
                                std::size_t internal_net, const std::string &role, bool required) {
    return circuit_
        .add_port_definition(module_def_id(module),
                             volt::PortDefinition{volt::PortName{name},
                                                  template_net_def_id(internal_net),
                                                  parse_port_role(role), required})
        .index();
}

std::size_t PyCircuit::add_module_component(std::size_t module, std::size_t definition,
                                            const std::string &reference,
                                            const py::dict &properties) {
    return circuit_
        .add_module_component(module_def_id(module),
                              volt::ModuleComponentTemplate{component_def_id(definition),
                                                            volt::ReferenceDesignator{reference},
                                                            properties_from_dict(properties)})
        .index();
}

std::size_t PyCircuit::module_component_pin_by_name(std::size_t component,
                                                    const std::string &name) const {
    const auto matches = module_component_pins_by_name(module_component_id(component), name);
    if (matches.empty()) {
        throw std::out_of_range{"Module component has no pin with that name"};
    }
    if (matches.size() > 1) {
        throw std::invalid_argument{"Module component pin name is ambiguous"};
    }

    return matches.front().index();
}

std::size_t PyCircuit::module_component_pin_by_number(std::size_t component,
                                                      const std::string &number) const {
    const auto component_handle = module_component_id(component);
    const auto &component_template = circuit_.module_component_template(component_handle);
    const auto &definition = circuit_.component_definition(component_template.definition());
    for (const auto pin : definition.pins()) {
        if (circuit_.pin_definition(pin).number() == number) {
            return pin.index();
        }
    }

    throw std::out_of_range{"Module component has no pin with that number"};
}

py::list PyCircuit::module_component_pin_refs(std::size_t component) const {
    auto result = py::list{};
    const auto component_handle = module_component_id(component);
    const auto &component_template = circuit_.module_component_template(component_handle);
    const auto &definition = circuit_.component_definition(component_template.definition());
    for (const auto pin : definition.pins()) {
        const auto &pin_definition = circuit_.pin_definition(pin);
        auto item = py::dict{};
        item["index"] = pin.index();
        item["name"] = pin_definition.name();
        item["number"] = pin_definition.number();
        result.append(std::move(item));
    }
    return result;
}

void PyCircuit::connect_module_pin(std::size_t module, std::size_t net, std::size_t component,
                                   std::size_t pin) {
    circuit_.connect_module_pin(module_def_id(module), template_net_def_id(net),
                                module_component_id(component), volt::PinDefId{pin});
}

std::size_t PyCircuit::instantiate_root_module(std::size_t definition, const std::string &name) {
    return circuit_
        .instantiate_root_module(module_def_id(definition), volt::ModuleInstanceName{name})
        .index();
}

std::size_t PyCircuit::concrete_component_for(std::size_t instance, std::size_t component) const {
    const auto concrete = circuit_.concrete_component_for(module_instance_id(instance),
                                                          module_component_id(component));
    if (!concrete.has_value()) {
        throw std::out_of_range{"Module instance has no concrete component for template"};
    }
    return concrete.value().index();
}

void PyCircuit::bind_port(std::size_t instance, std::size_t port, std::size_t parent_net) {
    [[maybe_unused]] const auto binding =
        circuit_.bind_port(module_instance_id(instance), port_def_id(port), net_id(parent_net));
}

py::list PyCircuit::template_nets(std::size_t module) const {
    auto result = py::list{};
    const auto &definition = circuit_.module_definition(module_def_id(module));
    for (const auto net_id : definition.template_nets()) {
        const auto &net = circuit_.template_net_definition(net_id);
        auto item = py::dict{};
        item["index"] = net_id.index();
        item["name"] = net.name().value();
        item["kind"] = net_kind_name(net.kind());
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::module_ports(std::size_t module) const {
    auto result = py::list{};
    const auto &definition = circuit_.module_definition(module_def_id(module));
    for (const auto port_id : definition.ports()) {
        const auto &port = circuit_.port_definition(port_id);
        auto item = py::dict{};
        item["index"] = port_id.index();
        item["name"] = port.name().value();
        item["internal_net"] = port.internal_net().index();
        item["role"] = port_role_name(port.role());
        item["required"] = port.required();
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::module_components(std::size_t module) const {
    auto result = py::list{};
    const auto &definition = circuit_.module_definition(module_def_id(module));
    for (const auto component_id : definition.components()) {
        const auto &component = circuit_.module_component_template(component_id);
        auto item = py::dict{};
        item["index"] = component_id.index();
        item["definition"] = component.definition().index();
        item["reference"] = component.reference().value();
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::module_connections(std::size_t module) const {
    auto result = py::list{};
    for (const auto &connection : circuit_.module_pin_connections(module_def_id(module))) {
        auto item = py::dict{};
        item["net"] = connection.net().index();
        item["component"] = connection.component().index();
        item["pin_definition"] = connection.pin().index();
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::module_net_origins(std::size_t instance) const {
    auto result = py::list{};
    for (const auto &[template_net, concrete_net] :
         circuit_.module_net_origins(module_instance_id(instance))) {
        auto item = py::dict{};
        item["template_net"] = template_net.index();
        item["net"] = concrete_net.index();
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::module_component_origins(std::size_t instance) const {
    auto result = py::list{};
    for (const auto &[module_component, concrete_component] :
         circuit_.module_component_origins(module_instance_id(instance))) {
        auto item = py::dict{};
        item["module_component"] = module_component.index();
        item["component"] = concrete_component.index();
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::port_bindings(std::size_t instance) const {
    auto result = py::list{};
    for (const auto binding_id : circuit_.port_bindings_for(module_instance_id(instance))) {
        const auto &binding = circuit_.port_binding(binding_id);
        auto item = py::dict{};
        item["port"] = binding.port().index();
        item["internal_net"] = binding.internal_net().index();
        item["parent_net"] = binding.parent_net().index();
        result.append(std::move(item));
    }
    return result;
}

std::size_t PyCircuit::schematic_sheet(const std::string &name, const py::dict &metadata) {
    auto &projection = schematic_projection();
    if (const auto existing = projection.sheet_by_name(name); existing.has_value()) {
        if (py::len(metadata) != 0) {
            const auto requested = sheet_metadata_from_dict(metadata, name);
            if (!(projection.sheet(existing.value()).metadata() == requested)) {
                throw std::invalid_argument{
                    "Schematic sheet already exists with different metadata"};
            }
        }
        return existing.value().index();
    }
    return projection.add_sheet(volt::Sheet{name, sheet_metadata_from_dict(metadata, name)})
        .index();
}

std::size_t PyCircuit::schematic_region(std::size_t sheet, const py::dict &region_data) {
    auto &projection = schematic_projection();
    const auto sheet_handle = sheet_id(sheet);
    const auto requested = sheet_region_from_dict(region_data);
    if (const auto existing = projection.sheet_region_by_name(sheet_handle, requested.name());
        existing.has_value()) {
        if (!(projection.sheet_region(sheet_handle, existing.value()) == requested)) {
            throw std::invalid_argument{"Schematic region already exists with different metadata"};
        }
        return existing.value();
    }
    return projection.add_sheet_region(sheet_handle, std::move(requested));
}

std::size_t PyCircuit::register_schematic_symbol(const py::dict &symbol_data) {
    auto symbol = symbol_definition_from_dict(symbol_data);
    auto &projection = schematic_projection();
    if (const auto existing = projection.symbol_definition_by_name(symbol.name());
        existing.has_value()) {
        if (projection.symbol_definition(existing.value()) != symbol) {
            throw std::invalid_argument{
                "Schematic symbol name already exists with a different definition"};
        }
        return existing.value().index();
    }
    return projection.add_symbol_definition(std::move(symbol)).index();
}

std::size_t PyCircuit::place_schematic_symbol(std::size_t sheet, std::size_t component,
                                              const std::string &symbol, double x, double y,
                                              const std::string &orientation,
                                              std::optional<std::size_t> authored_region) {
    require_finite(x, "Schematic coordinates must be finite");
    require_finite(y, "Schematic coordinates must be finite");

    auto &projection = schematic_projection();
    const auto sheet_handle = sheet_id(sheet);
    static_cast<void>(projection.sheet(sheet_handle));

    const auto component_handle = component_id(component);
    static_cast<void>(circuit_.component(component_handle));

    const auto symbol_definition = ensure_schematic_symbol(symbol);
    return projection
        .place_symbol(sheet_handle,
                      volt::SymbolInstance{symbol_definition, component_handle, volt::Point{x, y},
                                           schematic_orientation_from_string(orientation),
                                           authored_region})
        .index();
}

std::string PyCircuit::schematic_symbol_orientation(std::size_t instance) {
    auto &projection = schematic_projection();
    const auto &symbol_instance = projection.symbol_instance(volt::SymbolInstanceId{instance});
    return schematic_orientation_name(symbol_instance.orientation());
}

std::pair<double, double> PyCircuit::schematic_symbol_pin_anchor(std::size_t instance,
                                                                 const std::string &number) {
    auto &projection = schematic_projection();
    const auto &symbol_instance = projection.symbol_instance(volt::SymbolInstanceId{instance});
    const auto &symbol = projection.symbol_definition(symbol_instance.symbol_definition());

    for (const auto &pin : symbol.pins()) {
        if (pin.number() == number) {
            const auto anchor = volt::transform_schematic_point(
                pin.anchor(), symbol_instance.position(), symbol_instance.orientation());
            return {anchor.x(), anchor.y()};
        }
    }

    throw std::out_of_range{"Schematic symbol has no pin with that number"};
}

py::list PyCircuit::schematic_symbol_pin_refs(std::size_t instance) {
    auto result = py::list{};
    auto &projection = schematic_projection();
    const auto &symbol_instance = projection.symbol_instance(volt::SymbolInstanceId{instance});
    const auto &symbol = projection.symbol_definition(symbol_instance.symbol_definition());

    for (const auto &pin : symbol.pins()) {
        const auto anchor = volt::transform_schematic_point(
            pin.anchor(), symbol_instance.position(), symbol_instance.orientation());
        auto item = py::dict{};
        item["name"] = pin.name();
        item["number"] = pin.number();
        item["anchor"] = std::pair<double, double>{anchor.x(), anchor.y()};
        item["orientation"] = schematic_orientation_name(
            rotated_schematic_orientation(pin.orientation(), symbol_instance.orientation()));
        result.append(std::move(item));
    }
    return result;
}

std::size_t PyCircuit::add_schematic_wire(std::size_t sheet, std::size_t net,
                                          const std::vector<std::pair<double, double>> &points,
                                          const std::string &route_intent,
                                          std::optional<std::size_t> authored_region) {
    auto wire_points = std::vector<volt::Point>{};
    wire_points.reserve(points.size());
    for (const auto &[x, y] : points) {
        require_finite(x, "Schematic coordinates must be finite");
        require_finite(y, "Schematic coordinates must be finite");
        wire_points.emplace_back(x, y);
    }

    auto &projection = schematic_projection();
    return projection
        .add_wire_run(sheet_id(sheet),
                      volt::WireRun{net_id(net), std::move(wire_points),
                                    route_intent_from_string(route_intent), authored_region})
        .index();
}

py::tuple PyCircuit::add_schematic_wire_for_endpoints(
    std::size_t sheet, std::optional<std::size_t> net,
    const std::vector<std::pair<double, double>> &points, const py::list &endpoints,
    const std::string &route_intent, std::optional<std::size_t> authored_region) {
    auto wire_points = std::vector<volt::Point>{};
    wire_points.reserve(points.size());
    for (const auto &[x, y] : points) {
        require_finite(x, "Schematic coordinates must be finite");
        require_finite(y, "Schematic coordinates must be finite");
        wire_points.emplace_back(x, y);
    }

    auto &projection = schematic_projection();
    try {
        const auto id = projection.add_wire_run_for_endpoints(
            sheet_id(sheet), net.has_value() ? std::optional{net_id(net.value())} : std::nullopt,
            std::move(wire_points), schematic_endpoints_from_list(endpoints),
            route_intent_from_string(route_intent), authored_region);
        return schematic_entity_result(id.index(), projection.wire_run(id).net());
    } catch (const std::invalid_argument &error) {
        raise_schematic_authoring_error(error);
    }
}

std::size_t PyCircuit::add_schematic_net_label(std::size_t sheet, std::size_t net, double x,
                                               double y, const std::string &orientation,
                                               std::optional<std::size_t> authored_region,
                                               std::optional<std::string> label,
                                               const std::string &horizontal_alignment,
                                               const std::string &vertical_alignment,
                                               std::optional<double> font_size) {
    require_finite(x, "Schematic coordinates must be finite");
    require_finite(y, "Schematic coordinates must be finite");

    auto &projection = schematic_projection();
    return projection
        .add_net_label(sheet_id(sheet),
                       volt::NetLabel{net_id(net), volt::Point{x, y},
                                      schematic_orientation_from_string(orientation),
                                      authored_region, std::move(label),
                                      text_style_from_strings(horizontal_alignment,
                                                              vertical_alignment, font_size)})
        .index();
}

py::tuple PyCircuit::add_schematic_net_label_for_endpoint(
    std::size_t sheet, std::optional<std::size_t> net, const py::tuple &endpoint,
    const std::string &orientation, std::optional<std::size_t> authored_region,
    std::optional<std::string> label, const std::string &horizontal_alignment,
    const std::string &vertical_alignment, std::optional<double> font_size) {
    auto &projection = schematic_projection();
    try {
        const auto id = projection.add_net_label_for_endpoint(
            sheet_id(sheet), net.has_value() ? std::optional{net_id(net.value())} : std::nullopt,
            schematic_endpoint_from_tuple(endpoint), schematic_orientation_from_string(orientation),
            authored_region, std::move(label),
            text_style_from_strings(horizontal_alignment, vertical_alignment, font_size));
        return schematic_entity_result(id.index(), projection.net_label(id).net());
    } catch (const std::invalid_argument &error) {
        raise_schematic_authoring_error(error);
    }
}

std::size_t PyCircuit::add_schematic_junction(std::size_t sheet, std::size_t net, double x,
                                              double y,
                                              std::optional<std::size_t> authored_region) {
    require_finite(x, "Schematic coordinates must be finite");
    require_finite(y, "Schematic coordinates must be finite");

    auto &projection = schematic_projection();
    return projection
        .add_junction(sheet_id(sheet),
                      volt::Junction{net_id(net), volt::Point{x, y}, authored_region})
        .index();
}

py::tuple
PyCircuit::add_schematic_junction_for_endpoint(std::size_t sheet, std::optional<std::size_t> net,
                                               const py::tuple &endpoint,
                                               std::optional<std::size_t> authored_region) {
    auto &projection = schematic_projection();
    try {
        const auto id = projection.add_junction_for_endpoint(
            sheet_id(sheet), net.has_value() ? std::optional{net_id(net.value())} : std::nullopt,
            schematic_endpoint_from_tuple(endpoint), authored_region);
        return schematic_entity_result(id.index(), projection.junction(id).net());
    } catch (const std::invalid_argument &error) {
        raise_schematic_authoring_error(error);
    }
}

std::size_t PyCircuit::add_schematic_terminal_marker(std::size_t sheet, std::size_t net,
                                                     const std::string &kind, double x, double y,
                                                     const std::string &orientation,
                                                     std::optional<std::size_t> authored_region,
                                                     std::optional<std::string> label) {
    require_finite(x, "Schematic coordinates must be finite");
    require_finite(y, "Schematic coordinates must be finite");

    auto &projection = schematic_projection();
    return projection
        .add_terminal_marker(sheet_id(sheet),
                             volt::PowerPort{net_id(net), power_port_kind_from_string(kind),
                                             volt::Point{x, y},
                                             schematic_orientation_from_string(orientation),
                                             authored_region, std::move(label)})
        .index();
}

py::tuple PyCircuit::add_schematic_terminal_marker_for_endpoint(
    std::size_t sheet, std::optional<std::size_t> net, const std::string &kind,
    const py::tuple &endpoint, const std::string &orientation,
    std::optional<std::size_t> authored_region, std::optional<std::string> label) {
    auto &projection = schematic_projection();
    try {
        const auto id = projection.add_terminal_marker_for_endpoint(
            sheet_id(sheet), net.has_value() ? std::optional{net_id(net.value())} : std::nullopt,
            schematic_endpoint_from_tuple(endpoint), power_port_kind_from_string(kind),
            schematic_orientation_from_string(orientation), authored_region, std::move(label));
        return schematic_entity_result(id.index(), projection.power_port(id).net());
    } catch (const std::invalid_argument &error) {
        raise_schematic_authoring_error(error);
    }
}

std::size_t PyCircuit::add_schematic_no_connect_marker(std::size_t sheet, std::size_t pin, double x,
                                                       double y, const std::string &orientation,
                                                       const std::string &reason,
                                                       std::optional<std::size_t> authored_region) {
    require_finite(x, "Schematic coordinates must be finite");
    require_finite(y, "Schematic coordinates must be finite");

    auto &projection = schematic_projection();
    return projection
        .add_no_connect_marker(sheet_id(sheet),
                               volt::NoConnectMarker{pin_id(pin), volt::Point{x, y},
                                                     schematic_orientation_from_string(orientation),
                                                     reason, authored_region})
        .index();
}

std::size_t PyCircuit::add_schematic_sheet_port(std::size_t sheet, std::size_t net,
                                                const std::string &name, const std::string &kind,
                                                double x, double y, const std::string &orientation,
                                                std::optional<std::size_t> authored_region) {
    require_finite(x, "Schematic coordinates must be finite");
    require_finite(y, "Schematic coordinates must be finite");

    auto &projection = schematic_projection();
    return projection
        .add_sheet_port(
            sheet_id(sheet),
            volt::SheetPort{net_id(net), name, sheet_port_kind_from_string(kind), volt::Point{x, y},
                            schematic_orientation_from_string(orientation), authored_region})
        .index();
}

py::tuple PyCircuit::add_schematic_sheet_port_for_endpoint(
    std::size_t sheet, std::optional<std::size_t> net, const std::string &name,
    const std::string &kind, const py::tuple &endpoint, const std::string &orientation,
    std::optional<std::size_t> authored_region) {
    auto &projection = schematic_projection();
    try {
        const auto id = projection.add_sheet_port_for_endpoint(
            sheet_id(sheet), net.has_value() ? std::optional{net_id(net.value())} : std::nullopt,
            schematic_endpoint_from_tuple(endpoint), name, sheet_port_kind_from_string(kind),
            schematic_orientation_from_string(orientation), authored_region);
        return schematic_entity_result(id.index(), projection.sheet_port(id).net());
    } catch (const std::invalid_argument &error) {
        raise_schematic_authoring_error(error);
    }
}

std::size_t PyCircuit::add_schematic_symbol_field(
    std::size_t sheet, std::size_t instance, const std::string &name, const std::string &value,
    double x, double y, const std::string &orientation, std::optional<std::size_t> authored_region,
    const std::string &horizontal_alignment, const std::string &vertical_alignment,
    std::optional<double> font_size) {
    require_finite(x, "Schematic coordinates must be finite");
    require_finite(y, "Schematic coordinates must be finite");

    auto &projection = schematic_projection();
    return projection
        .add_symbol_field(
            sheet_id(sheet),
            volt::SymbolField{
                volt::SymbolInstanceId{instance}, name, value, volt::Point{x, y},
                schematic_orientation_from_string(orientation), authored_region,
                text_style_from_strings(horizontal_alignment, vertical_alignment, font_size)})
        .index();
}

std::string PyCircuit::schematic_to_json() {
    volt::layout_schematic_text(schematic_projection());
    auto out = std::ostringstream{};
    volt::io::write_schematic(out, schematic_document_);
    return out.str();
}

std::string PyCircuit::schematic_to_svg() {
    volt::layout_schematic_text(schematic_projection());
    auto out = std::ostringstream{};
    volt::io::write_schematic_svg(out, schematic_projection());
    return out.str();
}

std::string PyCircuit::schematic_to_body_svg(std::size_t sheet, double margin) {
    volt::layout_schematic_text(schematic_projection());
    auto options = volt::io::SchematicSvgBodyOptions{};
    options.margin = margin;
    auto out = std::ostringstream{};
    volt::io::write_schematic_body_svg(out, schematic_projection(), sheet_id(sheet), options);
    return out.str();
}

py::list PyCircuit::schematic_svg_pages() {
    volt::layout_schematic_text(schematic_projection());
    auto result = py::list{};
    for (const auto &page : volt::io::write_schematic_svg_pages(schematic_projection())) {
        auto item = py::dict{};
        item["sheet"] = page.sheet.index();
        item["name"] = page.name;
        item["svg"] = page.svg;
        result.append(std::move(item));
    }
    return result;
}

void PyCircuit::load_schematic_json(const std::string &text) {
    schematic_document_.replace_schematic(volt::io::read_schematic_text(text, circuit_));
}

std::vector<std::string> PyCircuit::schematic_sheet_names() const {
    const auto &projection = schematic_document_.schematic();
    auto names = std::vector<std::string>{};
    names.reserve(projection.sheet_count());
    for (std::size_t index = 0; index < projection.sheet_count(); ++index) {
        names.push_back(projection.sheet(volt::SheetId{index}).name());
    }
    return names;
}

py::list PyCircuit::validate() const {
    return diagnostics_to_list(volt::validate_circuit(circuit_));
}

py::list PyCircuit::validate_schematic() {
    return diagnostics_to_list(volt::validate_schematic_readiness(schematic_projection()));
}

py::list PyCircuit::validate_schematic_readability() {
    volt::layout_schematic_text(schematic_projection());
    return diagnostics_to_list(volt::validate_schematic_readability(schematic_projection()));
}

py::list PyCircuit::validate_for_pcb() const {
    return diagnostics_to_list(volt::validate_for_pcb(circuit_));
}

std::string PyCircuit::to_json() const {
    auto out = std::ostringstream{};
    volt::io::write_logical_circuit(out, circuit_);
    return out.str();
}

py::dict PyCircuit::board(const std::string &name) {
    const auto &projection = board_projection(name);
    auto result = py::dict{};
    result["name"] = projection.name().value();
    result["units"] = "mm";
    return result;
}

std::size_t PyCircuit::board_add_layer(const std::string &name, const std::string &role,
                                       const std::string &side, double thickness_mm, bool enabled) {
    return board_projection()
        .add_layer(volt::BoardLayer{name, parse_board_layer_role(role),
                                    parse_board_layer_side(side), thickness_mm, enabled})
        .index();
}

void PyCircuit::board_set_layer_stack(const std::vector<std::size_t> &layers,
                                      double board_thickness_mm) {
    auto layer_ids = std::vector<volt::BoardLayerId>{};
    layer_ids.reserve(layers.size());
    for (const auto layer : layers) {
        layer_ids.emplace_back(layer);
    }
    board_projection().set_layer_stack(volt::LayerStack{std::move(layer_ids), board_thickness_mm});
}

void PyCircuit::board_set_rectangular_outline(double x, double y, double width, double height) {
    board_projection().set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{x, y}, volt::BoardSize{width, height}));
}

void PyCircuit::board_set_polygon_outline(const std::vector<std::pair<double, double>> &vertices) {
    auto points = std::vector<volt::BoardPoint>{};
    points.reserve(vertices.size());
    for (const auto &[x, y] : vertices) {
        points.emplace_back(x, y);
    }
    board_projection().set_outline(volt::BoardOutline{std::move(points)});
}

std::size_t PyCircuit::board_add_mounting_hole(const std::string &label, double x, double y,
                                               double diameter_mm) {
    return board_projection()
        .add_feature(volt::BoardFeature::mounting_hole(label, volt::BoardPoint{x, y}, diameter_mm))
        .index();
}

std::size_t PyCircuit::board_cache_footprint_definition(const py::dict &definition) {
    return board_projection()
        .cache_footprint_definition(footprint_definition_from_dict(definition))
        .index();
}

std::size_t PyCircuit::board_place_component(std::size_t component, double x, double y,
                                             double rotation_degrees, const std::string &side,
                                             bool locked) {
    return board_projection()
        .place_component(volt::ComponentPlacement{component_id(component), volt::BoardPoint{x, y},
                                                  volt::BoardRotation::degrees(rotation_degrees),
                                                  parse_board_side(side), locked})
        .index();
}

py::list PyCircuit::board_resolve_pads() const {
    auto result = py::list{};
    for (const auto &resolution :
         board_projection().resolve_pads(volt::builtin_footprint_library())) {
        auto item = py::dict{};
        item["placement"] = resolution.placement().index();
        item["component"] = resolution.component().index();
        item["pad"] = resolution.pad().index();
        item["pad_label"] = resolution.pad_label();
        item["position"] =
            py::make_tuple(resolution.position().x_mm(), resolution.position().y_mm());
        item["pin"] =
            resolution.pin().has_value() ? py::cast(resolution.pin()->index()) : py::none{};
        item["net"] =
            resolution.net().has_value() ? py::cast(resolution.net()->index()) : py::none{};
        item["status"] = pad_resolution_status_name(resolution.status());
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::board_validate() const {
    return diagnostics_to_list(
        validate_board(board_projection(), volt::builtin_footprint_library()));
}

std::string PyCircuit::board_to_json() const {
    return volt::io::write_pcb_board(board_projection(), volt::builtin_footprint_library());
}

std::string PyCircuit::board_to_svg(bool pad_net_overlays, bool diagnostic_overlays) const {
    return volt::io::write_pcb_placement_svg(
        board_projection(), volt::builtin_footprint_library(),
        volt::io::PcbPlacementSvgOptions{.pad_net_overlays = pad_net_overlays,
                                         .diagnostic_overlays = diagnostic_overlays});
}

std::vector<volt::PinId> PyCircuit::pins_by_name(volt::ComponentId component,
                                                 const std::string &name) const {
    auto result = std::vector<volt::PinId>{};
    for (const auto pin : circuit_.pins_for(component)) {
        const auto definition = circuit_.pin(pin).definition();
        if (circuit_.pin_definition(definition).name() == name) {
            result.push_back(pin);
        }
    }
    return result;
}

std::vector<volt::PinDefId>
PyCircuit::module_component_pins_by_name(volt::ModuleComponentId component,
                                         const std::string &name) const {
    auto result = std::vector<volt::PinDefId>{};
    const auto &component_template = circuit_.module_component_template(component);
    const auto &definition = circuit_.component_definition(component_template.definition());
    for (const auto pin : definition.pins()) {
        if (circuit_.pin_definition(pin).name() == name) {
            result.push_back(pin);
        }
    }
    return result;
}

volt::Schematic &PyCircuit::schematic_projection() { return schematic_document_.schematic(); }

volt::SymbolDefId PyCircuit::ensure_schematic_symbol(const std::string &name) {
    auto &projection = schematic_projection();
    if (const auto existing = projection.symbol_definition_by_name(name); existing.has_value()) {
        return existing.value();
    }

    auto symbol = built_in_symbol(name);
    if (!symbol.has_value()) {
        throw std::invalid_argument{"Unknown schematic symbol"};
    }
    return projection.add_symbol_definition(std::move(symbol.value()));
}

volt::Board &PyCircuit::board_projection(const std::string &name) {
    if (!board_projection_.has_value()) {
        board_projection_.emplace(circuit_, volt::BoardName{name});
        return board_projection_.value();
    }
    if (board_projection_->name().value() != name) {
        throw std::invalid_argument{"Board projection already exists with a different name"};
    }
    return board_projection_.value();
}

volt::Board &PyCircuit::board_projection() {
    if (!board_projection_.has_value()) {
        throw std::logic_error{"Board projection has not been created"};
    }
    return board_projection_.value();
}

const volt::Board &PyCircuit::board_projection() const {
    if (!board_projection_.has_value()) {
        throw std::logic_error{"Board projection has not been created"};
    }
    return board_projection_.value();
}

} // namespace volt::python
