#include <volt/pcb/footprints/board_footprint_model.hpp>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>

namespace volt {

[[nodiscard]] FootprintDefId
BoardFootprintModel::cache_footprint_definition(FootprintDefinition footprint) {
    const auto existing = footprint_definition_id(footprint.ref());
    if (existing.has_value()) {
        if (footprint_definition(existing.value()) == footprint) {
            return existing.value();
        }
        throw std::logic_error{"Board footprint definition conflicts with existing definition"};
    }

    return footprint_definitions_.insert(std::move(footprint));
}

[[nodiscard]] const FootprintDefinition &
BoardFootprintModel::footprint_definition(FootprintDefId id) const {
    return footprint_definitions_.get(id);
}

[[nodiscard]] std::size_t BoardFootprintModel::footprint_definition_count() const noexcept {
    return footprint_definitions_.size();
}

[[nodiscard]] std::optional<FootprintDefId>
BoardFootprintModel::footprint_definition_id(const FootprintRef &ref) const noexcept {
    for (std::size_t index = 0; index < footprint_definitions_.size(); ++index) {
        const auto id = FootprintDefId{index};
        if (footprint_definitions_.get(id).ref() == ref) {
            return id;
        }
    }

    return std::nullopt;
}

} // namespace volt
