#pragma once

#include <volt/pcb/board.hpp>

namespace volt::detail {

void validate_footprint_geometry_drc(const ResolvedBoardView &resolved, DiagnosticReport &report);

} // namespace volt::detail
