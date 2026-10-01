#pragma once
#include <string>
#include <volt/electrical/transient_solve.hpp>

namespace volt::io {
/** Write whole successful observations with their exact native input and numerical policy. */
[[nodiscard]] std::string write_transient_solution(const TransientSolution &solution);
/** Write timed algebra/error/work evidence, including a failed attempt without partial solution. */
[[nodiscard]] std::string write_transient_solve_report(const TransientSolveReport &report);
} // namespace volt::io
