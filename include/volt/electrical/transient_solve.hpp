#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>
#include <volt/electrical/compiled_electrical_model.hpp>

namespace volt {

/** Explicit work/step limits with separate temporal and fixed algebra acceptance policies. */
class TransientSolveOptions {
  public:
    /** Require finite positive SI Time bounds with minimum <= initial <= maximum, positive
     * trial/accepted-macro budgets, 0 < relative temporal tolerance < 1 and positive
     * Voltage/Current absolute temporal tolerances. Output spacing does not choose step bounds.
     */
    TransientSolveOptions(Quantity minimum_step, Quantity initial_step, Quantity maximum_step,
                          std::size_t maximum_trials, std::size_t maximum_accepted_steps,
                          double relative_temporal_tolerance = 1e-4,
                          Quantity absolute_temporal_voltage = Quantity{UnitDimension::Voltage,
                                                                        1e-6},
                          Quantity absolute_temporal_current = Quantity{UnitDimension::Current,
                                                                        1e-9});

    /** Minimum proposed macro-trial duration; exact boundary clipping may go below it. */
    [[nodiscard]] const Quantity &minimum_step() const noexcept { return minimum_step_; }

    /** Initial proposed macro-trial duration, in seconds. */
    [[nodiscard]] const Quantity &initial_step() const noexcept { return initial_step_; }

    /** Maximum proposed macro-trial duration, in seconds. */
    [[nodiscard]] const Quantity &maximum_step() const noexcept { return maximum_step_; }

    /** Maximum attempted macro trials, including temporal rejections. */
    [[nodiscard]] std::size_t maximum_trials() const noexcept { return maximum_trials_; }

    /** Maximum accepted macro trials; each contributes two actual half steps. */
    [[nodiscard]] std::size_t maximum_accepted_steps() const noexcept {
        return maximum_accepted_steps_;
    }

    /** Relative storage-coordinate local-error tolerance, separate from algebra gates. */
    [[nodiscard]] double relative_temporal_tolerance() const noexcept { return relative_temporal_; }

    /** Absolute capacitor-voltage local-error tolerance, in volts. */
    [[nodiscard]] const Quantity &absolute_temporal_voltage() const noexcept {
        return temporal_voltage_;
    }

    /** Absolute inductor-current local-error tolerance, in amperes. */
    [[nodiscard]] const Quantity &absolute_temporal_current() const noexcept {
        return temporal_current_;
    }

    /** Fixed relative pivot threshold for the equilibrated tableau. */
    [[nodiscard]] static constexpr double relative_rank_threshold() noexcept { return 1e-12; }

    /** Fixed minimum acceptable reciprocal condition of the equilibrated tableau. */
    [[nodiscard]] static constexpr double minimum_reciprocal_condition() noexcept { return 1e-12; }

    /** Fixed relative tolerance for scaled algebra and original electrical residuals. */
    [[nodiscard]] static constexpr double relative_residual_tolerance() noexcept { return 1e-9; }

    /** Fixed absolute voltage-law residual floor, in volts. */
    [[nodiscard]] static Quantity absolute_voltage_tolerance() {
        return {UnitDimension::Voltage, 1e-9};
    }

    /** Fixed absolute KCL/current-law residual floor, in amperes. */
    [[nodiscard]] static Quantity absolute_current_tolerance() {
        return {UnitDimension::Current, 1e-12};
    }

    /** Deterministic row-then-column maximum-absolute-value equilibration policy. */
    [[nodiscard]] static constexpr std::string_view scaling() noexcept {
        return "max_abs_row_then_column";
    }

