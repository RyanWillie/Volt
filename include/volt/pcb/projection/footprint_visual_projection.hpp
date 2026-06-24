#pragma once

#include <string_view>
#include <vector>

#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints/footprints.hpp>

namespace volt::detail {

inline constexpr double board_text_width_factor = 0.6;
inline constexpr double default_reference_designator_size_mm = 1.8;

/** Local footprint bounds used for visual placement and preview sizing. */
struct FootprintVisualBounds {
    double min_x;
    double min_y;
    double max_x;
    double max_y;
};

[[nodiscard]] bool footprint_has_declared_visual_geometry(const FootprintDefinition &definition);

[[nodiscard]] FootprintVisualBounds footprint_pad_bounds(const FootprintDefinition &definition);

[[nodiscard]] FootprintVisualBounds
synthetic_footprint_envelope(const FootprintDefinition &definition);

[[nodiscard]] FootprintVisualBounds footprint_visual_bounds(const FootprintDefinition &definition);

[[nodiscard]] BoardPoint default_reference_designator_anchor(const ComponentPlacement &placement,
                                                             const FootprintDefinition &definition);

[[nodiscard]] std::vector<BoardPoint>
default_reference_designator_corners(const ComponentPlacement &placement,
                                     const FootprintDefinition &definition, std::string_view value);

} // namespace volt::detail
