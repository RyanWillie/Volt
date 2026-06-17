#pragma once

#include <cstddef>
#include <optional>

#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/mutation_access.hpp>
#include <volt/pcb/features/board_features.hpp>

namespace volt {

/**
 * Owns physical placements of logical components on a board, and pad-to-net resolution.
 *
 * Responsibility: stores where each placed logical component sits and resolves its footprint
 *   pads back to logical pins and existing nets.
 * Invariants: placements reference existing logical components; resolved pads map to existing
 *   pins/nets.
 * Collaborators: composed by Board; uses BoardFootprintModel for geometry; feeds ratsnest and
 *   DRC; acyclic.
 */
class BoardPlacementModel {
  public:
    /** Return a component placement by board-local ID. */
    [[nodiscard]] const ComponentPlacement &placement(ComponentPlacementId id) const;

    /** Return the number of component placements. */
    [[nodiscard]] std::size_t placement_count() const noexcept;

    /** Return the placement for a logical component, if present. */
    [[nodiscard]] std::optional<ComponentPlacementId>
    placement_for_component(ComponentId component) const noexcept;

    /** Place one logical component on the board and return its placement ID. */
    [[nodiscard]] ComponentPlacementId place_component(detail::KernelMutationAccess access,
                                                       ComponentPlacement placement);

  private:
    EntityTable<ComponentPlacement, ComponentPlacementId> placements_;
};

} // namespace volt
