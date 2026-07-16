#include "schematic_bindings.hpp"

#include "py_circuit.hpp"
#include "py_schematic.hpp"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace volt::python {

void bind_schematic(pybind11::module_ &module) {
    py::class_<PySchematicDocument>(module, "SchematicDocument")
        .def(py::init<const PyCircuit &>(), py::keep_alive<1, 2>())
        .def("schematic_sheet", &PySchematicDocument::schematic_sheet, py::arg("name"),
             py::arg("metadata") = py::dict{})
        .def("schematic_region", &PySchematicDocument::schematic_region, py::arg("sheet"),
             py::arg("region"))
        .def("register_schematic_symbol", &PySchematicDocument::register_schematic_symbol,
             py::arg("symbol"))
        .def("place_schematic_symbol", &PySchematicDocument::place_schematic_symbol,
             py::arg("sheet"), py::arg("component"), py::arg("symbol"), py::arg("x"), py::arg("y"),
             py::arg("orientation"), py::arg("authored_region") = std::nullopt)
        .def("schematic_symbol_orientation", &PySchematicDocument::schematic_symbol_orientation,
             py::arg("instance"))
        .def("schematic_symbol_pin_anchor", &PySchematicDocument::schematic_symbol_pin_anchor,
             py::arg("instance"), py::arg("number"))
        .def("schematic_symbol_pin_refs", &PySchematicDocument::schematic_symbol_pin_refs,
             py::arg("instance"))
        .def("add_schematic_wire", &PySchematicDocument::add_schematic_wire, py::arg("sheet"),
             py::arg("net"), py::arg("points"), py::arg("route_intent"),
             py::arg("authored_region") = std::nullopt)
        .def("add_schematic_wire_for_endpoints",
             &PySchematicDocument::add_schematic_wire_for_endpoints, py::arg("sheet"),
             py::arg("net"), py::arg("points"), py::arg("endpoints"), py::arg("route_intent"),
             py::arg("authored_region") = std::nullopt)
        .def("add_schematic_net_label", &PySchematicDocument::add_schematic_net_label,
             py::arg("sheet"), py::arg("net"), py::arg("x"), py::arg("y"), py::arg("orientation"),
             py::arg("authored_region") = std::nullopt, py::arg("label") = std::nullopt,
             py::arg("horizontal_alignment") = "Start", py::arg("vertical_alignment") = "Baseline",
             py::arg("font_size") = std::nullopt)
        .def("add_schematic_net_label_for_endpoint",
             &PySchematicDocument::add_schematic_net_label_for_endpoint, py::arg("sheet"),
             py::arg("net"), py::arg("endpoint"), py::arg("orientation"),
             py::arg("authored_region") = std::nullopt, py::arg("label") = std::nullopt,
             py::arg("horizontal_alignment") = "Start", py::arg("vertical_alignment") = "Baseline",
             py::arg("font_size") = std::nullopt)
        .def("add_schematic_junction", &PySchematicDocument::add_schematic_junction,
             py::arg("sheet"), py::arg("net"), py::arg("x"), py::arg("y"),
             py::arg("authored_region") = std::nullopt)
        .def("add_schematic_junction_for_endpoint",
             &PySchematicDocument::add_schematic_junction_for_endpoint, py::arg("sheet"),
             py::arg("net"), py::arg("endpoint"), py::arg("authored_region") = std::nullopt)
        .def("add_schematic_terminal_marker", &PySchematicDocument::add_schematic_terminal_marker,
             py::arg("sheet"), py::arg("net"), py::arg("kind"), py::arg("x"), py::arg("y"),
             py::arg("orientation"), py::arg("authored_region") = std::nullopt,
             py::arg("label") = std::nullopt)
        .def("add_schematic_terminal_marker_for_endpoint",
             &PySchematicDocument::add_schematic_terminal_marker_for_endpoint, py::arg("sheet"),
             py::arg("net"), py::arg("kind"), py::arg("endpoint"), py::arg("orientation"),
             py::arg("authored_region") = std::nullopt, py::arg("label") = std::nullopt)
        .def("add_schematic_no_connect_marker",
             &PySchematicDocument::add_schematic_no_connect_marker, py::arg("sheet"),
             py::arg("pin"), py::arg("x"), py::arg("y"), py::arg("orientation"),
             py::arg("reason") = "", py::arg("authored_region") = std::nullopt)
        .def("add_schematic_sheet_port", &PySchematicDocument::add_schematic_sheet_port,
             py::arg("sheet"), py::arg("net"), py::arg("name"), py::arg("kind"), py::arg("x"),
             py::arg("y"), py::arg("orientation"), py::arg("authored_region") = std::nullopt)
        .def("add_schematic_sheet_port_for_endpoint",
             &PySchematicDocument::add_schematic_sheet_port_for_endpoint, py::arg("sheet"),
             py::arg("net"), py::arg("name"), py::arg("kind"), py::arg("endpoint"),
             py::arg("orientation"), py::arg("authored_region") = std::nullopt)
        .def("add_schematic_symbol_field", &PySchematicDocument::add_schematic_symbol_field,
             py::arg("sheet"), py::arg("instance"), py::arg("name"), py::arg("value"), py::arg("x"),
             py::arg("y"), py::arg("orientation"), py::arg("authored_region") = std::nullopt,
             py::arg("horizontal_alignment") = "Middle", py::arg("vertical_alignment") = "Baseline",
             py::arg("font_size") = std::nullopt)
        .def("schematic_to_json", &PySchematicDocument::schematic_to_json)
        .def("schematic_to_svg", &PySchematicDocument::schematic_to_svg)
        .def("schematic_to_body_svg", &PySchematicDocument::schematic_to_body_svg, py::arg("sheet"),
             py::arg("margin") = 4.0)
        .def("schematic_svg_pages", &PySchematicDocument::schematic_svg_pages)
        .def("load_schematic_json", &PySchematicDocument::load_schematic_json, py::arg("text"))
        .def("schematic_sheet_names", &PySchematicDocument::schematic_sheet_names)
        .def("validate_schematic", &PySchematicDocument::validate_schematic)
        .def("validate_schematic_readability",
             &PySchematicDocument::validate_schematic_readability);
}

} // namespace volt::python
