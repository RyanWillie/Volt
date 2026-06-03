#pragma once

#include <cstddef>

#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/pcb/board_copper.hpp>

namespace volt {

class Board;

/** Owns routed and presentation copper primitives for a board projection. */
class BoardCopperModel {
  public:
    /** Return a routed track by board-local ID. */
    [[nodiscard]] const BoardTrack &track(BoardTrackId id) const;

    /** Return the number of routed tracks owned by this model. */
    [[nodiscard]] std::size_t track_count() const noexcept;

    /** Return a routed via by board-local ID. */
    [[nodiscard]] const BoardVia &via(BoardViaId id) const;

    /** Return the number of routed vias owned by this model. */
    [[nodiscard]] std::size_t via_count() const noexcept;

    /** Return a copper zone by board-local ID. */
    [[nodiscard]] const BoardZone &zone(BoardZoneId id) const;

    /** Return the number of copper zones owned by this model. */
    [[nodiscard]] std::size_t zone_count() const noexcept;

  private:
    friend class Board;

    /** Add a routed copper track and return its stable board-local ID. */
    [[nodiscard]] BoardTrackId add_track(BoardTrack track);

    /** Add a routed copper via and return its stable board-local ID. */
    [[nodiscard]] BoardViaId add_via(BoardVia via);

    /** Add a copper zone and return its stable board-local ID. */
    [[nodiscard]] BoardZoneId add_zone(BoardZone zone);

    EntityTable<BoardTrack, BoardTrackId> tracks_;
    EntityTable<BoardVia, BoardViaId> vias_;
    EntityTable<BoardZone, BoardZoneId> zones_;
};

} // namespace volt
