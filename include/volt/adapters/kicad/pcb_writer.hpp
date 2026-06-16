#pragma once

#include <string>

#include <volt/adapters/kicad/loss_report.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints/footprints.hpp>

namespace volt::adapters::kicad {

/** Result of exporting Volt board projection data to a KiCad PCB document. */
struct BoardExportResult {
    /** Deterministic `.kicad_pcb` text for the supported board subset. */
    std::string text;

    /** Structured warnings for constructs intentionally omitted by the first board subset. */
    LossReport loss_report;
};

/** Write a KiCad PCB handoff from Volt-owned logical and board data. */
[[nodiscard]] BoardExportResult write_board(const Board &board, const FootprintLibrary &footprints);

} // namespace volt::adapters::kicad