  private:
    Quantity minimum_step_, initial_step_, maximum_step_;
    std::size_t maximum_trials_, maximum_accepted_steps_;
    double relative_temporal_;
    Quantity temporal_voltage_, temporal_current_;
};

/** Failure classification never grants a partial solution successful status. */
enum class TransientSolveOutcome {
    /** Every requested observation passed applicable initialization, algebra and temporal gates. */
    Success,
    /** Fixed capacitor voltages/inductor currents contradict instantaneous constraints. */
    InconsistentInitialState,
    /** Frozen-state rank cannot establish unique coordinates; physical uniqueness is undecided. */
    UnsupportedInitializationTopology,
    /** A noninitial tableau cannot establish unique observable coordinates. */
    RankDeficient,
    /** Coefficient and augmented ranks establish incompatible noninitial constraints. */
    Inconsistent,
    /** The equilibrated reciprocal condition falls below the fixed floor. */
    IllConditioned,
    /** Assembly, reconstruction, derivatives or temporal normalization are nonfinite. */
    NumericalFailure,
    /** Scaled algebra or original electrical laws exceed their fixed tolerances. */
    ResidualFailure,
    /** Finite representable progress or the explicit minimum-step policy prevents continuation. */
    StepLimit,
    /** The explicit macro-trial or accepted-macro budget is exhausted. */
    WorkLimit
};

/** Algebra evidence applies to the frozen projector or a single actual BE solve. */
struct TransientAlgebraMetrics {
    /** Count of nonreference node potentials and all branch currents. */
    std::size_t coordinate_count = 0;
    /** Coefficient rank and coefficient/right-hand-side augmented rank, when established. */
    std::optional<std::size_t> rank,
        augmented_rank; /**< Augmented coefficient/right-hand-side rank. */
    /** Equilibrated reciprocal condition and maximum normalized tableau backward residual. */
    std::optional<double> reciprocal_condition,
        scaled_residual; /**< Normalized tableau backward residual. */
    /** Largest original voltage-law and KCL/current-law absolute residuals, in volts/amperes. */
    std::optional<Quantity> voltage_residual,
        current_residual; /**< Absolute KCL/current-law residual, in amperes. */
    /** Largest voltage and current residuals divided by their absolute-plus-relative allowances. */
    std::optional<double> voltage_error_ratio,
        current_error_ratio; /**< Current residual divided by its allowance. */
};

/** One model-order node potential relative to the request's explicit reference. */
struct TransientNodeResult {
    /** Model-local node identity; its durable origin remains in the owning compiled model. */
    ElectricalNodeId node;
    /** Finite potential relative to the reference, in volts. */
    Quantity potential;
};

/** Signed power is absorbed power in the retained from-to branch orientation. */
struct TransientBranchResult {
    /** Model-local oriented branch identity; its durable origin remains in the compiled model. */
    ElectricalBranchId branch;
    /** V(from)-V(to), from-to current and signed absorbed voltage*current power, in SI units. */
    Quantity voltage, current /**< From-to branch current, in amperes. */,
        power; /**< Signed absorbed power, in watts. */
};

/** One finite oriented voltage/current observation under its request-local probe key. */
struct TransientProbeResult {
    /** Original request-local key, interpreted under the solution's analysis identity. */
    ElectricalProbeKey key;
    /** Finite value with the native probe's orientation and Voltage/Current dimension. */
    Quantity value;
};

/** Complete observations at exactly one requested time; no dense interpolation. */
struct TransientSample {
    /** Exact SI Time at this observation or evaluation endpoint. */
    Quantity time;
    /** Complete node potentials in canonical model order. */
    std::vector<TransientNodeResult> nodes;
    /** Complete oriented branch observations, including ideal constraints, in model order. */
    std::vector<TransientBranchResult> branches;
    /** Complete primitive probe observations in canonical request-key order. */
    std::vector<TransientProbeResult> probes;
};

/** SI derivative: volts/second for C and amperes/second for L, identified by compiled origin. */
struct TransientStorageDerivative {
    /** Model-local oriented branch identity; its durable origin remains in the compiled model. */
    ElectricalBranchId branch;
    /** Finite initial dvC/dt in volts/second or diL/dt in amperes/second. */
    double derivative;
};
/** Closed stages of initialization, direct algebraic sampling and an adaptive BE macro trial. */
enum class TransientEvaluationKind {
    /** Independent frozen-state solve at t=0+, including continuous-law derivative checks. */
    Initialization,
    /** Direct solve at a requested time for a model with no storage. */
    AlgebraicSample,
    /** One full-duration BE solve used only for temporal comparison. */
    FullStep,
    /** First actual half-duration BE solve from the previously accepted state. */
    FirstHalf,
    /** Second actual half-duration BE solve, continuing from the first half. */
    SecondHalf
};

/** Timed numerical evidence retained for rejected trials and numerical failure. */
struct TransientEvaluation {
    /** Exact SI Time at this observation or evaluation endpoint. */
    Quantity time;
    /** Actual representable solve duration, in seconds; zero for initialization/direct sampling. */
    Quantity duration;
    /** Native numerical stage represented by this evidence. */
    TransientEvaluationKind kind;
    /** Typed numerical outcome of this evaluation. */
    TransientSolveOutcome outcome = TransientSolveOutcome::NumericalFailure;
    /** Available algebra evidence; absent fields were not established. */
    TransientAlgebraMetrics metrics;
    /** Evaluation-local numerical findings, including retained compiled-origin entities. */
    std::vector<Diagnostic> diagnostics;
    /** Number of linear solves attempted by this evaluation. */
    std::size_t solve_count = 0;
    /** Number of matrix factorizations attempted, counted independently from solves. */
    std::size_t factorization_count = 0;
    /** Available on the second half after a complete temporal trial. */
    std::optional<double> normalized_error;
    /** True for a full/first-half/second-half evaluation whose entire macro trial was accepted. */
    bool trial_accepted = false;
};

/** Each accepted macro trial contributes two actual half steps. */
struct TransientAcceptedHalfStep {
    /** Exact half-step endpoint and actual representable duration, in seconds. */
    Quantity time, duration; /**< Actual representable half-step duration, in seconds. */
    /** Finite full-versus-two-half temporal error of the accepted macro trial, at most one. */
    double normalized_error;
    /** Available algebra evidence; absent fields were not established. */
    TransientAlgebraMetrics metrics;
};

/** Immutable complete successful observations with exact model, request and numerical policy. */
class TransientSolution {
  public:
    /** Private native implementation owner of success-only solution construction. */
    class Solver;

