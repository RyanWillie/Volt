#include "py_circuit.hpp"

#include "py_circuit_reports_helpers.hpp"

#include <sstream>

#include <volt/circuit/bom/bom.hpp>
#include <volt/io/bom/bom_writer.hpp>
#include <volt/io/pcb/board_resolution.hpp>

namespace volt::python {

volt::Circuit PyCircuit::materialized_physical_circuit() const {
    auto logical = materialized_circuit();
    const auto board = volt::Board{logical, volt::BoardName{"Python reports"}};
    const auto resolution = volt::io::resolve_board(
        board, *selected_part_bundle_, volt::BoardResolutionCapabilities{std::nullopt});
    return resolution.board().circuit();
}

py::list PyCircuit::validate() const {
    const auto circuit = materialized_physical_circuit();
    return diagnostics_to_list(volt::validate_circuit(circuit));
}

py::list PyCircuit::validate_for_pcb() const {
    const auto circuit = materialized_physical_circuit();
    return diagnostics_to_list(volt::validate_for_pcb(circuit));
}

py::list PyCircuit::validate_bom_readiness() const {
    const auto circuit = materialized_physical_circuit();
    return diagnostics_to_list(volt::validate_bom_readiness(circuit));
}

std::string PyCircuit::bom_json(const py::dict &sourcing_snapshot) const {
    const auto circuit = materialized_physical_circuit();
    return volt::io::write_bom_json(
        volt::project_bom(circuit, sourcing_snapshot_from_dict(sourcing_snapshot)));
}

std::string PyCircuit::bom_csv(const py::dict &sourcing_snapshot) const {
    const auto circuit = materialized_physical_circuit();
    return volt::io::write_bom_csv(
        volt::project_bom(circuit, sourcing_snapshot_from_dict(sourcing_snapshot)));
}

std::string PyCircuit::bom_sourcing_snapshot_json(const py::dict &sourcing_snapshot) const {
    return volt::io::write_bom_sourcing_snapshot_json(
        sourcing_snapshot_from_dict(sourcing_snapshot));
}

std::string PyCircuit::to_json() const {
    const auto circuit = materialized_circuit();
    auto out = std::ostringstream{};
    volt::io::write_logical_circuit(out, circuit);
    return out.str();
}

} // namespace volt::python
