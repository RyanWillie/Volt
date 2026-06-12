#include <volt/pcb/board_router.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <volt/circuit/net_class_resolution.hpp>
#include <volt/pcb/board_copper.hpp>

namespace volt {

namespace {

/** Maximum perpendicular jog steps explored on each side during walk-around. */
constexpr int board_router_walk_around_steps = 16;

/** Board-space epsilon below which a segment is treated as degenerate and dropped. */
constexpr double board_router_min_segment_mm = 1.0e-6;

[[nodiscard]] bool same_point(BoardPoint lhs, BoardPoint rhs) {
    return detail::board_distance(lhs, rhs) < board_router_min_segment_mm;
}

[[nodiscard]] BoardLayerSide layer_side(const Board &board, BoardLayerId layer) {
    return board.layer(layer).side();
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

[[nodiscard]] bool room_contains_layer(const BoardRoom &room, BoardLayerId layer) {
    return std::find(room.layers().begin(), room.layers().end(), layer) != room.layers().end();
}

[[nodiscard]] bool room_has_higher_precedence(const Board &board, BoardRoomId candidate,
                                              BoardRoomId current) {
    const auto candidate_priority = board.room(candidate).priority();
    const auto current_priority = board.room(current).priority();
    return candidate_priority > current_priority ||
           (candidate_priority == current_priority && candidate.index() < current.index());
}

[[nodiscard]] std::optional<BoardRoomId> track_width_room(const Board &board, BoardLayerId layer,
                                                          BoardPoint start, BoardPoint end,
                                                          double width_mm) {
    auto result = std::optional<BoardRoomId>{};
    for (std::size_t index = 0; index < board.room_count(); ++index) {
        const auto room_id = BoardRoomId{index};
        const auto &room = board.room(room_id);
        if (!room.track_width_mm().has_value() || !room_contains_layer(room, layer) ||
            !detail::outline_contains_segment(room.outline(), start, end, width_mm / 2.0, 0.0)) {
            continue;
        }
        if (!result.has_value() || room_has_higher_precedence(board, room_id, result.value())) {
            result = room_id;
        }
    }
    return result;
}

[[nodiscard]] double track_width_for_segment(const Board &board, BoardLayerId layer,
                                             BoardPoint start, BoardPoint end,
                                             const BoardRouteParameters &params) {
    auto width_mm = params.track_width_mm;
    while (true) {
        const auto room = track_width_room(board, layer, start, end, width_mm);
        if (!room.has_value()) {
            return width_mm;
        }
        const auto room_width_mm = board.room(room.value()).track_width_mm().value();
        if (room_width_mm <= width_mm + detail::board_drc_epsilon) {
            return width_mm;
        }
        width_mm = room_width_mm;
    }
}

} // namespace

BoardRouter::BoardRouter(Board &board, const FootprintLibrary &footprints)
    : board_{&board}, index_{board, footprints} {}

[[nodiscard]] bool BoardRouter::layer_allowed(NetId net, BoardLayerId layer) const {
    if (board_->layer(layer).role() != BoardLayerRole::Copper) {
        return false;
    }
    const auto rules = resolve_net_class_rules(board_->circuit(), net);
    if (!rules.allowed_layer_names.empty()) {
        return std::find(rules.allowed_layer_names.begin(), rules.allowed_layer_names.end(),
                         board_->layer(layer).name()) != rules.allowed_layer_names.end();
    }
    return layer_scope_permits(rules.layer_scope, layer_side(*board_, layer));
}

void BoardRouter::require_routable_layer(BoardLayerId layer) const {
    if (board_->layer(layer).role() != BoardLayerRole::Copper) {
        throw std::invalid_argument{"Board router endpoint layer must be a copper layer"};
    }
}

[[nodiscard]] BoardRouteParameters BoardRouter::resolve_parameters(NetId net) const {
    const auto rules = resolve_net_class_rules(board_->circuit(), net);
    const auto &design = board_->design_rules();

    auto params = BoardRouteParameters{};
    params.track_width_mm = std::max(rules.track_width_mm.value_or(design.minimum_track_width_mm()),
                                     design.minimum_track_width_mm());
    params.via_drill_mm =
        std::max(rules.via_drill_mm.value_or(design.minimum_via_drill_diameter_mm()),
                 design.minimum_via_drill_diameter_mm());
    params.via_diameter_mm =
        std::max(rules.via_diameter_mm.value_or(design.minimum_via_annular_diameter_mm()),
                 design.minimum_via_annular_diameter_mm());

    for (std::size_t index = 0; index < board_->layer_count(); ++index) {
        const auto layer = BoardLayerId{index};
        if (layer_allowed(net, layer)) {
            params.allowed_layers.push_back(layer);
        }
    }
    return params;
}

[[nodiscard]] std::vector<BoardRouter::Candidate>
BoardRouter::pattern_candidates(const BoardRouteRequest &request,
                                const BoardRouteParameters &params) const {
    const auto start = request.start;
    const auto end = request.end;
    const auto start_layer = request.start_layer;
    const auto end_layer = request.end_layer;
    const auto corner_a = BoardPoint{end.x_mm(), start.y_mm()};
    const auto corner_b = BoardPoint{start.x_mm(), end.y_mm()};
    const auto mid_x = BoardPoint{(start.x_mm() + end.x_mm()) / 2.0, start.y_mm()};
    const auto mid_y = BoardPoint{(start.x_mm() + end.x_mm()) / 2.0, end.y_mm()};

    auto candidates = std::vector<Candidate>{};

    if (start_layer == end_layer) {
        // Straight.
        candidates.push_back(Candidate{{SegmentStep{start_layer, start, end}}, {}});
        // L, two corner orientations.
        candidates.push_back(Candidate{
            {SegmentStep{start_layer, start, corner_a}, SegmentStep{start_layer, corner_a, end}},
            {}});
        candidates.push_back(Candidate{
            {SegmentStep{start_layer, start, corner_b}, SegmentStep{start_layer, corner_b, end}},
            {}});
        // Z, two mid-jog orientations.
        candidates.push_back(Candidate{{SegmentStep{start_layer, start, mid_x},
                                        SegmentStep{start_layer, mid_x, mid_y},
                                        SegmentStep{start_layer, mid_y, end}},
                                       {}});
        const auto mid_y0 = BoardPoint{start.x_mm(), (start.y_mm() + end.y_mm()) / 2.0};
        const auto mid_y1 = BoardPoint{end.x_mm(), (start.y_mm() + end.y_mm()) / 2.0};
        candidates.push_back(Candidate{{SegmentStep{start_layer, start, mid_y0},
                                        SegmentStep{start_layer, mid_y0, mid_y1},
                                        SegmentStep{start_layer, mid_y1, end}},
                                       {}});
        return candidates;
    }

    // Layer change: place a via at a transition point legal on both layers, then route on
    // the start layer up to the via and on the end layer onward. Deterministic transition
    // order: end point, start point, then the two L corners.
    const auto transitions = std::vector<BoardPoint>{end, start, corner_a, corner_b};
    for (const auto transition : transitions) {
        const auto via_layers =
            via_candidate_layers(*board_, request.net, transition, start_layer, end_layer, params);
        if (!layers_in_allowed_parameters(via_layers, params)) {
            continue;
        }
        auto candidate = Candidate{};
        candidate.segments.push_back(SegmentStep{start_layer, start, transition});
        candidate.vias.push_back(ViaStep{transition, start_layer, end_layer});
        candidate.segments.push_back(SegmentStep{end_layer, transition, end});
        candidates.push_back(std::move(candidate));
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
            auto candidate = Candidate{};
            if (start_layer == end_layer) {
                candidate.segments.push_back(SegmentStep{start_layer, start, waypoint});
                candidate.segments.push_back(SegmentStep{start_layer, waypoint, end});
            } else {
                const auto via_layers = via_candidate_layers(*board_, request.net, waypoint,
                                                             start_layer, end_layer, params);
                if (!layers_in_allowed_parameters(via_layers, params)) {
                    continue;
                }
                candidate.segments.push_back(SegmentStep{start_layer, start, waypoint});
                candidate.vias.push_back(ViaStep{waypoint, start_layer, end_layer});
                candidate.segments.push_back(SegmentStep{end_layer, waypoint, end});
            }
            candidates.push_back(std::move(candidate));
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
        const auto result = index_.query_legality(shape);
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
        const auto result = index_.query_legality(shape);
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
        const auto width_mm =
            track_width_for_segment(*board_, segment.layer, segment.start, segment.end, params);
        const auto previous_geometry_mutation_count = board_->geometry_mutation_count();
        const auto track_id = board_->add_track(BoardTrack{
            request.net, segment.layer, std::vector{segment.start, segment.end}, width_mm});
        index_.insert_after_board_mutation(
            BoardSpatialQueryShape{
                BoardSpatialQueryShapeKind::Segment,
                request.net,
                std::vector{segment.layer},
                std::vector{segment.start, segment.end},
                width_mm / 2.0,
                BoardClearanceKind::Track,
                BoardKeepoutRestriction::Copper,
            },
            previous_geometry_mutation_count);
        result.tracks.push_back(track_id);
    }

    for (const auto &via : candidate.vias) {
        const auto previous_geometry_mutation_count = board_->geometry_mutation_count();
        const auto via_id =
            board_->add_via(BoardVia{request.net, via.position, via.start_layer, via.end_layer,
                                     params.via_drill_mm, params.via_diameter_mm});
        index_.insert_after_board_mutation(
            BoardSpatialQueryShape{
                BoardSpatialQueryShapeKind::Disc,
                request.net,
                detail::via_copper_layers(*board_, board_->via(via_id)),
                std::vector{via.position},
                params.via_diameter_mm / 2.0,
                BoardClearanceKind::Via,
                BoardKeepoutRestriction::Via,
            },
            previous_geometry_mutation_count);
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

} // namespace volt
