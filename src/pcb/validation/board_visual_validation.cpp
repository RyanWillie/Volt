#include <volt/pcb/board.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace volt::detail {
namespace {

inline constexpr double text_width_factor = 0.6;

struct PlacementVisualExtent {
    ComponentPlacementId placement;
    ComponentId component;
    BoardSide side;
    DiagnosticPoint min;
    DiagnosticPoint max;
};

struct TextVisualExtent {
    BoardTextId text;
    BoardLayerId layer;
    BoardLayerSide side;
    std::string value;
    DiagnosticPoint min;
    DiagnosticPoint max;
};

[[nodiscard]] DiagnosticPoint min_point(DiagnosticPoint lhs, BoardPoint rhs) {
    return DiagnosticPoint{std::min(lhs.x_mm, rhs.x_mm()), std::min(lhs.y_mm, rhs.y_mm())};
}

[[nodiscard]] DiagnosticPoint max_point(DiagnosticPoint lhs, BoardPoint rhs) {
    return DiagnosticPoint{std::max(lhs.x_mm, rhs.x_mm()), std::max(lhs.y_mm, rhs.y_mm())};
}

[[nodiscard]] std::optional<PlacementVisualExtent>
placement_visual_extent(const Board &board, const FootprintLibrary &footprints,
                        ComponentPlacementId placement_id) {
    const auto &placement = board.placement(placement_id);
    const auto &selected_part = board.circuit().selected_physical_part(placement.component());
    if (!selected_part.has_value()) {
        return std::nullopt;
    }

    const auto footprint_resolution = resolve_footprint(selected_part.value(), footprints);
    const auto *definition = footprint_resolution.definition();
    if (definition == nullptr || definition->pad_count() == 0U) {
        return std::nullopt;
    }

    auto min = std::optional<DiagnosticPoint>{};
    auto max = std::optional<DiagnosticPoint>{};
    for (std::size_t pad_index = 0; pad_index < definition->pad_count(); ++pad_index) {
        for (const auto corner :
             transformed_pad_body_corners(placement, definition->pad(FootprintPadId{pad_index}))) {
            if (!min.has_value()) {
                min = DiagnosticPoint{corner.x_mm(), corner.y_mm()};
                max = min;
                continue;
            }
            min = min_point(min.value(), corner);
            max = max_point(max.value(), corner);
        }
    }

    return PlacementVisualExtent{placement_id, placement.component(), placement.side(), min.value(),
                                 max.value()};
}

[[nodiscard]] BoardPoint transform_text_point(const BoardText &text, double local_x,
                                              double local_y) {
    constexpr double pi = 3.14159265358979323846264338327950288;
    const auto radians = text.rotation().degrees() * pi / 180.0;
    const auto rotated_x = (std::cos(radians) * local_x) - (std::sin(radians) * local_y);
    const auto rotated_y = (std::sin(radians) * local_x) + (std::cos(radians) * local_y);
    return BoardPoint{text.position().x_mm() + rotated_x, text.position().y_mm() + rotated_y};
}

[[nodiscard]] std::vector<BoardPoint> text_box_corners(const BoardText &text) {
    const auto width = static_cast<double>(text.text().size()) * text.size_mm() * text_width_factor;
    const auto height = text.size_mm();
    return std::vector{
        transform_text_point(text, 0.0, -height),
        transform_text_point(text, width, -height),
        transform_text_point(text, width, 0.0),
        transform_text_point(text, 0.0, 0.0),
    };
}

[[nodiscard]] std::pair<DiagnosticPoint, DiagnosticPoint>
box_bounds(const std::vector<BoardPoint> &corners) {
    auto min = DiagnosticPoint{corners.front().x_mm(), corners.front().y_mm()};
    auto max = min;
    for (const auto corner : corners) {
        min = min_point(min, corner);
        max = max_point(max, corner);
    }
    return {min, max};
}

[[nodiscard]] TextVisualExtent text_visual_extent(const Board &board, BoardTextId text_id) {
    const auto &text = board.text(text_id);
    const auto corners = text_box_corners(text);
    const auto [min, max] = box_bounds(corners);
    return TextVisualExtent{text_id,     text.layer(), board.layer(text.layer()).side(),
                            text.text(), min,          max};
}

[[nodiscard]] bool extents_overlap(const PlacementVisualExtent &lhs,
                                   const PlacementVisualExtent &rhs) noexcept {
    if (lhs.side != rhs.side) {
        return false;
    }
    return lhs.min.x_mm < rhs.max.x_mm && rhs.min.x_mm < lhs.max.x_mm &&
           lhs.min.y_mm < rhs.max.y_mm && rhs.min.y_mm < lhs.max.y_mm;
}

[[nodiscard]] bool text_extents_overlap(const TextVisualExtent &lhs,
                                        const TextVisualExtent &rhs) noexcept {
    if (lhs.side != rhs.side) {
        return false;
    }
    return lhs.min.x_mm < rhs.max.x_mm && rhs.min.x_mm < lhs.max.x_mm &&
           lhs.min.y_mm < rhs.max.y_mm && rhs.min.y_mm < lhs.max.y_mm;
}

[[nodiscard]] std::vector<EntityRef> placement_entities(const PlacementVisualExtent &extent) {
    return std::vector{EntityRef::component(extent.component),
                       EntityRef::component_placement(extent.placement)};
}

[[nodiscard]] std::vector<BoardLayerId> placement_overlay_layers(const Board &board,
                                                                 BoardSide side) {
    if (board.layer_stack().has_value()) {
        for (const auto layer_id : board.layer_stack()->layers()) {
            const auto layer_side = board.layer(layer_id).side();
            if ((side == BoardSide::Top && layer_side == BoardLayerSide::Top) ||
                (side == BoardSide::Bottom && layer_side == BoardLayerSide::Bottom)) {
                return std::vector{layer_id};
            }
        }
    }

    for (std::size_t index = 0; index < board.layer_count(); ++index) {
        const auto layer_id = BoardLayerId{index};
        const auto layer_side = board.layer(layer_id).side();
        if ((side == BoardSide::Top && layer_side == BoardLayerSide::Top) ||
            (side == BoardSide::Bottom && layer_side == BoardLayerSide::Bottom)) {
            return std::vector{layer_id};
        }
    }

    return {};
}

[[nodiscard]] std::vector<EntityRef> text_entities(const TextVisualExtent &extent) {
    return std::vector{EntityRef::board_text(extent.text)};
}

[[nodiscard]] DiagnosticOverlay text_overlay(const TextVisualExtent &extent) {
    return DiagnosticOverlay::bounding_box(extent.min, extent.max, text_entities(extent),
                                           std::vector{extent.layer});
}

[[nodiscard]] Diagnostic placement_overlap_diagnostic(const Board &board,
                                                      const PlacementVisualExtent &lhs,
                                                      const PlacementVisualExtent &rhs) {
    auto entities = placement_entities(lhs);
    const auto rhs_entities = placement_entities(rhs);
    entities.insert(entities.end(), rhs_entities.begin(), rhs_entities.end());

    const auto lhs_label = board.circuit().component(lhs.component).reference().value();
    const auto rhs_label = board.circuit().component(rhs.component).reference().value();

    return Diagnostic{
        Severity::Warning,
        DiagnosticCode{std::string{pcb_visual_diagnostic_codes::PlacementOverlap}},
        DiagnosticCategory{diagnostic_categories::PcbVisual},
        "Placed footprint extents for " + lhs_label + " and " + rhs_label + " overlap",
        std::move(entities),
        std::vector{
            DiagnosticOverlay::bounding_box(lhs.min, lhs.max, placement_entities(lhs),
                                            placement_overlay_layers(board, lhs.side)),
            DiagnosticOverlay::bounding_box(rhs.min, rhs.max, placement_entities(rhs),
                                            placement_overlay_layers(board, rhs.side)),
        }};
}

[[nodiscard]] Diagnostic text_overlap_diagnostic(const TextVisualExtent &lhs,
                                                 const TextVisualExtent &rhs) {
    auto entities = text_entities(lhs);
    const auto rhs_entities = text_entities(rhs);
    entities.insert(entities.end(), rhs_entities.begin(), rhs_entities.end());
    return Diagnostic{Severity::Warning,
                      DiagnosticCode{std::string{pcb_visual_diagnostic_codes::LabelOverlap}},
                      DiagnosticCategory{diagnostic_categories::PcbVisual},
                      "Board text '" + lhs.value + "' overlaps board text '" + rhs.value + "'",
                      std::move(entities),
                      std::vector{text_overlay(lhs), text_overlay(rhs)}};
}

[[nodiscard]] Diagnostic text_outside_board_diagnostic(const TextVisualExtent &extent) {
    return Diagnostic{Severity::Warning,
                      DiagnosticCode{std::string{pcb_visual_diagnostic_codes::LabelOutsideBoard}},
                      DiagnosticCategory{diagnostic_categories::PcbVisual},
                      "Board text '" + extent.value + "' is outside the board outline",
                      text_entities(extent),
                      std::vector{text_overlay(extent)}};
}

[[nodiscard]] std::vector<PlacementVisualExtent>
collect_placement_visual_extents(const Board &board, const FootprintLibrary &footprints) {
    auto extents = std::vector<PlacementVisualExtent>{};
    extents.reserve(board.placement_count());
    for (std::size_t index = 0; index < board.placement_count(); ++index) {
        const auto extent = placement_visual_extent(board, footprints, ComponentPlacementId{index});
        if (extent.has_value()) {
            extents.push_back(extent.value());
        }
    }
    return extents;
}

[[nodiscard]] std::vector<TextVisualExtent> collect_text_visual_extents(const Board &board) {
    auto extents = std::vector<TextVisualExtent>{};
    extents.reserve(board.text_count());
    for (std::size_t index = 0; index < board.text_count(); ++index) {
        extents.push_back(text_visual_extent(board, BoardTextId{index}));
    }
    return extents;
}

[[nodiscard]] bool text_exits_outline(const Board &board, const TextVisualExtent &extent) {
    if (!board.outline().has_value()) {
        return false;
    }
    return !outline_contains_polygon(*board.outline(), text_box_corners(board.text(extent.text)),
                                     0.0);
}

} // namespace

void validate_board_visual(const Board &board, const FootprintLibrary &footprints,
                           DiagnosticReport &report) {
    const auto extents = collect_placement_visual_extents(board, footprints);
    const auto texts = collect_text_visual_extents(board);
    for (std::size_t lhs_index = 0; lhs_index < extents.size(); ++lhs_index) {
        for (std::size_t rhs_index = lhs_index + 1U; rhs_index < extents.size(); ++rhs_index) {
            if (extents_overlap(extents[lhs_index], extents[rhs_index])) {
                report.add(
                    placement_overlap_diagnostic(board, extents[lhs_index], extents[rhs_index]));
            }
        }
    }

    for (const auto &text : texts) {
        if (text_exits_outline(board, text)) {
            report.add(text_outside_board_diagnostic(text));
        }
    }

    for (std::size_t lhs_index = 0; lhs_index < texts.size(); ++lhs_index) {
        for (std::size_t rhs_index = lhs_index + 1U; rhs_index < texts.size(); ++rhs_index) {
            if (text_extents_overlap(texts[lhs_index], texts[rhs_index])) {
                report.add(text_overlap_diagnostic(texts[lhs_index], texts[rhs_index]));
            }
        }
    }
}

} // namespace volt::detail
