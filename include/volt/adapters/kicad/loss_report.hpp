// NOTE: #ifndef guard is intentional. The macro VOLT_KICAD_ADAPTER_LOSS_REPORT_HPP is used as a
// compile-time sentinel by tests/adapters/kicad_boundary_test.cpp to verify that volt/volt.hpp
// does not transitively include this header. Do not replace with #pragma once.
#ifndef VOLT_KICAD_ADAPTER_LOSS_REPORT_HPP
#define VOLT_KICAD_ADAPTER_LOSS_REPORT_HPP

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/core/diagnostics.hpp>

namespace volt::adapters::kicad {

/** Severity for adapter reports returned to Python and CLI callers. */
enum class LossSeverity {
    Info,
    Warning,
    Error,
};

/** Fabrication impact for adapter losses that may or may not block handoff. */
enum class LossFabricationImpact {
    Informational,
    FabCritical,
};

/** Category for imperfect KiCad interoperability. */
enum class LossKind {
    UnsupportedConstruct,
    IncompleteConstruct,
    LossyConstruct,
};

/** One structured adapter warning for a construct that could not round-trip perfectly. */
struct LossWarning {
    /** Category describing why the KiCad construct could not map perfectly. */
    LossKind kind;

    /** KiCad construct name, such as a field, marker, stroke, or sheet feature. */
    std::string construct;

    /** Human-readable explanation for Python, CLI, and diagnostic presentation. */
    std::string message;

    /** Severity callers can use to decide whether to continue or fail the adapter run. */
    LossSeverity severity = LossSeverity::Warning;

    /** Whether this loss means the KiCad handoff is incomplete for fabrication. */
    LossFabricationImpact fabrication_impact = LossFabricationImpact::Informational;
};

/** Warnings collected while importing from or exporting to KiCad adapter documents. */
class LossReport {
  public:
    /** Add a warning for a KiCad construct with imperfect Volt representation. */
    void
    add_warning(LossKind kind, std::string construct, std::string message,
                LossSeverity severity = LossSeverity::Warning,
                LossFabricationImpact fabrication_impact = LossFabricationImpact::Informational);

    /** Return whether any adapter warnings were recorded. */
    [[nodiscard]] bool has_warnings() const noexcept { return !warnings_.empty(); }

    /** Return whether any warnings block fabrication handoff. */
    [[nodiscard]] bool has_fab_critical_warnings() const noexcept;

    /** Return warnings in discovery order. */
    [[nodiscard]] const std::vector<LossWarning> &warnings() const noexcept { return warnings_; }

    /** Return fab-critical warnings in discovery order. */
    [[nodiscard]] std::vector<LossWarning> fab_critical_warnings() const;

  private:
    std::vector<LossWarning> warnings_;
};

/** Convert fab-critical adapter losses to stable manufacturability diagnostics. */
[[nodiscard]] DiagnosticReport fabrication_diagnostics(const LossReport &report);

} // namespace volt::adapters::kicad

#endif
