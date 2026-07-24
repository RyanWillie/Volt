#pragma once

#include <optional>
#include <string>

#include <volt/io/pcb/pcb_svg_writer.hpp>
#include <volt/pcb/compiled/board_consumers.hpp>

namespace volt::io {

/** Deterministic PCB SVG bytes tied to one exact CompiledBoard revision. */
class CompiledBoardSvg {
  public:
    /** Bind deterministic full or layer-filtered SVG bytes to one exact source revision. */
    CompiledBoardSvg(CompiledBoardIdentity source, std::optional<BoardLayerId> layer,
                     std::string bytes);

    /** Return the exact immutable physical revision rendered by this result. */
    [[nodiscard]] const CompiledBoardIdentity &source() const noexcept { return source_; }

    /** Return the selected layer, or no value for the complete Board rendering. */
    [[nodiscard]] const std::optional<BoardLayerId> &layer() const noexcept { return layer_; }

    /** Return deterministic SVG bytes for the exact source revision. */
    [[nodiscard]] const std::string &bytes() const noexcept { return bytes_; }

  private:
    CompiledBoardIdentity source_;
    std::optional<BoardLayerId> layer_;
    std::string bytes_;
};

/** Render one immutable physical revision without consulting authoring or resolver state. */
[[nodiscard]] CompiledBoardSvg render_pcb_svg(const CompiledBoard &compiled,
                                              PcbPlacementSvgOptions options = {});

} // namespace volt::io
