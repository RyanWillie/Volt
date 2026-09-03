#include "electrical_model_bindings.hpp"

#include "binding_part_definition_conversions.hpp"

#include <volt/electrical/passive_model.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <pybind11/operators.h>
#include <pybind11/stl.h>

namespace volt::python {
namespace {

template <typename Key> using KeyInput = std::variant<std::string, Key>;

template <typename Key> [[nodiscard]] Key key_value(KeyInput<Key> input) {
    return std::visit([](auto value) { return Key{std::move(value)}; }, std::move(input));
}

template <typename Key> void bind_key(py::class_<Key> binding) {
    binding.def(py::init<std::string>(), py::arg("value"))
        .def_property_readonly("value", &Key::value)
        .def("__str__", &Key::value)
        .def("__hash__", [](const Key &value) { return py::hash(py::str{value.value()}); })
        .def(py::self == py::self);
}

[[nodiscard]] ModelEndpoint model_endpoint(const py::handle &value) {
    if (py::isinstance<ModelTerminalKey>(value)) {
        return value.cast<ModelTerminalKey>();
    }
    if (py::isinstance<ModelInternalNodeKey>(value)) {
        return value.cast<ModelInternalNodeKey>();
    }
    throw py::type_error{"Model endpoint must be a ModelTerminalKey or ModelInternalNodeKey"};
}

[[nodiscard]] PartElectricalModelBuilder::Endpoint builder_endpoint(const py::handle &value) {
    if (py::isinstance<PartElectricalModelBuilder::TerminalHandle>(value)) {
        return value.cast<PartElectricalModelBuilder::TerminalHandle>();
    }
    if (py::isinstance<PartElectricalModelBuilder::InternalNodeHandle>(value)) {
        return value.cast<PartElectricalModelBuilder::InternalNodeHandle>();
    }
    throw py::type_error{
        "Builder endpoint must be a ModelTerminalHandle or ModelInternalNodeHandle"};
}

template <typename Value> [[nodiscard]] py::tuple copied_tuple(const std::vector<Value> &values) {
    auto result = py::tuple{values.size()};
    for (std::size_t index = 0; index < values.size(); ++index) {
        result[index] = py::cast(values[index], py::return_value_policy::copy);
    }
    return result;
}

template <typename Element> void bind_element(py::class_<Element> binding) {
    binding
        .def(py::init([](KeyInput<ModelElementKey> key, const py::handle &from,
                         const py::handle &to, ModelParameter parameter) {
                 return Element{key_value(std::move(key)), model_endpoint(from), model_endpoint(to),
                                std::move(parameter)};
             }),
             py::arg("key"), py::arg("from_"), py::arg("to"), py::arg("parameter"))
        .def_property_readonly("key", &Element::key, py::return_value_policy::copy)
        .def_property_readonly("from_", &Element::from, py::return_value_policy::copy)
        .def_property_readonly("to", &Element::to, py::return_value_policy::copy)
        .def_property_readonly("parameter", &Element::parameter, py::return_value_policy::copy);
}

template <typename Element>
void bind_builder_element(py::class_<PartElectricalModelBuilder> &binding, const char *name) {
    binding.def(
        name,
        [](PartElectricalModelBuilder &builder, KeyInput<ModelElementKey> key,
           const py::handle &from, const py::handle &to,
           ModelParameter parameter) -> PartElectricalModelBuilder & {
            return builder.add<Element>(key_value(std::move(key)), builder_endpoint(from),
                                        builder_endpoint(to), std::move(parameter));
        },
        py::arg("key"), py::arg("from_"), py::arg("to"), py::arg("parameter"),
        py::return_value_policy::reference_internal);
}

void bind_quantities(py::module_ &module) {
    py::enum_<UnitDimension>(module, "UnitDimension")
        .value("RESISTANCE", UnitDimension::Resistance)
        .value("CAPACITANCE", UnitDimension::Capacitance)
        .value("INDUCTANCE", UnitDimension::Inductance)
        .value("VOLTAGE", UnitDimension::Voltage)
        .value("CURRENT", UnitDimension::Current)
        .value("POWER", UnitDimension::Power)
        .value("FREQUENCY", UnitDimension::Frequency)
        .value("TIME", UnitDimension::Time)
        .value("TEMPERATURE", UnitDimension::Temperature)
        .value("RATIO", UnitDimension::Ratio);
    py::class_<Quantity>(module, "Quantity")
        .def(py::init<UnitDimension, double>(), py::arg("dimension"), py::arg("value"))
        .def_property_readonly("dimension", &Quantity::dimension)
        .def_property_readonly("value", &Quantity::value)
        .def(py::self == py::self);
    py::enum_<ToleranceMode>(module, "ToleranceMode")
        .value("ABSOLUTE", ToleranceMode::Absolute)
        .value("PERCENT", ToleranceMode::Percent);
    py::class_<Tolerance>(module, "Tolerance")
        .def_static("absolute", &Tolerance::absolute, py::arg("minus"), py::arg("plus"))
        .def_static("percent", py::overload_cast<double>(&Tolerance::percent), py::arg("value"))
        .def_static("percent", py::overload_cast<double, double>(&Tolerance::percent),
                    py::arg("minus"), py::arg("plus"))
        .def_property_readonly("mode", &Tolerance::mode)
        .def_property_readonly("minus", &Tolerance::minus, py::return_value_policy::copy)
        .def_property_readonly("plus", &Tolerance::plus, py::return_value_policy::copy);
    py::class_<QuantityRange>(module, "QuantityRange")
        .def_static("bounded", &QuantityRange::bounded, py::arg("minimum"), py::arg("maximum"))
        .def_static("at_least", py::overload_cast<Quantity>(&QuantityRange::minimum),
                    py::arg("minimum"))
        .def_static("at_most", py::overload_cast<Quantity>(&QuantityRange::maximum),
                    py::arg("maximum"))
        .def_property_readonly("dimension", &QuantityRange::dimension)
        .def_property_readonly("minimum", py::overload_cast<>(&QuantityRange::minimum, py::const_),
                               py::return_value_policy::copy)
        .def_property_readonly("maximum", py::overload_cast<>(&QuantityRange::maximum, py::const_),
                               py::return_value_policy::copy);
    module.def(
        "ohms", [](double value) { return Quantity{UnitDimension::Resistance, value}; },
        py::arg("value"));
    module.def(
        "farads", [](double value) { return Quantity{UnitDimension::Capacitance, value}; },
        py::arg("value"));
    module.def(
        "henries", [](double value) { return Quantity{UnitDimension::Inductance, value}; },
        py::arg("value"));
    module.def(
        "hertz", [](double value) { return Quantity{UnitDimension::Frequency, value}; },
        py::arg("value"));
    module.def(
        "seconds", [](double value) { return Quantity{UnitDimension::Time, value}; },
        py::arg("value"));
}

} // namespace

void bind_electrical_model(py::module_ &module) {
    bind_quantities(module);
    bind_key(py::class_<ContentHash>(module, "ContentHash"));
    bind_key(py::class_<PinKey>(module, "PinKey"));
    bind_key(py::class_<ModelTerminalKey>(module, "ModelTerminalKey"));
    bind_key(py::class_<ModelInternalNodeKey>(module, "ModelInternalNodeKey"));
    bind_key(py::class_<ModelElementKey>(module, "ModelElementKey"));

    py::class_<ModelTerminal>(module, "ModelTerminal")
        .def(py::init([](KeyInput<ModelTerminalKey> key, KeyInput<PinKey> pin) {
                 return ModelTerminal{key_value(std::move(key)), key_value(std::move(pin))};
             }),
             py::arg("key"), py::arg("pin"))
        .def_property_readonly("key", &ModelTerminal::key, py::return_value_policy::copy)
        .def_property_readonly("pin", &ModelTerminal::pin, py::return_value_policy::copy);
    py::class_<ModelInternalNode>(module, "ModelInternalNode")
        .def(py::init([](KeyInput<ModelInternalNodeKey> key) {
                 return ModelInternalNode{key_value(std::move(key))};
             }),
             py::arg("key"))
        .def_property_readonly("key", &ModelInternalNode::key, py::return_value_policy::copy);
    py::class_<ModelParameter>(module, "ModelParameter")
        .def(py::init([](Quantity nominal, std::optional<Tolerance> tolerance,
                         const std::vector<KeyInput<ContentHash>> &evidence) {
                 auto references = std::vector<ContentHash>{};
                 references.reserve(evidence.size());
                 for (const auto &reference : evidence) {
                     references.push_back(key_value(reference));
                 }
                 return ModelParameter{nominal, tolerance, std::move(references)};
             }),
             py::arg("nominal"), py::arg("tolerance") = py::none(),
             py::arg("evidence") = py::tuple{})
        .def_property_readonly("nominal", &ModelParameter::nominal, py::return_value_policy::copy)
        .def_property_readonly("tolerance", &ModelParameter::tolerance,
                               py::return_value_policy::copy)
        .def_property_readonly("bounds", &ModelParameter::bounds)
        .def_property_readonly(
            "evidence", [](const ModelParameter &value) { return copied_tuple(value.evidence()); });
    bind_element(py::class_<ResistanceElement>(module, "ResistanceElement"));
    bind_element(py::class_<CapacitanceElement>(module, "CapacitanceElement"));
    bind_element(py::class_<InductanceElement>(module, "InductanceElement"));
    py::class_<PartElectricalModel>(module, "PartElectricalModel")
        .def_property_readonly("implemented_component", &PartElectricalModel::implemented_component,
                               py::return_value_policy::copy)
        .def_property_readonly(
            "terminals",
            [](const PartElectricalModel &value) { return copied_tuple(value.terminals()); })
        .def_property_readonly(
            "internal_nodes",
            [](const PartElectricalModel &value) { return copied_tuple(value.internal_nodes()); })
        .def_property_readonly("elements", [](const PartElectricalModel &value) {
            return copied_tuple(value.elements());
        });

    py::class_<PartElectricalModelBuilder::TerminalHandle>(module, "ModelTerminalHandle")
        .def_property_readonly("key", &PartElectricalModelBuilder::TerminalHandle::key,
                               py::return_value_policy::copy)
        .def("belongs_to", &PartElectricalModelBuilder::TerminalHandle::belongs_to,
             py::arg("builder"));
    py::class_<PartElectricalModelBuilder::InternalNodeHandle>(module, "ModelInternalNodeHandle")
        .def_property_readonly("key", &PartElectricalModelBuilder::InternalNodeHandle::key,
                               py::return_value_policy::copy)
        .def("belongs_to", &PartElectricalModelBuilder::InternalNodeHandle::belongs_to,
             py::arg("builder"));
    auto builder =
        py::class_<PartElectricalModelBuilder>(module, "PartElectricalModelBuilder")
            .def(py::init([](const py::dict &payload) {
                     auto circuit = Circuit{};
                     const auto id =
                         circuit.define_component(component_spec_from_part_dict(payload));
                     return std::make_unique<PartElectricalModelBuilder>(circuit.get(id));
                 }),
                 py::arg("component_payload"))
            .def(
                "terminal",
                [](PartElectricalModelBuilder &value, KeyInput<ModelTerminalKey> key,
                   KeyInput<PinKey> pin) {
                    return value.terminal(key_value(std::move(key)), key_value(std::move(pin)));
                },
                py::arg("key"), py::arg("pin"))
            .def(
                "internal_node",
                [](PartElectricalModelBuilder &value, KeyInput<ModelInternalNodeKey> key) {
                    return value.internal_node(key_value(std::move(key)));
                },
                py::arg("key"))
            .def("build", &PartElectricalModelBuilder::build);
    bind_builder_element<ResistanceElement>(builder, "resistance");
    bind_builder_element<CapacitanceElement>(builder, "capacitance");
    bind_builder_element<InductanceElement>(builder, "inductance");
}

} // namespace volt::python
