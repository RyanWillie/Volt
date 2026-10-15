#include "../copper/board_room_rules.hpp"
#include "route_detail.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

#include <volt/circuit/constraints/net_class_resolution.hpp>
#include <volt/circuit/validation/validation.hpp>
#include <volt/core/errors.hpp>
#include <volt/pcb/queries/board_queries.hpp>

namespace volt::route_detail {
namespace {

bool has_layer(const std::vector<BoardLayerId> &layers, BoardLayerId layer) {
    return std::find(layers.begin(), layers.end(), layer) != layers.end();
}

std::optional<FootprintDefinition> footprint(const Board &board, const ResolvedBoardView &resolved,
                                             ComponentPlacementId id,
                                             const FootprintDefinition &identity) {
    const auto &placement = board.get(id);
    const auto *part = resolved.part(placement.component());
    if (!part)
        return std::nullopt;
    owner(part->physical_part().footprint() == identity.ref());
    const auto resolution = resolve_footprint(part->physical_part(), resolved.footprints());
    const auto *definition = resolution.definition();
    if (definition)
        owner(*definition == identity);
    return definition ? std::optional{*definition} : std::nullopt;
}

void operand_ref(const Board &board, const ResolvedBoardView &resolved,
                 const RouteOperand &operand) {
    std::visit(
        [&](const auto &v) {
            owner(v.owner == &board);
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::same_as<T, RoutePad> || std::same_as<T, RoutePlacedCopper>) {
                if (const auto definition = footprint(board, resolved, v.placement, v.footprint)) {
                    if constexpr (std::same_as<T, RoutePad>)
                        static_cast<void>(definition->pad(v.pad));
                }
            } else if constexpr (std::same_as<T, RouteTrackCopper>)
                static_cast<void>(board.get(v.track));
            else if constexpr (std::same_as<T, RouteViaRef>)
                static_cast<void>(board.get(v.via));
            else if constexpr (std::same_as<T, RouteKeepout>)
                static_cast<void>(board.get(v.keepout));
            else if (board.outline() && v.edge_index >= board.outline()->vertices().size())
                throw KernelRangeError{ErrorCode::UnknownEntity,
                                       "Route board edge index is unknown"};
        },
        operand);
}

void axis_ref(RouteAxis axis) {
    if (axis != RouteAxis::X && axis != RouteAxis::Y)
        invalid("Unknown route target axis");
}

RouteAxis direction_axis(RouteDirection direction) {
    switch (direction) {
    case RouteDirection::PositiveX:
    case RouteDirection::NegativeX:
        return RouteAxis::X;
    case RouteDirection::PositiveY:
    case RouteDirection::NegativeY:
        return RouteAxis::Y;
    }
    invalid("Unknown route target direction");
    return RouteAxis::X;
}

bool positive_direction(RouteDirection direction) {
    return direction == RouteDirection::PositiveX || direction == RouteDirection::PositiveY;
}

double coordinate(BoardPoint p, RouteAxis axis) {
    return axis == RouteAxis::X ? p.x_mm() : p.y_mm();
}

struct Bounds {
    double low = std::numeric_limits<double>::infinity();
    double high = -std::numeric_limits<double>::infinity();
};

Bounds project(const std::vector<BoardPoint> &points, double radius, RouteAxis axis) {
    auto bounds = Bounds{};
    for (const auto p : points) {
        bounds.low = std::min(bounds.low, coordinate(p, axis) - radius);
        bounds.high = std::max(bounds.high, coordinate(p, axis) + radius);
    }
    return bounds;
}

struct Geometry {
    std::vector<detail::BoardCopperShape> copper;
    std::vector<BoardPoint> mechanical;
    std::optional<BoardKeepoutId> keepout;
    bool edge = false;
    double edge_coordinate = 0.0;
    std::string failure;
};

Geometry geometry(const Board &board, const ResolvedBoardView &resolved,
                  const std::vector<PadResolution> &pads, const RouteOperand &operand,
                  BoardLayerId layer, RouteAxis axis, bool lower_side, BoardPoint current) {
    auto out = Geometry{};
    std::visit(
        [&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::same_as<T, RouteTrackCopper>) {
                const auto &track = board.get(v.track);
                if (track.layer() != layer)
                    return;
                for (std::size_t i = 1; i < track.points().size(); ++i)
                    out.copper.push_back({detail::BoardCopperShapeKind::Segment,
                                          track.net(),
                                          {layer},
                                          {EntityRef::board_track(v.track)},
                                          {track.points()[i - 1], track.points()[i]},
                                          track.width_mm() / 2,
                                          std::nullopt,
                                          BoardClearanceKind::Track});
            } else if constexpr (std::same_as<T, RouteViaRef>) {
                const auto &via = board.get(v.via);
                if (has_layer(detail::via_copper_layers(board, via), layer))
                    out.copper.push_back({detail::BoardCopperShapeKind::Disc,
                                          via.net(),
                                          {layer},
                                          {EntityRef::board_via(v.via)},
                                          {via.position()},
                                          via.annular_diameter_mm() / 2,
                                          std::nullopt,
                                          BoardClearanceKind::Via});
            } else if constexpr (std::same_as<T, RoutePad> || std::same_as<T, RoutePlacedCopper>) {
                const auto definition = footprint(board, resolved, v.placement, v.footprint);
                if (!definition) {
                    out.failure = "target physical closure is unavailable";
                    return;
                }
                const auto &placement = board.get(v.placement);
                for (std::size_t i = 0; i < definition->pad_count(); ++i) {
                    const auto id = FootprintPadId{i};
                    if constexpr (std::same_as<T, RoutePad>)
                        if (id != v.pad)
                            continue;
                    const auto &pad = definition->pad(id);
                    if (!has_layer(detail::pad_copper_layers(board, pad, placement.side()), layer))
                        continue;
                    if (pad.shape() != FootprintPadShape::Circle &&
                        pad.shape() != FootprintPadShape::Rectangle &&
                        pad.shape() != FootprintPadShape::RoundedRectangle) {
                        out.failure = "target active pad shape is unsupported "
                                      "(Circle/Rectangle/RoundedRectangle only)";
                        return;
                    }
                    const auto match = std::find_if(pads.begin(), pads.end(), [&](const auto &p) {
                        return p.placement() == v.placement && p.pad() == id;
                    });
                    if (match == pads.end() || !match->net()) {
                        out.failure = "target active pad is not connected to a logical net";
                        return;
                    }
                    auto shape = detail::BoardCopperShape{
                        pad.shape() == FootprintPadShape::Circle
                            ? detail::BoardCopperShapeKind::Disc
                            : detail::BoardCopperShapeKind::Polygon,
                        *match->net(),
                        {layer},
                        {EntityRef::component_placement(v.placement), EntityRef::footprint_pad(id)},
                        pad.shape() == FootprintPadShape::Circle
                            ? std::vector{match->position()}
                            : detail::transformed_pad_body_corners(placement, pad),
                        pad.shape() == FootprintPadShape::Circle
                            ? std::max(pad.size().width_mm(), pad.size().height_mm()) / 2.0
                            : 0.0,
                        detail::BoardPadShapeKey{v.placement, id}};
                    shape.layers = {layer};
                    out.copper.push_back(std::move(shape));
                }
            } else if constexpr (std::same_as<T, RouteKeepout>) {
                const auto &keepout = board.get(v.keepout);
                if (!has_layer(keepout.layers(), layer) ||
                    !detail::keepout_restricts(keepout, BoardKeepoutRestriction::Copper)) {
                    out.failure = "target keepout does not restrict tracks on active layer";
                    return;
                }
                out.mechanical = keepout.outline();
                out.keepout = v.keepout;
            } else {
                if (!board.outline()) {
                    out.failure = "target board outline is unavailable";
                    return;
                }
                const auto &points = board.outline()->vertices();
                auto rectangular = points.size() == 4;
                for (std::size_t i = 0; i < points.size(); ++i) {
                    const auto a = points[i];
                    const auto b = points[(i + 1) % points.size()];
                    rectangular = rectangular && ((a.x_mm() == b.x_mm()) != (a.y_mm() == b.y_mm()));
                }
                if (!rectangular) {
                    out.failure = "target board edge requires an axis-aligned rectangular outline";
                    return;
                }
                const auto a = points[v.edge_index];
                const auto b = points[(v.edge_index + 1) % points.size()];
                const auto other = axis == RouteAxis::X ? RouteAxis::Y : RouteAxis::X;
                if (coordinate(a, axis) != coordinate(b, axis)) {
                    out.failure = "target board edge is not normal to target axis";
                    return;
                }
                const auto transverse = project({a, b}, 0, other);
                if (coordinate(current, other) < transverse.low ||
                    coordinate(current, other) > transverse.high) {
                    out.failure = "target transverse coordinate lies outside board edge segment";
                    return;
                }
                const auto bounds = project(points, 0, axis);
                out.edge_coordinate = coordinate(a, axis);
                if ((lower_side && out.edge_coordinate != bounds.low) ||
                    (!lower_side && out.edge_coordinate != bounds.high)) {
                    out.failure = "target board edge direction points outside the interior";
                    return;
                }
                out.edge = true;
                out.mechanical = {a, b};
            }
        },
        operand);
    if (out.failure.empty() && out.copper.empty() && out.mechanical.empty())
        out.failure = "target has no active-layer copper";
    return out;
}

