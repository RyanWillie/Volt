#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <volt/authoring/component_library.hpp>
#include <volt/authoring/reference_designators.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/circuit/validation.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/properties.hpp>
#include <volt/io/logical_circuit_writer.hpp>
#include <volt/io/schematic_svg_writer.hpp>
#include <volt/io/schematic_writer.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/symbols.hpp>

namespace py = pybind11;

namespace {

[[nodiscard]] volt::ComponentDefId component_def_id(std::size_t index) {
    return volt::ComponentDefId{index};
}

[[nodiscard]] volt::ComponentId component_id(std::size_t index) { return volt::ComponentId{index}; }

[[nodiscard]] volt::PinId pin_id(std::size_t index) { return volt::PinId{index}; }

[[nodiscard]] volt::NetId net_id(std::size_t index) { return volt::NetId{index}; }

[[nodiscard]] volt::SheetId sheet_id(std::size_t index) { return volt::SheetId{index}; }

[[nodiscard]] volt::ModuleDefId module_def_id(std::size_t index) {
    return volt::ModuleDefId{index};
}

[[nodiscard]] volt::TemplateNetDefId template_net_def_id(std::size_t index) {
    return volt::TemplateNetDefId{index};
}

[[nodiscard]] volt::PortDefId port_def_id(std::size_t index) { return volt::PortDefId{index}; }

[[nodiscard]] volt::ModuleComponentId module_component_id(std::size_t index) {
    return volt::ModuleComponentId{index};
}

[[nodiscard]] volt::ModuleInstanceId module_instance_id(std::size_t index) {
    return volt::ModuleInstanceId{index};
}

[[nodiscard]] volt::PinRole parse_pin_role(const std::string &value) {
    if (value == "passive" || value == "Passive") {
        return volt::PinRole::Passive;
    }
    if (value == "input" || value == "digital_input" || value == "DigitalInput") {
        return volt::PinRole::DigitalInput;
    }
    if (value == "output" || value == "digital_output" || value == "DigitalOutput") {
        return volt::PinRole::DigitalOutput;
    }
    if (value == "analog_input" || value == "AnalogInput") {
        return volt::PinRole::AnalogInput;
    }
    if (value == "analog_output" || value == "AnalogOutput") {
        return volt::PinRole::AnalogOutput;
    }
    if (value == "bidirectional" || value == "Bidirectional") {
        return volt::PinRole::Bidirectional;
    }
    if (value == "power" || value == "power_input" || value == "PowerInput") {
        return volt::PinRole::PowerInput;
    }
    if (value == "power_output" || value == "PowerOutput") {
        return volt::PinRole::PowerOutput;
    }
    if (value == "ground" || value == "Ground") {
        return volt::PinRole::Ground;
    }
    if (value == "no_connect" || value == "no-connect" || value == "NoConnect") {
        return volt::PinRole::NoConnect;
    }

    throw std::invalid_argument{"Unknown pin role"};
}

[[nodiscard]] volt::ConnectionRequirement parse_connection_requirement(const std::string &value) {
    if (value == "required" || value == "Required") {
        return volt::ConnectionRequirement::Required;
    }
    if (value == "optional" || value == "Optional") {
        return volt::ConnectionRequirement::Optional;
    }
    if (value == "must_not_connect" || value == "must-not-connect" || value == "MustNotConnect") {
        return volt::ConnectionRequirement::MustNotConnect;
    }

    throw std::invalid_argument{"Unknown connection requirement"};
}

[[nodiscard]] volt::ElectricalTerminalKind parse_terminal_kind(const std::string &value) {
    if (value == "unspecified" || value == "Unspecified") {
        return volt::ElectricalTerminalKind::Unspecified;
    }
    if (value == "passive" || value == "Passive") {
        return volt::ElectricalTerminalKind::Passive;
    }
    if (value == "signal" || value == "Signal") {
        return volt::ElectricalTerminalKind::Signal;
    }
    if (value == "power" || value == "Power") {
        return volt::ElectricalTerminalKind::Power;
    }
    if (value == "ground" || value == "Ground") {
        return volt::ElectricalTerminalKind::Ground;
    }
    if (value == "no_connect" || value == "no-connect" || value == "NoConnect") {
        return volt::ElectricalTerminalKind::NoConnect;
    }

    throw std::invalid_argument{"Unknown electrical terminal kind"};
}

[[nodiscard]] volt::ElectricalDirection parse_direction(const std::string &value) {
    if (value == "unspecified" || value == "Unspecified") {
        return volt::ElectricalDirection::Unspecified;
    }
    if (value == "input" || value == "Input") {
        return volt::ElectricalDirection::Input;
    }
    if (value == "output" || value == "Output") {
        return volt::ElectricalDirection::Output;
    }
    if (value == "bidirectional" || value == "Bidirectional") {
        return volt::ElectricalDirection::Bidirectional;
    }
    if (value == "passive" || value == "Passive") {
        return volt::ElectricalDirection::Passive;
    }

    throw std::invalid_argument{"Unknown electrical direction"};
}

[[nodiscard]] volt::ElectricalSignalDomain parse_signal_domain(const std::string &value) {
    if (value == "unspecified" || value == "Unspecified") {
        return volt::ElectricalSignalDomain::Unspecified;
    }
    if (value == "digital" || value == "Digital") {
        return volt::ElectricalSignalDomain::Digital;
    }
    if (value == "analog" || value == "Analog") {
        return volt::ElectricalSignalDomain::Analog;
    }
    if (value == "mixed" || value == "Mixed") {
        return volt::ElectricalSignalDomain::Mixed;
    }

    throw std::invalid_argument{"Unknown electrical signal domain"};
}

[[nodiscard]] volt::ElectricalDriveKind parse_drive_kind(const std::string &value) {
    if (value == "unspecified" || value == "Unspecified") {
        return volt::ElectricalDriveKind::Unspecified;
    }
    if (value == "push_pull" || value == "push-pull" || value == "PushPull") {
        return volt::ElectricalDriveKind::PushPull;
    }
    if (value == "open_collector" || value == "open-collector" || value == "OpenCollector") {
        return volt::ElectricalDriveKind::OpenCollector;
    }
    if (value == "open_drain" || value == "open-drain" || value == "OpenDrain") {
        return volt::ElectricalDriveKind::OpenDrain;
    }
    if (value == "high_impedance" || value == "high-impedance" || value == "HighImpedance") {
        return volt::ElectricalDriveKind::HighImpedance;
    }
    if (value == "passive" || value == "Passive") {
        return volt::ElectricalDriveKind::Passive;
    }

    throw std::invalid_argument{"Unknown electrical drive kind"};
}

[[nodiscard]] volt::ElectricalPolarity parse_polarity(const std::string &value) {
    if (value == "none" || value == "None") {
        return volt::ElectricalPolarity::None;
    }
    if (value == "active_high" || value == "active-high" || value == "ActiveHigh") {
        return volt::ElectricalPolarity::ActiveHigh;
    }
    if (value == "active_low" || value == "active-low" || value == "ActiveLow") {
        return volt::ElectricalPolarity::ActiveLow;
    }

    throw std::invalid_argument{"Unknown electrical polarity"};
}

[[nodiscard]] volt::NetKind parse_net_kind(const std::string &value) {
    if (value == "signal" || value == "Signal") {
        return volt::NetKind::Signal;
    }
    if (value == "power" || value == "Power") {
        return volt::NetKind::Power;
    }
    if (value == "ground" || value == "Ground") {
        return volt::NetKind::Ground;
    }
    if (value == "clock" || value == "Clock") {
        return volt::NetKind::Clock;
    }
    if (value == "analog" || value == "Analog") {
        return volt::NetKind::Analog;
    }
    if (value == "high_current" || value == "high-current" || value == "HighCurrent") {
        return volt::NetKind::HighCurrent;
    }

    throw std::invalid_argument{"Unknown net kind"};
}

[[nodiscard]] volt::PortRole parse_port_role(const std::string &value) {
    if (value == "passive" || value == "Passive") {
        return volt::PortRole::Passive;
    }
    if (value == "input" || value == "Input") {
        return volt::PortRole::Input;
    }
    if (value == "output" || value == "Output") {
        return volt::PortRole::Output;
    }
    if (value == "bidirectional" || value == "Bidirectional") {
        return volt::PortRole::Bidirectional;
    }
    if (value == "power" || value == "power_input" || value == "PowerInput") {
        return volt::PortRole::PowerInput;
    }
    if (value == "power_output" || value == "PowerOutput") {
        return volt::PortRole::PowerOutput;
    }
    if (value == "ground" || value == "Ground") {
        return volt::PortRole::Ground;
    }

    throw std::invalid_argument{"Unknown port role"};
}

[[nodiscard]] std::string net_kind_name(volt::NetKind kind) {
    switch (kind) {
    case volt::NetKind::Signal:
        return "Signal";
    case volt::NetKind::Power:
        return "Power";
    case volt::NetKind::Ground:
        return "Ground";
    case volt::NetKind::Clock:
        return "Clock";
    case volt::NetKind::Analog:
        return "Analog";
    case volt::NetKind::HighCurrent:
        return "HighCurrent";
    }

    throw std::logic_error{"Unhandled net kind"};
}

[[nodiscard]] std::string port_role_name(volt::PortRole role) {
    switch (role) {
    case volt::PortRole::Passive:
        return "Passive";
    case volt::PortRole::Input:
        return "Input";
    case volt::PortRole::Output:
        return "Output";
    case volt::PortRole::Bidirectional:
        return "Bidirectional";
    case volt::PortRole::PowerInput:
        return "PowerInput";
    case volt::PortRole::PowerOutput:
        return "PowerOutput";
    case volt::PortRole::Ground:
        return "Ground";
    }

    throw std::logic_error{"Unhandled port role"};
}

[[nodiscard]] std::string severity_name(volt::Severity severity) {
    switch (severity) {
    case volt::Severity::Info:
        return "info";
    case volt::Severity::Warning:
        return "warning";
    case volt::Severity::Error:
        return "error";
    }

    throw std::logic_error{"Unhandled diagnostic severity"};
}

void require_finite(double value, const char *message) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument{message};
    }
}

