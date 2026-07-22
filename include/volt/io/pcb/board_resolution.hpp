#pragma once

#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/pcb/resolution/board_resolution.hpp>

namespace volt::io {

/** Atomically resolve one named Board against exactly one selected P6 library closure. */
[[nodiscard]] BoardResolution resolve_board(const Board &board,
                                            const PartLibraryBundle &selected_closure,
                                            BoardResolutionCapabilities capabilities);

} // namespace volt::io
