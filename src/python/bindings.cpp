#include <cmath>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>

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

namespace py = pybind11;

namespace {

[[nodiscard]] volt::ComponentDefId component_def_id(std::size_t index) {
    return volt::ComponentDefId{index};
}

[[nodiscard]] volt::ComponentId component_id(std::size_t index) { return volt::ComponentId{index}; }

[[nodiscard]] volt::PinId pin_id(std::size_t index) { return volt::PinId{index}; }

[[nodiscard]] volt::NetId net_id(std::size_t index) { return volt::NetId{index}; }

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

    return volt::authoring::PinSpec{py::cast<std::string>(dict["name"]),
                                    py::cast<std::string>(dict["number"]), role, requirement};
}

[[nodiscard]] std::vector<volt::authoring::PinSpec> pin_specs_from_list(const py::list &pins) {
    auto specs = std::vector<volt::authoring::PinSpec>{};
    specs.reserve(static_cast<std::size_t>(py::len(pins)));
    for (const auto item : pins) {
        specs.push_back(pin_spec_from_dict(py::cast<py::dict>(item)));
    }
    return specs;
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

    [[nodiscard]] py::list validate() const {
        const auto report = volt::validate_circuit(circuit_);
        auto diagnostics = py::list{};
        for (const auto &diagnostic : report.diagnostics()) {
            diagnostics.append(diagnostic_to_dict(diagnostic));
        }

        return diagnostics;
    }

    [[nodiscard]] std::string to_json() const {
        auto out = std::ostringstream{};
        volt::io::write_logical_circuit(out, circuit_);
        return out.str();
    }

  private:
    volt::Circuit circuit_;
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
        .def("validate", &PyCircuit::validate)
        .def("to_json", &PyCircuit::to_json);
}