[[nodiscard]] volt::UnitDimension parse_dimension(const std::string &value) {
    if (value == "resistance") {
        return volt::UnitDimension::Resistance;
    }
    if (value == "capacitance") {
        return volt::UnitDimension::Capacitance;
    }
    if (value == "voltage") {
        return volt::UnitDimension::Voltage;
    }
    if (value == "power") {
        return volt::UnitDimension::Power;
    }
    if (value == "ratio") {
        return volt::UnitDimension::Ratio;
    }

    throw std::invalid_argument{"Unknown electrical attribute dimension"};
}

[[nodiscard]] volt::ElectricalAttributeSpec component_quantity_spec(const std::string &name,
                                                                    volt::UnitDimension dimension) {
    return volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{name},
                                         volt::ElectricalAttributeOwner::ComponentInstance,
                                         volt::ElectricalAttributeKind::DesignInput, dimension};
}

[[nodiscard]] volt::ElectricalAttributeSpec
selected_part_quantity_spec(const std::string &name, volt::UnitDimension dimension) {
    return volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{name},
                                         volt::ElectricalAttributeOwner::SelectedPart,
                                         volt::ElectricalAttributeKind::DesignInput, dimension};
}

[[nodiscard]] volt::ElectricalAttributeSpec net_quantity_spec(const std::string &name,
                                                              volt::UnitDimension dimension) {
    return volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{name},
                                         volt::ElectricalAttributeOwner::Net,
                                         volt::ElectricalAttributeKind::DesignInput, dimension};
}

