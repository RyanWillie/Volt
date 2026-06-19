#include "circuit_bindings.hpp"

#include "binding_part_definition_conversions.hpp"
#include "binding_pcb_conversions.hpp"
#include "py_circuit.hpp"

#include <volt/core/content_hash.hpp>
#include <volt/io/capabilities/board_capability_profile.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace volt::python {
namespace {

template <std::size_t Size>
[[nodiscard]] py::tuple
string_view_catalog_to_tuple(const std::array<std::string_view, Size> &catalog) {
    auto result = py::tuple{Size};
    for (std::size_t index = 0; index < Size; ++index) {
        result[index] = py::str{catalog[index]};
    }
    return result;
}

} // namespace

void bind_circuit(pybind11::module_ &module) {
    module.def("diagnostic_categories",
               []() { return string_view_catalog_to_tuple(diagnostic_category_catalogs::All); });
    module.def("erc_diagnostic_codes",
               []() { return string_view_catalog_to_tuple(diagnostic_code_catalogs::Erc); });
    module.def("drc_diagnostic_codes",
               []() { return string_view_catalog_to_tuple(diagnostic_code_catalogs::Drc); });
    module.def("part_lineup_diagnostic_codes",
               []() { return string_view_catalog_to_tuple(diagnostic_code_catalogs::PartLineup); });
    module.def("pcb_visual_diagnostic_codes",
               []() { return string_view_catalog_to_tuple(diagnostic_code_catalogs::PcbVisual); });
    module.def("pcb_fabrication_diagnostic_codes", []() {
        return string_view_catalog_to_tuple(diagnostic_code_catalogs::PcbFabrication);
    });
    module.def("bom_diagnostic_codes",
               []() { return string_view_catalog_to_tuple(diagnostic_code_catalogs::Bom); });
    module.def("read_capability_profile_text", [](const std::string &text) {
        return board_capability_profile_to_dict(volt::io::read_capability_profile_text(text));
    });
    module.def("normalize_capability_profile", [](const py::dict &profile) {
        return board_capability_profile_to_dict(board_capability_profile_from_dict(profile));
    });
    module.def("content_hash", [](const py::bytes &bytes) {
        const auto data = static_cast<std::string>(bytes);
        return volt::sha256_content_hash(data).value();
    });
    module.def("part_definition_artifact",
               [](const py::dict &part) { return part_definition_artifact_from_dict(part); });

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
        .def("add_net_class", &PyCircuit::add_net_class, py::arg("name"),
             py::arg("options") = py::dict{})
        .def("assign_net_class", &PyCircuit::assign_net_class, py::arg("net"), py::arg("net_class"))
        .def("net_class_info", &PyCircuit::net_class_info, py::arg("net_class"))
        .def("net_refs", &PyCircuit::net_refs)
        .def("component_refs", &PyCircuit::component_refs)
        .def("component_selected_part_model_3d", &PyCircuit::component_selected_part_model_3d,
             py::arg("component"))
        .def("select_physical_part", &PyCircuit::select_physical_part, py::arg("component"),
             py::arg("manufacturer"), py::arg("part_number"), py::arg("package"),
             py::arg("footprint_library"), py::arg("footprint_name"), py::arg("pin_pads"),
             py::arg("properties") = py::dict{}, py::arg("model_3d") = py::none(),
             py::arg("approved_alternate_mpns") = py::tuple{})
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
        .def("set_component_dnp", &PyCircuit::set_component_dnp, py::arg("component"),
             py::arg("dnp"))
        .def("set_component_selection_override", &PyCircuit::set_component_selection_override,
             py::arg("component"), py::arg("selection_override"))
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
        .def("validate_bom_readiness", &PyCircuit::validate_bom_readiness)
        .def("bom_json", &PyCircuit::bom_json, py::arg("sourcing_snapshot") = py::dict{})
        .def("bom_csv", &PyCircuit::bom_csv, py::arg("sourcing_snapshot") = py::dict{})
        .def("bom_sourcing_snapshot_json", &PyCircuit::bom_sourcing_snapshot_json,
             py::arg("sourcing_snapshot") = py::dict{})
        .def("board", &PyCircuit::board, py::arg("name") = "Main")
        .def("board_design_rules", &PyCircuit::board_design_rules)
        .def("board_set_design_rules", &PyCircuit::board_set_design_rules,
             py::arg("copper_clearance_mm"), py::arg("minimum_track_width_mm"),
             py::arg("minimum_via_drill_diameter_mm"), py::arg("minimum_via_annular_diameter_mm"),
             py::arg("board_outline_clearance_mm"))
        .def("board_set_capability_profile", &PyCircuit::board_set_capability_profile,
             py::arg("profile"))
        .def("board_add_layer", &PyCircuit::board_add_layer, py::arg("name"), py::arg("role"),
             py::arg("side"), py::arg("thickness_mm") = 0.0, py::arg("enabled") = true,
             py::arg("copper_weight_oz") = std::nullopt)
        .def("board_set_layer_stack", &PyCircuit::board_set_layer_stack, py::arg("layers"),
             py::arg("board_thickness_mm"),
             py::arg("dielectrics") = std::vector<std::pair<double, double>>{})
        .def("board_set_rectangular_outline", &PyCircuit::board_set_rectangular_outline,
             py::arg("x"), py::arg("y"), py::arg("width"), py::arg("height"))
        .def("board_set_polygon_outline", &PyCircuit::board_set_polygon_outline,
             py::arg("vertices"))
        .def("board_outline_vertices", &PyCircuit::board_outline_vertices)
        .def("board_add_hole", &PyCircuit::board_add_hole, py::arg("label"), py::arg("x"),
             py::arg("y"), py::arg("drill_diameter_mm"), py::arg("plated") = false,
             py::arg("role") = "", py::arg("finished_diameter_mm") = std::nullopt)
        .def("board_add_slot", &PyCircuit::board_add_slot, py::arg("label"), py::arg("start_x"),
             py::arg("start_y"), py::arg("end_x"), py::arg("end_y"), py::arg("width_mm"),
             py::arg("plated") = false, py::arg("role") = "")
        .def("board_add_cutout", &PyCircuit::board_add_cutout, py::arg("label"), py::arg("outline"),
             py::arg("role") = "")
        .def("board_add_circle", &PyCircuit::board_add_circle, py::arg("label"), py::arg("x"),
             py::arg("y"), py::arg("diameter_mm"), py::arg("side") = "top", py::arg("role") = "")
        .def("board_cache_footprint_definition", &PyCircuit::board_cache_footprint_definition,
             py::arg("definition"))
        .def("board_place_component", &PyCircuit::board_place_component, py::arg("component"),
             py::arg("x"), py::arg("y"), py::arg("rotation_degrees") = 0.0, py::arg("side") = "top",
             py::arg("locked") = false)
        .def("board_placement_refs", &PyCircuit::board_placement_refs)
        .def("board_stackup", &PyCircuit::board_stackup)
        .def("board_component_footprint_pads", &PyCircuit::board_component_footprint_pads,
             py::arg("component"))
        .def("board_add_track", &PyCircuit::board_add_track, py::arg("net"), py::arg("layer"),
             py::arg("points"), py::arg("width_mm"))
        .def("board_add_track_for_route", &PyCircuit::board_add_track_for_route, py::arg("net"),
             py::arg("layer"), py::arg("endpoints"), py::arg("width_mm"))
        .def("board_track_net", &PyCircuit::board_track_net, py::arg("track"))
        .def("board_add_via", &PyCircuit::board_add_via, py::arg("net"), py::arg("x"), py::arg("y"),
             py::arg("start_layer"), py::arg("end_layer"), py::arg("drill_diameter_mm"),
             py::arg("annular_diameter_mm"))
        .def("board_assisted_connect", &PyCircuit::board_assisted_connect, py::arg("net"),
             py::arg("start_x"), py::arg("start_y"), py::arg("start_layer"), py::arg("end_x"),
             py::arg("end_y"), py::arg("end_layer"))
        .def("board_escape", &PyCircuit::board_escape, py::arg("component"))
        .def("board_add_zone", &PyCircuit::board_add_zone, py::arg("net"), py::arg("layers"),
             py::arg("outline"), py::arg("fill"), py::arg("priority"))
        .def("board_add_keepout", &PyCircuit::board_add_keepout, py::arg("layers"),
             py::arg("outline"), py::arg("restrictions"))
        .def("board_add_room", &PyCircuit::board_add_room, py::arg("name"), py::arg("outline"),
             py::arg("layers"), py::arg("copper_clearance_mm") = std::nullopt,
             py::arg("track_width_mm") = std::nullopt, py::arg("priority") = 0)
        .def("board_add_text", &PyCircuit::board_add_text, py::arg("text"), py::arg("x"),
             py::arg("y"), py::arg("layer"), py::arg("rotation_degrees"), py::arg("size_mm"),
             py::arg("locked"))
        .def("board_resolve_pads", &PyCircuit::board_resolve_pads)
        .def("board_validate", &PyCircuit::board_validate)
        .def("board_to_json", &PyCircuit::board_to_json)
        .def("board_to_svg", &PyCircuit::board_to_svg, py::arg("pad_net_overlays") = true,
             py::arg("diagnostic_overlays") = true, py::arg("ratsnest_edges") = true,
             py::arg("layer_filter") = std::nullopt)
        .def("board_to_kicad_pcb", &PyCircuit::board_to_kicad_pcb)
        .def("to_json", &PyCircuit::to_json);
}

} // namespace volt::python
