#include "board_bindings.hpp"

#include "py_board.hpp"
#include "py_circuit.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace volt::python {

void bind_board(pybind11::module_ &module) {
    py::class_<PyBoardRegistry>(module, "BoardRegistry")
        .def(py::init<const PyCircuit &>(), py::keep_alive<1, 2>())
        .def("add", &PyBoardRegistry::add, py::arg("name"),
             py::return_value_policy::reference_internal)
        .def("board", &PyBoardRegistry::board, py::arg("name") = std::nullopt,
             py::return_value_policy::reference_internal)
        .def("names", &PyBoardRegistry::names);

    py::class_<PyBoard>(module, "Board")
        .def_property_readonly("name", &PyBoard::name)
        .def_property_readonly("units", &PyBoard::units)
        .def("design_rules", &PyBoard::design_rules)
        .def("set_design_rules", &PyBoard::set_design_rules, py::arg("copper_clearance_mm"),
             py::arg("minimum_track_width_mm"), py::arg("minimum_via_drill_diameter_mm"),
             py::arg("minimum_via_annular_diameter_mm"), py::arg("board_outline_clearance_mm"),
             py::arg("package_assembly_clearance_mm"))
        .def("set_capability_profile", &PyBoard::set_capability_profile, py::arg("profile"))
        .def("add_layer", &PyBoard::add_layer, py::arg("name"), py::arg("role"), py::arg("side"),
             py::arg("thickness_mm") = 0.0, py::arg("enabled") = true,
             py::arg("copper_weight_oz") = std::nullopt)
        .def("set_layer_stack", &PyBoard::set_layer_stack, py::arg("layers"),
             py::arg("board_thickness_mm"),
             py::arg("dielectrics") = std::vector<std::pair<double, double>>{})
        .def("set_rectangular_outline", &PyBoard::set_rectangular_outline, py::arg("x"),
             py::arg("y"), py::arg("width"), py::arg("height"))
        .def("set_polygon_outline", &PyBoard::set_polygon_outline, py::arg("vertices"))
        .def("outline_vertices", &PyBoard::outline_vertices)
        .def("add_hole", &PyBoard::add_hole, py::arg("label"), py::arg("x"), py::arg("y"),
             py::arg("drill_diameter_mm"), py::arg("plated") = false, py::arg("role") = "",
             py::arg("finished_diameter_mm") = std::nullopt)
        .def("add_slot", &PyBoard::add_slot, py::arg("label"), py::arg("start_x"),
             py::arg("start_y"), py::arg("end_x"), py::arg("end_y"), py::arg("width_mm"),
             py::arg("plated") = false, py::arg("role") = "")
        .def("add_cutout", &PyBoard::add_cutout, py::arg("label"), py::arg("outline"),
             py::arg("role") = "")
        .def("add_circle", &PyBoard::add_circle, py::arg("label"), py::arg("x"), py::arg("y"),
             py::arg("diameter_mm"), py::arg("side") = "top", py::arg("role") = "")
        .def("place_component", &PyBoard::place_component, py::arg("component"), py::arg("x"),
             py::arg("y"), py::arg("rotation_degrees") = 0.0, py::arg("side") = "top",
             py::arg("locked") = false)
        .def("placement_refs", &PyBoard::placement_refs)
        .def("placed_model_3d_refs", &PyBoard::placed_model_3d_refs)
        .def("stackup", &PyBoard::stackup)
        .def("component_footprint_pads", &PyBoard::component_footprint_pads, py::arg("component"))
        .def("add_track", &PyBoard::add_track, py::arg("net"), py::arg("layer"), py::arg("points"),
             py::arg("width_mm"))
        .def("add_track_for_route", &PyBoard::add_track_for_route, py::arg("net"), py::arg("layer"),
             py::arg("endpoints"), py::arg("width_mm"))
        .def("track_net", &PyBoard::track_net, py::arg("track"))
        .def("add_via", &PyBoard::add_via, py::arg("net"), py::arg("x"), py::arg("y"),
             py::arg("start_layer"), py::arg("end_layer"), py::arg("drill_diameter_mm"),
             py::arg("annular_diameter_mm"))
        .def("assisted_connect", &PyBoard::assisted_connect, py::arg("net"), py::arg("start_x"),
             py::arg("start_y"), py::arg("start_layer"), py::arg("end_x"), py::arg("end_y"),
             py::arg("end_layer"))
        .def("escape", &PyBoard::escape, py::arg("component"))
        .def("add_zone", &PyBoard::add_zone, py::arg("net"), py::arg("layers"), py::arg("outline"),
             py::arg("fill"), py::arg("priority"))
        .def("add_keepout", &PyBoard::add_keepout, py::arg("layers"), py::arg("outline"),
             py::arg("restrictions"))
        .def("add_room", &PyBoard::add_room, py::arg("name"), py::arg("outline"), py::arg("layers"),
             py::arg("copper_clearance_mm") = std::nullopt,
             py::arg("track_width_mm") = std::nullopt, py::arg("priority") = 0)
        .def("add_text", &PyBoard::add_text, py::arg("text"), py::arg("x"), py::arg("y"),
             py::arg("layer"), py::arg("rotation_degrees"), py::arg("size_mm"), py::arg("locked"))
        .def("resolve_pads", &PyBoard::resolve_pads)
        .def("validate", &PyBoard::validate)
        .def("validate_assembly", &PyBoard::validate_assembly,
             py::arg("rotation_offsets") = py::dict{})
        .def("cpl_json", &PyBoard::cpl_json, py::arg("rotation_offsets") = py::dict{})
        .def("cpl_csv", &PyBoard::cpl_csv, py::arg("rotation_offsets") = py::dict{})
        .def("to_json", &PyBoard::to_json)
        .def("to_svg", &PyBoard::to_svg, py::arg("pad_net_overlays") = true,
             py::arg("diagnostic_overlays") = true, py::arg("ratsnest_edges") = true,
             py::arg("layer_filter") = std::nullopt)
        .def("to_kicad_pcb", &PyBoard::to_kicad_pcb)
        .def("to_fabrication_files", &PyBoard::to_fabrication_files);
}

} // namespace volt::python
