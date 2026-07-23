#pragma once

#include <string>
#include <string_view>

#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/pcb/compiled/compiled_board.hpp>

namespace volt {

/** Validating IO owner for the standalone CompiledBoard archive format. */
class CompiledBoard::Codec final {
  public:
    /** Fail-closed reopen of one standalone immutable CompiledBoard archive. */
    [[nodiscard]] static CompiledBoard open(std::string_view bytes);
};

} // namespace volt

namespace volt::io {

/** Return the standalone CompiledBoard archive format name. */
[[nodiscard]] inline constexpr std::string_view compiled_board_format_name() noexcept {
    return "volt.compiled-board";
}

/** Compile one Circuit and one named Board through the exact selected P6 closure. */
[[nodiscard]] CompiledBoardCompileResult compile_board(const Circuit &circuit, const Board &board,
                                                       const PartLibraryBundle &selected_closure,
                                                       CompiledBoardCapabilities capabilities);

/** Return the exact canonical bytes owned by one immutable CompiledBoard. */
[[nodiscard]] std::string write_compiled_board(const CompiledBoard &compiled);

/** Fail-closed reopen of one standalone immutable CompiledBoard archive. */
[[nodiscard]] CompiledBoard open_compiled_board(std::string_view bytes);

} // namespace volt::io
