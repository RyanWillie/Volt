#pragma once

#include <cstddef>
#include <optional>

#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/pcb/footprints.hpp>

namespace volt {

class BoardFootprintModel {
  public:
    [[nodiscard]] FootprintDefId cache_footprint_definition(FootprintDefinition footprint);

    [[nodiscard]] const FootprintDefinition &footprint_definition(FootprintDefId id) const;

    [[nodiscard]] std::size_t footprint_definition_count() const noexcept;

    [[nodiscard]] std::optional<FootprintDefId>
    footprint_definition_id(const FootprintRef &ref) const noexcept;

  private:
    EntityTable<FootprintDefinition, FootprintDefId> footprint_definitions_;
};

} // namespace volt
