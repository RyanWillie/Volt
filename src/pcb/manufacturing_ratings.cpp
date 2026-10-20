#include "../library/selected_part_erc_detail.hpp"
#include "manufacturing_assembly_detail.hpp"
#include <algorithm>
#include <volt/core/errors.hpp>

namespace volt::detail {
SelectedPartRatings compiled_selected_part_ratings(const CompiledBoard &compiled) {
    const auto &circuit = compiled.board().circuit();
    auto subjects = std::vector<SelectedPartErcSubject>{};
    for (std::size_t i = 0; i < circuit.all<ComponentId>().size(); ++i) {
        const auto component = ComponentId{i};
        const auto &reference = queries::selected_library_part_ref(circuit, component);
        if (!reference) {
            subjects.push_back({component, nullptr, nullptr, {}});
            continue;
        }
        const auto &part = compiled.part_definitions().resolve(*reference);
        const auto *original = compiled.selected_component_definition(component);
        const auto &projected = circuit.get(circuit.get(component).definition());
        if (!original || part.implemented_component() != original->content_identity())
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Compiled rating subject differs from exact original component",
                                   EntityRef::component(component)};
        const auto &original_keys = original->contract().pin_keys();
        const auto &projected_keys = projected.contract().pin_keys();
        auto pins = std::vector<PinId>{};
        for (const auto &key : original_keys) {
            const auto found = std::ranges::find(projected_keys, key);
            if (found == projected_keys.end())
                throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                       "Compiled rating subject has no exact projected PinKey",
                                       EntityRef::component(component)};
            const auto index = static_cast<std::size_t>(found - projected_keys.begin());
            const auto pin =
                queries::pin_by_definition(circuit, component, projected.pins()[index]);
            if (!pin)
                throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                       "Compiled rating subject has no exact occurrence pin",
                                       EntityRef::component(component)};
            pins.push_back(*pin);
        }
        subjects.push_back({component, &part, original, std::move(pins)});
    }
    const auto view = compiled.view();
    return assess_selected_part_ratings(circuit, view.logical_continuity(), subjects,
                                        view.has_complete_logical_continuity());
}

void apply_manufacturing_ratings(ManufacturingCoverageRow &row,
                                 const SelectedPartRatings &ratings) {
    row.owner = ManufacturingOwner::NativeRatings;
    auto checked = false;
    auto missing = false;
    auto failed = false;
    for (const auto &subject : ratings.subjects) {
        failed = failed || subject.state == SelectedPartRatingState::Failed;
        missing = missing || subject.state == SelectedPartRatingState::NotEvaluated;
        checked = checked || !subject.observations.empty();
        row.entities.insert(row.entities.end(), subject.entities.begin(), subject.entities.end());
    }
    if (failed) {
        row.state = ManufacturingState::Failed;
        row.blocker = ManufacturingBlocker::ConstraintFailed;
        row.reason = "At least one supported selected-part rating comparison failed";
        row.remediation = "Correct the exact selected part or authored electrical intent";
    } else if (missing || !checked) {
        row.state = ManufacturingState::NotEvaluated;
        row.blocker = ManufacturingBlocker::MissingInput;
        row.reason = checked
                         ? "Selected-part rating subjects include explicit uncovered obligations"
                         : "No supported selected-part rating obligation was checked";
        row.remediation = "Supply complete supported unconditional rating subjects and inputs";
    } else {
        row.state = ManufacturingState::NativePass;
        row.blocker = ManufacturingBlocker::None;
        row.reason = "Every applicable selected-part subject has checked supported rating coverage";
        row.remediation.clear();
    }
}
} // namespace volt::detail
