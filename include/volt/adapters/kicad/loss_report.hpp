// NOTE: #ifndef guard is intentional. The macro VOLT_KICAD_ADAPTER_LOSS_REPORT_HPP is used as a
// compile-time sentinel by tests/adapters/kicad_boundary_test.cpp to verify that volt/volt.hpp
// does not transitively include this header. Do not replace with #pragma once.
#ifndef VOLT_KICAD_ADAPTER_LOSS_REPORT_HPP
#define VOLT_KICAD_ADAPTER_LOSS_REPORT_HPP

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace volt::adapters::kicad {

/** Severity for adapter reports returned to Python and CLI callers. */
enum class LossSeverity {
    Info,
    Warning,
    Error,
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
};

/** Warnings collected while importing from or exporting to KiCad adapter documents. */
class LossReport {
  public:
    /** Add a warning for a KiCad construct with imperfect Volt representation. */
    void add_warning(LossKind kind, std::string construct, std::string message,
                     LossSeverity severity = LossSeverity::Warning);

    /** Return whether any adapter warnings were recorded. */
    [[nodiscard]] bool has_warnings() const noexcept { return !warnings_.empty(); }

    /** Return warnings in discovery order. */
    [[nodiscard]] const std::vector<LossWarning> &warnings() const noexcept { return warnings_; }

  private:
    std::vector<LossWarning> warnings_;
};

} // namespace volt::adapters::kicad

#endif
