#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <volt/electrical/compiled_electrical_model.hpp>

namespace volt {

class NgspiceDcAnalysis;

/** Native-owned attribution; external observations and native validation are distinct. */
struct DcSolveProvenance {
    /** Backend that produced the candidate observations, never the acceptance checker. */
    std::string backend;
    /** Exact supported backend version for this result contract. */
    std::string backend_version;
    /** Closed native adapter identifier, not a user-defined backend registry key. */
    std::string adapter;
    /** Version of the adapter's lowering and ingestion contract. */
    std::uint32_t adapter_contract_version = 1;
    /** Deterministic backend settings; acceptance tolerances are retained in options. */
    std::string effective_settings;
    /** Native uniqueness, finite-observation and residual acceptance contract. */
    std::string acceptance_policy;
    /** Implementation that established the report's rank and residual metrics. */
    std::string validation_backend;
    /** Exact external deck bytes, absent for native evaluation. */
    std::optional<ContentHash> deck_identity;
    /** Exact external generated-name and observation map, absent for native evaluation. */
    std::optional<ContentHash> mapping_identity;
};

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

/** Explicit bounded Newton policy; acceptance is in original physical units. */
class NonlinearDcSolveOptions {
  public:
    /** Validate positive work budgets and one to 24 backtracking halvings. */
    explicit NonlinearDcSolveOptions(DcSolveOptions acceptance = DcSolveOptions{},
                                     std::size_t max_iterations = 80,
                                     std::size_t max_backtracks = 24,
                                     std::size_t max_residual_evaluations = 2048,
                                     std::size_t max_jacobian_evaluations = 81);

    /** Original-unit residual, undamped-correction, rank and conditioning acceptance. */
    [[nodiscard]] const DcSolveOptions &acceptance() const noexcept { return acceptance_; }

    /** Maximum accepted Newton corrections. */
    [[nodiscard]] std::size_t max_iterations() const noexcept { return max_iterations_; }

    /** Maximum trial halvings per correction; the first trial is additional. */
    [[nodiscard]] std::size_t max_backtracks() const noexcept { return max_backtracks_; }

    /** Independent bound on all base, attempted trial and final residual evaluations. */
    [[nodiscard]] std::size_t max_residual_evaluations() const noexcept {
        return max_residual_evaluations_;
    }

    /** Independent bound on current-state Jacobian/factorization evaluations. */
    [[nodiscard]] std::size_t max_jacobian_evaluations() const noexcept {
        return max_jacobian_evaluations_;
    }

    /** Closed explicitly selected native numerical algorithm. */
    [[nodiscard]] static constexpr std::string_view algorithm() noexcept { return "diode-newton"; }

    /** Deterministic initial coordinate policy; no cached state. */
    [[nodiscard]] static constexpr std::string_view initial_guess() noexcept { return "all-zero"; }

    /** Required fractional decrease coefficient with frozen base row weights. */
    [[nodiscard]] static constexpr double armijo_coefficient() noexcept { return 1e-4; }

    /** Smallest admissible damped Newton step fraction. */
    [[nodiscard]] static constexpr double minimum_step() noexcept { return 0x1p-24; }

  private:
    DcSolveOptions acceptance_;
    std::size_t max_iterations_;
    std::size_t max_backtracks_;
    std::size_t max_residual_evaluations_;
    std::size_t max_jacobian_evaluations_;
};

/** Numerical outcome, separate from the upstream S1/S2 coverage and topology assessment. */
enum class DcSolveOutcome {
    /** All coordinates are unique and every numerical trust gate passed. */
    Success,
    /** Accepted locally regular operating point; global uniqueness is not established. */
    Converged,
    /** This explicitly selected numerical method does not support the model laws. */
    UnsupportedModel,
    /** Rank policy found a singular Jacobian at the evaluated iterate. */
    JacobianSingular,
    /** No admissible progressing trial was found inside the authored domain. */
    DomainLimited,
    /** No Armijo-decreasing trial was found within the backtracking bound. */
    LineSearchFailed,
    /** Accepted-correction budget exhausted. */
    IterationLimit,
    /** Residual or Jacobian evaluation budget exhausted. */
    EvaluationLimit,
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
    /** Accepted Newton corrections; zero for linear evaluation. */
    std::size_t iterations = 0;
    /** All attempted nonlinear base, trial and independent final residual evaluations. */
    std::size_t residual_evaluations = 0;
    /** Nonlinear Jacobian/factorization evaluation attempts. */
    std::size_t jacobian_evaluations = 0;
    /** Actually evaluated halved nonlinear trials. */
    std::size_t backtracks = 0;
    /** Attempted trial evaluations rejected outside a declared diode domain. */
    std::size_t domain_rejections = 0;
    /** Attempted trial evaluations rejected for nonfinite equations or merit. */
    std::size_t nonfinite_rejections = 0;
    /** Largest undamped correction divided by its original-unit allowed error. */
    std::optional<double> correction_error_ratio;
    /** Stable weighted residual 2-norm at the last evaluated nonlinear base iterate. */
    std::optional<double> merit;
    /** Original-unit row denominators frozen at the last evaluated base iterate. */
    std::vector<Quantity> residual_weights;
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
    ElectricalProbeKey key;
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

