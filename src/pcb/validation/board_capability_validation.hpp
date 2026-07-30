#pragma once

#include <volt/core/diagnostics.hpp>

namespace volt {

class Board;
class FootprintLibrary;
class ResolvedBoardView;

namespace detail {

void validate_capability_profile_rules(const ResolvedBoardView &resolved, DiagnosticReport &report);

} // namespace detail
} // namespace volt
