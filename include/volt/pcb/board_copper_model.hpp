#pragma once

#include <cstddef>

#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/pcb/board_copper.hpp>

namespace volt {

/** Owns routed and presentation copper primitives for a board projection. */
class BoardCopperModel {
  public:
    /** Add a routed copper track and return its stable board-local ID. */
    [[nodiscard]] BoardTrackId add_track(BoardTrack track);

    /** Add a routed copper via and return its stable board-local ID. */
    [[nodiscard]] BoardViaId add_via(BoardVia via);

    /** Add a copper zone and return its stable board-local ID. */
    [[nodiscard]] BoardZoneId add_zone(BoardZone zone);

    /** Add a copper keepout and return its stable board-local ID. */
    [[nodiscard]] BoardKeepoutId add_keepout(BoardKeepout keepout);

    /** Add board text and return its stable board-local ID. */
    [[nodiscard]] BoardTextId add_text(BoardText text);

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

    /** Return a keepout by board-local ID. */
    [[nodiscard]] const BoardKeepout &keepout(BoardKeepoutId id) const;

    /** Return the number of keepouts owned by this model. */
    [[nodiscard]] std::size_t keepout_count() const noexcept;

    /** Return board text by board-local ID. */
    [[nodiscard]] const BoardText &text(BoardTextId id) const;

    /** Return the number of board text items owned by this model. */
    [[nodiscard]] std::size_t text_count() const noexcept;

  private:
    EntityTable<BoardTrack, BoardTrackId> tracks_;
    EntityTable<BoardVia, BoardViaId> vias_;
    EntityTable<BoardZone, BoardZoneId> zones_;
    EntityTable<BoardKeepout, BoardKeepoutId> keepouts_;
    EntityTable<BoardText, BoardTextId> texts_;
};

} // namespace volt