void add_two_pin_anchors(volt::SymbolDefinition &symbol, const std::string &left_name,
                         const std::string &left_number, const std::string &right_name,
                         const std::string &right_number) {
    symbol.add_pin(volt::SymbolPin{left_name, left_number, volt::Point{0.0, 0.0},
                                   volt::SchematicOrientation::Left});
    symbol.add_pin(volt::SymbolPin{right_name, right_number, volt::Point{20.0, 0.0},
                                   volt::SchematicOrientation::Right});
}

[[nodiscard]] std::optional<volt::SymbolDefinition> built_in_symbol(const std::string &name) {
    if (name == "resistor") {
        auto symbol = volt::SymbolDefinition{name};
        add_two_pin_anchors(symbol, "1", "1", "2", "2");
        symbol.add_primitive(volt::SymbolLine{volt::Point{0.0, 0.0}, volt::Point{4.0, 0.0}});
        symbol.add_primitive(volt::SymbolRectangle{volt::Point{4.0, -3.0}, volt::Point{16.0, 3.0}});
        symbol.add_primitive(volt::SymbolLine{volt::Point{16.0, 0.0}, volt::Point{20.0, 0.0}});
        symbol.add_primitive(volt::SymbolText{"R", volt::Point{10.0, -8.0}});
        return symbol;
    }
    if (name == "capacitor") {
        auto symbol = volt::SymbolDefinition{name};
        add_two_pin_anchors(symbol, "1", "1", "2", "2");
        symbol.add_primitive(volt::SymbolLine{volt::Point{0.0, 0.0}, volt::Point{8.0, 0.0}});
        symbol.add_primitive(volt::SymbolLine{volt::Point{8.0, -5.0}, volt::Point{8.0, 5.0}});
        symbol.add_primitive(volt::SymbolLine{volt::Point{12.0, -5.0}, volt::Point{12.0, 5.0}});
        symbol.add_primitive(volt::SymbolLine{volt::Point{12.0, 0.0}, volt::Point{20.0, 0.0}});
        symbol.add_primitive(volt::SymbolText{"C", volt::Point{10.0, -10.0}});
        return symbol;
    }
    if (name == "led") {
        auto symbol = volt::SymbolDefinition{name};
        add_two_pin_anchors(symbol, "K", "1", "A", "2");
        symbol.add_primitive(volt::SymbolLine{volt::Point{0.0, 0.0}, volt::Point{7.0, 0.0}});
        symbol.add_primitive(volt::SymbolLine{volt::Point{7.0, -5.0}, volt::Point{7.0, 5.0}});
        symbol.add_primitive(volt::SymbolLine{volt::Point{7.0, -5.0}, volt::Point{13.0, 0.0}});
        symbol.add_primitive(volt::SymbolLine{volt::Point{7.0, 5.0}, volt::Point{13.0, 0.0}});
        symbol.add_primitive(volt::SymbolLine{volt::Point{13.0, 0.0}, volt::Point{20.0, 0.0}});
        symbol.add_primitive(volt::SymbolLine{volt::Point{13.0, -6.0}, volt::Point{17.0, -10.0}});
        symbol.add_primitive(volt::SymbolLine{volt::Point{15.0, -4.0}, volt::Point{19.0, -8.0}});
        symbol.add_primitive(volt::SymbolText{"D", volt::Point{10.0, -13.0}});
        return symbol;
    }
    if (name == "connector_1x02") {
        auto symbol = volt::SymbolDefinition{name};
        add_two_pin_anchors(symbol, "+", "1", "-", "2");
        symbol.add_primitive(volt::SymbolRectangle{volt::Point{6.0, -7.0}, volt::Point{14.0, 7.0}});
        symbol.add_primitive(volt::SymbolLine{volt::Point{0.0, 0.0}, volt::Point{6.0, 0.0}});
        symbol.add_primitive(volt::SymbolLine{volt::Point{14.0, 0.0}, volt::Point{20.0, 0.0}});
        symbol.add_primitive(volt::SymbolText{"J", volt::Point{10.0, -12.0}});
        return symbol;
    }

    return std::nullopt;
}

