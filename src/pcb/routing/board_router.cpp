#include <volt/pcb/routing/board_router.hpp>

#include <volt/core/errors.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/constraints/net_class_resolution.hpp>
#include <volt/pcb/copper/board_copper.hpp>
#include <volt/pcb/queries/board_queries.hpp>

#include "../copper/board_room_rules.hpp"

namespace volt {

namespace {

/** Maximum perpendicular jog steps explored on each side during walk-around. */
constexpr int board_router_walk_around_steps = 16;

/** Board-space epsilon below which a segment is treated as degenerate and dropped. */
constexpr double board_router_min_segment_mm = 1.0e-6;

/** Deterministic first escape-leg length for one pad. */
constexpr double board_escape_stub_length_mm = 1.0;

[[nodiscard]] bool same_point(BoardPoint lhs, BoardPoint rhs) {
    return detail::board_distance(lhs, rhs) < board_router_min_segment_mm;
}

[[nodiscard]] bool octilinear_segment(BoardPoint start, BoardPoint end) {
    const auto dx = std::abs(end.x_mm() - start.x_mm());
    const auto dy = std::abs(end.y_mm() - start.y_mm());
    return dx < board_router_min_segment_mm || dy < board_router_min_segment_mm ||
           std::abs(dx - dy) < board_router_min_segment_mm;
}

[[nodiscard]] std::vector<BoardPoint> normalized_path(std::initializer_list<BoardPoint> points) {
    auto path = std::vector<BoardPoint>{};
    for (const auto point : points) {
        if (path.empty() || !same_point(path.back(), point)) {
            path.push_back(point);
        }
    }
    return path;
}

void append_path_shape(std::vector<std::vector<BoardPoint>> &paths,
                       std::initializer_list<BoardPoint> points) {
    auto path = normalized_path(points);
    if (!path.empty()) {
        paths.push_back(std::move(path));
    }
}

[[nodiscard]] std::vector<std::vector<BoardPoint>> octilinear_path_shapes(BoardPoint start,
                                                                          BoardPoint end) {
    auto paths = std::vector<std::vector<BoardPoint>>{};
    if (same_point(start, end)) {
        paths.push_back(std::vector{start});
        return paths;
    }

    if (octilinear_segment(start, end)) {
        paths.push_back(std::vector{start, end});
    }

    const auto dx = std::abs(end.x_mm() - start.x_mm());
    const auto dy = std::abs(end.y_mm() - start.y_mm());
    if (dx < board_router_min_segment_mm || dy < board_router_min_segment_mm) {
        return paths;
    }

    const auto corner_a = BoardPoint{end.x_mm(), start.y_mm()};
    const auto corner_b = BoardPoint{start.x_mm(), end.y_mm()};
    append_path_shape(paths, {start, corner_a, end});
    append_path_shape(paths, {start, corner_b, end});

    const auto mid_x0 = BoardPoint{(start.x_mm() + end.x_mm()) / 2.0, start.y_mm()};
    const auto mid_x1 = BoardPoint{(start.x_mm() + end.x_mm()) / 2.0, end.y_mm()};
    append_path_shape(paths, {start, mid_x0, mid_x1, end});

    const auto mid_y0 = BoardPoint{start.x_mm(), (start.y_mm() + end.y_mm()) / 2.0};
    const auto mid_y1 = BoardPoint{end.x_mm(), (start.y_mm() + end.y_mm()) / 2.0};
    append_path_shape(paths, {start, mid_y0, mid_y1, end});
    return paths;
}

[[nodiscard]] BoardLayerSide layer_side(const Board &board, BoardLayerId layer) {
    return board.get(layer).side();
}

[[nodiscard]] bool layer_scope_permits(NetClassLayerScope scope, BoardLayerSide side) {
    switch (scope) {
    case NetClassLayerScope::AnyCopper:
        return true;
    case NetClassLayerScope::OuterOnly:
        return side == BoardLayerSide::Top || side == BoardLayerSide::Bottom;
    case NetClassLayerScope::InnerOnly:
        return side == BoardLayerSide::Inner;
    case NetClassLayerScope::TopOnly:
        return side == BoardLayerSide::Top;
    case NetClassLayerScope::BottomOnly:
        return side == BoardLayerSide::Bottom;
    }
    return true;
}

[[nodiscard]] std::vector<BoardLayerId>
via_candidate_layers(const Board &board, NetId net, BoardPoint position, BoardLayerId start_layer,
                     BoardLayerId end_layer, const BoardRouteParameters &params) {
    return detail::via_copper_layers(board, BoardVia{net, position, start_layer, end_layer,
                                                     params.via_drill_mm, params.via_diameter_mm});
}

[[nodiscard]] bool layer_in_allowed_parameters(BoardLayerId layer,
                                               const BoardRouteParameters &params) {
    return std::find(params.allowed_layers.begin(), params.allowed_layers.end(), layer) !=
           params.allowed_layers.end();
}

[[nodiscard]] bool layers_in_allowed_parameters(const std::vector<BoardLayerId> &layers,
                                                const BoardRouteParameters &params) {
    return std::all_of(layers.begin(), layers.end(), [&params](BoardLayerId layer) {
        return layer_in_allowed_parameters(layer, params);
    });
}

[[nodiscard]] std::optional<BoardLayerId>
first_allowed_layer(const std::vector<BoardLayerId> &layers, const BoardRouteParameters &params) {
    const auto match = std::find_if(layers.begin(), layers.end(), [&params](BoardLayerId layer) {
        return layer_in_allowed_parameters(layer, params);
    });
    if (match == layers.end()) {
        return std::nullopt;
    }
    return *match;
}

[[nodiscard]] double track_width_for_segment(const Board &board, BoardLayerId layer,
                                             BoardPoint start, BoardPoint end,
                                             const BoardRouteParameters &params) {
    return detail::BoardRoomRuleResolver{board}.effective_track_width_mm(
        layer, std::vector{start, end}, params.track_width_mm);
}

[[nodiscard]] std::string fixed_millimeters(double value) {
    auto stream = std::ostringstream{};
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

[[nodiscard]] std::string escape_room_name(const Board &board, ComponentId component,
                                           const ComponentPlacement &placement) {
    const auto &reference = board.circuit().get(component).reference().value();
    return "escape-" + reference + "-at-" + fixed_millimeters(placement.position().x_mm()) + "-" +
           fixed_millimeters(placement.position().y_mm());
}

[[nodiscard]] std::vector<BoardPoint> escape_direction_points(BoardPoint pad,
                                                              BoardPoint placement_origin) {
    const auto dx = pad.x_mm() - placement_origin.x_mm();
    const auto dy = pad.y_mm() - placement_origin.y_mm();
    const auto horizontal = std::abs(dx) >= std::abs(dy);
    const auto primary_x = horizontal ? (dx < 0.0 ? -1.0 : 1.0) : 0.0;
    const auto primary_y = horizontal ? 0.0 : (dy < 0.0 ? -1.0 : 1.0);
    const auto secondary_x = horizontal ? 0.0 : (dx < 0.0 ? -1.0 : 1.0);
    const auto secondary_y = horizontal ? (dy < 0.0 ? -1.0 : 1.0) : 0.0;

    return std::vector{
        BoardPoint{pad.x_mm() + (primary_x * board_escape_stub_length_mm),
                   pad.y_mm() + (primary_y * board_escape_stub_length_mm)},
        BoardPoint{pad.x_mm() - (primary_x * board_escape_stub_length_mm),
                   pad.y_mm() - (primary_y * board_escape_stub_length_mm)},
        BoardPoint{pad.x_mm() + (secondary_x * board_escape_stub_length_mm),
                   pad.y_mm() + (secondary_y * board_escape_stub_length_mm)},
        BoardPoint{pad.x_mm() - (secondary_x * board_escape_stub_length_mm),
                   pad.y_mm() - (secondary_y * board_escape_stub_length_mm)},
    };
}

struct EscapePadCandidate {
    std::size_t result_index = 0;
    BoardLayerId layer;
    std::vector<BoardPoint> endpoints;
    std::vector<BoardPoint> room_points;
    BoardRouteParameters params;
};

[[nodiscard]] BoardOutline escape_room_outline(const std::vector<BoardEscapePadResult> &pads,
                                               const std::vector<EscapePadCandidate> &candidates,
                                               const BoardDesignRules &rules) {
    auto min_x = std::numeric_limits<double>::infinity();
    auto min_y = std::numeric_limits<double>::infinity();
    auto max_x = -std::numeric_limits<double>::infinity();
    auto max_y = -std::numeric_limits<double>::infinity();

    const auto include = [&](BoardPoint point) {
        min_x = std::min(min_x, point.x_mm());
        min_y = std::min(min_y, point.y_mm());
        max_x = std::max(max_x, point.x_mm());
        max_y = std::max(max_y, point.y_mm());
    };

    for (const auto &candidate : candidates) {
        include(pads[candidate.result_index].pad_position);
        for (const auto endpoint : candidate.endpoints) {
            include(endpoint);
        }
        for (const auto point : candidate.room_points) {
            include(point);
        }
    }

    auto expansion_basis = std::max(rules.minimum_track_width_mm(), rules.copper_clearance_mm());
    for (const auto &candidate : candidates) {
        expansion_basis = std::max(expansion_basis, candidate.params.track_width_mm);
    }
    const auto expansion = expansion_basis / 2.0;
    return BoardOutline::rectangle(
        BoardPoint{min_x - expansion, min_y - expansion},
        BoardSize{(max_x - min_x) + (2.0 * expansion), (max_y - min_y) + (2.0 * expansion)});
}

void apply_escape_room_overrides(const Board &board, BoardRoom &room) {
    const auto board_clearance = board.design_rules().copper_clearance_mm();
    if (detail::maximum_required_copper_clearance(board) <=
        board_clearance + detail::board_drc_epsilon) {
        room.set_copper_clearance_mm(board_clearance);
    }
}

[[nodiscard]] BoardSpatialQueryShape escape_segment_shape(NetId net, BoardLayerId layer,
                                                          BoardPoint start, BoardPoint end,
                                                          double width_mm) {
    return BoardSpatialQueryShape{
        BoardSpatialQueryShapeKind::Segment,
        net,
        std::vector{layer},
        std::vector{start, end},
        width_mm / 2.0,
        BoardClearanceKind::Track,
        BoardKeepoutRestriction::Copper,
    };
}

[[nodiscard]] BoardSpatialQueryShape committed_via_shape(const Board &board, const BoardVia &via) {
    return BoardSpatialQueryShape{
        BoardSpatialQueryShapeKind::Disc,      via.net(),
        detail::via_copper_layers(board, via), std::vector{via.position()},
        via.annular_diameter_mm() / 2.0,       BoardClearanceKind::Via,
        BoardKeepoutRestriction::Via,
    };
}

} // namespace

[[nodiscard]] BoardViaSize resolve_via_size(const Board &board, NetId net,
                                            double fallback_drill_diameter_mm,
                                            double fallback_annular_diameter_mm) {
    const auto rules = resolve_net_class_rules(board.circuit(), net);
    const auto &design = board.design_rules();
    return {
        std::max(rules.via_drill_mm.value_or(fallback_drill_diameter_mm),
                 design.minimum_via_drill_diameter_mm()),
        std::max(rules.via_diameter_mm.value_or(fallback_annular_diameter_mm),
                 design.minimum_via_annular_diameter_mm()),
    };
}

BoardRouter::BoardRouter(Board &board, const FootprintLibrary &footprints)
    : board_{&board}, footprints_{footprints} {}

[[nodiscard]] BoardSpatialIndex &BoardRouter::index() const {
    if (!index_.has_value()) {
        index_.emplace(*board_, footprints_);
    }
    return index_.value();
}

[[nodiscard]] bool BoardEscapeResult::complete() const noexcept {
    return !pads.empty() &&
           std::all_of(pads.begin(), pads.end(),
                       [](const BoardEscapePadResult &pad) { return pad.escaped; });
}

[[nodiscard]] bool BoardRouter::layer_allowed(NetId net, BoardLayerId layer) const {
    if (board_->get(layer).role() != BoardLayerRole::Copper) {
        return false;
    }
    const auto rules = resolve_net_class_rules(board_->circuit(), net);
    if (!rules.allowed_layer_names.empty()) {
        return std::find(rules.allowed_layer_names.begin(), rules.allowed_layer_names.end(),
                         board_->get(layer).name()) != rules.allowed_layer_names.end();
    }
    return layer_scope_permits(rules.layer_scope, layer_side(*board_, layer));
}

void BoardRouter::require_routable_layer(BoardLayerId layer) const {
    if (board_->get(layer).role() != BoardLayerRole::Copper) {
        throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                  "Board router endpoint layer must be a copper layer",
                                  EntityRef::board_layer(layer)};
    }
}

[[nodiscard]] BoardRouteParameters BoardRouter::resolve_parameters(NetId net) const {
    const auto rules = resolve_net_class_rules(board_->circuit(), net);
    const auto &design = board_->design_rules();
    const auto via_size = resolve_via_size(*board_, net, design.minimum_via_drill_diameter_mm(),
                                           design.minimum_via_annular_diameter_mm());

    auto params = BoardRouteParameters{};
    params.track_width_mm = std::max(rules.track_width_mm.value_or(design.minimum_track_width_mm()),
                                     design.minimum_track_width_mm());
    params.via_drill_mm = via_size.drill_diameter_mm;
    params.via_diameter_mm = via_size.annular_diameter_mm;

    for (std::size_t index = 0; index < board_->all<volt::BoardLayerId>().size(); ++index) {
        const auto layer = BoardLayerId{index};
        if (layer_allowed(net, layer)) {
            params.allowed_layers.push_back(layer);
        }
    }
    return params;
}

[[nodiscard]] BoardTrackRouteResult BoardRouter::add_track(BoardTrackRouteRequest request) {
    const auto net = queries::resolve_board_route_net(*board_, request, footprints_);
    auto points = std::vector<BoardPoint>{};
    points.reserve(request.endpoints.size());
    for (const auto &endpoint : request.endpoints) {
        points.push_back(endpoint.position);
    }

    const auto track =
        board_->add_track(BoardTrack{net, request.layer, std::move(points), request.width_mm});
    index_.reset();
    return BoardTrackRouteResult{track, net};
}

[[nodiscard]] std::vector<BoardRouter::Candidate>
BoardRouter::pattern_candidates(const BoardRouteRequest &request,
                                const BoardRouteParameters &params) const {
    const auto start = request.start;
    const auto end = request.end;
    const auto start_layer = request.start_layer;
    const auto end_layer = request.end_layer;

    const auto append_segments = [](Candidate &candidate, BoardLayerId layer,
                                    const std::vector<BoardPoint> &path) {
        for (std::size_t index = 1; index < path.size(); ++index) {
            candidate.segments.push_back(SegmentStep{layer, path[index - 1], path[index]});
        }
    };

    auto candidates = std::vector<Candidate>{};

    if (start_layer == end_layer) {
        for (const auto &path : octilinear_path_shapes(start, end)) {
            auto candidate = Candidate{};
            append_segments(candidate, start_layer, path);
            candidates.push_back(std::move(candidate));
        }
        return candidates;
    }

    // Layer change: place a via at a transition point legal on both layers, then route on
    // the start layer up to the via and on the end layer onward. Deterministic transition
    // order: end point, start point, then the two L corners.
    const auto corner_a = BoardPoint{end.x_mm(), start.y_mm()};
    const auto corner_b = BoardPoint{start.x_mm(), end.y_mm()};
    const auto transitions = std::vector<BoardPoint>{end, start, corner_a, corner_b};
    for (const auto transition : transitions) {
        const auto via_layers =
            via_candidate_layers(*board_, request.net, transition, start_layer, end_layer, params);
        if (!layers_in_allowed_parameters(via_layers, params)) {
            continue;
        }
        for (const auto &start_path : octilinear_path_shapes(start, transition)) {
            for (const auto &end_path : octilinear_path_shapes(transition, end)) {
                auto candidate = Candidate{};
                append_segments(candidate, start_layer, start_path);
                candidate.vias.push_back(ViaStep{transition, start_layer, end_layer});
                append_segments(candidate, end_layer, end_path);
                candidates.push_back(std::move(candidate));
            }
        }
    }
    return candidates;
}

[[nodiscard]] std::vector<BoardRouter::Candidate>
BoardRouter::walk_around_candidates(const BoardRouteRequest &request,
                                    const BoardRouteParameters &params) const {
    const auto start = request.start;
    const auto end = request.end;
    const auto start_layer = request.start_layer;
    const auto end_layer = request.end_layer;
    const auto append_segments = [](Candidate &candidate, BoardLayerId layer,
                                    const std::vector<BoardPoint> &path) {
        for (std::size_t index = 1; index < path.size(); ++index) {
            candidate.segments.push_back(SegmentStep{layer, path[index - 1], path[index]});
        }
    };

    const auto dx = end.x_mm() - start.x_mm();
    const auto dy = end.y_mm() - start.y_mm();
    const auto length = std::sqrt(dx * dx + dy * dy);
    auto candidates = std::vector<Candidate>{};
    if (length < board_router_min_segment_mm) {
        return candidates;
    }

    // Unit perpendicular to the direct line; step size scales with the connection length so
    // the bounded sweep covers obstacles of comparable size deterministically.
    const auto perp_x = -dy / length;
    const auto perp_y = dx / length;
    const auto step_mm = std::max(length / 8.0, params.track_width_mm * 4.0);
    const auto mid =
        BoardPoint{(start.x_mm() + end.x_mm()) / 2.0, (start.y_mm() + end.y_mm()) / 2.0};

    // Deterministic order: expanding offsets, each positive side before the matching negative.
    for (int step = 1; step <= board_router_walk_around_steps; ++step) {
        for (const int sign : {1, -1}) {
            const auto offset = static_cast<double>(step) * step_mm * static_cast<double>(sign);
            const auto waypoint =
                BoardPoint{mid.x_mm() + perp_x * offset, mid.y_mm() + perp_y * offset};
            if (start_layer == end_layer) {
                for (const auto &start_path : octilinear_path_shapes(start, waypoint)) {
                    for (const auto &end_path : octilinear_path_shapes(waypoint, end)) {
                        auto candidate = Candidate{};
                        append_segments(candidate, start_layer, start_path);
                        append_segments(candidate, start_layer, end_path);
                        candidates.push_back(std::move(candidate));
                    }
                }
            } else {
                const auto via_layers = via_candidate_layers(*board_, request.net, waypoint,
                                                             start_layer, end_layer, params);
                if (!layers_in_allowed_parameters(via_layers, params)) {
                    continue;
                }
                for (const auto &start_path : octilinear_path_shapes(start, waypoint)) {
                    for (const auto &end_path : octilinear_path_shapes(waypoint, end)) {
                        auto candidate = Candidate{};
                        append_segments(candidate, start_layer, start_path);
                        candidate.vias.push_back(ViaStep{waypoint, start_layer, end_layer});
                        append_segments(candidate, end_layer, end_path);
                        candidates.push_back(std::move(candidate));
                    }
                }
            }
        }
    }
    return candidates;
}

[[nodiscard]] std::optional<std::vector<BoardSpatialBlocker>>
BoardRouter::evaluate(const Candidate &candidate, const BoardRouteRequest &request,
                      const BoardRouteParameters &params) const {
    auto blockers = std::vector<BoardSpatialBlocker>{};
    for (const auto &segment : candidate.segments) {
        if (same_point(segment.start, segment.end)) {
            continue;
        }
        if (!octilinear_segment(segment.start, segment.end)) {
            return blockers;
        }
        if (!layer_in_allowed_parameters(segment.layer, params)) {
            return blockers;
        }
        const auto width_mm =
            track_width_for_segment(*board_, segment.layer, segment.start, segment.end, params);
        const auto shape = BoardSpatialQueryShape{
            BoardSpatialQueryShapeKind::Segment,     request.net,    std::vector{segment.layer},
            std::vector{segment.start, segment.end}, width_mm / 2.0, BoardClearanceKind::Track,
            BoardKeepoutRestriction::Copper,
        };
        const auto result = index().query_legality(shape);
        if (!result.legal) {
            return result.blockers;
        }
    }

    for (const auto &via : candidate.vias) {
        const auto layers = via_candidate_layers(*board_, request.net, via.position,
                                                 via.start_layer, via.end_layer, params);
        if (!layers_in_allowed_parameters(layers, params)) {
            return blockers;
        }
        const auto shape = BoardSpatialQueryShape{
            BoardSpatialQueryShapeKind::Disc,
            request.net,
            layers,
            std::vector{via.position},
            params.via_diameter_mm / 2.0,
            BoardClearanceKind::Via,
            BoardKeepoutRestriction::Via,
        };
        const auto result = index().query_legality(shape);
        if (!result.legal) {
            return result.blockers;
        }
    }
    return std::nullopt;
}

void BoardRouter::commit(const Candidate &candidate, const BoardRouteRequest &request,
                         const BoardRouteParameters &params, BoardRouteResult &result) {
    for (const auto &segment : candidate.segments) {
        if (same_point(segment.start, segment.end)) {
            continue;
        }
        if (!octilinear_segment(segment.start, segment.end)) {
            throw KernelLogicError{
                ErrorCode::InvalidState,
                "BoardRouter cannot commit a non-octilinear assisted route segment"};
        }
        const auto width_mm =
            track_width_for_segment(*board_, segment.layer, segment.start, segment.end, params);
        const auto track_id = board_->add_track(BoardTrack{
            request.net, segment.layer, std::vector{segment.start, segment.end}, width_mm});
        index().insert(
            escape_segment_shape(request.net, segment.layer, segment.start, segment.end, width_mm));
        result.tracks.push_back(track_id);
    }

    for (const auto &via : candidate.vias) {
        const auto via_id =
            board_->add_via(BoardVia{request.net, via.position, via.start_layer, via.end_layer,
                                     params.via_drill_mm, params.via_diameter_mm});
        index().insert(committed_via_shape(*board_, board_->get(via_id)));
        result.vias.push_back(via_id);
    }
}

[[nodiscard]] BoardRouteResult BoardRouter::connect(const BoardRouteRequest &request) {
    require_routable_layer(request.start_layer);
    require_routable_layer(request.end_layer);
    const auto params = resolve_parameters(request.net);

    auto result = BoardRouteResult{};
    // A route on a class-disallowed layer would only trade a clearance failure for a
    // disallowed-layer DRC diagnostic, so reject those endpoints up front as no route.
    const auto start_ok = std::find(params.allowed_layers.begin(), params.allowed_layers.end(),
                                    request.start_layer) != params.allowed_layers.end();
    const auto end_ok = std::find(params.allowed_layers.begin(), params.allowed_layers.end(),
                                  request.end_layer) != params.allowed_layers.end();
    if (!start_ok || !end_ok) {
        return result;
    }

    auto candidates = pattern_candidates(request, params);
    auto walk_around = walk_around_candidates(request, params);
    candidates.insert(candidates.end(), std::make_move_iterator(walk_around.begin()),
                      std::make_move_iterator(walk_around.end()));

    auto primary_blockers = std::optional<std::vector<BoardSpatialBlocker>>{};
    for (const auto &candidate : candidates) {
        auto rejected = evaluate(candidate, request, params);
        if (!rejected.has_value()) {
            commit(candidate, request, params, result);
            result.routed = true;
            return result;
        }
        if (!primary_blockers.has_value()) {
            primary_blockers = std::move(rejected.value());
        }
    }

    result.routed = false;
    result.blockers = std::move(primary_blockers).value_or(std::vector<BoardSpatialBlocker>{});
    return result;
}

[[nodiscard]] BoardEscapeResult BoardRouter::escape(ComponentId component) {
    static_cast<void>(board_->circuit().get(component));

    auto result = BoardEscapeResult{};
    result.component = component;
    const auto placement_id = queries::placement_for_component(*board_, component);
    if (!placement_id.has_value()) {
        throw KernelArgumentError{ErrorCode::InvalidState,
                                  "Cannot escape component without a board placement",
                                  EntityRef::component(component)};
    }
    result.placement = placement_id;
    const auto &placement = board_->get(placement_id.value());

    const auto &selected_part = volt::queries::selected_physical_part(board_->circuit(), component);
    if (!selected_part.has_value()) {
        throw KernelArgumentError{ErrorCode::InvalidState,
                                  "Cannot escape component without a selected physical part",
                                  EntityRef::component(component)};
    }

    const auto resolution_footprints = queries::board_resolution_footprints(*board_, footprints_);
    const auto footprint_resolution =
        resolve_footprint(selected_part.value(), resolution_footprints);
    const auto *definition = footprint_resolution.definition();
    if (definition == nullptr) {
        throw KernelArgumentError{ErrorCode::InvalidState,
                                  "Cannot escape component with an unresolved footprint",
                                  EntityRef::component(component)};
    }

    const auto pad_resolutions = queries::resolve_pads(*board_, resolution_footprints);
    auto candidates = std::vector<EscapePadCandidate>{};
    auto room_layers = std::vector<BoardLayerId>{};

    for (std::size_t pad_index = 0; pad_index < definition->pad_count(); ++pad_index) {
        const auto pad_id = FootprintPadId{pad_index};
        const auto &pad = definition->pad(pad_id);
        const auto *resolution =
            detail::find_board_pad_resolution(pad_resolutions, placement_id.value(), pad_id);

        auto pad_result = BoardEscapePadResult{};
        pad_result.pad_label = pad.label();
        pad_result.pad = pad_id;
        if (resolution != nullptr) {
            pad_result.pad_position = resolution->position();
            pad_result.pin = resolution->pin();
            pad_result.net = resolution->net();
        } else {
            pad_result.pad_position = detail::transform_footprint_point(placement, pad.position());
        }

        if (resolution == nullptr || resolution->status() != PadResolutionStatus::Connected ||
            !resolution->net().has_value()) {
            pad_result.failure_reason = BoardEscapeFailureReason::PadUnconnected;
            result.pads.push_back(std::move(pad_result));
            continue;
        }

        const auto layers = detail::pad_copper_layers(*board_, pad, placement.side());
        if (layers.empty()) {
            pad_result.failure_reason = BoardEscapeFailureReason::NoCopperLayer;
            result.pads.push_back(std::move(pad_result));
            continue;
        }

        const auto params = resolve_parameters(resolution->net().value());
        const auto layer = first_allowed_layer(layers, params);
        if (!layer.has_value()) {
            pad_result.failure_reason = BoardEscapeFailureReason::DisallowedLayer;
            result.pads.push_back(std::move(pad_result));
            continue;
        }

        const auto result_index = result.pads.size();
        result.pads.push_back(std::move(pad_result));
        candidates.push_back(EscapePadCandidate{
            result_index,
            layer.value(),
            escape_direction_points(result.pads[result_index].pad_position, placement.position()),
            detail::transformed_pad_body_corners(placement, pad),
            params,
        });
        detail::append_unique_layer(room_layers, layer.value());
    }

    if (!candidates.empty()) {
        auto room = BoardRoom{
            escape_room_name(*board_, component, placement),
            escape_room_outline(result.pads, candidates, board_->design_rules()),
            room_layers,
            0,
        };
        apply_escape_room_overrides(*board_, room);
        result.room = board_->add_room(std::move(room));
        index_.reset();
    }

    for (const auto &candidate : candidates) {
        auto &pad = result.pads[candidate.result_index];
        auto primary_blockers = std::optional<std::vector<BoardSpatialBlocker>>{};

        for (const auto endpoint : candidate.endpoints) {
            const auto width_mm = track_width_for_segment(
                *board_, candidate.layer, pad.pad_position, endpoint, candidate.params);
            const auto shape = escape_segment_shape(pad.net.value(), candidate.layer,
                                                    pad.pad_position, endpoint, width_mm);
            const auto legality = index().query_legality(shape);
            if (!legality.legal) {
                if (!primary_blockers.has_value()) {
                    primary_blockers = legality.blockers;
                }
                continue;
            }

            const auto track_id =
                board_->add_track(BoardTrack{pad.net.value(), candidate.layer,
                                             std::vector{pad.pad_position, endpoint}, width_mm});
            index().insert(shape);
            pad.endpoint = endpoint;
            pad.escaped = true;
            pad.failure_reason = BoardEscapeFailureReason::None;
            pad.tracks.push_back(track_id);
            break;
        }

        if (!pad.escaped) {
            pad.failure_reason = BoardEscapeFailureReason::NoLegalCandidate;
            pad.blockers = std::move(primary_blockers).value_or(std::vector<BoardSpatialBlocker>{});
        }
    }

    return result;
}

} // namespace volt
