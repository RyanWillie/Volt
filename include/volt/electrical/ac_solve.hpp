#pragma once

#include <complex>
#include <volt/electrical/dc_solve.hpp>

namespace volt {

/** Finite complex SI observation; phase is absent at zero magnitude. */
class AcComplexQuantity {
  public:
    /** Construct a voltage, current, resistance or ratio observation with finite magnitude. */
    AcComplexQuantity(UnitDimension dimension, double real, double imaginary);

    /** Return the physical dimension; values are expressed in SI units. */
    [[nodiscard]] UnitDimension dimension() const noexcept { return dimension_; }

    /** Return the real SI component. */
    [[nodiscard]] double real() const noexcept { return value_.real(); }

    /** Return the imaginary SI component. */
    [[nodiscard]] double imaginary() const noexcept { return value_.imag(); }

    /** Return the finite complex SI phasor. */
    [[nodiscard]] std::complex<double> value() const noexcept { return value_; }

    /** Return the finite absolute magnitude in SI units. */
    [[nodiscard]] double magnitude() const noexcept { return std::abs(value_); }

    /** Return phase in radians, or no value for an exactly zero magnitude. */
    [[nodiscard]] std::optional<double> phase() const noexcept;

  private:
    UnitDimension dimension_;
    std::complex<double> value_;
};

/** Same validated numerical policy as DC, applied to complex residual magnitudes. */
class AcSolveOptions : public DcSolveOptions {
  public:
    using DcSolveOptions::DcSolveOptions;
};

/** Native backend attribution, with no external AC adapter in this contract. */
using AcSolveProvenance = DcSolveProvenance;
/** Typed numerical or derived-measurement outcome for a frequency or whole sweep. */
enum class AcSolveOutcome {
    /** Unique finite observations satisfy every numerical and measurement gate. */
    Success,
    /** The rank policy cannot establish unique branch currents and node potentials. */
    RankDeficient,
    /** Coefficient and augmented ranks establish incompatible equations. */
    Inconsistent,
    /** The reciprocal-condition estimate falls below the configured floor. */
    IllConditioned,
    /** Assembly, reconstruction or a derived value is nonfinite. */
    NumericalFailure,
    /** Scaled algebra or original complex electrical laws exceed tolerance. */
    ResidualFailure,
    /** A requested ratio has an exactly zero denominator. */
    UndefinedMeasurement
};

/** Per-frequency trust evidence; electrical residuals are complex absolute magnitudes. */
struct AcSolveMetrics {
    /** Count of nonreference potentials plus all branch currents. */
    std::size_t coordinate_count = 0;
    /** Coefficient rank under the configured relative pivot threshold. */
    std::optional<std::size_t> rank;
    /** Augmented rank, equal to coefficient rank for a full-rank system. */
    std::optional<std::size_t> augmented_rank;
    /** Reciprocal-condition estimate of the equilibrated complex matrix. */
    std::optional<double> reciprocal_condition;
    /** Maximum normalized equilibrated-system backward residual. */
    std::optional<double> scaled_residual;
    /** Largest absolute complex voltage-law residual, in volts. */
    std::optional<Quantity> voltage_residual;
    /** Largest absolute complex KCL or current-law residual, in amperes. */
    std::optional<Quantity> current_residual;
    /** Maximum voltage residual divided by its absolute-plus-relative allowance. */
    std::optional<double> voltage_error_ratio;
    /** Maximum current residual divided by its absolute-plus-relative allowance. */
    std::optional<double> current_error_ratio;
};

/** One graph-order node potential relative to the explicit reference. */
struct AcNodeResult {
    /** Model-local node identity. */
    ElectricalNodeId node;
    /** Potential relative to the request reference, in volts. */
    AcComplexQuantity potential;
};

/** One graph-order branch voltage and current with canonical orientation. */
struct AcBranchResult {
    /** Model-local oriented branch identity. */
    ElectricalBranchId branch;
    /** V(from)-V(to), in volts. */
    AcComplexQuantity voltage;
    /** Current from the stored from endpoint to its to endpoint, in amperes. */
    AcComplexQuantity current;
};

/** One primitive or derived observation identified by its request-local key. */
struct AcProbeResult {
    /** Request-local primitive, gain or impedance key. */
    DcProbeKey key;
    /** Finite phasor in the observation dimension. */
    AcComplexQuantity value;
};

/** Complete successful observations for one requested frequency. */
struct AcFrequencyResult {
    /** Finite positive SI frequency in hertz. */
    Quantity frequency;
    /** All graph nodes in canonical model order. */
    std::vector<AcNodeResult> nodes;
    /** All graph branches, including exact ideal constraints, in model order. */
    std::vector<AcBranchResult> branches;
    /** Primitive and derived observations in global request-key order. */
    std::vector<AcProbeResult> probes;
};

/** Trust metrics and diagnostics retained even when this frequency fails. */
struct AcPointReport {
    /** Finite positive SI frequency in hertz. */
    Quantity frequency;
    /** Typed outcome of this frequency. */
    AcSolveOutcome outcome = AcSolveOutcome::NumericalFailure;
    /** Available numerical evidence; missing fields were not established. */
    AcSolveMetrics metrics;
    /** Deterministically ordered point-local failure evidence. */
    std::vector<Diagnostic> diagnostics;
};

/** Immutable successful whole-sweep observations with exact model and numerical provenance. */
class AcSolution {
  public:
    /** Private implementation owner of successful solution construction. */
    class Solver;

