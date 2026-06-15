#include "board_footprint_drc.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace volt::detail {
namespace {

[[nodiscard]] Diagnostic drc_diagnostic(std::string_view code, std::string message,
                                        std::vector<EntityRef> entities = {},
                                        std::vector<DiagnosticOverlay> overlays = {}) {
    return Diagnostic{Severity::Error,
                      DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::Drc},
                      std::move(message),
                      std::move(entities),
                      std::move(overlays)};
}

[[nodiscard]] DiagnosticPoint to_diagnostic_point(const BoardPoint &point) {
    return DiagnosticPoint{point.x_mm(), point.y_mm()};
}

[[nodiscard]] DiagnosticOverlay footprint_geometry_overlay(ComponentPlacementId placement,
                                                           const std::vector<BoardPoint> &polygon) {
    auto vertices = std::vector<DiagnosticPoint>{};
    vertices.reserve(polygon.size());
    for (const auto &point : polygon) {
        vertices.push_back(to_diagnostic_point(point));
    }
    return DiagnosticOverlay::polygon(std::move(vertices),
                                      std::vector{EntityRef::component_placement(placement)});
}

[[nodiscard]] std::vector<EntityRef>
footprint_geometry_entities(const ProjectedFootprintGeometry &lhs,
                            const ProjectedFootprintGeometry &rhs) {
    return std::vector{EntityRef::component_placement(lhs.placement()),
                       EntityRef::component_placement(rhs.placement()),
                       EntityRef::component(lhs.component()),
                       EntityRef::component(rhs.component())};
}

void append_component_geometry_overlap(const ProjectedFootprintGeometry &lhs,
                                       const ProjectedFootprintGeometry &rhs,
                                       const std::optional<std::vector<BoardPoint>> &lhs_polygon,
                                       const std::optional<std::vector<BoardPoint>> &rhs_polygon,
                                       std::string_view code, std::string message,
                                       DiagnosticReport &report) {
    if (!lhs_polygon.has_value() || !rhs_polygon.has_value()) {
        return;
    }
    if (polygon_polygon_distance(lhs_polygon.value(), rhs_polygon.value()) > board_drc_epsilon) {
        return;
    }

    report.add(drc_diagnostic(
        code, std::move(message), footprint_geometry_entities(lhs, rhs),
        std::vector{footprint_geometry_overlay(lhs.placement(), lhs_polygon.value()),
                    footprint_geometry_overlay(rhs.placement(), rhs_polygon.value())}));
}

void validate_component_geometry_overlaps(const std::vector<ProjectedFootprintGeometry> &geometries,
                                          DiagnosticReport &report) {
    for (std::size_t lhs_index = 0; lhs_index < geometries.size(); ++lhs_index) {
        for (std::size_t rhs_index = lhs_index + 1U; rhs_index < geometries.size(); ++rhs_index) {
            const auto &lhs = geometries[lhs_index];
            const auto &rhs = geometries[rhs_index];
            if (lhs.side() != rhs.side()) {
                continue;
            }
            append_component_geometry_overlap(
                lhs, rhs, lhs.body(), rhs.body(), drc_diagnostic_codes::ComponentBodyOverlap,
                "Component body geometry overlaps another placed component", report);
            append_component_geometry_overlap(
                lhs, rhs, lhs.courtyard(), rhs.courtyard(),
                drc_diagnostic_codes::ComponentCourtyardOverlap,
                "Component courtyard geometry overlaps another placed component", report);
        }
    }
}

} // namespace

void validate_footprint_geometry_drc(const Board &board, const FootprintLibrary &footprints,
                                     DiagnosticReport &report) {
    validate_component_geometry_overlaps(board.project_footprint_geometries(footprints), report);
}

} // namespace volt::detail
