#include <volt/adapters/kicad/loss_report.hpp>

#include <algorithm>
#include <optional>

namespace volt::adapters::kicad {
namespace {

[[nodiscard]] bool is_fab_critical(const LossWarning &warning) noexcept {
    return warning.fabrication_impact == LossFabricationImpact::FabCritical;
}

[[nodiscard]] std::string fabrication_diagnostic_message(const LossWarning &warning) {
    return "KiCad PCB export has fab-critical loss for " + warning.construct + ": " +
           warning.message;
}

} // namespace

void LossReport::add_warning(LossKind kind, std::string construct, std::string message,
                             LossSeverity severity, LossFabricationImpact fabrication_impact) {
    if (construct.empty()) {
        throw std::invalid_argument{"KiCad loss warning construct must not be empty"};
    }
    if (message.empty()) {
        throw std::invalid_argument{"KiCad loss warning message must not be empty"};
    }

    warnings_.push_back(
        LossWarning{kind, std::move(construct), std::move(message), severity, fabrication_impact});
}

[[nodiscard]] bool LossReport::has_fab_critical_warnings() const noexcept {
    return std::any_of(warnings_.begin(), warnings_.end(), is_fab_critical);
}

[[nodiscard]] std::vector<LossWarning> LossReport::fab_critical_warnings() const {
    auto critical = std::vector<LossWarning>{};
    for (const auto &warning : warnings_) {
        if (is_fab_critical(warning)) {
            critical.push_back(warning);
        }
    }
    return critical;
}

[[nodiscard]] DiagnosticReport fabrication_diagnostics(const LossReport &report) {
    auto diagnostics = DiagnosticReport{};
    for (const auto &warning : report.warnings()) {
        if (!is_fab_critical(warning)) {
            continue;
        }
        diagnostics.add(Diagnostic{
            Severity::Error,
            DiagnosticCode{std::string{pcb_fabrication_diagnostic_codes::KiCadFabExportLoss}},
            DiagnosticCategory{diagnostic_categories::PcbFabrication},
            fabrication_diagnostic_message(warning),
            std::vector{EntityRef::board()},
            {},
            std::nullopt,
            warning.construct,
        });
    }
    return diagnostics;
}

} // namespace volt::adapters::kicad
