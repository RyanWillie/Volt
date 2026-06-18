#pragma once

#include <cstddef>
#include <memory>
#include <optional>

#include <volt/core/ids.hpp>
#include <volt/pcb/footprints/footprints.hpp>

namespace volt {

namespace detail {
struct BoardFootprintState;
}

/**
 * Owns board-cached footprint definitions used by placements and projection IO.
 *
 * Responsibility: caches resolved footprint geometry so placements and IO share one definition.
 * Invariants: footprint identity comes from selected physical parts — this model stores it, it
 *   does not choose footprints.
 * Collaborators: composed by Board; consumed by BoardPlacementModel and projection IO; acyclic.
 */
class BoardFootprintModel {
  public:
    /** Construct an empty board-footprint facade. */
    BoardFootprintModel();
    /** Copy board-footprint state. */
    BoardFootprintModel(const BoardFootprintModel &other);
    /** Move board-footprint state. */
    BoardFootprintModel(BoardFootprintModel &&other) noexcept;
    /** Copy board-footprint state. */
    BoardFootprintModel &operator=(const BoardFootprintModel &other);
    /** Move board-footprint state. */
    BoardFootprintModel &operator=(BoardFootprintModel &&other) noexcept;
    /** Destroy board-footprint state. */
    ~BoardFootprintModel();

    /** Return a cached footprint definition by board-local ID. */
    [[nodiscard]] const FootprintDefinition &footprint_definition(FootprintDefId id) const;

    /** Return the number of cached footprint definitions. */
    [[nodiscard]] std::size_t footprint_definition_count() const noexcept;

    /** Return the cached definition ID for a footprint reference, if present. */
    [[nodiscard]] std::optional<FootprintDefId>
    footprint_definition_id(const FootprintRef &ref) const noexcept;

  protected:
    /** Construct a read-only facade over owner-private storage. */
    explicit BoardFootprintModel(std::shared_ptr<const detail::BoardFootprintState> state);

  private:
    [[nodiscard]] const detail::BoardFootprintState &state() const noexcept;

    std::shared_ptr<const detail::BoardFootprintState> state_;
};

} // namespace volt
