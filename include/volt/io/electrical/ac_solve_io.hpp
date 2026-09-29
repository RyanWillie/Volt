#pragma once
#include <string>
#include <volt/electrical/ac_solve.hpp>

namespace volt::io {
/** Encode finite successful observations with exact model and numerical provenance. */
[[nodiscard]] std::string write_ac_solution(const AcSolution &);
/** Encode per-frequency outcomes, metrics and diagnostics, including failed sweeps. */
[[nodiscard]] std::string write_ac_solve_report(const AcSolveReport &);
} // namespace volt::io
