#pragma once

#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/pcb/resolution/board_resolution.hpp>

namespace volt {

/** Validating IO owner for exact BoardResolution publication. */
class BoardResolution::Codec final {
  public:
    /** Resolve one named Board against exactly one selected native library closure. */
    [[nodiscard]] static BoardResolution resolve(const Board &board,
                                                 const io::PartLibraryBundle &selected_closure,
                                                 BoardResolutionCapabilities capabilities);

    [[nodiscard]] static BoardResolution resolve(Board &&board,
                                                 const io::PartLibraryBundle &selected_closure,
                                                 BoardResolutionCapabilities capabilities) = delete;
    [[nodiscard]] static BoardResolution resolve(const Board &&board,
                                                 const io::PartLibraryBundle &selected_closure,
                                                 BoardResolutionCapabilities capabilities) = delete;
};

} // namespace volt

namespace volt::io {

/** Atomically resolve one named Board against exactly one selected P6 library closure. */
[[nodiscard]] BoardResolution resolve_board(const Board &board,
                                            const PartLibraryBundle &selected_closure,
                                            BoardResolutionCapabilities capabilities);
[[nodiscard]] BoardResolution resolve_board(Board &&board,
                                            const PartLibraryBundle &selected_closure,
                                            BoardResolutionCapabilities capabilities) = delete;
[[nodiscard]] BoardResolution resolve_board(const Board &&board,
                                            const PartLibraryBundle &selected_closure,
                                            BoardResolutionCapabilities capabilities) = delete;

} // namespace volt::io
