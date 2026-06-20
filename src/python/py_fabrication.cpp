#include "py_circuit.hpp"

#include "binding_diagnostic_conversions.hpp"

#include <stdexcept>
#include <string>

#include <volt/io/pcb/pcb_fabrication_writer.hpp>

namespace volt::python {
namespace {

[[nodiscard]] std::string fabrication_loss_kind_name(volt::io::PcbFabricationLossKind kind) {
    switch (kind) {
    case volt::io::PcbFabricationLossKind::UnsupportedConstruct:
        return "unsupported";
    case volt::io::PcbFabricationLossKind::IncompleteConstruct:
        return "incomplete";
    case volt::io::PcbFabricationLossKind::LossyConstruct:
        return "lossy";
    }
    throw std::logic_error{"Unhandled native fabrication loss kind"};
}

[[nodiscard]] std::string
fabrication_loss_severity_name(volt::io::PcbFabricationLossSeverity severity) {
    switch (severity) {
    case volt::io::PcbFabricationLossSeverity::Info:
        return "info";
    case volt::io::PcbFabricationLossSeverity::Warning:
        return "warning";
    case volt::io::PcbFabricationLossSeverity::Error:
        return "error";
    }
    throw std::logic_error{"Unhandled native fabrication loss severity"};
}

[[nodiscard]] std::string fabrication_loss_impact_name(volt::io::PcbFabricationLossImpact impact) {
    switch (impact) {
    case volt::io::PcbFabricationLossImpact::Informational:
        return "informational";
    case volt::io::PcbFabricationLossImpact::FabCritical:
        return "fab-critical";
    }
    throw std::logic_error{"Unhandled native fabrication loss impact"};
}

[[nodiscard]] py::dict
fabrication_loss_warning_to_dict(const volt::io::PcbFabricationLossWarning &warning) {
    auto result = py::dict{};
    result["kind"] = fabrication_loss_kind_name(warning.kind);
    result["construct"] = warning.construct;
    result["message"] = warning.message;
    result["severity"] = fabrication_loss_severity_name(warning.severity);
    result["fabrication_impact"] = fabrication_loss_impact_name(warning.fabrication_impact);
    return result;
}

[[nodiscard]] py::dict fabrication_file_to_dict(const volt::io::PcbFabricationFile &file) {
    auto result = py::dict{};
    result["filename"] = file.filename;
    result["function"] = file.function;
    result["text"] = file.text;
    return result;
}

} // namespace

py::dict PyCircuit::board_to_fabrication_files() const {
    const auto export_result = volt::io::write_pcb_fabrication_files(
        board_projection(), volt::builtin_footprint_library());

    auto result = py::dict{};

    auto files = py::list{};
    for (const auto &file : export_result.files) {
        files.append(fabrication_file_to_dict(file));
    }
    result["files"] = std::move(files);

    auto warnings = py::list{};
    for (const auto &warning : export_result.loss_report.warnings()) {
        warnings.append(fabrication_loss_warning_to_dict(warning));
    }
    result["warnings"] = std::move(warnings);
    result["diagnostics"] = diagnostics_to_list(fabrication_diagnostics(export_result.loss_report));
    return result;
}

} // namespace volt::python