    /** Identity of exact model/request, integrator/backend version and effective numerical
     * settings. */
    [[nodiscard]] const ContentHash &analysis_identity() const noexcept {
        return analysis_identity_;
    }

    /** Return the owning immutable compiled model, including request and durable origins. */
    [[nodiscard]] const CompiledElectricalModel &model() const noexcept { return model_; }

    /** Return validated effective step/work bounds and temporal/algebra policy. */
    [[nodiscard]] const TransientSolveOptions &options() const noexcept { return options_; }

    /** Return complete observations at every requested time, in increasing order. */
    [[nodiscard]] const std::vector<TransientSample> &samples() const noexcept { return samples_; }

  private:
    TransientSolution(ContentHash id, CompiledElectricalModel model, TransientSolveOptions options,
                      std::vector<TransientSample> samples);
    ContentHash analysis_identity_;
    CompiledElectricalModel model_;
    TransientSolveOptions options_;
    std::vector<TransientSample> samples_;
};

/** Explicit native backward-Euler evaluation; solution exists only after whole success. */
class TransientSolveReport {
  public:
    /** Explicitly evaluate a complete transient model, retaining timed failure evidence. */
    TransientSolveReport(const CompiledElectricalModel &model,
                         const TransientSolveOptions &options);

    /** Current native numerical result contract version, separate from authored circuit formats. */
    [[nodiscard]] static constexpr std::uint32_t contract_version() noexcept { return 1; }

    /** Pinned native linear-algebra backend and backward-Euler tableau formulation identity. */
    [[nodiscard]] static constexpr std::string_view backend() noexcept {
        return "eigen-5.0.0-full-piv-lu-double-dense-branch-tableau-be";
    }

