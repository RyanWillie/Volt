#include <volt/pcb/board.hpp>
#include <volt/pcb/routing/board_spatial_index.hpp>

#include <volt/circuit/validation/validation.hpp>

#include "../validation/board_capability_validation.hpp"
#include "../validation/board_footprint_drc.hpp"
#include "board_copper_detail.hpp"
#include "board_room_rules.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/circuit/constraints/net_class_resolution.hpp>
#include <volt/core/rule_set.hpp>

namespace volt::detail {

[[nodiscard]] double shape_distance(const BoardCopperShape &lhs, const BoardCopperShape &rhs) {
    if (lhs.kind == BoardCopperShapeKind::Disc && rhs.kind == BoardCopperShapeKind::Disc) {
        return board_distance(lhs.points[0], rhs.points[0]);
    }
    if (lhs.kind == BoardCopperShapeKind::Segment && rhs.kind == BoardCopperShapeKind::Segment) {
        return segment_segment_distance(lhs.points[0], lhs.points[1], rhs.points[0], rhs.points[1]);
    }
    if (lhs.kind == BoardCopperShapeKind::Polygon && rhs.kind == BoardCopperShapeKind::Polygon) {
        return polygon_polygon_distance(lhs.points, rhs.points);
    }
    if (lhs.kind == BoardCopperShapeKind::Segment && rhs.kind == BoardCopperShapeKind::Disc) {
        return point_segment_distance(rhs.points[0], lhs.points[0], lhs.points[1]);
    }
    if (lhs.kind == BoardCopperShapeKind::Disc && rhs.kind == BoardCopperShapeKind::Segment) {
        return shape_distance(rhs, lhs);
    }
    if (lhs.kind == BoardCopperShapeKind::Polygon && rhs.kind == BoardCopperShapeKind::Disc) {
        return point_polygon_distance(rhs.points[0], lhs.points);
    }
    if (lhs.kind == BoardCopperShapeKind::Disc && rhs.kind == BoardCopperShapeKind::Polygon) {
        return shape_distance(rhs, lhs);
    }
    if (lhs.kind == BoardCopperShapeKind::Polygon && rhs.kind == BoardCopperShapeKind::Segment) {
        return segment_polygon_distance(rhs.points[0], rhs.points[1], lhs.points);
    }
    return shape_distance(rhs, lhs);
}

[[nodiscard]] std::optional<BoardLayerId> first_common_layer(const BoardCopperShape &lhs,
                                                             const BoardCopperShape &rhs) {
    for (const auto lhs_layer : lhs.layers) {
        if (std::find(rhs.layers.begin(), rhs.layers.end(), lhs_layer) != rhs.layers.end()) {
            return lhs_layer;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool layers_overlap(const BoardCopperShape &lhs, const BoardCopperShape &rhs) {
    return first_common_layer(lhs, rhs).has_value();
}

void append_unique_layer(std::vector<BoardLayerId> &layers, BoardLayerId layer) {
    if (std::find(layers.begin(), layers.end(), layer) == layers.end()) {
        layers.push_back(layer);
    }
}

[[nodiscard]] Diagnostic drc_diagnostic(std::string_view code, std::string message,
                                        std::vector<EntityRef> entities,
                                        std::vector<DiagnosticOverlay> overlays,
                                        std::optional<DiagnosticMeasurement> measurement) {
    return Diagnostic{Severity::Error,
                      DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::Drc},
                      std::move(message),
                      std::move(entities),
                      std::move(overlays),
                      measurement};
}

[[nodiscard]] Diagnostic drc_warning(std::string_view code, std::string message,
                                     std::vector<EntityRef> entities,
                                     std::vector<DiagnosticOverlay> overlays) {
    return Diagnostic{Severity::Warning,
                      DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::Drc},
                      std::move(message),
                      std::move(entities),
                      std::move(overlays)};
}

/** Build a board-space overlay point from a board point. */
[[nodiscard]] DiagnosticPoint to_diagnostic_point(const BoardPoint &point) {
    return DiagnosticPoint{point.x_mm(), point.y_mm()};
}

/**
 * Build one overlay describing a copper shape's geometry on a single layer:
 * segment overlays for tracks, point overlays for vias and circular pads, and polygon overlays
 * for zones and rectangular pads.
 */
[[nodiscard]] DiagnosticOverlay shape_overlay(const BoardCopperShape &shape, BoardLayerId layer) {
    const auto layers = std::vector{layer};
    if (shape.kind == BoardCopperShapeKind::Segment) {
        return DiagnosticOverlay::segment(to_diagnostic_point(shape.points[0]),
                                          to_diagnostic_point(shape.points[1]),
                                          shape.primary_entities, layers);
    }
    if (shape.kind == BoardCopperShapeKind::Disc) {
        return DiagnosticOverlay::point(to_diagnostic_point(shape.points[0]),
                                        shape.primary_entities, layers);
    }
    auto vertices = std::vector<DiagnosticPoint>{};
    vertices.reserve(shape.points.size());
    for (const auto &point : shape.points) {
        vertices.push_back(to_diagnostic_point(point));
    }
    return DiagnosticOverlay::polygon(std::move(vertices), shape.primary_entities, layers);
}

[[nodiscard]] std::vector<BoardLayerId> via_copper_layers(const Board &board, const BoardVia &via) {
    auto result = std::vector<BoardLayerId>{};
    if (board.layer_stack().has_value()) {
        auto start = std::optional<std::size_t>{};
        auto end = std::optional<std::size_t>{};
        const auto &layers = board.layer_stack()->layers();
        for (std::size_t index = 0; index < layers.size(); ++index) {
            if (layers[index] == via.start_layer()) {
                start = index;
            }
            if (layers[index] == via.end_layer()) {
                end = index;
            }
        }
        if (start.has_value() && end.has_value()) {
            const auto first = std::min(start.value(), end.value());
            const auto last = std::max(start.value(), end.value());
            for (std::size_t index = first; index <= last; ++index) {
                if (board.layer(layers[index]).role() == BoardLayerRole::Copper) {
                    append_unique_layer(result, layers[index]);
                }
            }
            return result;
        }
    }

    append_unique_layer(result, via.start_layer());
    append_unique_layer(result, via.end_layer());
    return result;
}

[[nodiscard]] std::vector<BoardLayerId>
pad_copper_layers(const Board &board, const FootprintPad &pad, BoardSide placement_side) {
    auto result = std::vector<BoardLayerId>{};
    if (pad.layers().is_through_hole()) {
        for (std::size_t index = 0; index < board.layer_count(); ++index) {
            const auto layer_id = BoardLayerId{index};
            if (board.layer(layer_id).role() == BoardLayerRole::Copper) {
                result.push_back(layer_id);
            }
        }
        return result;
    }

    const auto front_copper = pad.layers().contains(FootprintLayer::FrontCopper);
    const auto back_copper = pad.layers().contains(FootprintLayer::BackCopper);
    const auto maps_to_top = placement_side == BoardSide::Top ? front_copper : back_copper;
    const auto maps_to_bottom = placement_side == BoardSide::Top ? back_copper : front_copper;
    for (std::size_t index = 0; index < board.layer_count(); ++index) {
        const auto layer_id = BoardLayerId{index};
        const auto &layer = board.layer(layer_id);
        if (layer.role() != BoardLayerRole::Copper) {
            continue;
        }
        if ((layer.side() == BoardLayerSide::Top && maps_to_top) ||
            (layer.side() == BoardLayerSide::Bottom && maps_to_bottom)) {
            result.push_back(layer_id);
        }
    }
    return result;
}

[[nodiscard]] const PadResolution *
find_board_pad_resolution(const std::vector<PadResolution> &resolutions,
                          ComponentPlacementId placement, FootprintPadId pad) {
    const auto match = std::find_if(
        resolutions.begin(), resolutions.end(), [placement, pad](const PadResolution &candidate) {
            return candidate.placement() == placement && candidate.pad() == pad;
        });
    if (match == resolutions.end()) {
        return nullptr;
    }
    return &*match;
}

void append_track_shapes(const Board &board, std::vector<BoardCopperShape> &shapes) {
    for (std::size_t track_index = 0; track_index < board.track_count(); ++track_index) {
        const auto track_id = BoardTrackId{track_index};
        const auto &track = board.track(track_id);
        for (std::size_t point_index = 1; point_index < track.points().size(); ++point_index) {
            shapes.push_back(BoardCopperShape{
                BoardCopperShapeKind::Segment,
                track.net(),
                std::vector{track.layer()},
                std::vector{EntityRef::board_track(track_id)},
                std::vector{track.points()[point_index - 1U], track.points()[point_index]},
                track.width_mm() / 2.0,
                std::nullopt,
            });
        }
    }
}

void append_via_shapes(const Board &board, std::vector<BoardCopperShape> &shapes) {
    for (std::size_t via_index = 0; via_index < board.via_count(); ++via_index) {
        const auto via_id = BoardViaId{via_index};
        const auto &via = board.via(via_id);
        shapes.push_back(BoardCopperShape{
            BoardCopperShapeKind::Disc,
            via.net(),
            via_copper_layers(board, via),
            std::vector{EntityRef::board_via(via_id)},
            std::vector{via.position()},
            via.annular_diameter_mm() / 2.0,
            std::nullopt,
        });
    }
}

void append_zone_shapes(const Board &board, std::vector<BoardCopperShape> &shapes) {
    for (std::size_t zone_index = 0; zone_index < board.zone_count(); ++zone_index) {
        const auto zone_id = BoardZoneId{zone_index};
        const auto &zone = board.zone(zone_id);
        if (!zone.net().has_value()) {
            continue;
        }
        shapes.push_back(BoardCopperShape{
            BoardCopperShapeKind::Polygon,
            zone.net().value(),
            zone.layers(),
            std::vector{EntityRef::board_zone(zone_id)},
            zone.outline(),
            0.0,
            std::nullopt,
        });
    }
}

void append_pad_shapes(const Board &board, const FootprintLibrary &footprints,
                       const std::vector<PadResolution> &resolutions,
                       std::vector<BoardCopperShape> &shapes) {
    for (std::size_t placement_index = 0; placement_index < board.placement_count();
         ++placement_index) {
        const auto placement_id = ComponentPlacementId{placement_index};
        const auto &placement = board.placement(placement_id);
        const auto &selected_part = board.circuit().selected_physical_part(placement.component());
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
            const auto *resolution = find_board_pad_resolution(resolutions, placement_id, pad_id);
            if (resolution == nullptr || resolution->status() != PadResolutionStatus::Connected ||
                !resolution->net().has_value()) {
                continue;
            }
            const auto net = resolution->net().value();

            const auto &pad = definition->pad(pad_id);
            auto layers = pad_copper_layers(board, pad, placement.side());
            if (layers.empty()) {
                continue;
            }

            auto shape_points = std::vector<BoardPoint>{};
            auto radius = 0.0;
            auto kind = BoardCopperShapeKind::Polygon;
            if (pad.shape() == FootprintPadShape::Circle ||
                pad.shape() == FootprintPadShape::Oval) {
                kind = BoardCopperShapeKind::Disc;
                shape_points.push_back(resolution->position());
                radius = std::max(pad.size().width_mm(), pad.size().height_mm()) / 2.0;
            } else {
                shape_points = transformed_pad_body_corners(placement, pad);
            }

            shapes.push_back(BoardCopperShape{
                kind,
                net,
                std::move(layers),
                std::vector{EntityRef::component_placement(placement_id),
                            EntityRef::footprint_pad(pad_id)},
                std::move(shape_points),
                radius,
                BoardPadShapeKey{placement_id, pad_id},
            });
        }
    }
}

[[nodiscard]] std::vector<BoardCopperShape>
collect_copper_shapes(const Board &board, const FootprintLibrary &footprints,
                      const std::vector<PadResolution> &resolutions) {
    auto shapes = std::vector<BoardCopperShape>{};
    append_track_shapes(board, shapes);
    append_via_shapes(board, shapes);
    append_zone_shapes(board, shapes);
    append_pad_shapes(board, footprints, resolutions, shapes);
    return shapes;
}

[[nodiscard]] bool shape_satisfies_outline(const BoardCopperShape &shape,
                                           const BoardOutline &outline, double clearance_mm) {
    if (shape.kind == BoardCopperShapeKind::Disc) {
        return outline_contains_disc(outline, shape.points[0], shape.radius_mm, clearance_mm);
    }
    if (shape.kind == BoardCopperShapeKind::Segment) {
        return outline_contains_segment(outline, shape.points[0], shape.points[1], shape.radius_mm,
                                        clearance_mm);
    }

    return outline_contains_polygon(outline, shape.points, clearance_mm);
}

[[nodiscard]] std::vector<EntityRef> copper_shape_entities(const BoardCopperShape &shape, NetId net,
                                                           BoardLayerId layer) {
    auto entities = shape.primary_entities;
    entities.push_back(EntityRef::net(net));
    entities.push_back(EntityRef::board_layer(layer));
    return entities;
}

[[nodiscard]] std::vector<DiagnosticOverlay> track_overlays(const BoardTrack &track,
                                                            BoardTrackId track_id) {
    auto overlays = std::vector<DiagnosticOverlay>{};
    const auto entities = std::vector{EntityRef::board_track(track_id)};
    const auto layers = std::vector{track.layer()};
    for (std::size_t point_index = 1; point_index < track.points().size(); ++point_index) {
        overlays.push_back(DiagnosticOverlay::segment(
            to_diagnostic_point(track.points()[point_index - 1U]),
            to_diagnostic_point(track.points()[point_index]), entities, layers));
    }
    return overlays;
}

[[nodiscard]] DiagnosticOverlay via_overlay(const Board &board, const BoardVia &via,
                                            BoardViaId via_id) {
    return DiagnosticOverlay::point(to_diagnostic_point(via.position()),
                                    std::vector{EntityRef::board_via(via_id)},
                                    via_copper_layers(board, via));
}

[[nodiscard]] bool shape_has_entity_kind(const BoardCopperShape &shape, EntityKind kind) {
    return std::any_of(shape.primary_entities.begin(), shape.primary_entities.end(),
                       [kind](EntityRef entity) { return entity.kind() == kind; });
}

[[nodiscard]] BoardClearanceKind shape_clearance_kind(const BoardCopperShape &shape) {
    if (shape.pad.has_value() || shape_has_entity_kind(shape, EntityKind::FootprintPad)) {
        return BoardClearanceKind::Pad;
    }
    if (shape_has_entity_kind(shape, EntityKind::BoardVia)) {
        return BoardClearanceKind::Via;
    }
    if (shape_has_entity_kind(shape, EntityKind::BoardZone)) {
        return BoardClearanceKind::Zone;
    }
    return BoardClearanceKind::Track;
}

[[nodiscard]] std::string_view clearance_kind_name(BoardClearanceKind kind) {
    switch (kind) {
    case BoardClearanceKind::Track:
        return "track";
    case BoardClearanceKind::Pad:
        return "pad";
    case BoardClearanceKind::Via:
        return "via";
    case BoardClearanceKind::Zone:
        return "zone";
    case BoardClearanceKind::BoardEdge:
        return "board-edge";
    }
    return "track";
}

[[nodiscard]] std::string clearance_pair_message(BoardClearanceKind lhs, BoardClearanceKind rhs) {
    auto low = lhs;
    auto high = rhs;
    if (static_cast<int>(low) > static_cast<int>(high)) {
        std::swap(low, high);
    }
    return std::string{"Copper on different nets violates required "} +
           std::string{clearance_kind_name(low)} + "-to-" + std::string{clearance_kind_name(high)} +
           " clearance";
}

struct RequiredCopperClearance {
    double clearance_mm;
    std::optional<BoardRoomId> room;
};

[[nodiscard]] RequiredCopperClearance
required_copper_clearance(const Board &board, const BoardRoomRuleResolver &rooms,
                          const BoardCopperShape &lhs, BoardClearanceKind lhs_kind,
                          const BoardCopperShape &rhs, BoardClearanceKind rhs_kind) {
    const auto room_override = rooms.copper_clearance_override(lhs, rhs);
    if (room_override.has_value()) {
        return RequiredCopperClearance{room_override->value_mm, room_override->room};
    }

    const auto pair_floor = board.design_rules().clearance_mm(lhs_kind, rhs_kind);
    return RequiredCopperClearance{
        resolve_copper_clearance_mm(board.circuit(), lhs.net, rhs.net, pair_floor),
        std::nullopt,
    };
}

[[nodiscard]] RequiredCopperClearance required_copper_clearance(const Board &board,
                                                                const BoardRoomRuleResolver &rooms,
                                                                const BoardCopperShape &lhs,
                                                                const BoardCopperShape &rhs) {
    return required_copper_clearance(board, rooms, lhs, shape_clearance_kind(lhs), rhs,
                                     shape_clearance_kind(rhs));
}

[[nodiscard]] BoardCopperClearanceCheck
check_copper_clearance(const Board &board, const BoardCopperShape &lhs, BoardClearanceKind lhs_kind,
                       const BoardCopperShape &rhs, BoardClearanceKind rhs_kind) {
    auto result = BoardCopperClearanceCheck{};
    const auto continuity = NetContinuityView{board.circuit()};
    if (continuity.same_group(lhs.net, rhs.net)) {
        return result;
    }
    const auto layer = first_common_layer(lhs, rhs);
    if (!layer.has_value()) {
        return result;
    }

    const auto rooms = BoardRoomRuleResolver{board};
    const auto required = required_copper_clearance(board, rooms, lhs, lhs_kind, rhs, rhs_kind);
    result.participates = true;
    result.layer = layer;
    result.actual_clearance_mm = shape_distance(lhs, rhs) - lhs.radius_mm - rhs.radius_mm;
    result.required_clearance_mm = required.clearance_mm;
    result.room = required.room;
    result.violates = result.actual_clearance_mm + board_drc_epsilon < result.required_clearance_mm;
    return result;
}

[[nodiscard]] BoardCopperClearanceCheck check_copper_clearance(const Board &board,
                                                               const BoardCopperShape &lhs,
                                                               const BoardCopperShape &rhs) {
    return check_copper_clearance(board, lhs, shape_clearance_kind(lhs), rhs,
                                  shape_clearance_kind(rhs));
}

[[nodiscard]] bool shape_violates_keepout(const BoardCopperShape &shape,
                                          const BoardKeepout &keepout) {
    if (shape.kind == BoardCopperShapeKind::Disc) {
        return point_polygon_distance(shape.points[0], keepout.outline()) <=
               shape.radius_mm + board_drc_epsilon;
    }
    if (shape.kind == BoardCopperShapeKind::Segment) {
        return segment_polygon_distance(shape.points[0], shape.points[1], keepout.outline()) <=
               shape.radius_mm + board_drc_epsilon;
    }
    return polygon_polygon_distance(shape.points, keepout.outline()) <= board_drc_epsilon;
}

[[nodiscard]] std::vector<EntityRef>
keepout_copper_entities(BoardKeepoutId keepout, const BoardCopperShape &shape, BoardLayerId layer) {
    auto entities = std::vector{EntityRef::board_keepout(keepout)};
    entities.insert(entities.end(), shape.primary_entities.begin(), shape.primary_entities.end());
    entities.push_back(EntityRef::net(shape.net));
    entities.push_back(EntityRef::board_layer(layer));
    return entities;
}

[[nodiscard]] std::optional<std::size_t>
shape_index_for_pad(const std::vector<BoardCopperShape> &shapes, ComponentPlacementId placement,
                    FootprintPadId pad) {
    for (std::size_t index = 0; index < shapes.size(); ++index) {
        if (!shapes[index].pad.has_value()) {
            continue;
        }
        const auto shape_pad = shapes[index].pad.value();
        if (shape_pad.placement == placement && shape_pad.pad == pad) {
            return index;
        }
    }
    return std::nullopt;
}

} // namespace volt::detail
