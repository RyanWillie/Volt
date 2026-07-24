#include <volt/io/pcb/compiled_board_consumers.hpp>

#include <utility>

namespace volt::io {

CompiledBoardSvg::CompiledBoardSvg(CompiledBoardIdentity source, std::optional<BoardLayerId> layer,
                                   std::string bytes)
    : source_{std::move(source)}, layer_{layer}, bytes_{std::move(bytes)} {}

CompiledBoardSvg render_pcb_svg(const CompiledBoard &compiled, PcbPlacementSvgOptions options) {
    return CompiledBoardSvg{
        compiled.identity(),
        options.layer_filter,
        write_pcb_placement_svg(compiled.board(), compiled.footprints(), options),
    };
}

} // namespace volt::io