    /** Identity of exact model/request, integrator/backend version and effective numerical
     * settings. */
    [[nodiscard]] const ContentHash &analysis_identity() const noexcept {
        return analysis_identity_;
    }

    /** Return the owning immutable compiled model, including request and durable origins. */
    [[nodiscard]] const CompiledElectricalModel &model() const noexcept { return model_; }

    /** Return validated effective step/work bounds and temporal/algebra policy. */
    [[nodiscard]] const TransientSolveOptions &options() const noexcept { return options_; }

    /** Return success or the first terminating initialization/integration outcome. */
    [[nodiscard]] TransientSolveOutcome outcome() const noexcept { return outcome_; }

    /** True only after every requested sample has succeeded. */
    [[nodiscard]] bool success() const noexcept {
        return outcome_ == TransientSolveOutcome::Success;
    }

    /** Return ordered evaluation-local and terminal controller/work-limit findings. */
    [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const noexcept {
        return diagnostics_;
    }

    /** Return timed initialization, direct-sample and full/half trial evidence, including failures.
     */
    [[nodiscard]] const std::vector<TransientEvaluation> &evaluations() const noexcept {
        return evaluations_;
    }

    /** Return complete finite initial storage derivatives after successful frozen-state evaluation.
     */
    [[nodiscard]] const std::vector<TransientStorageDerivative> &
    initial_derivatives() const noexcept {
        return derivatives_;
    }

    /** Return both actual half-step metrics for every accepted macro trial. */
    [[nodiscard]] const std::vector<TransientAcceptedHalfStep> &
    accepted_half_steps() const noexcept {
        return accepted_half_steps_;
    }

    /** Last accepted macro endpoint or direct sample time; zero if no later state was accepted. */
    [[nodiscard]] const Quantity &last_accepted_time() const noexcept {
        return last_accepted_time_;
    }

    /** Number of attempted adaptive macro trials, including rejections; zero for storage-free
     * models. */
    [[nodiscard]] std::size_t trial_count() const noexcept { return trial_count_; }

    /** Number of accepted adaptive macro trials; each contributes two accepted half steps. */
    [[nodiscard]] std::size_t accepted_step_count() const noexcept { return accepted_step_count_; }

    /** Total attempted linear solves, including initialization, direct samples and rejected trials.
     */
    [[nodiscard]] std::size_t solve_count() const noexcept { return solve_count_; }

    /** Total attempted matrix factorizations, counted independently from linear solves. */
    [[nodiscard]] std::size_t factorization_count() const noexcept { return factorization_count_; }

    /** Return the immutable complete solution, or null on incomplete numerical execution. */
    [[nodiscard]] const TransientSolution *solution() const & noexcept {
        return solution_ ? &*solution_ : nullptr;
    }

    /** Prevent borrowing a solution from a temporary report. */
    [[nodiscard]] const TransientSolution *solution() const && = delete;

  private:
    CompiledElectricalModel model_;
    TransientSolveOptions options_;
    ContentHash analysis_identity_;
    TransientSolveOutcome outcome_ = TransientSolveOutcome::NumericalFailure;
    std::vector<Diagnostic> diagnostics_;
    std::vector<TransientEvaluation> evaluations_;
    std::vector<TransientStorageDerivative> derivatives_;
    std::vector<TransientAcceptedHalfStep> accepted_half_steps_;
    Quantity last_accepted_time_{UnitDimension::Time, 0};
    std::size_t trial_count_ = 0, accepted_step_count_ = 0, solve_count_ = 0,
                factorization_count_ = 0;
    std::optional<TransientSolution> solution_;
};

/** Explicitly solve a complete transient model with mandatory work/step bounds; no hidden DC solve.
 */
[[nodiscard]] TransientSolveReport solve_transient(const CompiledElectricalModel &model,
                                                   const TransientSolveOptions &options);
} // namespace volt
