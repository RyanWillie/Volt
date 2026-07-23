#include "circuit_bindings.hpp"

#include "binding_part_definition_conversions.hpp"
#include "binding_pcb_conversions.hpp"
#include "py_circuit.hpp"
#include "py_part_library.hpp"

#include <volt/core/content_hash.hpp>
#include <volt/core/errors.hpp>
#include <volt/io/capabilities/board_capability_profile.hpp>

#include <array>
#include <cstddef>
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
    module.def("_raise_cross_reference_error", [](const std::string &message) {
        throw volt::KernelLogicError{volt::ErrorCode::CrossReferenceViolation, message};
    });
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
    module.def("assembly_diagnostic_codes",
               []() { return string_view_catalog_to_tuple(diagnostic_code_catalogs::Assembly); });
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
    module.def("standard_feature_schema", &standard_feature_schema_to_dict, py::arg("name"));
    py::class_<PyPartLibrary>(module, "PartLibrarySnapshot")
        .def(py::init<std::string, std::string, const py::list &, bool>(), py::arg("namespace"),
             py::arg("version"), py::arg("parts"), py::arg("selected_bundle") = false)
        .def_property_readonly("digest", &PyPartLibrary::digest)
        .def("part_result", &PyPartLibrary::part_result, py::arg("part_key"))
        .def("exact_reference", &PyPartLibrary::exact_reference, py::arg("part_key"));

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
             py::arg("schematic_symbols") = py::list{}, py::arg("contract") = py::none())
        .def("define_library_part", &PyCircuit::define_library_part, py::arg("library"),
             py::arg("part_key"))
        .def("select_library_part", &PyCircuit::select_library_part, py::arg("component"),
             py::arg("library"), py::arg("part_key"))
        .def("validate_selected_part_erc", &PyCircuit::validate_selected_part_erc,
             py::arg("library"))
        .def("add_net", &PyCircuit::add_net, py::arg("name"), py::arg("kind") = "signal")
        .def("add_net_class", &PyCircuit::add_net_class, py::arg("name"),
             py::arg("options") = py::dict{})
        .def("assign_net_class", &PyCircuit::assign_net_class, py::arg("nets"),
             py::arg("net_class"))
        .def("net_class_info", &PyCircuit::net_class_info, py::arg("net_class"))
        .def("net_refs", &PyCircuit::net_refs)
        .def("component_refs", &PyCircuit::component_refs)
        .def("select_authored_part", &PyCircuit::select_authored_part, py::arg("component"),
             py::arg("manufacturer"), py::arg("part_number"), py::arg("package"),
             py::arg("footprint"), py::arg("pin_pads"), py::arg("voltage_rating") = py::none(),
             py::arg("model_3d") = py::none(), py::arg("model_3d_bytes") = py::none(),
             py::arg("approved_alternate_mpns") = py::tuple{})
        .def("set_component_quantity", &PyCircuit::set_component_quantity, py::arg("component"),
             py::arg("name"), py::arg("dimension"), py::arg("value"))
        .def("set_component_percent_tolerance", &PyCircuit::set_component_percent_tolerance,
             py::arg("component"), py::arg("value"))
        .def("set_net_quantity", &PyCircuit::set_net_quantity, py::arg("net"), py::arg("name"),
             py::arg("dimension"), py::arg("value"))
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
        .def("connect_endpoints", &PyCircuit::connect_endpoints, py::arg("net"),
             py::arg("endpoints"))
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
        .def("add_module_port", &PyCircuit::add_module_port, py::arg("module"), py::arg("name"),
             py::arg("kind") = "signal", py::arg("role") = "passive", py::arg("required") = true)
        .def("add_module_component", &PyCircuit::add_module_component, py::arg("module"),
             py::arg("definition"), py::arg("reference"), py::arg("properties") = py::dict{})
        .def("module_component_pin_by_name", &PyCircuit::module_component_pin_by_name,
             py::arg("component"), py::arg("name"))
        .def("module_component_pin_by_number", &PyCircuit::module_component_pin_by_number,
             py::arg("component"), py::arg("number"))
        .def("module_component_pin_refs", &PyCircuit::module_component_pin_refs,
             py::arg("component"))
        .def("connect_module_pins", &PyCircuit::connect_module_pins, py::arg("module"),
             py::arg("net"), py::arg("component_pins"))
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
        .def("validate", &PyCircuit::validate)
        .def("validate_for_pcb", &PyCircuit::validate_for_pcb)
        .def("validate_bom_readiness", &PyCircuit::validate_bom_readiness)
        .def("bom_json", &PyCircuit::bom_json, py::arg("sourcing_snapshot") = py::dict{})
        .def("bom_csv", &PyCircuit::bom_csv, py::arg("sourcing_snapshot") = py::dict{})
        .def("bom_sourcing_snapshot_json", &PyCircuit::bom_sourcing_snapshot_json,
             py::arg("sourcing_snapshot") = py::dict{})
        .def("to_json", &PyCircuit::to_json);
}

} // namespace volt::python
