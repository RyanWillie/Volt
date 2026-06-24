#include "board_footprint_drc.hpp"

#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace volt::detail {
namespace {

[[nodiscard]] double rounded_measurement(double value) noexcept {
    const auto rounded = std::round(value * 1.0e12) / 1.0e12;
    if (std::abs(rounded) <= board_drc_epsilon) {
        return 0.0;
    }
    return rounded;
}

[[nodiscard]] Diagnostic
drc_diagnostic(std::string_view code, std::string message, std::vector<EntityRef> entities = {},
               std::vector<DiagnosticOverlay> overlays = {},
               std::optional<DiagnosticMeasurement> measurement = std::nullopt,
               std::optional<std::string> rule = std::nullopt) {
    return Diagnostic{Severity::Error,
                      DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::Drc},
                      std::move(message),
                      std::move(entities),
                      std::move(overlays),
                      measurement,
                      std::move(rule)};
}

[[nodiscard]] Diagnostic
drc_warning(std::string_view code, std::string message, std::vector<EntityRef> entities = {},
            std::vector<DiagnosticOverlay> overlays = {},
            std::optional<DiagnosticMeasurement> measurement = std::nullopt,
            std::optional<std::string> rule = std::nullopt) {
    return Diagnostic{Severity::Warning,
                      DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::Drc},
                      std::move(message),
                      std::move(entities),
                      std::move(overlays),
                      measurement,
                      std::move(rule)};
}

[[nodiscard]] DiagnosticPoint to_diagnostic_point(const BoardPoint &point) {
    return DiagnosticPoint{point.x_mm(), point.y_mm()};
}

[[nodiscard]] bool layer_side_matches_placement(BoardLayerSide layer_side,
                                                BoardSide placement_side) {
    switch (layer_side) {
    case BoardLayerSide::Top:
        return placement_side == BoardSide::Top;
    case BoardLayerSide::Bottom:
        return placement_side == BoardSide::Bottom;
    case BoardLayerSide::Both:
        return true;
    case BoardLayerSide::Inner:
    case BoardLayerSide::None:
        return false;
    }
    throw std::logic_error{"Unhandled PCB board layer side"};
}

[[nodiscard]] std::vector<BoardLayerId> placement_side_layers(const Board &board,
                                                              BoardSide placement_side) {
    auto layers = std::vector<BoardLayerId>{};
    for (std::size_t index = 0; index < board.layer_count(); ++index) {
        const auto layer = BoardLayerId{index};
        if (layer_side_matches_placement(board.layer(layer).side(), placement_side)) {
            layers.push_back(layer);
        }
    }
    return layers;
}

[[nodiscard]] DiagnosticOverlay footprint_geometry_overlay(ComponentPlacementId placement,
                                                           const std::vector<BoardPoint> &polygon,
                                                           std::vector<BoardLayerId> layers) {
    auto vertices = std::vector<DiagnosticPoint>{};
    vertices.reserve(polygon.size());
    for (const auto &point : polygon) {
        vertices.push_back(to_diagnostic_point(point));
    }
    return DiagnosticOverlay::polygon(std::move(vertices),
                                      std::vector{EntityRef::component_placement(placement)},
                                      std::move(layers));
}

[[nodiscard]] std::vector<EntityRef>
footprint_geometry_entities(const ProjectedFootprintGeometry &lhs,
                            const ProjectedFootprintGeometry &rhs) {
    return std::vector{EntityRef::component_placement(lhs.placement()),
                       EntityRef::component_placement(rhs.placement()),
                       EntityRef::component(lhs.component()),
                       EntityRef::component(rhs.component())};
}

void append_component_geometry_overlap(const Board &board, const ProjectedFootprintGeometry &lhs,
                                       const ProjectedFootprintGeometry &rhs,
                                       const std::optional<std::vector<BoardPoint>> &lhs_polygon,
                                       const std::optional<std::vector<BoardPoint>> &rhs_polygon,
                                       std::string_view code, std::string message, std::string rule,
                                       DiagnosticReport &report) {
    if (!lhs_polygon.has_value() || !rhs_polygon.has_value()) {
        return;
    }
    const auto distance =
        rounded_measurement(polygon_polygon_distance(lhs_polygon.value(), rhs_polygon.value()));
    if (distance > board_drc_epsilon) {
        return;
    }

    report.add(drc_diagnostic(
        code, std::move(message), footprint_geometry_entities(lhs, rhs),
        std::vector{footprint_geometry_overlay(lhs.placement(), lhs_polygon.value(),
                                               placement_side_layers(board, lhs.side())),
                    footprint_geometry_overlay(rhs.placement(), rhs_polygon.value(),
                                               placement_side_layers(board, rhs.side()))},
        DiagnosticMeasurement{0.0, 0.0}, std::move(rule)));
}

