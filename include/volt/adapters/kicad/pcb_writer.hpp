#pragma once

#include <string>

#include <volt/adapters/kicad/loss_report.hpp>
#include <volt/pcb/compiled/compiled_board.hpp>

namespace volt::adapters::kicad {

/** Result of exporting Volt board projection data to a KiCad PCB document. */
struct BoardExportResult {
    /** Exact immutable physical revision consumed by this export. */
    CompiledBoardIdentity source;

    /** Deterministic `.kicad_pcb` text for the supported board subset. */
    std::string text;

    /** Structured warnings for constructs intentionally omitted by the first board subset. */
    LossReport loss_report;
};

/** Write a KiCad PCB handoff from one exact immutable physical revision. */
[[nodiscard]] BoardExportResult write_board(const CompiledBoard &compiled);

} // namespace volt::adapters::kicad
