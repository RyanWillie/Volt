#include <volt/pcb/board.hpp>

#include <volt/core/errors.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/pcb/projection/footprint_visual_projection.hpp>

namespace volt::detail {
namespace {

[[nodiscard]] std::string pcb_reference_label(const Circuit &circuit, ComponentId component_id) {
    const auto &component = circuit.get(component_id);
    const auto key = PropertyKey{"pcb_reference"};
    if (component.properties().contains(key)) {
        const auto &value = component.properties().get(key);
        if (value.kind() == PropertyValueKind::String) {
            return value.as_string();
        }
    }
    return component.reference().value();
}

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
    std::vector<BoardPoint> corners;
    DiagnosticPoint min;
    DiagnosticPoint max;
};

struct PadVisualGeometry {
    ComponentPlacementId placement;
    ComponentId component;
    FootprintPadId pad;
    BoardSide side;
    std::vector<BoardLayerId> layers;
    std::vector<BoardPoint> outline;
};

struct ReferenceDesignatorVisualExtent {
    ComponentPlacementId placement;
    ComponentId component;
    BoardSide side;
    std::string value;
    std::vector<BoardPoint> corners;
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
    const auto &selected_part =
        volt::queries::selected_physical_part(board.circuit(), placement.component());
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
    const auto width =
        static_cast<double>(text.text().size()) * text.size_mm() * board_text_width_factor;
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
    return TextVisualExtent{
        text_id, text.layer(), board.layer(text.layer()).side(), text.text(), corners, min, max};
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

[[nodiscard]] DiagnosticOverlay
reference_label_overlay(const ReferenceDesignatorVisualExtent &extent, const Board &board) {
    return DiagnosticOverlay::bounding_box(
        extent.min, extent.max,
        std::vector{EntityRef::component(extent.component),
                    EntityRef::component_placement(extent.placement)},
        placement_overlay_layers(board, extent.side));
}

[[nodiscard]] DiagnosticPoint to_diagnostic_point(BoardPoint point) {
    return DiagnosticPoint{point.x_mm(), point.y_mm()};
}

[[nodiscard]] DiagnosticOverlay polygon_overlay(std::vector<BoardPoint> points,
                                                std::vector<EntityRef> entities,
                                                std::vector<BoardLayerId> layers) {
    auto diagnostic_points = std::vector<DiagnosticPoint>{};
    diagnostic_points.reserve(points.size());
    for (const auto point : points) {
        diagnostic_points.push_back(to_diagnostic_point(point));
    }
    return DiagnosticOverlay::polygon(std::move(diagnostic_points), std::move(entities),
                                      std::move(layers));
}

[[nodiscard]] bool layer_side_matches_board_side(BoardLayerSide layer_side, BoardSide side) {
    switch (layer_side) {
    case BoardLayerSide::Top:
        return side == BoardSide::Top;
    case BoardLayerSide::Bottom:
        return side == BoardSide::Bottom;
    case BoardLayerSide::Both:
        return true;
    case BoardLayerSide::Inner:
    case BoardLayerSide::None:
        return false;
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled PCB board layer side"};
}

[[nodiscard]] bool text_shares_side(const TextVisualExtent &text, BoardSide side) {
    return layer_side_matches_board_side(text.side, side);
}

[[nodiscard]] bool text_shares_pad_layer(const TextVisualExtent &text,
                                         const PadVisualGeometry &pad) {
    return std::find(pad.layers.begin(), pad.layers.end(), text.layer) != pad.layers.end();
}

[[nodiscard]] bool text_intersects_polygon(const TextVisualExtent &text,
                                           const std::vector<BoardPoint> &polygon) {
    return polygon_polygon_distance(text.corners, polygon) <= board_drc_epsilon;
}

[[nodiscard]] Diagnostic placement_overlap_diagnostic(const Board &board,
                                                      const PlacementVisualExtent &lhs,
                                                      const PlacementVisualExtent &rhs) {
    auto entities = placement_entities(lhs);
    const auto rhs_entities = placement_entities(rhs);
    entities.insert(entities.end(), rhs_entities.begin(), rhs_entities.end());

    const auto lhs_label = board.circuit().get(lhs.component).reference().value();
    const auto rhs_label = board.circuit().get(rhs.component).reference().value();

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

[[nodiscard]] Diagnostic text_pad_obstruction_diagnostic(const TextVisualExtent &text,
                                                         const PadVisualGeometry &pad) {
    return Diagnostic{
        Severity::Warning,
        DiagnosticCode{std::string{pcb_visual_diagnostic_codes::LabelObstruction}},
        DiagnosticCategory{diagnostic_categories::PcbVisual},
        "Board text '" + text.value + "' overlaps placed pad geometry",
        std::vector{EntityRef::board_text(text.text), EntityRef::component_placement(pad.placement),
                    EntityRef::component(pad.component), EntityRef::footprint_pad(pad.pad)},
        std::vector{text_overlay(text),
                    polygon_overlay(pad.outline,
                                    std::vector{EntityRef::component_placement(pad.placement),
                                                EntityRef::footprint_pad(pad.pad)},
                                    pad.layers)},
        std::nullopt,
        "board-text-over-pad"};
}

[[nodiscard]] Diagnostic text_package_obstruction_diagnostic(
    const Board &board, const TextVisualExtent &text, const ProjectedFootprintGeometry &geometry,
    const std::vector<BoardPoint> &polygon, std::string_view label, std::string rule) {
    return Diagnostic{
        Severity::Warning,
        DiagnosticCode{std::string{pcb_visual_diagnostic_codes::LabelObstruction}},
        DiagnosticCategory{diagnostic_categories::PcbVisual},
        "Board text '" + text.value + "' overlaps component " + std::string{label} + " geometry",
        std::vector{EntityRef::board_text(text.text),
                    EntityRef::component_placement(geometry.placement()),
                    EntityRef::component(geometry.component())},
        std::vector{text_overlay(text),
                    polygon_overlay(
                        polygon, std::vector{EntityRef::component_placement(geometry.placement())},
                        placement_overlay_layers(board, geometry.side()))},
        std::nullopt,
        std::move(rule)};
}

[[nodiscard]] Diagnostic text_hole_obstruction_diagnostic(const TextVisualExtent &text,
                                                          BoardFeatureId feature,
                                                          const BoardFeature &hole) {
    return Diagnostic{
        Severity::Warning,
        DiagnosticCode{std::string{pcb_visual_diagnostic_codes::LabelObstruction}},
        DiagnosticCategory{diagnostic_categories::PcbVisual},
        "Board text '" + text.value + "' overlaps board hole geometry",
        std::vector{EntityRef::board_text(text.text), EntityRef::board_feature(feature)},
        std::vector{text_overlay(text),
                    DiagnosticOverlay::point(to_diagnostic_point(hole.hole().center()),
                                             std::vector{EntityRef::board_feature(feature)},
                                             std::vector{text.layer})},
        std::nullopt,
        "board-text-over-hole"};
}

[[nodiscard]] Diagnostic reference_designator_obstruction_diagnostic(
    const Board &board, const ReferenceDesignatorVisualExtent &label,
    const ProjectedFootprintGeometry &geometry, const std::vector<BoardPoint> &polygon) {
    return Diagnostic{
        Severity::Warning,
        DiagnosticCode{std::string{pcb_visual_diagnostic_codes::ReferenceDesignatorHidden}},
        DiagnosticCategory{diagnostic_categories::PcbVisual},
        "Default reference designator '" + label.value + "' overlaps placed package geometry",
        std::vector{EntityRef::component(label.component),
                    EntityRef::component_placement(label.placement),
                    EntityRef::component(geometry.component()),
                    EntityRef::component_placement(geometry.placement())},
        std::vector{reference_label_overlay(label, board),
                    polygon_overlay(
                        polygon, std::vector{EntityRef::component_placement(geometry.placement())},
                        placement_overlay_layers(board, geometry.side()))},
        std::nullopt,
        "default-reference-designator-over-package"};
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

[[nodiscard]] std::vector<PadVisualGeometry>
collect_pad_visual_geometry(const Board &board, const FootprintLibrary &footprints) {
    auto pads = std::vector<PadVisualGeometry>{};
    for (std::size_t placement_index = 0; placement_index < board.placement_count();
         ++placement_index) {
        const auto placement_id = ComponentPlacementId{placement_index};
        const auto &placement = board.placement(placement_id);
        const auto &selected_part =
            volt::queries::selected_physical_part(board.circuit(), placement.component());
        if (!selected_part.has_value()) {
            continue;
        }
        const auto footprint_resolution = resolve_footprint(selected_part.value(), footprints);
        const auto *definition = footprint_resolution.definition();
        if (definition == nullptr) {
            continue;
        }
        for (std::size_t pad_index = 0; pad_index < definition->pad_count(); ++pad_index) {
            const auto pad_id = FootprintPadId{pad_index};
            const auto &pad = definition->pad(pad_id);
            pads.push_back(PadVisualGeometry{
                placement_id,
                placement.component(),
                pad_id,
                placement.side(),
                pad_copper_layers(board, pad, placement.side()),
                transformed_pad_body_corners(placement, pad),
            });
        }
    }
    return pads;
}

[[nodiscard]] ReferenceDesignatorVisualExtent
reference_designator_extent(const Board &board, const ComponentPlacement &placement,
                            ComponentPlacementId placement_id,
                            const FootprintDefinition &definition) {
    const auto value = pcb_reference_label(board.circuit(), placement.component());
    const auto corners = default_reference_designator_corners(placement, definition, value);
    const auto [min, max] = box_bounds(corners);
    return ReferenceDesignatorVisualExtent{
        placement_id, placement.component(), placement.side(), value, corners, min, max};
}

[[nodiscard]] std::vector<ReferenceDesignatorVisualExtent>
collect_reference_designator_extents(const Board &board, const FootprintLibrary &footprints) {
    auto labels = std::vector<ReferenceDesignatorVisualExtent>{};
    labels.reserve(board.placement_count());
    for (std::size_t placement_index = 0; placement_index < board.placement_count();
         ++placement_index) {
        const auto placement_id = ComponentPlacementId{placement_index};
        const auto &placement = board.placement(placement_id);
        const auto &selected_part =
            volt::queries::selected_physical_part(board.circuit(), placement.component());
        if (!selected_part.has_value()) {
            continue;
        }
        const auto footprint_resolution = resolve_footprint(selected_part.value(), footprints);
        const auto *definition = footprint_resolution.definition();
        if (definition == nullptr) {
            continue;
        }
        labels.push_back(reference_designator_extent(board, placement, placement_id, *definition));
    }
    return labels;
}

[[nodiscard]] bool text_exits_outline(const Board &board, const TextVisualExtent &extent) {
    if (!board.outline().has_value()) {
        return false;
    }
    return !outline_contains_polygon(*board.outline(), text_box_corners(board.text(extent.text)),
                                     0.0);
}

void validate_text_pad_obstructions(const std::vector<TextVisualExtent> &texts,
                                    const std::vector<PadVisualGeometry> &pads,
                                    DiagnosticReport &report) {
    for (const auto &text : texts) {
        for (const auto &pad : pads) {
            if (!text_shares_pad_layer(text, pad) || !text_intersects_polygon(text, pad.outline)) {
                continue;
            }
            report.add(text_pad_obstruction_diagnostic(text, pad));
        }
    }
}

void validate_text_package_obstructions(const Board &board,
                                        const std::vector<TextVisualExtent> &texts,
                                        const std::vector<ProjectedFootprintGeometry> &geometries,
                                        DiagnosticReport &report) {
    for (const auto &text : texts) {
        for (const auto &geometry : geometries) {
            if (!text_shares_side(text, geometry.side())) {
                continue;
            }
            if (geometry.body().has_value() &&
                text_intersects_polygon(text, geometry.body().value())) {
                report.add(text_package_obstruction_diagnostic(board, text, geometry,
                                                               geometry.body().value(), "body",
                                                               "board-text-over-package-body"));
            }
            if (geometry.courtyard().has_value() &&
                text_intersects_polygon(text, geometry.courtyard().value())) {
                report.add(text_package_obstruction_diagnostic(
                    board, text, geometry, geometry.courtyard().value(), "courtyard",
                    "board-text-over-package-courtyard"));
            }
        }
    }
}

[[nodiscard]] bool text_intersects_hole(const TextVisualExtent &text, const BoardFeature &feature) {
    return point_polygon_distance(feature.hole().center(), text.corners) <=
           (feature.hole().drill_diameter_mm() / 2.0) + board_drc_epsilon;
}

void validate_text_hole_obstructions(const Board &board, const std::vector<TextVisualExtent> &texts,
                                     DiagnosticReport &report) {
    for (const auto &text : texts) {
        for (std::size_t feature_index = 0; feature_index < board.feature_count();
             ++feature_index) {
            const auto feature_id = BoardFeatureId{feature_index};
            const auto &feature = board.feature(feature_id);
            if (feature.kind() != BoardFeatureKind::Hole || !text_intersects_hole(text, feature)) {
                continue;
            }
            report.add(text_hole_obstruction_diagnostic(text, feature_id, feature));
        }
    }
}

void validate_reference_designator_obstructions(
    const Board &board, const std::vector<ReferenceDesignatorVisualExtent> &labels,
    const std::vector<ProjectedFootprintGeometry> &geometries, DiagnosticReport &report) {
    for (const auto &label : labels) {
        for (const auto &geometry : geometries) {
            if (label.placement == geometry.placement() || label.side != geometry.side()) {
                continue;
            }
            if (geometry.body().has_value() &&
                polygon_polygon_distance(label.corners, geometry.body().value()) <=
                    board_drc_epsilon) {
                report.add(reference_designator_obstruction_diagnostic(board, label, geometry,
                                                                       geometry.body().value()));
                continue;
            }
            if (geometry.courtyard().has_value() &&
                polygon_polygon_distance(label.corners, geometry.courtyard().value()) <=
                    board_drc_epsilon) {
                report.add(reference_designator_obstruction_diagnostic(
                    board, label, geometry, geometry.courtyard().value()));
            }
        }
    }
}

} // namespace

void validate_board_visual(const Board &board, const FootprintLibrary &footprints,
                           DiagnosticReport &report) {
    const auto extents = collect_placement_visual_extents(board, footprints);
    const auto texts = collect_text_visual_extents(board);
    const auto pads = collect_pad_visual_geometry(board, footprints);
    const auto geometries = board.project_footprint_geometries(footprints);
    const auto reference_labels = collect_reference_designator_extents(board, footprints);
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

    validate_text_pad_obstructions(texts, pads, report);
    validate_text_package_obstructions(board, texts, geometries, report);
    validate_text_hole_obstructions(board, texts, report);
    validate_reference_designator_obstructions(board, reference_labels, geometries, report);
}

} // namespace volt::detail
