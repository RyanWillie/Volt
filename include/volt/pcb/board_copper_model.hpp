#pragma once

#include <cstddef>

#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/pcb/board_copper.hpp>

namespace volt {

class BoardCopperModel {
  public:
    [[nodiscard]] BoardTrackId add_track(BoardTrack track);

    [[nodiscard]] BoardViaId add_via(BoardVia via);

    [[nodiscard]] BoardZoneId add_zone(BoardZone zone);

    [[nodiscard]] BoardKeepoutId add_keepout(BoardKeepout keepout);

    [[nodiscard]] BoardTextId add_text(BoardText text);

    [[nodiscard]] const BoardTrack &track(BoardTrackId id) const;

    [[nodiscard]] std::size_t track_count() const noexcept;

    [[nodiscard]] const BoardVia &via(BoardViaId id) const;

    [[nodiscard]] std::size_t via_count() const noexcept;

    [[nodiscard]] const BoardZone &zone(BoardZoneId id) const;

    [[nodiscard]] std::size_t zone_count() const noexcept;

    [[nodiscard]] const BoardKeepout &keepout(BoardKeepoutId id) const;

    [[nodiscard]] std::size_t keepout_count() const noexcept;

    [[nodiscard]] const BoardText &text(BoardTextId id) const;

    [[nodiscard]] std::size_t text_count() const noexcept;

  private:
    EntityTable<BoardTrack, BoardTrackId> tracks_;
    EntityTable<BoardVia, BoardViaId> vias_;
    EntityTable<BoardZone, BoardZoneId> zones_;
    EntityTable<BoardKeepout, BoardKeepoutId> keepouts_;
    EntityTable<BoardText, BoardTextId> texts_;
};

} // namespace volt
