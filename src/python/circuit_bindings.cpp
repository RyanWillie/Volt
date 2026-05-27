#include "circuit_bindings.hpp"

#include "py_circuit.hpp"

namespace volt::python {

void bind_circuit(pybind11::module_ &module) {
    py::class_<PyCircuit>(module, "Circuit")
        .def(py::init<>())
        .def("define_resistor", &PyCircuit::define_resistor)
        .def("define_capacitor", &PyCircuit::define_capacitor)
        .def("define_polarized_capacitor", &PyCircuit::define_polarized_capacitor)
        .def("define_inductor", &PyCircuit::define_inductor)
        .def("define_diode", &PyCircuit::define_diode)
        .def("define_led", &PyCircuit::define_led)
        .def("define_switch_spst", &PyCircuit::define_switch_spst)
        .def("define_crystal_2pin", &PyCircuit::define_crystal_2pin)
        .def("define_test_point", &PyCircuit::define_test_point)
        .def("define_connector_1x01", &PyCircuit::define_connector_1x01)
        .def("define_connector_1x02", &PyCircuit::define_connector_1x02)
        .def("define_connector_1x03", &PyCircuit::define_connector_1x03)
        .def("define_regulator_3pin", &PyCircuit::define_regulator_3pin)
        .def("define_op_amp_5pin", &PyCircuit::define_op_amp_5pin)
        .def("define_component", &PyCircuit::define_component, py::arg("name"), py::arg("pins"),
             py::arg("properties") = py::dict{}, py::arg("source_namespace") = "",
             py::arg("source_name") = "", py::arg("source_version") = "",
             py::arg("schematic_symbols") = py::list{})
        .def("add_net", &PyCircuit::add_net, py::arg("name"), py::arg("kind") = "signal")
        .def("net_refs", &PyCircuit::net_refs)
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
        .def("pin_component", &PyCircuit::pin_component, py::arg("pin"))
        .def("component_reference", &PyCircuit::component_reference, py::arg("component"))
        .def("pin_refs", &PyCircuit::pin_refs, py::arg("component"))
        .def("component_schematic_symbol", &PyCircuit::component_schematic_symbol,
             py::arg("component"), py::arg("variant"))
        .def("connect", &PyCircuit::connect, py::arg("net"), py::arg("pin"))
        .def("net_of", &PyCircuit::net_of, py::arg("pin"))
        .def("net_pins", &PyCircuit::net_pins, py::arg("net"))
        .def("mark_intentional_stub_net", &PyCircuit::mark_intentional_stub_net, py::arg("net"))
        .def("mark_intentional_no_connect_pin", &PyCircuit::mark_intentional_no_connect_pin,
             py::arg("pin"))
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
        .def("module_component_pin_refs", &PyCircuit::module_component_pin_refs,
             py::arg("component"))
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
        .def("schematic_sheet", &PyCircuit::schematic_sheet, py::arg("name"),
             py::arg("metadata") = py::dict{})
        .def("schematic_region", &PyCircuit::schematic_region, py::arg("sheet"), py::arg("region"))
        .def("register_schematic_symbol", &PyCircuit::register_schematic_symbol, py::arg("symbol"))
        .def("place_schematic_symbol", &PyCircuit::place_schematic_symbol, py::arg("sheet"),
             py::arg("component"), py::arg("symbol"), py::arg("x"), py::arg("y"),
             py::arg("orientation"), py::arg("authored_region") = std::nullopt)
        .def("schematic_symbol_orientation", &PyCircuit::schematic_symbol_orientation,
             py::arg("instance"))
        .def("schematic_symbol_pin_anchor", &PyCircuit::schematic_symbol_pin_anchor,
             py::arg("instance"), py::arg("number"))
        .def("schematic_symbol_pin_refs", &PyCircuit::schematic_symbol_pin_refs,
             py::arg("instance"))
        .def("add_schematic_wire", &PyCircuit::add_schematic_wire, py::arg("sheet"), py::arg("net"),
             py::arg("points"), py::arg("route_intent"), py::arg("authored_region") = std::nullopt)
        .def("add_schematic_wire_for_endpoints", &PyCircuit::add_schematic_wire_for_endpoints,
             py::arg("sheet"), py::arg("net"), py::arg("points"), py::arg("endpoints"),
             py::arg("route_intent"), py::arg("authored_region") = std::nullopt)
        .def("add_schematic_net_label", &PyCircuit::add_schematic_net_label, py::arg("sheet"),
             py::arg("net"), py::arg("x"), py::arg("y"), py::arg("orientation"),
             py::arg("authored_region") = std::nullopt, py::arg("label") = std::nullopt,
             py::arg("horizontal_alignment") = "Start", py::arg("vertical_alignment") = "Baseline",
             py::arg("font_size") = std::nullopt)
        .def("add_schematic_net_label_for_endpoint",
             &PyCircuit::add_schematic_net_label_for_endpoint, py::arg("sheet"), py::arg("net"),
             py::arg("endpoint"), py::arg("orientation"), py::arg("authored_region") = std::nullopt,
             py::arg("label") = std::nullopt, py::arg("horizontal_alignment") = "Start",
             py::arg("vertical_alignment") = "Baseline", py::arg("font_size") = std::nullopt)
        .def("add_schematic_junction", &PyCircuit::add_schematic_junction, py::arg("sheet"),
             py::arg("net"), py::arg("x"), py::arg("y"), py::arg("authored_region") = std::nullopt)
        .def("add_schematic_junction_for_endpoint", &PyCircuit::add_schematic_junction_for_endpoint,
             py::arg("sheet"), py::arg("net"), py::arg("endpoint"),
             py::arg("authored_region") = std::nullopt)
        .def("add_schematic_terminal_marker", &PyCircuit::add_schematic_terminal_marker,
             py::arg("sheet"), py::arg("net"), py::arg("kind"), py::arg("x"), py::arg("y"),
             py::arg("orientation"), py::arg("authored_region") = std::nullopt,
             py::arg("label") = std::nullopt)
        .def("add_schematic_terminal_marker_for_endpoint",
             &PyCircuit::add_schematic_terminal_marker_for_endpoint, py::arg("sheet"),
             py::arg("net"), py::arg("kind"), py::arg("endpoint"), py::arg("orientation"),
             py::arg("authored_region") = std::nullopt, py::arg("label") = std::nullopt)
        .def("add_schematic_no_connect_marker", &PyCircuit::add_schematic_no_connect_marker,
             py::arg("sheet"), py::arg("pin"), py::arg("x"), py::arg("y"), py::arg("orientation"),
             py::arg("reason") = "", py::arg("authored_region") = std::nullopt)
        .def("add_schematic_sheet_port", &PyCircuit::add_schematic_sheet_port, py::arg("sheet"),
             py::arg("net"), py::arg("name"), py::arg("kind"), py::arg("x"), py::arg("y"),
             py::arg("orientation"), py::arg("authored_region") = std::nullopt)
        .def("add_schematic_sheet_port_for_endpoint",
             &PyCircuit::add_schematic_sheet_port_for_endpoint, py::arg("sheet"), py::arg("net"),
             py::arg("name"), py::arg("kind"), py::arg("endpoint"), py::arg("orientation"),
             py::arg("authored_region") = std::nullopt)
        .def("add_schematic_symbol_field", &PyCircuit::add_schematic_symbol_field, py::arg("sheet"),
             py::arg("instance"), py::arg("name"), py::arg("value"), py::arg("x"), py::arg("y"),
             py::arg("orientation"), py::arg("authored_region") = std::nullopt,
             py::arg("horizontal_alignment") = "Middle", py::arg("vertical_alignment") = "Baseline",
             py::arg("font_size") = std::nullopt)
        .def("schematic_to_json", &PyCircuit::schematic_to_json)
        .def("schematic_to_svg", &PyCircuit::schematic_to_svg)
        .def("schematic_to_body_svg", &PyCircuit::schematic_to_body_svg, py::arg("sheet"),
             py::arg("margin") = 4.0)
        .def("schematic_svg_pages", &PyCircuit::schematic_svg_pages)
        .def("load_schematic_json", &PyCircuit::load_schematic_json, py::arg("text"))
        .def("schematic_sheet_names", &PyCircuit::schematic_sheet_names)
        .def("validate", &PyCircuit::validate)
        .def("validate_schematic", &PyCircuit::validate_schematic)
        .def("validate_schematic_readability", &PyCircuit::validate_schematic_readability)
        .def("validate_for_pcb", &PyCircuit::validate_for_pcb)
        .def("to_json", &PyCircuit::to_json);
}

} // namespace volt::python
