#pragma once

#include <volt/core/diagnostics.hpp>

namespace volt {

class Board;

namespace detail {

void validate_capability_profile_rules(const Board &board, DiagnosticReport &report);

} // namespace detail
} // namespace volt
