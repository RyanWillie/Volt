#include "completion_detail.hpp"
#include <ranges>
#include <volt/circuit/constraints/net_class_resolution.hpp>

namespace volt::detail {
namespace {
[[nodiscard]] BoardClearanceKind clearance_kind(const BoardCompletionCopper &item) {
    return item.pad                                          ? BoardClearanceKind::Pad
           : item.kind == BoardCompletionCopperKind::Segment ? BoardClearanceKind::Track
           : item.kind == BoardCompletionCopperKind::Disc    ? BoardClearanceKind::Via
                                                             : BoardClearanceKind::Zone;
}

[[nodiscard]] bool trimmed_track(const BoardCompletionCopper &item,
                                 const std::vector<CompletionOpening> &openings) {
    return item.kind == BoardCompletionCopperKind::Segment &&
           std::ranges::any_of(openings, [&](const auto &opening) {
               return primitive_distance(primitive(item), {opening.kind, opening.points}) <=
                      item.radius_mm + opening.radius_mm + board_drc_epsilon;
           });
}
} // namespace

void validate_completion_clearance(CompletionWork &work) {
    const auto &board = work.compiled.board();
    const auto continuity = work.compiled.view().logical_continuity();
    auto island_of = std::vector<std::size_t>(work.copper.size());
    for (std::size_t i = 0; i < work.islands.size(); ++i)
        for (const auto item : work.islands[i].copper)
            island_of[item] = i;
    for (std::size_t a = 0; a < work.copper.size(); ++a)
        for (std::size_t b = a + 1; b < work.copper.size(); ++b) {
            const auto &lhs = work.copper[a];
            const auto &rhs = work.copper[b];
            // Proven contacts form one physical conductor; shorts/isolated contacts
            // have already been diagnosed without assigning labels to netless copper.
            if (island_of[a] == island_of[b] || !lhs.contact_evaluated || !rhs.contact_evaluated)
                continue;
            const auto check = check_copper_clearance(
                board, continuity, {primitive(lhs), lhs.net, lhs.layers, lhs.radius_mm},
                clearance_kind(lhs), {primitive(rhs), rhs.net, rhs.layers, rhs.radius_mm},
                clearance_kind(rhs));
            if (!check.violates)
                continue;
            auto entities = completion_entities(lhs);
            const auto other = completion_entities(rhs);
            entities.insert(entities.end(), other.begin(), other.end());
            entities.push_back(EntityRef::board());
            if (check.room)
                entities.push_back(EntityRef::board_room(*check.room));
            else
                for (const auto net : {lhs.net, rhs.net})
                    if (net)
                        if (const auto net_class = resolve_net_class(board.circuit(), *net))
                            entities.push_back(EntityRef::net_class(*net_class));
            const auto measurement =
                DiagnosticMeasurement{check.actual_clearance_mm, check.required_clearance_mm};
            // An ideal boundary inside a removed drill is not a finished-copper
            // spacing witness. Do not turn that lower bound into a proven failure.
            if (check.actual_clearance_mm <= board_drc_epsilon ||
                trimmed_track(lhs, work.openings) || trimmed_track(rhs, work.openings)) {
                work.note(BoardCompletionCheck::Geometry, BoardCompletionState::NotEvaluated,
                          pcb_completion_diagnostic_codes::GeometryNotEvaluated,
                          "Finished copper clearance near an opening or uncertain contact is not "
                          "evaluated; resolve the scoped geometry",
                          std::move(entities), measurement);
                continue;
            }
            work.note(BoardCompletionCheck::Geometry, BoardCompletionState::Failed,
                      drc_diagnostic_codes::CopperClearanceViolation,
                      "Separated emitted copper violates native kind/class/room clearance; "
                      "increase spacing without assigning a logical net",
                      std::move(entities), measurement);
        }
}
} // namespace volt::detail
