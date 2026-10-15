#pragma once
#include <volt/pcb/compiled/board_completion.hpp>

namespace volt::io::detail {
/** Check actual native writer coordinate/size representation, including fabrication transform. */
[[nodiscard]] DiagnosticReport
completion_export_grid(const CompiledBoard &compiled,
                       std::span<const BoardCompletionCopper> copper);
} // namespace volt::io::detail
