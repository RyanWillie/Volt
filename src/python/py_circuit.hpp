#pragma once

#include "binding_conversions.hpp"

namespace volt::python {

class PyCircuit {
  public:
    PyCircuit() : circuit_{}, schematic_document_{circuit_} {}

    [[nodiscard]] std::size_t define_resistor() {
        return volt::authoring::define_component(circuit_, volt::authoring::resistor()).index();
    }

    [[nodiscard]] std::size_t define_capacitor() {
        return volt::authoring::define_component(circuit_, volt::authoring::capacitor()).index();
    }

    [[nodiscard]] std::size_t define_polarized_capacitor() {
        return volt::authoring::define_component(circuit_, volt::authoring::polarized_capacitor())
            .index();
    }

    [[nodiscard]] std::size_t define_inductor() {
        return volt::authoring::define_component(circuit_, volt::authoring::inductor()).index();
    }

    [[nodiscard]] std::size_t define_diode() {
        return volt::authoring::define_component(circuit_, volt::authoring::diode()).index();
    }

    [[nodiscard]] std::size_t define_led() {
        return volt::authoring::define_component(circuit_, volt::authoring::led()).index();
    }

    [[nodiscard]] std::size_t define_switch_spst() {
        return volt::authoring::define_component(circuit_, volt::authoring::switch_spst()).index();
    }

    [[nodiscard]] std::size_t define_crystal_2pin() {
        return volt::authoring::define_component(circuit_, volt::authoring::crystal_2pin()).index();
    }

    [[nodiscard]] std::size_t define_test_point() {
        return volt::authoring::define_component(circuit_, volt::authoring::test_point()).index();
    }

    [[nodiscard]] std::size_t define_connector_1x01() {
        return volt::authoring::define_component(circuit_, volt::authoring::connector_1x01())
            .index();
    }

    [[nodiscard]] std::size_t define_connector_1x02() {
        return volt::authoring::define_component(circuit_, volt::authoring::connector_1x02())
            .index();
    }

    [[nodiscard]] std::size_t define_connector_1x03() {
        return volt::authoring::define_component(circuit_, volt::authoring::connector_1x03())
            .index();
    }

    [[nodiscard]] std::size_t define_regulator_3pin() {
        return volt::authoring::define_component(circuit_, volt::authoring::regulator_3pin())
            .index();
    }

    [[nodiscard]] std::size_t define_op_amp_5pin() {
        return volt::authoring::define_component(circuit_, volt::authoring::op_amp_5pin()).index();
    }

