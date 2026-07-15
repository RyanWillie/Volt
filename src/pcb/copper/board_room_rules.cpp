#include "board_room_rules.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

namespace volt::detail {

BoardRoomRuleResolver::BoardRoomRuleResolver(const Board &board) : board_{board} {}

[[nodiscard]] std::optional<RoomRuleValue>
BoardRoomRuleResolver::track_width_override(const BoardTrack &track) const {
    return track_width_override(track.layer(), track.points(), track.width_mm());
}

[[nodiscard]] std::optional<RoomRuleValue> BoardRoomRuleResolver::track_width_override(
    BoardLayerId layer, const std::vector<BoardPoint> &points, double width_mm) const {
    const auto room_id = track_width_room(layer, points, width_mm);
    if (!room_id.has_value()) {
        return std::nullopt;
    }
    return RoomRuleValue{board_.get(room_id.value()).track_width_mm().value(), room_id.value()};
}

[[nodiscard]] double BoardRoomRuleResolver::effective_track_width_mm(
    BoardLayerId layer, const std::vector<BoardPoint> &points, double base_width_mm) const {
    auto width_mm = base_width_mm;
    while (true) {
        const auto room_override = track_width_override(layer, points, width_mm);
        if (!room_override.has_value()) {
            return width_mm;
        }
        if (room_override->value_mm <= width_mm + board_drc_epsilon) {
            return width_mm;
        }
        width_mm = room_override->value_mm;
    }
}

[[nodiscard]] std::optional<RoomRuleValue>
BoardRoomRuleResolver::copper_clearance_override(const BoardCopperShape &lhs,
                                                 const BoardCopperShape &rhs) const {
    const auto room_id = copper_clearance_room(lhs, rhs);
    if (!room_id.has_value()) {
        return std::nullopt;
    }
    return RoomRuleValue{board_.get(room_id.value()).copper_clearance_mm().value(),
                         room_id.value()};
}

[[nodiscard]] std::optional<BoardRoomId>
BoardRoomRuleResolver::track_width_room(BoardLayerId layer, const std::vector<BoardPoint> &points,
                                        double width_mm) const {
    auto result = std::optional<BoardRoomId>{};
    for (std::size_t index = 0; index < board_.all<volt::BoardRoomId>().size(); ++index) {
        const auto room_id = BoardRoomId{index};
        const auto &room = board_.get(room_id);
        if (!room.track_width_mm().has_value() ||
            !track_satisfies_room(layer, points, width_mm, room)) {
            continue;
        }
        if (!result.has_value() || room_has_higher_precedence(room_id, result.value())) {
            result = room_id;
        }
    }
    return result;
}

[[nodiscard]] std::optional<BoardRoomId>
BoardRoomRuleResolver::copper_clearance_room(const BoardCopperShape &lhs,
                                             const BoardCopperShape &rhs) const {
    auto result = std::optional<BoardRoomId>{};
    for (std::size_t index = 0; index < board_.all<volt::BoardRoomId>().size(); ++index) {
        const auto room_id = BoardRoomId{index};
        const auto &room = board_.get(room_id);
        if (!room.copper_clearance_mm().has_value() || !shape_satisfies_room(lhs, room) ||
            !shape_satisfies_room(rhs, room)) {
            continue;
        }
        if (!result.has_value() || room_has_higher_precedence(room_id, result.value())) {
            result = room_id;
        }
    }
    return result;
}

[[nodiscard]] bool
BoardRoomRuleResolver::room_layers_intersect(const BoardRoom &room,
                                             const std::vector<BoardLayerId> &layers) {
    for (const auto room_layer : room.layers()) {
        if (std::find(layers.begin(), layers.end(), room_layer) != layers.end()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool BoardRoomRuleResolver::shape_satisfies_room(const BoardCopperShape &shape,
                                                               const BoardRoom &room) {
    return room_layers_intersect(room, shape.layers) &&
           shape_satisfies_outline(shape, room.outline(), 0.0);
}

[[nodiscard]] bool
BoardRoomRuleResolver::track_satisfies_room(BoardLayerId layer,
                                            const std::vector<BoardPoint> &points, double width_mm,
                                            const BoardRoom &room) {
    if (std::find(room.layers().begin(), room.layers().end(), layer) == room.layers().end()) {
        return false;
    }
    const auto radius = width_mm / 2.0;
    for (std::size_t index = 1; index < points.size(); ++index) {
        if (!outline_contains_segment(room.outline(), points[index - 1U], points[index], radius,
                                      0.0)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool BoardRoomRuleResolver::room_has_higher_precedence(BoardRoomId candidate,
                                                                     BoardRoomId current) const {
    const auto candidate_priority = board_.get(candidate).priority();
    const auto current_priority = board_.get(current).priority();
    return candidate_priority > current_priority ||
           (candidate_priority == current_priority && candidate.index() < current.index());
}

} // namespace volt::detail
