#pragma once

#include <cstddef>
#include <optional>

#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/pcb/footprints/footprints.hpp>

namespace volt {

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
    /** Cache a footprint definition for board projection use. */
    [[nodiscard]] FootprintDefId cache_footprint_definition(FootprintDefinition footprint);

    /** Return a cached footprint definition by board-local ID. */
    [[nodiscard]] const FootprintDefinition &footprint_definition(FootprintDefId id) const;

    /** Return the number of cached footprint definitions. */
    [[nodiscard]] std::size_t footprint_definition_count() const noexcept;

    /** Return the cached definition ID for a footprint reference, if present. */
    [[nodiscard]] std::optional<FootprintDefId>
    footprint_definition_id(const FootprintRef &ref) const noexcept;

  private:
    EntityTable<FootprintDefinition, FootprintDefId> footprint_definitions_;
};

} // namespace volt
