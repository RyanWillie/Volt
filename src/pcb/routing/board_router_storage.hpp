#pragma once

#include <volt/pcb/routing/board_router.hpp>

namespace volt::detail {

// Only mutable consumed Circuit relationships are copied. Other input families are immutable
// and append-only, so their counts are sufficient while borrowed owners remain at stable addresses.
struct BoardRoutingInputs {
    board_entity_range_t<BoardLayerId> geometry;
    std::size_t definitions;
    std::size_t net_classes;
    std::size_t port_bindings;
    std::size_t module_instances;
    std::vector<std::optional<LibraryPartRef>> selected;
    std::vector<std::optional<NetClassId>> classes;
    std::vector<std::optional<NetId>> memberships;

    explicit BoardRoutingInputs(const Board &board);
    [[nodiscard]] bool current(const Board &board) const;

    void rebase(const Board &board) noexcept { geometry = board.all<BoardLayerId>(); }
};

struct BoardRoutingState {
    BoardRoutingInputs inputs;
    std::vector<PadResolution> pads;
    std::optional<BoardSpatialIndex> index;
};

} // namespace volt::detail
