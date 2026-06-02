#include <volt/pcb/board_placement_model.hpp>

#include <cstddef>
#include <optional>
#include <stdexcept>

namespace volt {

[[nodiscard]] ComponentPlacementId
BoardPlacementModel::place_component(ComponentPlacement placement) {
    if (placement_for_component(placement.component()).has_value()) {
        throw std::logic_error{"Component already has a board placement"};
    }

    return placements_.insert(placement);
}
[[nodiscard]] const ComponentPlacement &
BoardPlacementModel::placement(ComponentPlacementId id) const {
    return placements_.get(id);
}
[[nodiscard]] std::size_t BoardPlacementModel::placement_count() const noexcept {
    return placements_.size();
}
[[nodiscard]] std::optional<ComponentPlacementId>
BoardPlacementModel::placement_for_component(ComponentId component) const noexcept {
    for (std::size_t index = 0; index < placements_.size(); ++index) {
        const auto id = ComponentPlacementId{index};
        if (placements_.get(id).component() == component) {
            return id;
        }
    }

    return std::nullopt;
}

} // namespace volt
