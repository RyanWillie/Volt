#include <volt/pcb/footprints/board_footprint_model.hpp>

#include <volt/core/errors.hpp>

#include "../board_storage.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

namespace volt {

BoardFootprintModel::BoardFootprintModel()
    : BoardFootprintModel{std::make_shared<detail::BoardFootprintState>()} {}

BoardFootprintModel::BoardFootprintModel(std::shared_ptr<const detail::BoardFootprintState> state)
    : state_{std::move(state)} {}

BoardFootprintModel::BoardFootprintModel(const BoardFootprintModel &other)
    : BoardFootprintModel{std::make_shared<detail::BoardFootprintState>(other.state())} {}

BoardFootprintModel::BoardFootprintModel(BoardFootprintModel &&other) noexcept = default;

BoardFootprintModel &BoardFootprintModel::operator=(const BoardFootprintModel &other) {
    if (this != &other) {
        state_ = std::make_shared<detail::BoardFootprintState>(other.state());
    }
    return *this;
}

BoardFootprintModel &BoardFootprintModel::operator=(BoardFootprintModel &&other) noexcept = default;

BoardFootprintModel::~BoardFootprintModel() = default;

Board::FootprintStorage::FootprintStorage()
    : FootprintStorage{std::make_shared<detail::BoardFootprintState>()} {}

Board::FootprintStorage::FootprintStorage(std::shared_ptr<detail::BoardFootprintState> state)
    : BoardFootprintModel{state}, state_{std::move(state)} {}

Board::FootprintStorage::FootprintStorage(const FootprintStorage &other)
    : FootprintStorage{std::make_shared<detail::BoardFootprintState>(other.state())} {}

Board::FootprintStorage &Board::FootprintStorage::operator=(const FootprintStorage &other) {
    if (this != &other) {
        auto replacement =
            FootprintStorage{std::make_shared<detail::BoardFootprintState>(other.state())};
        *this = std::move(replacement);
    }
    return *this;
}

[[nodiscard]] detail::BoardFootprintState &Board::FootprintStorage::mutable_state() noexcept {
    return *state_;
}

[[nodiscard]] const detail::BoardFootprintState &Board::FootprintStorage::state() const noexcept {
    return *state_;
}

[[nodiscard]] FootprintDefId
Board::FootprintStorage::cache_footprint_definition(FootprintDefinition footprint) {
    const auto existing = footprint_definition_id(footprint.ref());
    if (existing.has_value()) {
        if (footprint_definition(existing.value()) == footprint) {
            return existing.value();
        }
        throw KernelLogicError{ErrorCode::DuplicateName,
                               "Board footprint definition conflicts with existing definition"};
    }

    return mutable_state().footprint_definitions.insert(std::move(footprint));
}

[[nodiscard]] const FootprintDefinition &
BoardFootprintModel::footprint_definition(FootprintDefId id) const {
    return state().footprint_definitions.get(id);
}

[[nodiscard]] std::size_t BoardFootprintModel::footprint_definition_count() const noexcept {
    return state().footprint_definitions.size();
}

[[nodiscard]] std::optional<FootprintDefId>
BoardFootprintModel::footprint_definition_id(const FootprintRef &ref) const noexcept {
    for (std::size_t index = 0; index < state().footprint_definitions.size(); ++index) {
        const auto id = FootprintDefId{index};
        if (state().footprint_definitions.get(id).ref() == ref) {
            return id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] const detail::BoardFootprintState &BoardFootprintModel::state() const noexcept {
    return *state_;
}

} // namespace volt
