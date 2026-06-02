#pragma once

#include <cstddef>
#include <optional>

#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/pcb/board_features.hpp>

namespace volt {

class Board;

/** Owns physical placements of logical components on a board. */
class BoardPlacementModel {
  public:
    /** Return a component placement by board-local ID. */
    [[nodiscard]] const ComponentPlacement &placement(ComponentPlacementId id) const;

    /** Return the number of component placements. */
    [[nodiscard]] std::size_t placement_count() const noexcept;

    /** Return the placement for a logical component, if present. */
    [[nodiscard]] std::optional<ComponentPlacementId>
    placement_for_component(ComponentId component) const noexcept;

  private:
    friend class Board;

    /** Place one logical component on the board and return its placement ID. */
    [[nodiscard]] ComponentPlacementId place_component(ComponentPlacement placement);

    EntityTable<ComponentPlacement, ComponentPlacementId> placements_;
};

} // namespace volt
