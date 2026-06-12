#pragma once

#include <optional>
#include <vector>

#include <volt/pcb/board.hpp>

namespace volt::detail {

struct RoomRuleValue {
    double value_mm;
    BoardRoomId room;
};

class BoardRoomRuleResolver {
  public:
    explicit BoardRoomRuleResolver(const Board &board);

    [[nodiscard]] std::optional<RoomRuleValue> track_width_override(const BoardTrack &track) const;

    [[nodiscard]] std::optional<RoomRuleValue>
    track_width_override(BoardLayerId layer, const std::vector<BoardPoint> &points,
                         double width_mm) const;

    [[nodiscard]] double effective_track_width_mm(BoardLayerId layer,
                                                  const std::vector<BoardPoint> &points,
                                                  double base_width_mm) const;

    [[nodiscard]] std::optional<RoomRuleValue>
    copper_clearance_override(const BoardCopperShape &lhs, const BoardCopperShape &rhs) const;

  private:
    [[nodiscard]] std::optional<BoardRoomId> track_width_room(BoardLayerId layer,
                                                              const std::vector<BoardPoint> &points,
                                                              double width_mm) const;

    [[nodiscard]] std::optional<BoardRoomId>
    copper_clearance_room(const BoardCopperShape &lhs, const BoardCopperShape &rhs) const;

    [[nodiscard]] static bool room_layers_intersect(const BoardRoom &room,
                                                    const std::vector<BoardLayerId> &layers);

    [[nodiscard]] static bool shape_satisfies_room(const BoardCopperShape &shape,
                                                   const BoardRoom &room);

    [[nodiscard]] static bool track_satisfies_room(BoardLayerId layer,
                                                   const std::vector<BoardPoint> &points,
                                                   double width_mm, const BoardRoom &room);

    [[nodiscard]] bool room_has_higher_precedence(BoardRoomId candidate, BoardRoomId current) const;

    const Board &board_;
};

} // namespace volt::detail
