#include <algorithm>
#include <volt/core/errors.hpp>
#include <volt/pcb/compiled/manufacturing_review.hpp>

namespace volt {
bool manufacturing_review_requires_minimum_mm(ManufacturingCheck check) {
    return check == ManufacturingCheck::MaskCopperClearance ||
           check == ManufacturingCheck::MaskWeb || check == ManufacturingCheck::MaskSliver ||
           check == ManufacturingCheck::LegendHeight || check == ManufacturingCheck::LegendStroke ||
           check == ManufacturingCheck::LegendPadClearance ||
           check == ManufacturingCheck::LegendMaskClearance;
}

std::optional<ManufacturingReviewRequirement>
manufacturing_review_requirement(const CompiledBoard &compiled, ManufacturingCheck check) {
    const auto emitted =
        check >= ManufacturingCheck::MaskOpenings && check <= ManufacturingCheck::RequiredPolarity;
    const auto assembly = check >= ManufacturingCheck::PartPackageReview &&
                          check <= ManufacturingCheck::AssemblyOrientation;
    if (!emitted && !assembly)
        return std::nullopt;
    auto result = ManufacturingReviewRequirement{
        check,
        assembly ? ManufacturingReviewMethod::IndependentAssemblyInspection
                 : ManufacturingReviewMethod::EmittedFabricationInspection,
        {{EntityRef::board()}},
        manufacturing_review_requires_minimum_mm(check)};
    const auto &board = compiled.board();
    const auto logical_scope = [&] {
        for (std::size_t i = 0; i < board.circuit().all<ComponentId>().size(); ++i)
            result.scope.push_back({EntityRef::component(ComponentId{i})});
        for (std::size_t i = 0; i < board.circuit().all<PinId>().size(); ++i)
            result.scope.push_back({EntityRef::pin(PinId{i})});
    };
    if (assembly)
        logical_scope();
    else
        for (std::size_t i = 0; i < board.all<BoardLayerId>().size(); ++i)
            result.scope.push_back({EntityRef::board_layer(BoardLayerId{i})});
    if (check >= ManufacturingCheck::LegendHeight && check <= ManufacturingCheck::RequiredPolarity)
        for (std::size_t i = 0; i < board.all<BoardTextId>().size(); ++i)
            result.scope.push_back({EntityRef::board_text(BoardTextId{i})});
    for (const auto &frozen : compiled.placements()) {
        const auto id = frozen.placement();
        result.scope.push_back(
            {EntityRef::component_placement(id), EntityRef::component(board.get(id).component())});
    }
    for (const auto &frozen : compiled.placements()) {
        const auto id = frozen.placement();
        if (frozen.footprint())
            for (std::size_t i = 0; i < frozen.footprint()->pads().size(); ++i)
                result.scope.push_back({EntityRef::component_placement(id),
                                        EntityRef::footprint_pad(FootprintPadId{i})});
    }
    if (!assembly)
        for (std::size_t i = 0; i < board.all<BoardViaId>().size(); ++i)
            result.scope.push_back({EntityRef::board_via(BoardViaId{i})});
    if (assembly)
        for (std::size_t i = 0; i < board.all<BoardLayerId>().size(); ++i)
            result.scope.push_back({EntityRef::board_layer(BoardLayerId{i})});
    else if (check == ManufacturingCheck::RequiredPinOne ||
             check == ManufacturingCheck::RequiredPolarity)
        logical_scope();
    return result;
}

bool manufacturing_review_allowed(const ManufacturingCoverage &coverage,
                                  const BoardCompletion &fabrication) {
    if (coverage.completion().source() != fabrication.source() ||
        coverage.completion().content_digest() != fabrication.content_digest() ||
        fabrication.status() != BoardCompletionStatus::Complete)
        return false;
    for (const auto &row : coverage.coverage()) {
        if (row.state == ManufacturingState::Failed)
            return false;
        if (row.check <= ManufacturingCheck::ProfileApplicability &&
            row.state != ManufacturingState::NativePass)
            return false;
    }
    return true;
}

ManufacturingState manufacturing_review_state(ManufacturingReviewOutcome outcome) {
    switch (outcome) {
    case ManufacturingReviewOutcome::Pass:
        return ManufacturingState::ReviewedPass;
    case ManufacturingReviewOutcome::Fail:
        return ManufacturingState::Failed;
    }
    throw KernelArgumentError{ErrorCode::InvalidArgument, "Invalid manufacturing review outcome"};
}
} // namespace volt
