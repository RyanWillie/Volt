#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <volt/electrical/compiled_electrical_model.hpp>

namespace volt {

/** Validated numerical acceptance policy; never physical parasitics or model uncertainty. */
class DcSolveOptions {
  public:
    /** Require finite positive tolerances; relative thresholds are strictly below one. */
    explicit DcSolveOptions(
        double relative_rank_threshold = 1e-12, double minimum_reciprocal_condition = 1e-12,
        double relative_residual_tolerance = 1e-9,
        Quantity absolute_voltage_tolerance = Quantity{UnitDimension::Voltage, 1e-9},
        Quantity absolute_current_tolerance = Quantity{UnitDimension::Current, 1e-12});

    /** Relative FullPivLU pivot threshold on the equilibrated matrix. */
    [[nodiscard]] double relative_rank_threshold() const noexcept { return rank_threshold_; }

    /** Minimum acceptable estimated reciprocal condition of the equilibrated matrix. */
    [[nodiscard]] double minimum_reciprocal_condition() const noexcept { return minimum_rcond_; }

    /** Relative tolerance for scaled algebra and original-unit residuals. */
    [[nodiscard]] double relative_residual_tolerance() const noexcept { return relative_residual_; }

    /** Absolute voltage-law tolerance in volts. */
    [[nodiscard]] const Quantity &absolute_voltage_tolerance() const noexcept {
        return voltage_tolerance_;
    }

    /** Absolute KCL/current-law tolerance in amperes. */
    [[nodiscard]] const Quantity &absolute_current_tolerance() const noexcept {
        return current_tolerance_;
    }

    /** Fixed deterministic equilibration policy for this numerical contract. */
    [[nodiscard]] static constexpr std::string_view scaling() noexcept {
        return "max_abs_row_then_column";
    }

  private:
    double rank_threshold_;
    double minimum_rcond_;
    double relative_residual_;
    Quantity voltage_tolerance_;
    Quantity current_tolerance_;
};

/** Numerical outcome, separate from the upstream S1/S2 coverage and topology assessment. */
enum class DcSolveOutcome {
    /** All coordinates are unique and every numerical trust gate passed. */
    Success,
    /** The chosen rank policy cannot establish unique observable coordinates. */
    RankDeficient,
    /** The assembled constraints cannot be satisfied together under the rank policy. */
    Inconsistent,
    /** Estimated conditioning is below the accepted numerical policy. */
    IllConditioned,
    /** Nonfinite assembly, factorization, reconstruction or derived observations. */
    NumericalFailure,
    /** Scaled algebra or an original-unit electrical equation failed its residual gate. */
    ResidualFailure,
};

/** Available evidence from numerical evaluation; absent metrics were not established. */
struct DcSolveMetrics {
    /** Number of deterministic backend coordinates after removing the reference potential. */
    std::size_t coordinate_count = 0;
    /** Rank of the equilibrated coefficient matrix, when factorization completed. */
    std::optional<std::size_t> rank;
    /** Rank after adding the normalized right-hand side, when uniqueness failed. */
    std::optional<std::size_t> augmented_rank;
    /** Estimated reciprocal condition, only when finite and meaningful. */
    std::optional<double> reciprocal_condition;
    /** Maximum normalized scaled-system backward residual. */
    std::optional<double> scaled_residual;
    /** Largest absolute reconstructed voltage-law residual. */
    std::optional<Quantity> voltage_residual;
    /** Largest absolute reconstructed KCL/current-law residual. */
    std::optional<Quantity> current_residual;
    /** Largest voltage residual divided by its absolute-plus-relative allowed error. */
    std::optional<double> voltage_error_ratio;
    /** Largest KCL/current residual divided by its absolute-plus-relative allowed error. */
    std::optional<double> current_error_ratio;
};

/** One potential keyed to the retained compiled-model node origin. */
struct DcNodeResult {
    /** Model-local node identity. */
    ElectricalNodeId node;
    /** Finite potential relative to the request's explicit reference. */
    Quantity potential;
};

/** One oriented observation keyed to the retained compiled branch/law origin. */
struct DcBranchResult {
    /** Model-local branch identity. */
    ElectricalBranchId branch;
    /** V(from)-V(to), in volts. */
    Quantity voltage;
    /** Current from the stored from endpoint to the stored to endpoint, in amperes. */
    Quantity current;
    /** Signed absorbed power voltage*current, in watts; delivery is negative. */
    Quantity power;
};

/** One finite observation in the requested probe's native voltage/current dimension. */
struct DcProbeResult {
    /** Original request-local key, interpreted under the solution's analysis identity. */
    DcProbeKey key;
    /** Result with the native probe's orientation and dimension. */
    Quantity value;
};

/** Immutable successful observations retaining all original model and request provenance. */
class DcSolution {
  public:
    /** Native evaluator; its definition and numerical types are private implementation. */
    class Solver;

