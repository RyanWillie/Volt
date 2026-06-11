#include "board_capability_validation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/pcb/board.hpp>

namespace volt::detail {
namespace {

[[nodiscard]] std::string_view clearance_kind_name(BoardClearanceKind kind) {
    switch (kind) {
    case BoardClearanceKind::Track:
        return "track";
    case BoardClearanceKind::Pad:
        return "pad";
    case BoardClearanceKind::Via:
        return "via";
    case BoardClearanceKind::Zone:
        return "zone";
    case BoardClearanceKind::BoardEdge:
        return "board-edge";
    }
    throw std::logic_error{"Unhandled board clearance kind"};
}

[[nodiscard]] std::string capability_clearance_label(BoardClearanceKind first,
                                                     BoardClearanceKind second) {
    auto low = first;
    auto high = second;
    if (static_cast<int>(low) > static_cast<int>(high)) {
        std::swap(low, high);
    }
    return std::string{clearance_kind_name(low)} + "-to-" + std::string{clearance_kind_name(high)} +
           " clearance";
}

[[nodiscard]] std::optional<double>
profile_max_clearance_minimum(const BoardCapabilityProfile &profile, bool board_edge) {
    auto result = std::optional<double>{};
    for (const auto &entry : profile.minimum_clearances()) {
        const auto touches_edge = entry.first == BoardClearanceKind::BoardEdge ||
                                  entry.second == BoardClearanceKind::BoardEdge;
        if (touches_edge != board_edge) {
            continue;
        }
        if (!result.has_value() || entry.clearance_mm > result.value()) {
            result = entry.clearance_mm;
        }
    }
    return result;
}

[[nodiscard]] Diagnostic drc_diagnostic(std::string_view code, std::string message,
                                        std::vector<EntityRef> entities = {}) {
    return Diagnostic{Severity::Error, DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::Drc}, std::move(message),
                      std::move(entities)};
}

[[nodiscard]] Diagnostic drc_warning(std::string_view code, std::string message,
                                     std::vector<EntityRef> entities = {}) {
    return Diagnostic{Severity::Warning, DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::Drc}, std::move(message),
                      std::move(entities)};
}

void report_capability_rule(DiagnosticReport &report, double value, double minimum,
                            const std::string &label, std::vector<EntityRef> entities) {
    if (value + board_drc_epsilon < minimum) {
        report.add(drc_diagnostic(drc_diagnostic_codes::RuleBelowCapability,
                                  label + " is below capability profile minimum",
                                  std::move(entities)));
        return;
    }
    if (std::abs(value - minimum) <= board_drc_epsilon) {
        report.add(drc_warning(drc_diagnostic_codes::RuleAtCapabilityMinimum,
                               label + " is at manufacturing minimum", std::move(entities)));
    }
}

void validate_board_rule_capability(const Board &board, const BoardCapabilityProfile &profile,
                                    DiagnosticReport &report) {
    const auto &rules = board.design_rules();
    if (const auto minimum = profile_max_clearance_minimum(profile, false); minimum.has_value()) {
        report_capability_rule(report, rules.copper_clearance_mm(), minimum.value(),
                               "Board copper clearance", std::vector{EntityRef::board()});
    }
    report_capability_rule(report, rules.minimum_track_width_mm(), profile.minimum_track_width_mm(),
                           "Board minimum track width", std::vector{EntityRef::board()});
    report_capability_rule(report, rules.minimum_via_drill_diameter_mm(),
                           profile.minimum_via_drill_mm(), "Board minimum via drill",
                           std::vector{EntityRef::board()});
    report_capability_rule(report, rules.minimum_via_annular_diameter_mm(),
                           profile.minimum_via_annular_mm(), "Board minimum via annular diameter",
                           std::vector{EntityRef::board()});
    if (const auto minimum = profile_max_clearance_minimum(profile, true); minimum.has_value()) {
        report_capability_rule(report, rules.board_outline_clearance_mm(), minimum.value(),
                               "Board outline clearance", std::vector{EntityRef::board()});
    }

    for (const auto &entry : rules.clearance_matrix()) {
        const auto minimum = profile.minimum_clearance(entry.first, entry.second);
        if (!minimum.has_value()) {
            continue;
        }
        report_capability_rule(
            report, rules.clearance_mm(entry.first, entry.second), minimum.value(),
            "Board " + capability_clearance_label(entry.first, entry.second) + " matrix entry",
            std::vector{EntityRef::board()});
    }
}

