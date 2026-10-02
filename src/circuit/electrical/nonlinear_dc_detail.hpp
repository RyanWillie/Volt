#pragma once

#include <vector>

#include <volt/electrical/dc_solve.hpp>

namespace volt::detail {
/** Private original-equation evaluation; direction uses the native tableau coordinate order. */
struct NonlinearDcEquationEvaluation {
    ShockleyDiodeEvaluationStatus status;
    std::vector<double> residual;
    std::vector<double> jacobian_direction;
};

[[nodiscard]] NonlinearDcEquationEvaluation
evaluate_nonlinear_dc_equations(const CompiledElectricalModel &model, const DcSolveOptions &options,
                                const std::vector<double> &coordinates,
                                const std::vector<double> &direction);
} // namespace volt::detail