    /** Identity of model, numerical contract, backend and effective settings. */
    [[nodiscard]] const ContentHash &analysis_identity() const noexcept {
        return analysis_identity_;
    }

    /** Owning immutable model, including request, exact input and origin records. */
    [[nodiscard]] const CompiledElectricalModel &model() const noexcept { return model_; }

    /** Effective validated numerical settings retained with these observations. */
    [[nodiscard]] const DcSolveOptions &options() const noexcept { return options_; }

    /** Complete model-order node observations. */
    [[nodiscard]] const std::vector<DcNodeResult> &nodes() const noexcept { return nodes_; }

    /** Complete model-order branch observations, including ideal constraints and zero C current. */
    [[nodiscard]] const std::vector<DcBranchResult> &branches() const noexcept { return branches_; }

    /** Complete request-key-order probe observations. */
    [[nodiscard]] const std::vector<DcProbeResult> &probes() const noexcept { return probes_; }

  private:
    DcSolution(ContentHash analysis_identity, CompiledElectricalModel model, DcSolveOptions options,
               std::vector<DcNodeResult> nodes, std::vector<DcBranchResult> branches,
               std::vector<DcProbeResult> probes);
    ContentHash analysis_identity_;
    CompiledElectricalModel model_;
    DcSolveOptions options_;
    std::vector<DcNodeResult> nodes_;
    std::vector<DcBranchResult> branches_;
    std::vector<DcProbeResult> probes_;
};

/** Numerical report with an optional success-only immutable solution. */
class DcSolveReport {
  public:
    /** Evaluate exactly this compiled model with validated effective settings. */
    explicit DcSolveReport(const CompiledElectricalModel &model,
                           const DcSolveOptions &options = DcSolveOptions{});

    /** Current native numerical contract, not an artifact or Circuit format version. */
    [[nodiscard]] static constexpr std::uint32_t contract_version() noexcept { return 1; }

    /** Pinned established linear algebra backend and formulation identity. */
    [[nodiscard]] static std::string_view backend() noexcept;

    /** Identity of the complete numerical input and effective policy. */
    [[nodiscard]] const ContentHash &analysis_identity() const noexcept {
        return analysis_identity_;
    }

    /** Exact owning compiled input for inspecting origins even after numerical failure. */
    [[nodiscard]] const CompiledElectricalModel &model() const noexcept { return model_; }

    /** Effective validated policy used in this attempt. */
    [[nodiscard]] const DcSolveOptions &options() const noexcept { return options_; }

    /** Typed reason for success or refusal to publish observations. */
    [[nodiscard]] DcSolveOutcome outcome() const noexcept { return outcome_; }

    /** True only when the unique finite solution passed every trust gate. */
    [[nodiscard]] bool success() const noexcept { return outcome_ == DcSolveOutcome::Success; }

    /** Available rank, conditioning and residual evidence, also retained on failure. */
    [[nodiscard]] const DcSolveMetrics &metrics() const noexcept { return metrics_; }

    /** Deterministically ordered numerical diagnostics; no PCB-length measurements. */
    [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const noexcept {
        return diagnostics_;
    }

    /** Complete solution or null, never a partial or arbitrary least-squares result. */
    [[nodiscard]] const DcSolution *solution() const & noexcept {
        return solution_ ? &*solution_ : nullptr;
    }

    /** Prevent borrowing the solution from a temporary report. */
    [[nodiscard]] const DcSolution *solution() const && = delete;

  private:
    CompiledElectricalModel model_;
    DcSolveOptions options_;
    ContentHash analysis_identity_;
    DcSolveOutcome outcome_ = DcSolveOutcome::NumericalFailure;
    DcSolveMetrics metrics_;
    std::vector<Diagnostic> diagnostics_;
    std::optional<DcSolution> solution_;
};

/** Solve only a complete compiled model; no independent resolver/request or implicit compilation.
 */
[[nodiscard]] DcSolveReport solve_dc(const CompiledElectricalModel &model,
                                     const DcSolveOptions &options = DcSolveOptions{});

} // namespace volt
