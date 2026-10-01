#pragma once
// INTERNAL numerical evidence seam. Uses the same initializer/substep and gates as production.
#include <volt/electrical/transient_solve.hpp>

namespace volt::detail {
struct TransientSubstep {
    TransientEvaluation evaluation;
    std::optional<TransientSample> sample;
    std::vector<TransientStorageDerivative> derivatives;
};

[[nodiscard]] TransientSubstep initialize_transient(const CompiledElectricalModel &model,
                                                    const TransientSolveOptions &options);
[[nodiscard]] TransientSubstep
transient_be_substep(const CompiledElectricalModel &model, const TransientSolveOptions &options,
                     const TransientSample &previous, Quantity time,
                     TransientEvaluationKind kind = TransientEvaluationKind::FullStep);
} // namespace volt::detail
