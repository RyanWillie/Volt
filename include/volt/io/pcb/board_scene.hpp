#pragma once

#include <string>
#include <string_view>

#include <volt/pcb/compiled/board_consumers.hpp>

namespace volt::io {

/** Return the owner-canonical bytes for one exact CompiledBoard-derived BoardScene. */
[[nodiscard]] std::string write_board_scene(const BoardScene &scene);

/** Recompute and verify one BoardScene against its exact immutable CompiledBoard owner. */
[[nodiscard]] BoardScene read_board_scene(std::string_view bytes, const CompiledBoard &compiled);

} // namespace volt::io
