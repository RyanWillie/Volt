#include <volt/pcb/copper/board_copper_model.hpp>

#include <volt/core/errors.hpp>

#include "../board_storage.hpp"

#include <cstddef>
#include <memory>
#include <utility>

namespace volt {

BoardCopperModel::BoardCopperModel()
    : BoardCopperModel{std::make_shared<detail::BoardCopperState>()} {}

BoardCopperModel::BoardCopperModel(std::shared_ptr<const detail::BoardCopperState> state)
    : state_{std::move(state)} {}

BoardCopperModel::BoardCopperModel(const BoardCopperModel &other)
    : BoardCopperModel{std::make_shared<detail::BoardCopperState>(other.state())} {}

BoardCopperModel::BoardCopperModel(BoardCopperModel &&other) noexcept = default;

BoardCopperModel &BoardCopperModel::operator=(const BoardCopperModel &other) {
    if (this != &other) {
        state_ = std::make_shared<detail::BoardCopperState>(other.state());
    }
    return *this;
}

BoardCopperModel &BoardCopperModel::operator=(BoardCopperModel &&other) noexcept = default;

BoardCopperModel::~BoardCopperModel() = default;

Board::CopperStorage::CopperStorage()
    : CopperStorage{std::make_shared<detail::BoardCopperState>()} {}

Board::CopperStorage::CopperStorage(std::shared_ptr<detail::BoardCopperState> state)
    : BoardCopperModel{state}, state_{std::move(state)} {}

Board::CopperStorage::CopperStorage(const CopperStorage &other)
    : CopperStorage{std::make_shared<detail::BoardCopperState>(other.state())} {}

Board::CopperStorage &Board::CopperStorage::operator=(const CopperStorage &other) {
    if (this != &other) {
        auto replacement = CopperStorage{std::make_shared<detail::BoardCopperState>(other.state())};
        *this = std::move(replacement);
    }
    return *this;
}

[[nodiscard]] detail::BoardCopperState &Board::CopperStorage::mutable_state() noexcept {
    return *state_;
}

[[nodiscard]] const detail::BoardCopperState &Board::CopperStorage::state() const noexcept {
    return *state_;
}

[[nodiscard]] BoardTrackId Board::CopperStorage::add_track(BoardTrack track) {
    return mutable_state().tracks.insert(std::move(track));
}

[[nodiscard]] BoardViaId Board::CopperStorage::add_via(BoardVia via) {
    return mutable_state().vias.insert(via);
}

[[nodiscard]] BoardZoneId Board::CopperStorage::add_zone(BoardZone zone) {
    return mutable_state().zones.insert(std::move(zone));
}

[[nodiscard]] BoardKeepoutId Board::CopperStorage::add_keepout(BoardKeepout keepout) {
    return mutable_state().keepouts.insert(std::move(keepout));
}

[[nodiscard]] BoardRoomId Board::CopperStorage::add_room(BoardRoom room) {
    for (std::size_t index = 0; index < state().rooms.size(); ++index) {
        if (state().rooms.get(BoardRoomId{index}).name() == room.name()) {
            throw KernelLogicError{ErrorCode::DuplicateName, "Board room name already exists"};
        }
    }
    return mutable_state().rooms.insert(std::move(room));
}

[[nodiscard]] BoardTextId Board::CopperStorage::add_text(BoardText text) {
    return mutable_state().texts.insert(std::move(text));
}

[[nodiscard]] const BoardTrack &BoardCopperModel::track(BoardTrackId id) const {
    return state().tracks.get(id);
}

[[nodiscard]] std::size_t BoardCopperModel::track_count() const noexcept {
    return state().tracks.size();
}

[[nodiscard]] const BoardVia &BoardCopperModel::via(BoardViaId id) const {
    return state().vias.get(id);
}

[[nodiscard]] std::size_t BoardCopperModel::via_count() const noexcept {
    return state().vias.size();
}

[[nodiscard]] const BoardZone &BoardCopperModel::zone(BoardZoneId id) const {
    return state().zones.get(id);
}

[[nodiscard]] std::size_t BoardCopperModel::zone_count() const noexcept {
    return state().zones.size();
}

[[nodiscard]] const BoardKeepout &BoardCopperModel::keepout(BoardKeepoutId id) const {
    return state().keepouts.get(id);
}

[[nodiscard]] std::size_t BoardCopperModel::keepout_count() const noexcept {
    return state().keepouts.size();
}

[[nodiscard]] const BoardRoom &BoardCopperModel::room(BoardRoomId id) const {
    return state().rooms.get(id);
}

[[nodiscard]] std::size_t BoardCopperModel::room_count() const noexcept {
    return state().rooms.size();
}

[[nodiscard]] const BoardText &BoardCopperModel::text(BoardTextId id) const {
    return state().texts.get(id);
}

[[nodiscard]] std::size_t BoardCopperModel::text_count() const noexcept {
    return state().texts.size();
}

[[nodiscard]] const detail::BoardCopperState &BoardCopperModel::state() const noexcept {
    return *state_;
}

} // namespace volt