void validate_net_class_capability(const Board &board, const BoardCapabilityProfile &profile,
                                   DiagnosticReport &report) {
    const auto copper_clearance_minimum = profile_max_clearance_minimum(profile, false);
    for (std::size_t index = 0; index < board.circuit().net_class_count(); ++index) {
        const auto net_class_id = NetClassId{index};
        const auto &net_class = board.circuit().net_class(net_class_id);
        const auto prefix = std::string{"Net class '"} + net_class.name().value() + "' ";
        if (net_class.copper_clearance_mm().has_value() && copper_clearance_minimum.has_value()) {
            report_capability_rule(report, net_class.copper_clearance_mm().value(),
                                   copper_clearance_minimum.value(), prefix + "copper clearance",
                                   {});
        }
        if (net_class.track_width_mm().has_value()) {
            report_capability_rule(report, net_class.track_width_mm().value(),
                                   profile.minimum_track_width_mm(), prefix + "track width", {});
        }
        if (net_class.via_drill_mm().has_value()) {
            report_capability_rule(report, net_class.via_drill_mm().value(),
                                   profile.minimum_via_drill_mm(), prefix + "via drill", {});
        }
        if (net_class.via_diameter_mm().has_value()) {
            report_capability_rule(report, net_class.via_diameter_mm().value(),
                                   profile.minimum_via_annular_mm(), prefix + "via diameter", {});
        }
    }
}

struct RoomCapabilityMinimums {
    double track_width_mm;
    double copper_clearance_mm;
};

[[nodiscard]] RoomCapabilityMinimums room_capability_minimums(const Board &board,
                                                              const BoardCapabilityProfile &profile,
                                                              const BoardRoom &room) {
    auto result = RoomCapabilityMinimums{
        profile.minimum_track_width_mm(),
        profile_max_clearance_minimum(profile, false).value_or(0.0),
    };
    if (profile.copper_weight_refinements().empty()) {
        return result;
    }

    for (const auto layer_id : room.layers()) {
        const auto &layer = board.layer(layer_id);
        if (!layer.copper_weight_oz().has_value()) {
            return result;
        }
    }

    for (const auto layer_id : room.layers()) {
        const auto weight = board.layer(layer_id).copper_weight_oz().value();
        auto applicable = std::optional<BoardCapabilityCopperWeightRefinement>{};
        for (const auto &refinement : profile.copper_weight_refinements()) {
            if (refinement.copper_weight_oz <= weight + board_drc_epsilon) {
                applicable = refinement;
            }
        }
        if (!applicable.has_value()) {
            continue;
        }
        result.track_width_mm = std::max(result.track_width_mm, applicable->minimum_track_width_mm);
        result.copper_clearance_mm =
            std::max(result.copper_clearance_mm, applicable->minimum_clearance_mm);
    }
    return result;
}

void validate_room_capability(const Board &board, const BoardCapabilityProfile &profile,
                              DiagnosticReport &report) {
    for (std::size_t index = 0; index < board.room_count(); ++index) {
        const auto room_id = BoardRoomId{index};
        const auto &room = board.room(room_id);
        const auto minimums = room_capability_minimums(board, profile, room);
        const auto entities = std::vector{EntityRef::board_room(room_id)};
        const auto prefix = std::string{"Room '"} + room.name() + "' ";
        if (room.copper_clearance_mm().has_value()) {
            report_capability_rule(report, room.copper_clearance_mm().value(),
                                   minimums.copper_clearance_mm, prefix + "copper clearance",
                                   entities);
        }
        if (room.track_width_mm().has_value()) {
            report_capability_rule(report, room.track_width_mm().value(), minimums.track_width_mm,
                                   prefix + "track width", entities);
        }
    }
}

} // namespace

void validate_capability_profile_rules(const Board &board, DiagnosticReport &report) {
    if (!board.capability_profile().has_value()) {
        return;
    }
    const auto &profile = board.capability_profile().value();
    validate_board_rule_capability(board, profile, report);
    validate_net_class_capability(board, profile, report);
    validate_room_capability(board, profile, report);
}

} // namespace volt::detail