    /** Explicit nonlinear settings, absent for the linear method. */
    [[nodiscard]] const NonlinearDcSolveOptions *nonlinear_options() const noexcept {
        return nonlinear_options_ ? &*nonlinear_options_ : nullptr;
    }

    /** Actual observation producer and separately attributed native trust checks. */
    [[nodiscard]] const DcSolveProvenance &provenance() const noexcept { return provenance_; }

    /** Complete model-order node observations. */
    [[nodiscard]] const std::vector<DcNodeResult> &nodes() const noexcept { return nodes_; }

    /** Complete model-order branch observations, including ideal constraints and zero C current. */
    [[nodiscard]] const std::vector<DcBranchResult> &branches() const noexcept { return branches_; }

    /** Complete request-key-order probe observations. */
    [[nodiscard]] const std::vector<DcProbeResult> &probes() const noexcept { return probes_; }

  private:
    DcSolution(ContentHash analysis_identity, CompiledElectricalModel model, DcSolveOptions options,
               DcSolveProvenance provenance, std::vector<DcNodeResult> nodes,
               std::vector<DcBranchResult> branches, std::vector<DcProbeResult> probes,
               std::optional<NonlinearDcSolveOptions> nonlinear_options = std::nullopt);
    ContentHash analysis_identity_;
    CompiledElectricalModel model_;
    DcSolveOptions options_;
    std::optional<NonlinearDcSolveOptions> nonlinear_options_;
    DcSolveProvenance provenance_;
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

    /** Explicit nonlinear method; default linear execution never selects it. */
    explicit DcSolveReport(const CompiledElectricalModel &model,
                           const NonlinearDcSolveOptions &options);

    /** Parse adapter-owned output and accept only unique, finite, validated observations. */
    explicit DcSolveReport(const NgspiceDcAnalysis &analysis, std::string_view output,
                           const DcSolveOptions &options = DcSolveOptions{});

    /** Current native numerical contract, not an artifact or Circuit format version. */
    [[nodiscard]] static constexpr std::uint32_t contract_version() noexcept { return 2; }

    /** Pinned established linear algebra backend and formulation identity. */
    [[nodiscard]] std::string_view backend() const noexcept { return provenance_.backend; }

    /** Immutable attribution, including deck/mapping identity for an external backend. */
    [[nodiscard]] const DcSolveProvenance &provenance() const noexcept { return provenance_; }

    /** Identity of the complete numerical input and effective policy. */
    [[nodiscard]] const ContentHash &analysis_identity() const noexcept {
        return analysis_identity_;
    }

    /** Exact owning compiled input for inspecting origins even after numerical failure. */
    [[nodiscard]] const CompiledElectricalModel &model() const noexcept { return model_; }

    /** Effective validated policy used in this attempt. */
    [[nodiscard]] const DcSolveOptions &options() const noexcept { return options_; }

    /** Explicit nonlinear settings, absent for the linear method. */
    [[nodiscard]] const NonlinearDcSolveOptions *nonlinear_options() const noexcept {
        return nonlinear_options_ ? &*nonlinear_options_ : nullptr;
    }

    /** Typed reason for success or refusal to publish observations. */
    [[nodiscard]] DcSolveOutcome outcome() const noexcept { return outcome_; }

    /** True for accepted linear Success or locally regular nonlinear Converged. */
    [[nodiscard]] bool success() const noexcept {
        return outcome_ == DcSolveOutcome::Success || outcome_ == DcSolveOutcome::Converged;
    }

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
    std::optional<NonlinearDcSolveOptions> nonlinear_options_;
    DcSolveProvenance provenance_;
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

/** Execute bounded native Newton only when explicitly selected. */
[[nodiscard]] DcSolveReport solve_dc(const CompiledElectricalModel &model,
                                     const NonlinearDcSolveOptions &options);

/** Ingest only the narrow output contract of a retained native ngspice preparation. */
[[nodiscard]] DcSolveReport solve_ngspice_dc(const NgspiceDcAnalysis &analysis,
                                             std::string_view output,
                                             const DcSolveOptions &options = DcSolveOptions{});

} // namespace volt