double raw_bound(const Geometry &g, RouteAxis axis, bool lower_side) {
    if (g.edge)
        return g.edge_coordinate;
    auto bounds = project(g.mechanical, 0, axis);
    for (const auto &s : g.copper) {
        const auto part = project(s.points, s.radius_mm, axis);
        bounds.low = std::min(bounds.low, part.low);
        bounds.high = std::max(bounds.high, part.high);
    }
    return lower_side ? bounds.high : bounds.low;
}

std::vector<double> possible_bounds(const Board &board, const detail::NetContinuityView &continuity,
                                    NetId net, const Geometry &g, RouteAxis axis, bool lower_side,
                                    double width, double extra) {
    auto out = std::vector<double>{};
    const auto sign = lower_side ? 1.0 : -1.0;
    if (g.copper.empty()) {
        const auto reserve = g.edge ? board.design_rules().clearance_mm(
                                          BoardClearanceKind::Track, BoardClearanceKind::BoardEdge)
                                    : 2 * detail::board_drc_epsilon;
        return {raw_bound(g, axis, lower_side) + sign * (width / 2 + reserve + extra)};
    }
    for (const auto &shape : g.copper) {
        auto values = std::vector<double>{0};
        if (!continuity.same_group(net, shape.net)) {
            values = {resolve_copper_clearance_mm(
                board.circuit(), net, shape.net,
                board.design_rules().clearance_mm(BoardClearanceKind::Track,
                                                  detail::shape_clearance_kind(shape)))};
            for (const auto &room : board.all<BoardRoomId>())
                if (room.copper_clearance_mm())
                    values.push_back(*room.copper_clearance_mm());
        }
        const auto projection = project(shape.points, shape.radius_mm, axis);
        const auto boundary = lower_side ? projection.high : projection.low;
        for (const auto clearance : values)
            out.push_back(boundary + sign * (width / 2 + clearance + extra));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

double final_bound(const Board &board, const detail::NetContinuityView &continuity,
                   const detail::BoardCopperShape &candidate, const Geometry &g, RouteAxis axis,
                   bool lower_side, double extra, std::vector<RouteTargetConstraint> &constraints) {
    const auto sign = lower_side ? 1.0 : -1.0;
    auto bound = lower_side ? -std::numeric_limits<double>::infinity()
                            : std::numeric_limits<double>::infinity();
    if (g.copper.empty()) {
        const auto reserve = g.edge ? board.design_rules().clearance_mm(
                                          BoardClearanceKind::Track, BoardClearanceKind::BoardEdge)
                                    : 2 * detail::board_drc_epsilon;
        auto source = board_source(RouteRuleField::Clearance);
        auto entities = std::vector<EntityRef>{};
        if (g.keepout) {
            source.kind = RouteRuleSourceKind::Keepout;
            source.keepout = g.keepout;
            entities.push_back(EntityRef::board_keepout(*g.keepout));
        } else {
            source.first_kind = BoardClearanceKind::Track;
            source.second_kind = BoardClearanceKind::BoardEdge;
        }
        constraints.push_back(
            {!lower_side, entities, {reserve, reserve, std::nullopt, {source}, {}}, false});
        return raw_bound(g, axis, lower_side) + sign * (candidate.radius_mm + reserve + extra);
    }
    for (const auto &obstacle : g.copper) {
        const auto kind = detail::shape_clearance_kind(obstacle);
        const auto check = detail::check_copper_clearance(
            board, continuity, candidate, BoardClearanceKind::Track, obstacle, kind);
        const auto required = check.required_clearance_mm;
        const auto exempt = continuity.same_group(candidate.net, obstacle.net);
        const auto sources =
            exempt
                ? std::vector<RouteRuleSource>{}
                : clearance_sources(board, candidate.net, BoardClearanceKind::Track,
                                    {obstacle.net, kind, obstacle.primary_entities, obstacle.pad},
                                    required, check.room);
        constraints.push_back({!lower_side,
                               obstacle.primary_entities,
                               {required, required, std::nullopt, sources, {}},
                               exempt});
        const auto projection = project(obstacle.points, obstacle.radius_mm, axis);
        const auto value = (lower_side ? projection.high : projection.low) +
                           sign * (candidate.radius_mm + required + extra);
        bound = lower_side ? std::max(bound, value) : std::min(bound, value);
    }
    return bound;
}

void resolve(const Board &board, const ResolvedBoardView &resolved,
             const std::vector<PadResolution> &pads, const RouteSpec &spec,
             const RouteOperand &first, const RouteOperand *second, RouteAxis axis, bool lower_side,
             bool exit, double extra, BoardPoint current, BoardLayerId layer, std::size_t step,
             RouteResult &result) {
    const auto first_geometry =
        geometry(board, resolved, pads, first, layer, axis, lower_side, current);
    const auto second_geometry =
        second ? geometry(board, resolved, pads, *second, layer, axis, false, current) : Geometry{};
    auto geometry_failure = [&](std::string message) {
        fail(result, RouteFailureKind::GeometryUnresolvable, step, std::move(message));
    };
    if (!first_geometry.failure.empty() || !second_geometry.failure.empty()) {
        geometry_failure(!first_geometry.failure.empty() ? first_geometry.failure
                                                         : second_geometry.failure);
        return;
    }
    if (exit) {
        if (first_geometry.edge) {
            geometry_failure("Exit from board edge is unsupported");
            return;
        }
        for (const auto test_axis : {RouteAxis::X, RouteAxis::Y}) {
            if (coordinate(current, test_axis) < raw_bound(first_geometry, test_axis, false) ||
                coordinate(current, test_axis) > raw_bound(first_geometry, test_axis, true)) {
                geometry_failure("Exit current point lies outside operand projected envelope");
                return;
            }
        }
    }
    const auto raw_first = raw_bound(first_geometry, axis, lower_side);
    const auto raw_second = second ? raw_bound(second_geometry, axis, false) : raw_first;
    if (!std::isfinite(raw_first) || !std::isfinite(raw_second)) {
        geometry_failure("target projection arithmetic is non-finite");
        return;
    }
    if (second && raw_first >= raw_second) {
        geometry_failure("Gap operands overlap or are not ordered along the target axis");
        return;
    }
    const auto net = *result.net;
    const auto continuity = resolved.logical_continuity();
    const auto parameters = resolve_net_class_rules(board.circuit(), net);
    const auto base = std::max(board.design_rules().minimum_track_width_mm(),
                               parameters.track_width_mm.value_or(0));
    auto widths = std::vector<double>{spec.width_mm.value_or(base)};
    if (!spec.width_mm)
        for (const auto &room : board.all<BoardRoomId>())
            if (room.track_width_mm() && *room.track_width_mm() >= base)
                widths.push_back(*room.track_width_mm());
    std::sort(widths.begin(), widths.end());
    widths.erase(std::unique(widths.begin(), widths.end()), widths.end());
    auto solutions = std::vector<RouteTargetResolution>{};
    auto nonfinite = false;
    auto zero_motion = false;
    for (const auto width : widths) {
        const auto first_bounds =
            possible_bounds(board, continuity, net, first_geometry, axis, lower_side, width, extra);
        const auto second_bounds =
            second ? possible_bounds(board, continuity, net, second_geometry, axis, false, width, 0)
                   : std::vector<double>{0};
        for (const auto a : first_bounds) {
            for (const auto b : second_bounds) {
                if (!std::isfinite(a) || !std::isfinite(b)) {
                    nonfinite = true;
                    continue;
                }
                if (second && a > b + detail::board_drc_epsilon)
                    continue;
                const auto value = second ? a / 2 + b / 2 : a;
                if (!std::isfinite(value)) {
                    nonfinite = true;
                    continue;
                }
                const auto target = axis == RouteAxis::X ? BoardPoint{value, current.y_mm()}
                                                         : BoardPoint{current.x_mm(), value};
                if (target == current) {
                    zero_motion = true;
                    continue;
                }
                auto below = false;
                auto actual_width =
                    resolve_width(board, net, layer, {current, target}, spec.width_mm, below);
                if (actual_width.value_mm != width)
                    continue;
                const auto candidate =
                    detail::BoardCopperShape{detail::BoardCopperShapeKind::Segment,
                                             net,
                                             {layer},
                                             {},
                                             {current, target},
                                             width / 2,
                                             std::nullopt,
                                             BoardClearanceKind::Track};
                auto constraints = std::vector<RouteTargetConstraint>{};
                const auto final_first = final_bound(board, continuity, candidate, first_geometry,
                                                     axis, lower_side, extra, constraints);
                const auto final_second =
                    second ? final_bound(board, continuity, candidate, second_geometry, axis, false,
                                         0, constraints)
                           : final_first;
                if (a != final_first || (second && b != final_second))
                    continue;
                if (std::none_of(solutions.begin(), solutions.end(), [&](const auto &s) {
                        return s.position == target && s.width.value_mm == width &&
                               s.lower_mm == final_first && s.upper_mm == final_second;
                    }))
                    solutions.push_back({step, target, axis, raw_first, raw_second, final_first,
                                         final_second, std::move(actual_width),
                                         std::move(constraints)});
            }
        }
    }
    if (solutions.size() > 1) {
        geometry_failure("target has multiple self-consistent native rule solutions");
    } else if (solutions.empty()) {
        if (nonfinite)
            geometry_failure("target clearance arithmetic is non-finite");
        else if (zero_motion)
            geometry_failure("symbolic target requires a nonzero axis leg");
        else
            fail(result, RouteFailureKind::RuleUnresolvable, step,
                 "target has no self-consistent feasible native rule solution");
    } else {
        result.target_resolutions.push_back(std::move(solutions.front()));
    }
}

} // namespace

bool is_symbolic(const RouteStep &step) {
    return std::holds_alternative<RouteExit>(step) || std::holds_alternative<RouteBeside>(step) ||
           std::holds_alternative<RouteGap>(step);
}

void preflight_target(const Board &board, const ResolvedBoardView &resolved,
                      const RouteBeside &step) {
    operand_ref(board, resolved, step.operand);
    static_cast<void>(direction_axis(step.direction));
    if (!std::isfinite(step.distance_mm) || step.distance_mm < 0)
        invalid("Route target distance must be finite and nonnegative");
}

void preflight_target(const Board &board, const ResolvedBoardView &resolved,
                      const RouteExit &step) {
    preflight_target(board, resolved, RouteBeside{step.operand, step.direction, step.distance_mm});
}

void preflight_target(const Board &board, const ResolvedBoardView &resolved, const RouteGap &step) {
    operand_ref(board, resolved, step.lower);
    operand_ref(board, resolved, step.upper);
    axis_ref(step.axis);
}

void resolve_target(const Board &board, const ResolvedBoardView &resolved,
                    const std::vector<PadResolution> &pads, const RouteSpec &spec,
                    const RouteBeside &step, BoardPoint current, BoardLayerId layer,
                    std::size_t index, RouteResult &result) {
    resolve(board, resolved, pads, spec, step.operand, nullptr, direction_axis(step.direction),
            positive_direction(step.direction), false, step.distance_mm, current, layer, index,
            result);
}

void resolve_target(const Board &board, const ResolvedBoardView &resolved,
                    const std::vector<PadResolution> &pads, const RouteSpec &spec,
                    const RouteExit &step, BoardPoint current, BoardLayerId layer,
                    std::size_t index, RouteResult &result) {
    resolve(board, resolved, pads, spec, step.operand, nullptr, direction_axis(step.direction),
            positive_direction(step.direction), true, step.distance_mm, current, layer, index,
            result);
}

void resolve_target(const Board &board, const ResolvedBoardView &resolved,
                    const std::vector<PadResolution> &pads, const RouteSpec &spec,
                    const RouteGap &step, BoardPoint current, BoardLayerId layer, std::size_t index,
                    RouteResult &result) {
    resolve(board, resolved, pads, spec, step.lower, &step.upper, step.axis, true, false, 0,
            current, layer, index, result);
}
} // namespace volt::route_detail