    [[nodiscard]] std::size_t
    define_component(const std::string &name, const py::list &pins, const py::dict &properties,
                     const std::string &source_namespace, const std::string &source_name,
                     const std::string &source_version, const py::list &schematic_symbols) {
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

    [[nodiscard]] std::size_t add_net(const std::string &name, const std::string &kind) {
        return circuit_.add_net(volt::Net{volt::NetName{name}, parse_net_kind(kind)}).index();
    }

    [[nodiscard]] py::list net_refs() const {
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

    void select_physical_part(std::size_t component, const std::string &manufacturer,
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

    void set_component_quantity(std::size_t component, const std::string &name,
                                const std::string &dimension_name, double value) {
        require_finite(value, "Electrical attribute quantities must be finite");
        const auto dimension = parse_dimension(dimension_name);
        circuit_.set_component_electrical_attribute(
            component_id(component), component_quantity_spec(name, dimension),
            volt::ElectricalAttributeValue{volt::Quantity{dimension, value}});
    }

    void set_component_percent_tolerance(std::size_t component, double value) {
        require_finite(value, "Tolerance values must be finite");
        circuit_.set_component_electrical_attribute(
            component_id(component),
            component_quantity_spec("tolerance", volt::UnitDimension::Ratio),
            volt::ElectricalAttributeValue{volt::Tolerance::percent(value)});
    }

    void set_net_quantity(std::size_t net, const std::string &name,
                          const std::string &dimension_name, double value) {
        require_finite(value, "Electrical attribute quantities must be finite");
        const auto dimension = parse_dimension(dimension_name);
        circuit_.set_net_electrical_attribute(
            net_id(net), net_quantity_spec(name, dimension),
            volt::ElectricalAttributeValue{volt::Quantity{dimension, value}});
    }

    void select_generic_physical_part(std::size_t component) {
        const auto component_handle = component_id(component);
        const auto &definition =
            circuit_.component_definition(circuit_.component(component_handle).definition());
        auto mappings = std::vector<volt::PinPadMapping>{};
        mappings.reserve(definition.pins().size());
        for (std::size_t index = 0; index < definition.pins().size(); ++index) {
            mappings.emplace_back(definition.pins()[index], std::to_string(index + 1));
        }
        circuit_.select_physical_part(
            component_handle, volt::PhysicalPart{volt::ManufacturerPart{"Volt", "generic"},
                                                 volt::PackageRef{"unspecified"},
                                                 volt::FootprintRef{"volt.generic", "unspecified"},
                                                 std::move(mappings)});
    }

    void set_selected_part_quantity(std::size_t component, const std::string &name,
                                    const std::string &dimension_name, double value) {
        require_finite(value, "Electrical attribute quantities must be finite");
        const auto dimension = parse_dimension(dimension_name);
        circuit_.set_selected_part_electrical_attribute(
            component_id(component), selected_part_quantity_spec(name, dimension),
            volt::ElectricalAttributeValue{volt::Quantity{dimension, value}});
    }

    [[nodiscard]] std::size_t instantiate_ref(std::size_t definition, const std::string &reference,
                                              const py::dict &properties) {
        return volt::authoring::instantiate(circuit_, component_def_id(definition),
                                            volt::ReferenceDesignator{reference},
                                            properties_from_dict(properties))
            .index();
    }

    [[nodiscard]] std::size_t instantiate_auto(std::size_t definition, const std::string &prefix,
                                               const py::dict &properties) {
        return volt::authoring::instantiate(circuit_, component_def_id(definition), prefix,
                                            properties_from_dict(properties))
            .index();
    }

    [[nodiscard]] std::size_t pin_by_name(std::size_t component, const std::string &name) const {
        const auto matches = pins_by_name(component_id(component), name);
        if (matches.empty()) {
            throw std::out_of_range{"Component has no pin with that name"};
        }
        if (matches.size() > 1) {
            throw std::invalid_argument{"Component pin name is ambiguous"};
        }

        return matches.front().index();
    }

    [[nodiscard]] std::size_t pin_by_number(std::size_t component,
                                            const std::string &number) const {
        const auto pin = circuit_.pin_by_number(component_id(component), number);
        if (!pin.has_value()) {
            throw std::out_of_range{"Component has no pin with that number"};
        }

        return pin.value().index();
    }

    [[nodiscard]] std::size_t pin_component(std::size_t pin) const {
        return circuit_.pin(pin_id(pin)).component().index();
    }

    [[nodiscard]] std::string component_reference(std::size_t component) const {
        return circuit_.component(component_id(component)).reference().value();
    }

    [[nodiscard]] py::list pin_refs(std::size_t component) const {
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

    [[nodiscard]] std::optional<std::string>
    component_schematic_symbol(std::size_t component, const std::string &variant) const {
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

    void connect(std::size_t net, std::size_t pin) { circuit_.connect(net_id(net), pin_id(pin)); }

    [[nodiscard]] std::optional<std::size_t> net_of(std::size_t pin) const {
        const auto net = circuit_.net_of(pin_id(pin));
        if (!net.has_value()) {
            return std::nullopt;
        }
        return net.value().index();
    }

    [[nodiscard]] py::list net_pins(std::size_t net) const {
        auto result = py::list{};
        for (const auto pin : circuit_.net(net_id(net)).pins()) {
            result.append(pin.index());
        }
        return result;
    }

    void mark_intentional_stub_net(std::size_t net) {
        static_cast<void>(circuit_.mark_intentional_stub_net(net_id(net)));
    }

    void mark_intentional_no_connect_pin(std::size_t pin) {
        static_cast<void>(circuit_.mark_intentional_no_connect_pin(pin_id(pin)));
    }

    [[nodiscard]] std::size_t define_module(const std::string &name) {
        return circuit_.add_module_definition(volt::ModuleDefinition{volt::ModuleName{name}})
            .index();
    }

    [[nodiscard]] std::size_t add_template_net(std::size_t module, const std::string &name,
                                               const std::string &kind) {
        return circuit_
            .add_template_net(
                module_def_id(module),
                volt::TemplateNetDefinition{volt::NetName{name}, parse_net_kind(kind)})
            .index();
    }

    [[nodiscard]] std::size_t add_port(std::size_t module, const std::string &name,
                                       std::size_t internal_net, const std::string &role,
                                       bool required) {
        return circuit_
            .add_port_definition(module_def_id(module),
                                 volt::PortDefinition{volt::PortName{name},
                                                      template_net_def_id(internal_net),
                                                      parse_port_role(role), required})
            .index();
    }

    [[nodiscard]] std::size_t add_module_component(std::size_t module, std::size_t definition,
                                                   const std::string &reference,
                                                   const py::dict &properties) {
        return circuit_
            .add_module_component(
                module_def_id(module),
                volt::ModuleComponentTemplate{component_def_id(definition),
                                              volt::ReferenceDesignator{reference},
                                              properties_from_dict(properties)})
            .index();
    }

    [[nodiscard]] std::size_t module_component_pin_by_name(std::size_t component,
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

    [[nodiscard]] std::size_t module_component_pin_by_number(std::size_t component,
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

    [[nodiscard]] py::list module_component_pin_refs(std::size_t component) const {
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

    void connect_module_pin(std::size_t module, std::size_t net, std::size_t component,
                            std::size_t pin) {
        circuit_.connect_module_pin(module_def_id(module), template_net_def_id(net),
                                    module_component_id(component), volt::PinDefId{pin});
    }

    [[nodiscard]] std::size_t instantiate_root_module(std::size_t definition,
                                                      const std::string &name) {
        return circuit_
            .instantiate_root_module(module_def_id(definition), volt::ModuleInstanceName{name})
            .index();
    }

    [[nodiscard]] std::size_t concrete_component_for(std::size_t instance,
                                                     std::size_t component) const {
        const auto concrete = circuit_.concrete_component_for(module_instance_id(instance),
                                                              module_component_id(component));
        if (!concrete.has_value()) {
            throw std::out_of_range{"Module instance has no concrete component for template"};
        }
        return concrete.value().index();
    }

    void bind_port(std::size_t instance, std::size_t port, std::size_t parent_net) {
        [[maybe_unused]] const auto binding =
            circuit_.bind_port(module_instance_id(instance), port_def_id(port), net_id(parent_net));
    }

    [[nodiscard]] py::list template_nets(std::size_t module) const {
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

    [[nodiscard]] py::list module_ports(std::size_t module) const {
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

    [[nodiscard]] py::list module_components(std::size_t module) const {
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

    [[nodiscard]] py::list module_connections(std::size_t module) const {
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

    [[nodiscard]] py::list module_net_origins(std::size_t instance) const {
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

    [[nodiscard]] py::list module_component_origins(std::size_t instance) const {
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

    [[nodiscard]] py::list port_bindings(std::size_t instance) const {
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

    [[nodiscard]] std::size_t schematic_sheet(const std::string &name, const py::dict &metadata) {
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

    [[nodiscard]] std::size_t schematic_region(std::size_t sheet, const py::dict &region_data) {
        auto &projection = schematic_projection();
        const auto sheet_handle = sheet_id(sheet);
        const auto requested = sheet_region_from_dict(region_data);
        if (const auto existing = projection.sheet_region_by_name(sheet_handle, requested.name());
            existing.has_value()) {
            if (!(projection.sheet_region(sheet_handle, existing.value()) == requested)) {
                throw std::invalid_argument{
                    "Schematic region already exists with different metadata"};
            }
            return existing.value();
        }
        return projection.add_sheet_region(sheet_handle, std::move(requested));
    }

    [[nodiscard]] std::size_t register_schematic_symbol(const py::dict &symbol_data) {
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

    [[nodiscard]] std::size_t place_schematic_symbol(std::size_t sheet, std::size_t component,
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
                          volt::SymbolInstance{
                              symbol_definition, component_handle, volt::Point{x, y},
                              schematic_orientation_from_string(orientation), authored_region})
            .index();
    }

    [[nodiscard]] std::string schematic_symbol_orientation(std::size_t instance) {
        auto &projection = schematic_projection();
        const auto &symbol_instance = projection.symbol_instance(volt::SymbolInstanceId{instance});
        return schematic_orientation_name(symbol_instance.orientation());
    }

    [[nodiscard]] std::pair<double, double> schematic_symbol_pin_anchor(std::size_t instance,
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

    [[nodiscard]] py::list schematic_symbol_pin_refs(std::size_t instance) {
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

    [[nodiscard]] std::size_t add_schematic_wire(
        std::size_t sheet, std::size_t net, const std::vector<std::pair<double, double>> &points,
        const std::string &route_intent, std::optional<std::size_t> authored_region) {
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

    [[nodiscard]] std::size_t add_schematic_net_label(std::size_t sheet, std::size_t net, double x,
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

    [[nodiscard]] std::size_t add_schematic_junction(std::size_t sheet, std::size_t net, double x,
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

    [[nodiscard]] std::size_t
    add_schematic_terminal_marker(std::size_t sheet, std::size_t net, const std::string &kind,
                                  double x, double y, const std::string &orientation,
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

    [[nodiscard]] std::size_t
    add_schematic_no_connect_marker(std::size_t sheet, std::size_t pin, double x, double y,
                                    const std::string &orientation, const std::string &reason,
                                    std::optional<std::size_t> authored_region) {
        require_finite(x, "Schematic coordinates must be finite");
        require_finite(y, "Schematic coordinates must be finite");

        auto &projection = schematic_projection();
        return projection
            .add_no_connect_marker(
                sheet_id(sheet),
                volt::NoConnectMarker{pin_id(pin), volt::Point{x, y},
                                      schematic_orientation_from_string(orientation), reason,
                                      authored_region})
            .index();
    }

    [[nodiscard]] std::size_t add_schematic_sheet_port(std::size_t sheet, std::size_t net,
                                                       const std::string &name,
                                                       const std::string &kind, double x, double y,
                                                       const std::string &orientation,
                                                       std::optional<std::size_t> authored_region) {
        require_finite(x, "Schematic coordinates must be finite");
        require_finite(y, "Schematic coordinates must be finite");

        auto &projection = schematic_projection();
        return projection
            .add_sheet_port(sheet_id(sheet),
                            volt::SheetPort{net_id(net), name, sheet_port_kind_from_string(kind),
                                            volt::Point{x, y},
                                            schematic_orientation_from_string(orientation),
                                            authored_region})
            .index();
    }

    [[nodiscard]] std::size_t add_schematic_symbol_field(
        std::size_t sheet, std::size_t instance, const std::string &name, const std::string &value,
        double x, double y, const std::string &orientation,
        std::optional<std::size_t> authored_region, const std::string &horizontal_alignment,
        const std::string &vertical_alignment, std::optional<double> font_size) {
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

    [[nodiscard]] std::string schematic_to_json() {
        volt::layout_schematic_text(schematic_projection());
        auto out = std::ostringstream{};
        volt::io::write_schematic(out, schematic_document_);
        return out.str();
    }

    [[nodiscard]] std::string schematic_to_svg() {
        volt::layout_schematic_text(schematic_projection());
        auto out = std::ostringstream{};
        volt::io::write_schematic_svg(out, schematic_projection());
        return out.str();
    }

    [[nodiscard]] std::string schematic_to_body_svg(std::size_t sheet, double margin) {
        volt::layout_schematic_text(schematic_projection());
        auto options = volt::io::SchematicSvgBodyOptions{};
        options.margin = margin;
        auto out = std::ostringstream{};
        volt::io::write_schematic_body_svg(out, schematic_projection(), sheet_id(sheet), options);
        return out.str();
    }

    [[nodiscard]] py::list schematic_svg_pages() {
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

    void load_schematic_json(const std::string &text) {
        schematic_document_.replace_schematic(volt::io::read_schematic_text(text, circuit_));
    }

    [[nodiscard]] std::vector<std::string> schematic_sheet_names() const {
        const auto &projection = schematic_document_.schematic();
        auto names = std::vector<std::string>{};
        names.reserve(projection.sheet_count());
        for (std::size_t index = 0; index < projection.sheet_count(); ++index) {
            names.push_back(projection.sheet(volt::SheetId{index}).name());
        }
        return names;
    }

    [[nodiscard]] py::list validate() const {
        return diagnostics_to_list(volt::validate_circuit(circuit_));
    }

    [[nodiscard]] py::list validate_schematic() {
        return diagnostics_to_list(volt::validate_schematic_readiness(schematic_projection()));
    }

    [[nodiscard]] py::list validate_schematic_readability() {
        volt::layout_schematic_text(schematic_projection());
        return diagnostics_to_list(volt::validate_schematic_readability(schematic_projection()));
    }

    [[nodiscard]] py::list validate_for_pcb() const {
        return diagnostics_to_list(volt::validate_for_pcb(circuit_));
    }

    [[nodiscard]] std::string to_json() const {
        auto out = std::ostringstream{};
        volt::io::write_logical_circuit(out, circuit_);
        return out.str();
    }

  private:
    [[nodiscard]] std::vector<volt::PinId> pins_by_name(volt::ComponentId component,
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

    [[nodiscard]] std::vector<volt::PinDefId>
    module_component_pins_by_name(volt::ModuleComponentId component,
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

    [[nodiscard]] volt::Schematic &schematic_projection() {
        return schematic_document_.schematic();
    }

    [[nodiscard]] volt::SymbolDefId ensure_schematic_symbol(const std::string &name) {
        auto &projection = schematic_projection();
        if (const auto existing = projection.symbol_definition_by_name(name);
            existing.has_value()) {
            return existing.value();
        }

        auto symbol = built_in_symbol(name);
        if (!symbol.has_value()) {
            throw std::invalid_argument{"Unknown schematic symbol"};
        }
        return projection.add_symbol_definition(std::move(symbol.value()));
    }

    volt::Circuit circuit_;
    volt::SchematicDocument schematic_document_;
};

} // namespace volt::python
