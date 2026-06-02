#pragma once

#include <cstddef>
#include <optional>

#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/pcb/footprints.hpp>

namespace volt {

/** Owns board-cached footprint definitions used by placements and projection IO. */
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