[[nodiscard]] std::string entity_kind_name(volt::EntityKind kind) {
    switch (kind) {
    case volt::EntityKind::ComponentDef:
        return "component_definition";
    case volt::EntityKind::Component:
        return "component";
    case volt::EntityKind::PinDef:
        return "pin_definition";
    case volt::EntityKind::Pin:
        return "pin";
    case volt::EntityKind::Net:
        return "net";
    case volt::EntityKind::ModuleDef:
        return "module_definition";
    case volt::EntityKind::ModuleInstance:
        return "module_instance";
    case volt::EntityKind::PortDef:
        return "port_definition";
    }

    throw std::logic_error{"Unhandled diagnostic entity kind"};
}

[[nodiscard]] volt::PropertyMap properties_from_dict(const py::dict &dict) {
    auto properties = volt::PropertyMap{};

    for (const auto item : dict) {
        if (!py::isinstance<py::str>(item.first)) {
            throw std::invalid_argument{"Property keys must be strings"};
        }

        auto key = volt::PropertyKey{py::cast<std::string>(item.first)};
        const auto value = item.second;

        if (py::isinstance<py::bool_>(value)) {
            properties.set(std::move(key), volt::PropertyValue{py::cast<bool>(value)});
        } else if (py::isinstance<py::int_>(value)) {
            properties.set(std::move(key), volt::PropertyValue{static_cast<std::int64_t>(
                                               py::cast<long long>(value))});
        } else if (py::isinstance<py::float_>(value)) {
            const auto number = py::cast<double>(value);
            if (!std::isfinite(number)) {
                throw std::invalid_argument{"Property numbers must be finite"};
            }
            properties.set(std::move(key), volt::PropertyValue{number});
        } else if (py::isinstance<py::str>(value)) {
            properties.set(std::move(key), volt::PropertyValue{py::cast<std::string>(value)});
        } else {
            throw std::invalid_argument{
                "Property values must be strings, booleans, ints, or floats"};
        }
    }

    return properties;
}

