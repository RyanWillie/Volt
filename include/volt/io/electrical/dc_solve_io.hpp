#pragma once

#include <string>

#include <volt/electrical/dc_solve.hpp>

namespace volt::io {

/** Write an owner-local inspection snapshot with effective numerical policy and available metrics.
 */
[[nodiscard]] std::string write_dc_solve_report(const DcSolveReport &report);

/** Write immutable successful observations and their complete model/request provenance. */
[[nodiscard]] std::string write_dc_solution(const DcSolution &solution);

} // namespace volt::io
