#include <volt/pcb/copper/board_copper_model.hpp>

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace volt {

[[nodiscard]] BoardTrackId BoardCopperModel::add_track(detail::KernelMutationAccess,
                                                       BoardTrack track) {
    return tracks_.insert(std::move(track));
}

[[nodiscard]] BoardViaId BoardCopperModel::add_via(detail::KernelMutationAccess, BoardVia via) {
    return vias_.insert(via);
}

[[nodiscard]] BoardZoneId BoardCopperModel::add_zone(detail::KernelMutationAccess, BoardZone zone) {
    return zones_.insert(std::move(zone));
}

[[nodiscard]] BoardKeepoutId BoardCopperModel::add_keepout(detail::KernelMutationAccess,
                                                           BoardKeepout keepout) {
    return keepouts_.insert(std::move(keepout));
}

[[nodiscard]] BoardRoomId BoardCopperModel::add_room(detail::KernelMutationAccess, BoardRoom room) {
    for (std::size_t index = 0; index < rooms_.size(); ++index) {
        if (rooms_.get(BoardRoomId{index}).name() == room.name()) {
            throw std::logic_error{"Board room name already exists"};
        }
    }
    return rooms_.insert(std::move(room));
}

[[nodiscard]] BoardTextId BoardCopperModel::add_text(detail::KernelMutationAccess, BoardText text) {
    return texts_.insert(std::move(text));
}

[[nodiscard]] const BoardTrack &BoardCopperModel::track(BoardTrackId id) const {
    return tracks_.get(id);
}

[[nodiscard]] std::size_t BoardCopperModel::track_count() const noexcept { return tracks_.size(); }

[[nodiscard]] const BoardVia &BoardCopperModel::via(BoardViaId id) const { return vias_.get(id); }

[[nodiscard]] std::size_t BoardCopperModel::via_count() const noexcept { return vias_.size(); }

[[nodiscard]] const BoardZone &BoardCopperModel::zone(BoardZoneId id) const {
    return zones_.get(id);
}

[[nodiscard]] std::size_t BoardCopperModel::zone_count() const noexcept { return zones_.size(); }

[[nodiscard]] const BoardKeepout &BoardCopperModel::keepout(BoardKeepoutId id) const {
    return keepouts_.get(id);
}

[[nodiscard]] std::size_t BoardCopperModel::keepout_count() const noexcept {
    return keepouts_.size();
}

[[nodiscard]] const BoardRoom &BoardCopperModel::room(BoardRoomId id) const {
    return rooms_.get(id);
}

[[nodiscard]] std::size_t BoardCopperModel::room_count() const noexcept { return rooms_.size(); }

[[nodiscard]] const BoardText &BoardCopperModel::text(BoardTextId id) const {
    return texts_.get(id);
}

[[nodiscard]] std::size_t BoardCopperModel::text_count() const noexcept { return texts_.size(); }

} // namespace volt
