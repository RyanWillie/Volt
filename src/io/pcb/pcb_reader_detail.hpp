#pragma once

#include <string_view>

#include <volt/circuit/circuit.hpp>
#include <volt/pcb/board.hpp>

namespace volt::io::detail {

/**
 * Decode a canonical v1 Board document when its owning v1 logical payload retained only an exact
 * LibraryPartRef, not the physical materialization needed for the current cross-check.
 *
 * This uses the one native PCB parser and preserves the embedded Board-owned footprint snapshot.
 * It does not resolve, infer, or materialize a selected part.
 */
[[nodiscard]] Board read_project_bundle_v1_pcb_board_text(const Circuit &circuit,
                                                          std::string_view text);

} // namespace volt::io::detail
