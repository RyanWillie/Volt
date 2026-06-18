#pragma once

#include <cstddef>
#include <memory>
#include <optional>

#include <volt/core/ids.hpp>
#include <volt/pcb/features/board_features.hpp>

namespace volt {

namespace detail {
struct BoardPlacementState;
}

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
    /** Construct an empty board-placement facade. */
    BoardPlacementModel();
    /** Copy board-placement state. */
    BoardPlacementModel(const BoardPlacementModel &other);
    /** Move board-placement state. */
    BoardPlacementModel(BoardPlacementModel &&other) noexcept;
    /** Copy board-placement state. */
    BoardPlacementModel &operator=(const BoardPlacementModel &other);
    /** Move board-placement state. */
    BoardPlacementModel &operator=(BoardPlacementModel &&other) noexcept;
    /** Destroy board-placement state. */
    ~BoardPlacementModel();

    /** Return a component placement by board-local ID. */
    [[nodiscard]] const ComponentPlacement &placement(ComponentPlacementId id) const;

    /** Return the number of component placements. */
    [[nodiscard]] std::size_t placement_count() const noexcept;

    /** Return the placement for a logical component, if present. */
    [[nodiscard]] std::optional<ComponentPlacementId>
    placement_for_component(ComponentId component) const noexcept;

  protected:
    /** Construct a read-only facade over owner-private storage. */
    explicit BoardPlacementModel(std::shared_ptr<const detail::BoardPlacementState> state);

  private:
    [[nodiscard]] const detail::BoardPlacementState &state() const noexcept;

    std::shared_ptr<const detail::BoardPlacementState> state_;
};

} // namespace volt
