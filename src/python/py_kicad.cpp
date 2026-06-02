#include "py_circuit.hpp"

#include <stdexcept>
#include <string>
#include <utility>

#include <volt/adapters/kicad/pcb_writer.hpp>

namespace volt::python {
namespace {

[[nodiscard]] std::string kicad_loss_kind_name(volt::adapters::kicad::LossKind kind) {
    switch (kind) {
    case volt::adapters::kicad::LossKind::UnsupportedConstruct:
        return "unsupported";
    case volt::adapters::kicad::LossKind::IncompleteConstruct:
        return "incomplete";
    case volt::adapters::kicad::LossKind::LossyConstruct:
        return "lossy";
    }
    throw std::logic_error{"Unhandled KiCad loss kind"};
}

[[nodiscard]] std::string kicad_loss_severity_name(volt::adapters::kicad::LossSeverity severity) {
    switch (severity) {
    case volt::adapters::kicad::LossSeverity::Info:
        return "info";
    case volt::adapters::kicad::LossSeverity::Warning:
        return "warning";
    case volt::adapters::kicad::LossSeverity::Error:
        return "error";
    }
    throw std::logic_error{"Unhandled KiCad loss severity"};
}

[[nodiscard]] py::dict
kicad_loss_warning_to_dict(const volt::adapters::kicad::LossWarning &warning) {
    auto result = py::dict{};
    result["kind"] = kicad_loss_kind_name(warning.kind);
    result["construct"] = warning.construct;
    result["message"] = warning.message;
    result["severity"] = kicad_loss_severity_name(warning.severity);
    return result;
}

} // namespace

py::dict PyCircuit::board_to_kicad_pcb() const {
    const auto export_result =
        volt::adapters::kicad::write_board(board_projection(), volt::builtin_footprint_library());

    auto result = py::dict{};
    result["text"] = export_result.text;

    auto warnings = py::list{};
    for (const auto &warning : export_result.loss_report.warnings()) {
        warnings.append(kicad_loss_warning_to_dict(warning));
    }
    result["warnings"] = std::move(warnings);
    return result;
}

} // namespace volt::python
