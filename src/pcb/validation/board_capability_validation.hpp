#pragma once

#include <volt/core/diagnostics.hpp>
#include <volt/pcb/copper/board_copper.hpp>

namespace volt {

class Board;
class FootprintLibrary;
class ResolvedBoardView;

namespace detail {

[[nodiscard]] std::optional<BoardCapabilityCopperWeightRefinement>
capability_refinement_for_weight(const BoardCapabilityProfile &profile, double weight_oz);

void validate_capability_profile_rules(const ResolvedBoardView &resolved, DiagnosticReport &report);

} // namespace detail
} // namespace volt