    /** Identity of the model, numerical contract, backend and effective settings. */
    [[nodiscard]] const ContentHash &analysis_identity() const noexcept {
        return analysis_identity_;
    }

    /** Return the exact owning compiled input and its origins. */
    [[nodiscard]] const CompiledElectricalModel &model() const noexcept { return model_; }

    /** Return the validated effective numerical policy. */
    [[nodiscard]] const AcSolveOptions &options() const noexcept { return options_; }

    /** Return native backend version and acceptance attribution. */
    [[nodiscard]] const AcSolveProvenance &provenance() const noexcept { return provenance_; }

    /** Return successful observations in canonical frequency order. */
    [[nodiscard]] const std::vector<AcFrequencyResult> &points() const noexcept { return points_; }

  private:
    AcSolution(ContentHash, CompiledElectricalModel, AcSolveOptions, AcSolveProvenance,
               std::vector<AcFrequencyResult>);
    ContentHash analysis_identity_;
    CompiledElectricalModel model_;
    AcSolveOptions options_;
    AcSolveProvenance provenance_;
    std::vector<AcFrequencyResult> points_;
};

/** Explicit sweep execution report; a solution exists only when every point succeeds. */
class AcSolveReport {
  public:
    /** Explicitly evaluate a complete AC model, retaining failed-point evidence. */
    explicit AcSolveReport(const CompiledElectricalModel &,
                           const AcSolveOptions & = AcSolveOptions{});

    /** Current native numerical result contract version. */
    [[nodiscard]] static constexpr std::uint32_t contract_version() noexcept { return 1; }

    /** Pinned native backend and complex tableau formulation identity. */
    [[nodiscard]] std::string_view backend() const noexcept { return provenance_.backend; }

    /** Identity of the model, numerical contract, backend and effective settings. */
    [[nodiscard]] const ContentHash &analysis_identity() const noexcept {
        return analysis_identity_;
    }

    /** Return the exact owning compiled input and its origins. */
    [[nodiscard]] const CompiledElectricalModel &model() const noexcept { return model_; }

    /** Return the validated effective numerical policy. */
    [[nodiscard]] const AcSolveOptions &options() const noexcept { return options_; }

    /** Return native backend version and acceptance attribution. */
    [[nodiscard]] const AcSolveProvenance &provenance() const noexcept { return provenance_; }

    /** Return one report for every requested frequency, including failures. */
    [[nodiscard]] const std::vector<AcPointReport> &points() const noexcept { return points_; }

    /** Return sweep diagnostics in frequency and point-diagnostic order. */
    [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const noexcept {
        return diagnostics_;
    }

    /** Return success or the first failed frequency outcome. */
    [[nodiscard]] AcSolveOutcome outcome() const noexcept { return outcome_; }

    /** True only when every requested frequency and derived measurement succeeded. */
    [[nodiscard]] bool success() const noexcept { return outcome_ == AcSolveOutcome::Success; }

    /** Return the immutable whole-sweep solution, or null on any failure. */
    [[nodiscard]] const AcSolution *solution() const & noexcept {
        return solution_ ? &*solution_ : nullptr;
    }

    /** Return the immutable whole-sweep solution, or null on any failure. */
    [[nodiscard]] const AcSolution *solution() const && = delete;

  private:
    CompiledElectricalModel model_;
    AcSolveOptions options_;
    AcSolveProvenance provenance_;
    ContentHash analysis_identity_;
    AcSolveOutcome outcome_ = AcSolveOutcome::NumericalFailure;
    std::vector<AcPointReport> points_;
    std::vector<Diagnostic> diagnostics_;
    std::optional<AcSolution> solution_;
};

/** Solve a complete native AC model explicitly; never execute during ordinary compilation. */
[[nodiscard]] AcSolveReport solve_ac(const CompiledElectricalModel &,
                                     const AcSolveOptions & = AcSolveOptions{});
} // namespace volt
