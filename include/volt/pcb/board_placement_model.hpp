#pragma once

#include <cstddef>
#include <optional>

#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/pcb/board_features.hpp>

namespace volt {

class BoardPlacementModel {
  public:
    [[nodiscard]] ComponentPlacementId place_component(ComponentPlacement placement);

    [[nodiscard]] const ComponentPlacement &placement(ComponentPlacementId id) const;

    [[nodiscard]] std::size_t placement_count() const noexcept;

    [[nodiscard]] std::optional<ComponentPlacementId>
    placement_for_component(ComponentId component) const noexcept;

  private:
    EntityTable<ComponentPlacement, ComponentPlacementId> placements_;
};

} // namespace volt
