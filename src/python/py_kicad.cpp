#include "py_board.hpp"

#include "binding_diagnostic_conversions.hpp"

#include <string>
#include <utility>

#include <volt/adapters/kicad/pcb_writer.hpp>
#include <volt/core/errors.hpp>

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
    throw KernelLogicError{ErrorCode::InvalidArgument, "Unhandled KiCad loss kind"};
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
    throw KernelLogicError{ErrorCode::InvalidArgument, "Unhandled KiCad loss severity"};
}

[[nodiscard]] std::string
kicad_loss_fabrication_impact_name(volt::adapters::kicad::LossFabricationImpact impact) {
    switch (impact) {
    case volt::adapters::kicad::LossFabricationImpact::Informational:
        return "informational";
    case volt::adapters::kicad::LossFabricationImpact::FabCritical:
        return "fab-critical";
    }
    throw KernelLogicError{ErrorCode::InvalidArgument, "Unhandled KiCad loss fabrication impact"};
}

[[nodiscard]] py::dict
kicad_loss_warning_to_dict(const volt::adapters::kicad::LossWarning &warning) {
    auto result = py::dict{};
    result["kind"] = kicad_loss_kind_name(warning.kind);
    result["construct"] = warning.construct;
    result["message"] = warning.message;
    result["severity"] = kicad_loss_severity_name(warning.severity);
    result["fabrication_impact"] = kicad_loss_fabrication_impact_name(warning.fabrication_impact);
    return result;
}

} // namespace

py::dict PyBoard::to_kicad_pcb() const {
    const auto resolution = resolve();
    const auto export_result =
        volt::adapters::kicad::write_board(resolution.board(), resolution.footprints());

    auto result = py::dict{};
    result["text"] = export_result.text;
    result["diagnostics"] = diagnostics_to_list(
        volt::adapters::kicad::fabrication_diagnostics(export_result.loss_report));

    auto warnings = py::list{};
    for (const auto &warning : export_result.loss_report.warnings()) {
        warnings.append(kicad_loss_warning_to_dict(warning));
    }
    result["warnings"] = std::move(warnings);
    return result;
}

} // namespace volt::python
