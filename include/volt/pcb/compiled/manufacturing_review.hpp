#pragma once
#include <volt/pcb/compiled/manufacturing_target.hpp>

namespace volt {
/** Only explicitly reviewed native inspection methods may satisfy a review leaf. */
enum class ManufacturingReviewMethod {
    EmittedFabricationInspection,
    IndependentAssemblyInspection
};
/** A declared human outcome, never proof that inspection actually happened. */
enum class ManufacturingReviewOutcome { Pass, Fail };

/** Native-derived complete compound scope for one finite review obligation. */
struct ManufacturingReviewRequirement {
    /** Exact closed leaf. */
    ManufacturingCheck check;
    /** Only accepted inspection method for this leaf. */
    ManufacturingReviewMethod method;
    /** Ordered tuples preserve placement-local pad associations. */
    std::vector<std::vector<EntityRef>> scope;
    /** Whether the assertion must declare a minimum in millimetres. */
    bool requires_minimum_mm;
};

/** Closed criterion shape; only these leaves require an explicit minimum in mm. */
[[nodiscard]] bool manufacturing_review_requires_minimum_mm(ManufacturingCheck check);
/** Derive one eligible requirement; other deferred/native leaves are ineligible. */
[[nodiscard]] std::optional<ManufacturingReviewRequirement>
manufacturing_review_requirement(const CompiledBoard &compiled, ManufacturingCheck check);
/** Closed non-waiver preconditions over authentic immutable native results. */
[[nodiscard]] bool manufacturing_review_allowed(const ManufacturingCoverage &coverage,
                                                const BoardCompletion &fabrication);
/** Native transition for a byte-verified eligible human assertion. */
[[nodiscard]] ManufacturingState manufacturing_review_state(ManufacturingReviewOutcome outcome);
} // namespace volt
