#include <volt/pcb/projection/footprint_visual_projection.hpp>

#include <algorithm>
#include <cstddef>

#include <volt/pcb/features/board_features.hpp>

namespace volt::detail {
namespace {

void include_footprint_point(FootprintVisualBounds &bounds, FootprintPoint point) {
    bounds.min_x = std::min(bounds.min_x, point.x_mm());
    bounds.min_y = std::min(bounds.min_y, point.y_mm());
    bounds.max_x = std::max(bounds.max_x, point.x_mm());
    bounds.max_y = std::max(bounds.max_y, point.y_mm());
}

void include_footprint_polygon(FootprintVisualBounds &bounds, const FootprintPolygon &polygon) {
    for (const auto point : polygon.vertices()) {
        include_footprint_point(bounds, point);
    }
}

} // namespace

[[nodiscard]] bool footprint_has_declared_visual_geometry(const FootprintDefinition &definition) {
    return definition.body().has_value() || definition.courtyard().has_value() ||
           definition.fabrication_outline().has_value() ||
           definition.assembly_outline().has_value() || !definition.markings().empty();
}

[[nodiscard]] FootprintVisualBounds footprint_pad_bounds(const FootprintDefinition &definition) {
    const auto &first_pad = definition.pad(FootprintPadId{0});
    const auto first_half_width = first_pad.size().width_mm() / 2.0;
    const auto first_half_height = first_pad.size().height_mm() / 2.0;
    auto bounds = FootprintVisualBounds{
        first_pad.position().x_mm() - first_half_width,
        first_pad.position().y_mm() - first_half_height,
        first_pad.position().x_mm() + first_half_width,
        first_pad.position().y_mm() + first_half_height,
    };

    for (std::size_t index = 1; index < definition.pad_count(); ++index) {
        const auto &pad = definition.pad(FootprintPadId{index});
        const auto half_width = pad.size().width_mm() / 2.0;
        const auto half_height = pad.size().height_mm() / 2.0;
        include_footprint_point(bounds, FootprintPoint{pad.position().x_mm() - half_width,
                                                       pad.position().y_mm() - half_height});
        include_footprint_point(bounds, FootprintPoint{pad.position().x_mm() + half_width,
                                                       pad.position().y_mm() + half_height});
    }
    return bounds;
}

[[nodiscard]] FootprintVisualBounds
synthetic_footprint_envelope(const FootprintDefinition &definition) {
    const auto bounds = footprint_pad_bounds(definition);
    return FootprintVisualBounds{
        bounds.min_x - 0.5,
        bounds.min_y - 0.5,
        bounds.max_x + 0.5,
        bounds.max_y + 0.5,
    };
}

[[nodiscard]] FootprintVisualBounds footprint_visual_bounds(const FootprintDefinition &definition) {
    if (!footprint_has_declared_visual_geometry(definition)) {
        return synthetic_footprint_envelope(definition);
    }

    auto bounds = footprint_pad_bounds(definition);
    if (definition.courtyard().has_value()) {
        include_footprint_polygon(bounds, definition.courtyard().value());
    }
    if (definition.body().has_value()) {
        include_footprint_polygon(bounds, definition.body().value());
    }
    if (definition.fabrication_outline().has_value()) {
        include_footprint_polygon(bounds, definition.fabrication_outline().value());
    }
    if (definition.assembly_outline().has_value()) {
        include_footprint_polygon(bounds, definition.assembly_outline().value());
    }
    for (const auto &marking : definition.markings()) {
        include_footprint_polygon(bounds, marking.polygon());
    }
    return bounds;
}

[[nodiscard]] BoardPoint
default_reference_designator_anchor(const ComponentPlacement &placement,
                                    const FootprintDefinition &definition) {
    const auto bounds = footprint_visual_bounds(definition);
    return transform_footprint_point(placement, FootprintPoint{0.0, bounds.min_y - 1.0});
}

[[nodiscard]] std::vector<BoardPoint>
default_reference_designator_corners(const ComponentPlacement &placement,
                                     const FootprintDefinition &definition,
                                     std::string_view value) {
    const auto anchor = default_reference_designator_anchor(placement, definition);
    const auto width = static_cast<double>(value.size()) * default_reference_designator_size_mm *
                       board_text_width_factor;
    return std::vector{
        BoardPoint{anchor.x_mm() - (width / 2.0),
                   anchor.y_mm() - default_reference_designator_size_mm},
        BoardPoint{anchor.x_mm() + (width / 2.0),
                   anchor.y_mm() - default_reference_designator_size_mm},
        BoardPoint{anchor.x_mm() + (width / 2.0), anchor.y_mm()},
        BoardPoint{anchor.x_mm() - (width / 2.0), anchor.y_mm()},
    };
}

} // namespace volt::detail
