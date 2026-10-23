#pragma once
#include "manufacturing_fixture.hpp"
#include <volt/io/pcb/manufacturing_evidence.hpp>

namespace volt::test {
inline CompletionFixtureOptions evidence_options() {
    auto options = manufacturing_options();
    options.tracks = false;
    options.via = false;
    options.explicit_fitted = true;
    options.rules = BoardDesignRules{0.15, 0.15, 0.20, 0.60};
    return options;
}

inline std::string synthetic_profile_source() {
    return "Synthetic profile A v1; not vendor acceptance";
}

inline std::string synthetic_review_bytes() {
    return "SYNTHETIC ASSERTION: exercises native binding only; no human inspected a board";
}

inline ManufacturingTarget evidence_target(const CompiledBoard &compiled, bool assembly = false,
                                           CplProjectionOptions offsets = {}) {
    auto process = manufacturing_process();
    if (assembly)
        process.assembly = ManufacturingAssembly::SmtAndThroughHole;
    return {compiled.identity(),
            compiled.content_digest(),
            assembly ? ManufacturingGoal::FabricationAndAssembly : ManufacturingGoal::Fabrication,
            process,
            manufacturing_profile(false, process),
            assembly ? std::optional{std::move(offsets)} : std::nullopt};
}

inline io::ManufacturingEvidenceInput
synthetic_evidence(const CompiledBoard &compiled, const io::ManufacturingEvaluation &evaluated,
                   std::optional<ManufacturingCheck> only = {}) {
    auto result = io::ManufacturingEvidenceInput{};
    result.options.basename = evaluated.binding().basename;
    for (std::size_t i = 0; i < static_cast<std::size_t>(ManufacturingCheck::Count); ++i) {
        const auto check = static_cast<ManufacturingCheck>(i);
        const auto requirement = manufacturing_review_requirement(compiled, check);
        if (!requirement || (only && check != *only) ||
            !evaluated.coverage().coverage()[i].applicable)
            continue;
        result.assertions.push_back(
            {evaluated.binding(), *requirement, ManufacturingReviewOutcome::Pass,
             "Synthetic fixture, not an actual reviewer", "synthetic://test/profile-a", "source",
             "Synthetic complete leaf criterion; not manufacturer policy",
             requirement->requires_minimum_mm ? std::optional{0.15} : std::nullopt,
             "Synthetic observation only", "Binding and policy regression only", "review",
             sha256_content_hash(synthetic_review_bytes())});
    }
    result.attachments = {{"source", synthetic_profile_source()},
                          {"review", synthetic_review_bytes()}};
    return result;
}
} // namespace volt::test
