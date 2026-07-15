#pragma once

#include <volt/core/ids.hpp>
#include <volt/pcb/geometry/board_geometry.hpp>

namespace volt {

/** Complete physical move of one existing component placement. */
struct BoardPlacementMove {
    /// Placement to update.
    ComponentPlacementId placement;
    /// New board-space origin.
    BoardPoint position;
    /// New board-space rotation.
    BoardRotation rotation;
    /// New board side.
    BoardSide side = BoardSide::Top;
};

} // namespace volt
