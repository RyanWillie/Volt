#include "py_circuit.hpp"

#include "py_circuit_helpers.hpp"

#include <sstream>

#include <volt/circuit/bom/bom.hpp>
#include <volt/io/bom/bom_writer.hpp>

namespace volt::python {

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

py::list PyCircuit::validate_bom_readiness() const {
    return diagnostics_to_list(volt::validate_bom_readiness(circuit_));
}

std::string PyCircuit::bom_json(const py::dict &sourcing_snapshot) const {
    return volt::io::write_bom_json(
        volt::project_bom(circuit_, sourcing_snapshot_from_dict(sourcing_snapshot)));
}

std::string PyCircuit::bom_csv(const py::dict &sourcing_snapshot) const {
    return volt::io::write_bom_csv(
        volt::project_bom(circuit_, sourcing_snapshot_from_dict(sourcing_snapshot)));
}

std::string PyCircuit::bom_sourcing_snapshot_json(const py::dict &sourcing_snapshot) const {
    return volt::io::write_bom_sourcing_snapshot_json(
        sourcing_snapshot_from_dict(sourcing_snapshot));
}

std::string PyCircuit::to_json() const {
    auto out = std::ostringstream{};
    volt::io::write_logical_circuit(out, circuit_);
    return out.str();
}

} // namespace volt::python
