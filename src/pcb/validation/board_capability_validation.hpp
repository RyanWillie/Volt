#pragma once

#include <volt/core/diagnostics.hpp>

namespace volt {

class Board;
class FootprintLibrary;

namespace detail {

void validate_capability_profile_rules(const Board &board, const FootprintLibrary &footprints,
                                       DiagnosticReport &report);

} // namespace detail
} // namespace volt
