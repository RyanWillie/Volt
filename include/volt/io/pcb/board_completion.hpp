#pragma once

#include <string>
#include <volt/pcb/compiled/board_completion.hpp>

namespace volt::io {
/** Write all native completion payloads deterministically; reopen source and recompute. */
[[nodiscard]] std::string write_board_completion(const BoardCompletion &completion);
} // namespace volt::io
