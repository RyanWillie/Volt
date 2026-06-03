#include <volt/pcb/board_copper_model.hpp>

#include <cstddef>
#include <utility>

namespace volt {

[[nodiscard]] BoardTrackId BoardCopperModel::add_track(BoardTrack track) {
    return tracks_.insert(std::move(track));
}
[[nodiscard]] BoardViaId BoardCopperModel::add_via(BoardVia via) { return vias_.insert(via); }
[[nodiscard]] BoardZoneId BoardCopperModel::add_zone(BoardZone zone) {
    return zones_.insert(std::move(zone));
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

} // namespace volt
