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
    TransientSolveOptions(Quantity minimum_step, Quantity initial_step, Quantity maximum_step,
                          std::size_t maximum_trials, std::size_t maximum_accepted_steps,
                          double relative_temporal_tolerance = 1e-4,
                          Quantity absolute_temporal_voltage = Quantity{UnitDimension::Voltage,
                                                                        1e-6},
                          Quantity absolute_temporal_current = Quantity{UnitDimension::Current,
                                                                        1e-9});

    [[nodiscard]] const Quantity &minimum_step() const noexcept { return minimum_step_; }

    [[nodiscard]] const Quantity &initial_step() const noexcept { return initial_step_; }

    [[nodiscard]] const Quantity &maximum_step() const noexcept { return maximum_step_; }

    [[nodiscard]] std::size_t maximum_trials() const noexcept { return maximum_trials_; }

    [[nodiscard]] std::size_t maximum_accepted_steps() const noexcept {
        return maximum_accepted_steps_;
    }

    [[nodiscard]] double relative_temporal_tolerance() const noexcept { return relative_temporal_; }

    [[nodiscard]] const Quantity &absolute_temporal_voltage() const noexcept {
        return temporal_voltage_;
    }

    [[nodiscard]] const Quantity &absolute_temporal_current() const noexcept {
        return temporal_current_;
    }

    [[nodiscard]] static constexpr double relative_rank_threshold() noexcept { return 1e-12; }

    [[nodiscard]] static constexpr double minimum_reciprocal_condition() noexcept { return 1e-12; }

    [[nodiscard]] static constexpr double relative_residual_tolerance() noexcept { return 1e-9; }

    [[nodiscard]] static Quantity absolute_voltage_tolerance() {
        return {UnitDimension::Voltage, 1e-9};
    }

    [[nodiscard]] static Quantity absolute_current_tolerance() {
        return {UnitDimension::Current, 1e-12};
    }

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
    Success,
    InconsistentInitialState,
    UnsupportedInitializationTopology,
    RankDeficient,
    Inconsistent,
    IllConditioned,
    NumericalFailure,
    ResidualFailure,
    StepLimit,
    WorkLimit
};

/** Algebra evidence applies to the frozen projector or a single actual BE solve. */
struct TransientAlgebraMetrics {
    std::size_t coordinate_count = 0;
    std::optional<std::size_t> rank, augmented_rank;
    std::optional<double> reciprocal_condition, scaled_residual;
    std::optional<Quantity> voltage_residual, current_residual;
    std::optional<double> voltage_error_ratio, current_error_ratio;
};

struct TransientNodeResult {
    ElectricalNodeId node;
    Quantity potential;
};

/** Signed power is absorbed power in the retained from-to branch orientation. */
struct TransientBranchResult {
    ElectricalBranchId branch;
    Quantity voltage, current, power;
};

struct TransientProbeResult {
    ElectricalProbeKey key;
    Quantity value;
};

/** Complete observations at exactly one requested time; no dense interpolation. */
struct TransientSample {
    Quantity time;
    std::vector<TransientNodeResult> nodes;
    std::vector<TransientBranchResult> branches;
    std::vector<TransientProbeResult> probes;
};

/** SI derivative: volts/second for C and amperes/second for L, identified by compiled origin. */
struct TransientStorageDerivative {
    ElectricalBranchId branch;
    double derivative;
};
enum class TransientEvaluationKind {
    Initialization,
    AlgebraicSample,
    FullStep,
    FirstHalf,
    SecondHalf
};

/** Timed numerical evidence retained for rejected trials and numerical failure. */
struct TransientEvaluation {
    Quantity time;
    Quantity duration;
    TransientEvaluationKind kind;
    TransientSolveOutcome outcome = TransientSolveOutcome::NumericalFailure;
    TransientAlgebraMetrics metrics;
    std::vector<Diagnostic> diagnostics;
    std::size_t solve_count = 0;
    std::size_t factorization_count = 0;
    /** Available on the second half after a complete temporal trial. */
    std::optional<double> normalized_error;
    bool trial_accepted = false;
};

/** Each accepted macro trial contributes two actual half steps. */
struct TransientAcceptedHalfStep {
    Quantity time, duration;
    double normalized_error;
    TransientAlgebraMetrics metrics;
};

class TransientSolution {
  public:
    class Solver;

    [[nodiscard]] const ContentHash &analysis_identity() const noexcept {
        return analysis_identity_;
    }

    [[nodiscard]] const CompiledElectricalModel &model() const noexcept { return model_; }

    [[nodiscard]] const TransientSolveOptions &options() const noexcept { return options_; }

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
    TransientSolveReport(const CompiledElectricalModel &model,
                         const TransientSolveOptions &options);

    [[nodiscard]] static constexpr std::uint32_t contract_version() noexcept { return 1; }

    [[nodiscard]] static constexpr std::string_view backend() noexcept {
        return "eigen-5.0.0-full-piv-lu-double-dense-branch-tableau-be";
    }

    [[nodiscard]] const ContentHash &analysis_identity() const noexcept {
        return analysis_identity_;
    }

    [[nodiscard]] const CompiledElectricalModel &model() const noexcept { return model_; }

    [[nodiscard]] const TransientSolveOptions &options() const noexcept { return options_; }

    [[nodiscard]] TransientSolveOutcome outcome() const noexcept { return outcome_; }

    [[nodiscard]] bool success() const noexcept {
        return outcome_ == TransientSolveOutcome::Success;
    }

    [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const noexcept {
        return diagnostics_;
    }

    [[nodiscard]] const std::vector<TransientEvaluation> &evaluations() const noexcept {
        return evaluations_;
    }

    [[nodiscard]] const std::vector<TransientStorageDerivative> &
    initial_derivatives() const noexcept {
        return derivatives_;
    }

    [[nodiscard]] const std::vector<TransientAcceptedHalfStep> &
    accepted_half_steps() const noexcept {
        return accepted_half_steps_;
    }

    [[nodiscard]] const Quantity &last_accepted_time() const noexcept {
        return last_accepted_time_;
    }

    [[nodiscard]] std::size_t trial_count() const noexcept { return trial_count_; }

    [[nodiscard]] std::size_t accepted_step_count() const noexcept { return accepted_step_count_; }

    [[nodiscard]] std::size_t solve_count() const noexcept { return solve_count_; }

    [[nodiscard]] std::size_t factorization_count() const noexcept { return factorization_count_; }

    [[nodiscard]] const TransientSolution *solution() const & noexcept {
        return solution_ ? &*solution_ : nullptr;
    }

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

[[nodiscard]] TransientSolveReport solve_transient(const CompiledElectricalModel &model,
                                                   const TransientSolveOptions &options);
} // namespace volt