void append_component_assembly_clearance(const Board &board, const ProjectedFootprintGeometry &lhs,
                                         const ProjectedFootprintGeometry &rhs,
                                         const std::optional<std::vector<BoardPoint>> &lhs_polygon,
                                         const std::optional<std::vector<BoardPoint>> &rhs_polygon,
                                         std::string_view label, std::string rule,
                                         DiagnosticReport &report) {
    if (!lhs_polygon.has_value() || !rhs_polygon.has_value()) {
        return;
    }
    const auto required = board.design_rules().package_assembly_clearance_mm();
    const auto distance =
        rounded_measurement(polygon_polygon_distance(lhs_polygon.value(), rhs_polygon.value()));
    if (required <= board_drc_epsilon || distance <= board_drc_epsilon ||
        distance + board_drc_epsilon >= required) {
        return;
    }

    report.add(drc_warning(
        drc_diagnostic_codes::ComponentAssemblyClearanceWarning,
        std::string{"Component "} + std::string{label} +
            " clearance is below assembly comfort recommendation",
        footprint_geometry_entities(lhs, rhs),
        std::vector{footprint_geometry_overlay(lhs.placement(), lhs_polygon.value(),
                                               placement_side_layers(board, lhs.side())),
                    footprint_geometry_overlay(rhs.placement(), rhs_polygon.value(),
                                               placement_side_layers(board, rhs.side()))},
        DiagnosticMeasurement{distance, required}, std::move(rule)));
}

[[nodiscard]] double package_board_edge_clearance(const BoardOutline &outline,
                                                  const std::vector<BoardPoint> &polygon) {
    if (!outline_contains_polygon(outline, polygon, 0.0)) {
        return 0.0;
    }
    return rounded_measurement(polygon_outline_boundary_distance(outline, polygon));
}

void append_component_board_edge_clearance(const Board &board,
                                           const ProjectedFootprintGeometry &geometry,
                                           const std::optional<std::vector<BoardPoint>> &polygon,
                                           std::string_view label, std::string rule,
                                           DiagnosticReport &report) {
    if (!board.outline().has_value() || !polygon.has_value()) {
        return;
    }
    const auto required = board.design_rules().board_outline_clearance_mm();
    if (outline_contains_polygon(*board.outline(), polygon.value(), required)) {
        return;
    }
    const auto actual = package_board_edge_clearance(*board.outline(), polygon.value());
    report.add(drc_diagnostic(
        drc_diagnostic_codes::ComponentBoardEdgeClearanceViolation,
        std::string{"Component "} + std::string{label} + " does not satisfy board-edge clearance",
        std::vector{EntityRef::board(), EntityRef::component_placement(geometry.placement()),
                    EntityRef::component(geometry.component())},
        std::vector{footprint_geometry_overlay(geometry.placement(), polygon.value(),
                                               placement_side_layers(board, geometry.side()))},
        DiagnosticMeasurement{actual, required}, std::move(rule)));
}

void validate_component_geometry_overlaps(const Board &board,
                                          const std::vector<ProjectedFootprintGeometry> &geometries,
                                          DiagnosticReport &report) {
    for (std::size_t lhs_index = 0; lhs_index < geometries.size(); ++lhs_index) {
        for (std::size_t rhs_index = lhs_index + 1U; rhs_index < geometries.size(); ++rhs_index) {
            const auto &lhs = geometries[lhs_index];
            const auto &rhs = geometries[rhs_index];
            if (lhs.side() != rhs.side()) {
                continue;
            }
            append_component_geometry_overlap(
                board, lhs, rhs, lhs.body(), rhs.body(), drc_diagnostic_codes::ComponentBodyOverlap,
                "Component body geometry overlaps another placed component",
                "component-body-overlap", report);
            append_component_assembly_clearance(board, lhs, rhs, lhs.body(), rhs.body(), "body",
                                                "component-body-to-body-assembly-clearance",
                                                report);
            append_component_geometry_overlap(
                board, lhs, rhs, lhs.courtyard(), rhs.courtyard(),
                drc_diagnostic_codes::ComponentCourtyardOverlap,
                "Component courtyard geometry overlaps another placed component",
                "component-courtyard-overlap", report);
            append_component_assembly_clearance(
                board, lhs, rhs, lhs.courtyard(), rhs.courtyard(), "courtyard",
                "component-courtyard-to-courtyard-assembly-clearance", report);
        }
    }
}

void validate_component_board_edge_clearance(
    const Board &board, const std::vector<ProjectedFootprintGeometry> &geometries,
    DiagnosticReport &report) {
    for (const auto &geometry : geometries) {
        append_component_board_edge_clearance(board, geometry, geometry.body(), "body",
                                              "component-body-to-board-edge-clearance", report);
        append_component_board_edge_clearance(board, geometry, geometry.courtyard(), "courtyard",
                                              "component-courtyard-to-board-edge-clearance",
                                              report);
    }
}

} // namespace

void validate_footprint_geometry_drc(const Board &board, const FootprintLibrary &footprints,
                                     DiagnosticReport &report) {
    const auto geometries = board.project_footprint_geometries(footprints);
    validate_component_geometry_overlaps(board, geometries, report);
    validate_component_board_edge_clearance(board, geometries, report);
}

} // namespace volt::detail
