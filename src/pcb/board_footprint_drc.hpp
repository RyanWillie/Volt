#pragma once

#include <volt/pcb/board.hpp>

namespace volt::detail {

void validate_footprint_geometry_drc(const Board &board, const FootprintLibrary &footprints,
                                     DiagnosticReport &report);

} // namespace volt::detail