[[nodiscard]] volt::authoring::PinSpec pin_spec_from_dict(const py::dict &dict) {
    if (!dict.contains("name")) {
        throw std::invalid_argument{"Pin specs must include a name"};
    }
    if (!dict.contains("number")) {
        throw std::invalid_argument{"Pin specs must include a number"};
    }

    auto role = volt::PinRole::Passive;
    if (dict.contains("role")) {
        role = parse_pin_role(py::cast<std::string>(dict["role"]));
    }

    auto requirement = volt::ConnectionRequirement::Required;
    if (dict.contains("requirement")) {
        requirement = parse_connection_requirement(py::cast<std::string>(dict["requirement"]));
    }

    auto terminal_kind = volt::ElectricalTerminalKind::Unspecified;
    if (dict.contains("terminal")) {
        terminal_kind = parse_terminal_kind(py::cast<std::string>(dict["terminal"]));
    }

    auto direction = volt::ElectricalDirection::Unspecified;
    if (dict.contains("direction")) {
        direction = parse_direction(py::cast<std::string>(dict["direction"]));
    }

    auto signal_domain = volt::ElectricalSignalDomain::Unspecified;
    if (dict.contains("signal")) {
        signal_domain = parse_signal_domain(py::cast<std::string>(dict["signal"]));
    }

    auto drive_kind = volt::ElectricalDriveKind::Unspecified;
    if (dict.contains("drive")) {
        drive_kind = parse_drive_kind(py::cast<std::string>(dict["drive"]));
    }

    auto polarity = volt::ElectricalPolarity::None;
    if (dict.contains("polarity")) {
        polarity = parse_polarity(py::cast<std::string>(dict["polarity"]));
    }

    auto voltage_range = std::optional<volt::QuantityRange>{};
    if (dict.contains("voltage_range")) {
        const auto range = py::cast<std::pair<std::optional<double>, std::optional<double>>>(
            dict["voltage_range"]);
        if (!range.first.has_value() && !range.second.has_value()) {
            throw std::invalid_argument{"voltage_range must include at least one bound"};
        }
        if (range.first.has_value()) {
            require_finite(range.first.value(), "Voltage range bounds must be finite");
        }
        if (range.second.has_value()) {
            require_finite(range.second.value(), "Voltage range bounds must be finite");
        }
        if (range.first.has_value() && range.second.has_value()) {
            voltage_range = volt::QuantityRange::bounded(
                volt::Quantity{volt::UnitDimension::Voltage, range.first.value()},
                volt::Quantity{volt::UnitDimension::Voltage, range.second.value()});
        } else if (range.first.has_value()) {
            voltage_range = volt::QuantityRange::minimum(
                volt::Quantity{volt::UnitDimension::Voltage, range.first.value()});
        } else {
            voltage_range = volt::QuantityRange::maximum(
                volt::Quantity{volt::UnitDimension::Voltage, range.second.value()});
        }
    }

    return volt::authoring::PinSpec{py::cast<std::string>(dict["name"]),
                                    py::cast<std::string>(dict["number"]),
                                    role,
                                    requirement,
                                    terminal_kind,
                                    direction,
                                    signal_domain,
                                    drive_kind,
                                    polarity,
                                    voltage_range};
}

