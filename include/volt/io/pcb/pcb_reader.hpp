#pragma once

#include <istream>
#include <string_view>

#include <volt/circuit/circuit.hpp>
#include <volt/pcb/board.hpp>

namespace volt::io {

/** Read a PCB board projection from text, validating references against the circuit. */
[[nodiscard]] Board read_pcb_board_text(const Circuit &circuit, std::string_view text);

/** Read a PCB board projection from a stream, validating references against the circuit. */
[[nodiscard]] Board read_pcb_board(const Circuit &circuit, std::istream &input);

/**
 * Convert one legacy PCB document carrying the mixed derived viewer cache.
 *
 * The cache is validated against canonical Board state and discarded. Current readers never
 * accept it implicitly, and current writers never emit it.
 */
[[nodiscard]] Board read_legacy_pcb_board_text(const Circuit &circuit, std::string_view text);

/** Stream form of the explicit legacy mixed-cache conversion boundary. */
[[nodiscard]] Board read_legacy_pcb_board(const Circuit &circuit, std::istream &input);

} // namespace volt::io
