#include <volt/pcb/placement/board_placement_model.hpp>

#include "../board_storage.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>

namespace volt {

BoardPlacementModel::BoardPlacementModel()
    : BoardPlacementModel{std::make_shared<detail::BoardPlacementState>()} {}

BoardPlacementModel::BoardPlacementModel(std::shared_ptr<const detail::BoardPlacementState> state)
    : state_{std::move(state)} {}

BoardPlacementModel::BoardPlacementModel(const BoardPlacementModel &other)
    : BoardPlacementModel{std::make_shared<detail::BoardPlacementState>(other.state())} {}

BoardPlacementModel::BoardPlacementModel(BoardPlacementModel &&other) noexcept = default;

BoardPlacementModel &BoardPlacementModel::operator=(const BoardPlacementModel &other) {
    if (this != &other) {
        state_ = std::make_shared<detail::BoardPlacementState>(other.state());
    }
    return *this;
}

BoardPlacementModel &BoardPlacementModel::operator=(BoardPlacementModel &&other) noexcept = default;

BoardPlacementModel::~BoardPlacementModel() = default;

Board::PlacementStorage::PlacementStorage()
    : PlacementStorage{std::make_shared<detail::BoardPlacementState>()} {}

Board::PlacementStorage::PlacementStorage(std::shared_ptr<detail::BoardPlacementState> state)
    : BoardPlacementModel{state}, state_{std::move(state)} {}

Board::PlacementStorage::PlacementStorage(const PlacementStorage &other)
    : PlacementStorage{std::make_shared<detail::BoardPlacementState>(other.state())} {}

Board::PlacementStorage &Board::PlacementStorage::operator=(const PlacementStorage &other) {
    if (this != &other) {
        auto replacement =
            PlacementStorage{std::make_shared<detail::BoardPlacementState>(other.state())};
        *this = std::move(replacement);
    }
    return *this;
}

[[nodiscard]] detail::BoardPlacementState &Board::PlacementStorage::mutable_state() noexcept {
    return *state_;
}

[[nodiscard]] const detail::BoardPlacementState &Board::PlacementStorage::state() const noexcept {
    return *state_;
}

[[nodiscard]] ComponentPlacementId
Board::PlacementStorage::place_component(ComponentPlacement placement) {
    if (placement_for_component(placement.component()).has_value()) {
        throw std::logic_error{"Component already has a board placement"};
    }

    return mutable_state().placements.insert(placement);
}

[[nodiscard]] const ComponentPlacement &
BoardPlacementModel::placement(ComponentPlacementId id) const {
    return state().placements.get(id);
}

[[nodiscard]] std::size_t BoardPlacementModel::placement_count() const noexcept {
    return state().placements.size();
}

[[nodiscard]] std::optional<ComponentPlacementId>
BoardPlacementModel::placement_for_component(ComponentId component) const noexcept {
    for (std::size_t index = 0; index < state().placements.size(); ++index) {
        const auto id = ComponentPlacementId{index};
        if (state().placements.get(id).component() == component) {
            return id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] const detail::BoardPlacementState &BoardPlacementModel::state() const noexcept {
    return *state_;
}

} // namespace volt