[[nodiscard]] std::vector<volt::authoring::PinSpec> pin_specs_from_list(const py::list &pins) {
    auto specs = std::vector<volt::authoring::PinSpec>{};
    specs.reserve(static_cast<std::size_t>(py::len(pins)));
    for (const auto item : pins) {
        specs.push_back(pin_spec_from_dict(py::cast<py::dict>(item)));
    }
    return specs;
}

[[nodiscard]] std::string string_from_pin_key(const py::handle &key) {
    if (py::isinstance<py::int_>(key) || py::isinstance<py::str>(key)) {
        return py::cast<std::string>(py::str(key));
    }

    throw std::invalid_argument{"Pin-pad mapping keys must be pin numbers or names"};
}

[[nodiscard]] py::dict diagnostic_to_dict(const volt::Diagnostic &diagnostic) {
    auto result = py::dict{};
    result["severity"] = severity_name(diagnostic.severity());
    result["code"] = diagnostic.code().value();
    result["message"] = diagnostic.message();

    auto entities = py::list{};
    for (const auto entity : diagnostic.entities()) {
        auto entity_dict = py::dict{};
        entity_dict["kind"] = entity_kind_name(entity.kind());
        entity_dict["index"] = entity.index();
        entities.append(std::move(entity_dict));
    }
    result["entities"] = std::move(entities);

    return result;
}

[[nodiscard]] py::list diagnostics_to_list(const volt::DiagnosticReport &report) {
    auto diagnostics = py::list{};
    for (const auto &diagnostic : report.diagnostics()) {
        diagnostics.append(diagnostic_to_dict(diagnostic));
    }

    return diagnostics;
}

class PyCircuit {
  public:
    [[nodiscard]] std::size_t define_resistor() {
        return volt::authoring::define_component(circuit_, volt::authoring::resistor()).index();
    }

    [[nodiscard]] std::size_t define_capacitor() {
        return volt::authoring::define_component(circuit_, volt::authoring::capacitor()).index();
    }

    [[nodiscard]] std::size_t define_led() {
        return volt::authoring::define_component(circuit_, volt::authoring::led()).index();
    }

    [[nodiscard]] std::size_t define_connector_1x02() {
        return volt::authoring::define_component(circuit_, volt::authoring::connector_1x02())
            .index();
    }

    [[nodiscard]] std::size_t define_component(const std::string &name, const py::list &pins,
                                               const py::dict &properties) {
        return volt::authoring::define_component(
                   circuit_, volt::authoring::ComponentSpec{name, pin_specs_from_list(pins),
                                                            properties_from_dict(properties)})
            .index();
    }

    [[nodiscard]] std::size_t add_net(const std::string &name, const std::string &kind) {
        return circuit_.add_net(volt::Net{volt::NetName{name}, parse_net_kind(kind)}).index();
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
                pin = circuit_.pin_by_name(component_handle, key);
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
        const auto pin = circuit_.pin_by_name(component_id(component), name);
        if (!pin.has_value()) {
            throw std::out_of_range{"Component has no pin with that name"};
        }

        return pin.value().index();
    }

    [[nodiscard]] std::size_t pin_by_number(std::size_t component,
                                            const std::string &number) const {
        const auto pin = circuit_.pin_by_number(component_id(component), number);
        if (!pin.has_value()) {
            throw std::out_of_range{"Component has no pin with that number"};
        }

        return pin.value().index();
    }

    void connect(std::size_t net, std::size_t pin) { circuit_.connect(net_id(net), pin_id(pin)); }

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
        const auto component_handle = module_component_id(component);
        const auto &component_template = circuit_.module_component_template(component_handle);
        const auto &definition = circuit_.component_definition(component_template.definition());
        for (const auto pin : definition.pins()) {
            if (circuit_.pin_definition(pin).name() == name) {
                return pin.index();
            }
        }

        throw std::out_of_range{"Module component has no pin with that name"};
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

    [[nodiscard]] std::size_t schematic_sheet(const std::string &name) {
        auto &projection = schematic_projection();
        if (const auto existing = projection.sheet_by_name(name); existing.has_value()) {
            return existing.value().index();
        }
        return projection.add_sheet(volt::Sheet{name}).index();
    }

    [[nodiscard]] std::size_t place_schematic_symbol(std::size_t sheet, std::size_t component,
                                                     const std::string &symbol, double x,
                                                     double y) {
        require_finite(x, "Schematic coordinates must be finite");
        require_finite(y, "Schematic coordinates must be finite");

        auto &projection = schematic_projection();
        const auto sheet_handle = sheet_id(sheet);
        static_cast<void>(projection.sheet(sheet_handle));

        const auto component_handle = component_id(component);
        static_cast<void>(circuit_.component(component_handle));

        const auto symbol_definition = ensure_schematic_symbol(symbol);
        return projection
            .place_symbol(sheet_handle, volt::SymbolInstance{symbol_definition, component_handle,
                                                             volt::Point{x, y}})
            .index();
    }

    [[nodiscard]] std::string schematic_to_json() {
        auto out = std::ostringstream{};
        volt::io::write_schematic(out, schematic_projection());
        return out.str();
    }

    [[nodiscard]] std::string schematic_to_svg() {
        auto out = std::ostringstream{};
        volt::io::write_schematic_svg(out, schematic_projection());
        return out.str();
    }

    [[nodiscard]] py::list validate() const {
        return diagnostics_to_list(volt::validate_circuit(circuit_));
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
    [[nodiscard]] volt::Schematic &schematic_projection() {
        if (!schematic_.has_value()) {
            schematic_.emplace(circuit_);
        }
        return schematic_.value();
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
    std::optional<volt::Schematic> schematic_;
};

} // namespace

PYBIND11_MODULE(_volt, module) {
    module.doc() = "Private Volt kernel bindings used by the Python authoring facade.";

    py::class_<PyCircuit>(module, "Circuit")
        .def(py::init<>())
        .def("define_resistor", &PyCircuit::define_resistor)
        .def("define_capacitor", &PyCircuit::define_capacitor)
        .def("define_led", &PyCircuit::define_led)
        .def("define_connector_1x02", &PyCircuit::define_connector_1x02)
        .def("define_component", &PyCircuit::define_component, py::arg("name"), py::arg("pins"),
             py::arg("properties") = py::dict{})
        .def("add_net", &PyCircuit::add_net, py::arg("name"), py::arg("kind") = "signal")
        .def("select_physical_part", &PyCircuit::select_physical_part, py::arg("component"),
             py::arg("manufacturer"), py::arg("part_number"), py::arg("package"),
             py::arg("footprint_library"), py::arg("footprint_name"), py::arg("pin_pads"),
             py::arg("properties") = py::dict{})
        .def("set_component_quantity", &PyCircuit::set_component_quantity, py::arg("component"),
             py::arg("name"), py::arg("dimension"), py::arg("value"))
        .def("set_component_percent_tolerance", &PyCircuit::set_component_percent_tolerance,
             py::arg("component"), py::arg("value"))
        .def("set_net_quantity", &PyCircuit::set_net_quantity, py::arg("net"), py::arg("name"),
             py::arg("dimension"), py::arg("value"))
        .def("select_generic_physical_part", &PyCircuit::select_generic_physical_part,
             py::arg("component"))
        .def("set_selected_part_quantity", &PyCircuit::set_selected_part_quantity,
             py::arg("component"), py::arg("name"), py::arg("dimension"), py::arg("value"))
        .def("instantiate_ref", &PyCircuit::instantiate_ref, py::arg("definition"),
             py::arg("reference"), py::arg("properties") = py::dict{})
        .def("instantiate_auto", &PyCircuit::instantiate_auto, py::arg("definition"),
             py::arg("prefix"), py::arg("properties") = py::dict{})
        .def("pin_by_name", &PyCircuit::pin_by_name, py::arg("component"), py::arg("name"))
        .def("pin_by_number", &PyCircuit::pin_by_number, py::arg("component"), py::arg("number"))
        .def("connect", &PyCircuit::connect, py::arg("net"), py::arg("pin"))
        .def("define_module", &PyCircuit::define_module, py::arg("name"))
        .def("add_template_net", &PyCircuit::add_template_net, py::arg("module"), py::arg("name"),
             py::arg("kind") = "signal")
        .def("add_port", &PyCircuit::add_port, py::arg("module"), py::arg("name"),
             py::arg("internal_net"), py::arg("role") = "passive", py::arg("required") = true)
        .def("add_module_component", &PyCircuit::add_module_component, py::arg("module"),
             py::arg("definition"), py::arg("reference"), py::arg("properties") = py::dict{})
        .def("module_component_pin_by_name", &PyCircuit::module_component_pin_by_name,
             py::arg("component"), py::arg("name"))
        .def("module_component_pin_by_number", &PyCircuit::module_component_pin_by_number,
             py::arg("component"), py::arg("number"))
        .def("connect_module_pin", &PyCircuit::connect_module_pin, py::arg("module"),
             py::arg("net"), py::arg("component"), py::arg("pin"))
        .def("instantiate_root_module", &PyCircuit::instantiate_root_module, py::arg("definition"),
             py::arg("name"))
        .def("concrete_component_for", &PyCircuit::concrete_component_for, py::arg("instance"),
             py::arg("component"))
        .def("bind_port", &PyCircuit::bind_port, py::arg("instance"), py::arg("port"),
             py::arg("parent_net"))
        .def("template_nets", &PyCircuit::template_nets, py::arg("module"))
        .def("module_ports", &PyCircuit::module_ports, py::arg("module"))
        .def("module_components", &PyCircuit::module_components, py::arg("module"))
        .def("module_connections", &PyCircuit::module_connections, py::arg("module"))
        .def("module_net_origins", &PyCircuit::module_net_origins, py::arg("instance"))
        .def("module_component_origins", &PyCircuit::module_component_origins, py::arg("instance"))
        .def("port_bindings", &PyCircuit::port_bindings, py::arg("instance"))
        .def("schematic_sheet", &PyCircuit::schematic_sheet, py::arg("name"))
        .def("place_schematic_symbol", &PyCircuit::place_schematic_symbol, py::arg("sheet"),
             py::arg("component"), py::arg("symbol"), py::arg("x"), py::arg("y"))
        .def("schematic_to_json", &PyCircuit::schematic_to_json)
        .def("schematic_to_svg", &PyCircuit::schematic_to_svg)
        .def("validate", &PyCircuit::validate)
        .def("validate_for_pcb", &PyCircuit::validate_for_pcb)
        .def("to_json", &PyCircuit::to_json);
}
