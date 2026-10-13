#include <volt/pcb/routing/board_router.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <tuple>
#include <type_traits>

#include <volt/circuit/constraints/net_class_resolution.hpp>
#include <volt/core/errors.hpp>
#include <volt/pcb/queries/board_queries.hpp>

#include "../copper/board_room_rules.hpp"
#include "route_detail.hpp"

namespace volt {
namespace route_detail {

void invalid(const std::string &message) {
    throw KernelArgumentError{ErrorCode::InvalidArgument, message};
}

void owner(bool matches) {
    if (!matches) {
        throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                  "Route reference belongs to another owner"};
    }
}

void positive(double value) {
    if (!std::isfinite(value) || value <= 0.0) {
        invalid("Route sizing must be finite and positive");
    }
}

bool contains(const std::vector<BoardLayerId> &layers, BoardLayerId layer) {
    return std::find(layers.begin(), layers.end(), layer) != layers.end();
}

void layer_ref(const Board &board, RouteLayerRef ref) {
    owner(ref.owner == &board);
    const auto &layer = board.get(ref.layer);
    if (!layer.enabled() || layer.role() != BoardLayerRole::Copper ||
        !board.layer_stack().has_value() || !contains(board.layer_stack()->layers(), ref.layer)) {
        invalid("Route layer must be enabled copper in the committed stack");
    }
}

void corner(CornerStyle style) {
    switch (style) {
    case CornerStyle::Chamfer45:
    case CornerStyle::Orthogonal:
    case CornerStyle::Direct:
        return;
    }
    invalid("Unknown route corner style");
}

struct EndpointTruth {
    std::optional<BoardPoint> position;
    std::optional<NetId> net;
    std::vector<BoardLayerId> layers;
    bool identity = false;
    std::string failure;
};

EndpointTruth endpoint_truth(const Board &board, const ResolvedBoardView &resolved,
                             const RouteEndpoint &endpoint,
                             const std::vector<PadResolution> &pads) {
    return std::visit(
        [&](const auto &value) -> EndpointTruth {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::same_as<T, RoutePoint>) {
                return {value.position, std::nullopt, {}, false, {}};
            } else {
                owner(value.owner == &board);
                if constexpr (std::same_as<T, RouteViaRef>) {
                    const auto &via = board.get(value.via);
                    return {
                        via.position(), via.net(), detail::via_copper_layers(board, via), true, {}};
                } else if constexpr (std::same_as<T, RouteTrackRef>) {
                    const auto &track = board.get(value.track);
                    if (value.end != RouteTrackEnd::First && value.end != RouteTrackEnd::Last) {
                        invalid("Unknown route track end");
                    }
                    return {value.end == RouteTrackEnd::First ? track.points().front()
                                                              : track.points().back(),
                            track.net(),
                            {track.layer()},
                            true,
                            {}};
                } else {
                    const auto &placement = board.get(value.placement);
                    const auto *part = resolved.part(placement.component());
                    if (part == nullptr) {
                        return {std::nullopt,
                                std::nullopt,
                                {},
                                true,
                                "pad physical closure is unavailable"};
                    }
                    owner(part->physical_part().footprint() == value.footprint.ref());
                    const auto resolution =
                        resolve_footprint(part->physical_part(), resolved.footprints());
                    const auto *definition = resolution.definition();
                    if (definition == nullptr) {
                        return {
                            std::nullopt, std::nullopt, {}, true, "pad footprint is unavailable"};
                    }
                    owner(*definition == value.footprint);
                    const auto &pad = definition->pad(value.pad);
                    const auto match =
                        std::find_if(pads.begin(), pads.end(), [&](const auto &candidate) {
                            return candidate.placement() == value.placement &&
                                   candidate.pad() == value.pad;
                        });
                    if (match == pads.end() || !match->net().has_value()) {
                        return {detail::transform_footprint_point(placement, pad.position()),
                                std::nullopt,
                                detail::pad_copper_layers(board, pad, placement.side()), true,
                                "pad is not connected to a logical net"};
                    }
                    return {match->position(),
                            match->net(),
                            detail::pad_copper_layers(board, pad, placement.side()),
                            true,
                            {}};
                }
            }
        },
        endpoint);
}

RouteRuleSource board_source(RouteRuleField field) {
    auto source = RouteRuleSource{};
    source.field = field;
    return source;
}

RouteRuleSource class_source(const ResolvedNetClassRules &rules, RouteRuleField field) {
    auto source = board_source(field);
    source.kind = RouteRuleSourceKind::NetClassRule;
    source.net_class = rules.net_class;
    if (field == RouteRuleField::Width)
        source.derivation = rules.track_width_derivation;
    if (field == RouteRuleField::Clearance)
        source.derivation = rules.copper_clearance_derivation;
    return source;
}

RouteRuleSource room_source(BoardRoomId room, RouteRuleField field) {
    auto source = board_source(field);
    source.kind = RouteRuleSourceKind::RoomRule;
    source.room = room;
    return source;
}

RouteRuleValue floor_value(double board_floor, std::optional<double> class_floor,
                           const ResolvedNetClassRules &rules, RouteRuleField field) {
    auto value = RouteRuleValue{};
    value.default_mm = std::max(board_floor, class_floor.value_or(board_floor));
    value.value_mm = value.default_mm;
    if (board_floor == value.value_mm)
        value.sources.push_back(board_source(field));
    if (class_floor == value.value_mm)
        value.sources.push_back(class_source(rules, field));
    value.enforced_sources = value.sources;
    return value;
}

void override_value(RouteRuleValue &value, double requested, RouteRuleField field) {
    value.requested_mm = requested;
    value.value_mm = requested;
    auto source = board_source(field);
    source.kind = RouteRuleSourceKind::ExplicitOverride;
    value.sources = {source};
}

std::string mm(double value) {
    auto out = std::ostringstream{};
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(3) << value;
    return out.str();
}

std::string failure_name(RouteFailureKind kind) {
    switch (kind) {
    case RouteFailureKind::Blocked:
        return "blocked";
    case RouteFailureKind::EndpointUnreachable:
        return "endpoint unreachable";
    case RouteFailureKind::LayerNotAllowed:
        return "layer not allowed";
    case RouteFailureKind::RuleViolation:
        return "rule violation";
    case RouteFailureKind::RuleUnresolvable:
        return "rule unresolvable";
    case RouteFailureKind::GeometryUnresolvable:
        return "geometry unresolvable";
    case RouteFailureKind::NoCandidate:
        return "no candidate";
    }
    return {};
}

void fail(RouteResult &result, RouteFailureKind kind, std::optional<std::size_t> step,
          std::string message) {
    const auto key = std::tuple{step, static_cast<int>(kind)};
    if (!result.failure.has_value() ||
        key < std::tuple{result.failure->step_index, static_cast<int>(result.failure->kind)}) {
        result.failure = RouteFailure{kind, step, std::move(message)};
    }
}

RouteResult reject(const Board &board, RouteResult result, RoutePolicy policy) {
    const auto name =
        result.net.has_value() ? board.circuit().get(*result.net).name().value() : "?";
    result.message = "route " + name;
    if (result.failure->step_index.has_value())
        result.message += " step " + std::to_string(*result.failure->step_index);
    result.message += " " + failure_name(result.failure->kind) + ": " + result.failure->message +
                      "; board unchanged";
    if (policy == RoutePolicy::Strict)
        throw RouteRejected{std::move(result)};
    return result;
}

std::optional<std::vector<BoardPoint>> lower(BoardPoint a, BoardPoint b, CornerStyle style) {
    const auto dx = b.x_mm() - a.x_mm();
    const auto dy = b.y_mm() - a.y_mm();
    if (!std::isfinite(dx) || !std::isfinite(dy))
        return std::nullopt;
    auto points = std::vector{a};
    auto x = a.x_mm();
    auto y = a.y_mm();
    if (dx != 0.0 && dy != 0.0 && style != CornerStyle::Direct) {
        if (style == CornerStyle::Orthogonal)
            x = b.x_mm();
        else if (std::abs(dx) > std::abs(dy))
            x = b.x_mm() - std::copysign(std::abs(dy), dx);
        else if (std::abs(dy) > std::abs(dx))
            y = b.y_mm() - std::copysign(std::abs(dx), dy);
        else {
            x = b.x_mm();
            y = b.y_mm();
        }
        if (!std::isfinite(x) || !std::isfinite(y))
            return std::nullopt;
        const auto bend = BoardPoint{x, y};
        if (bend != points.back())
            points.push_back(bend);
    }
    if (b != points.back())
        points.push_back(b);
    return points;
}

BoardSpatialQueryShape shape(const std::variant<BoardTrack, BoardVia> &copper, const Board &board) {
    return std::visit(
        [&](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::same_as<T, BoardTrack>) {
                return BoardSpatialQueryShape{BoardSpatialQueryShapeKind::Segment,
                                              value.net(),
                                              {value.layer()},
                                              value.points(),
                                              value.width_mm() / 2.0,
                                              BoardClearanceKind::Track,
                                              BoardKeepoutRestriction::Copper};
            } else {
                return BoardSpatialQueryShape{BoardSpatialQueryShapeKind::Disc,
                                              value.net(),
                                              detail::via_copper_layers(board, value),
                                              {value.position()},
                                              value.annular_diameter_mm() / 2.0,
                                              BoardClearanceKind::Via,
                                              BoardKeepoutRestriction::Via};
            }
        },
        copper);
}

std::vector<RouteRuleSource> clearance_sources(const Board &board, NetId net,
                                               BoardClearanceKind kind,
                                               const BoardSpatialObstacle &obstacle,
                                               double required, std::optional<BoardRoomId> room) {
    if (room.has_value())
        return {room_source(*room, RouteRuleField::Clearance)};
    auto sources = std::vector<RouteRuleSource>{};
    if (board.design_rules().clearance_mm(kind, obstacle.clearance_kind) == required) {
        auto source = board_source(RouteRuleField::Clearance);
        source.first_kind = kind;
        source.second_kind = obstacle.clearance_kind;
        sources.push_back(source);
    }
    auto classes = std::vector<RouteRuleSource>{};
    for (const auto id : {net, obstacle.net}) {
        const auto rules = resolve_net_class_rules(board.circuit(), id);
        if (rules.copper_clearance_mm == required)
            classes.push_back(class_source(rules, RouteRuleField::Clearance));
    }
    std::sort(classes.begin(), classes.end(), [](const auto &a, const auto &b) {
        return a.net_class->index() < b.net_class->index();
    });
    classes.erase(
        std::unique(classes.begin(), classes.end(),
                    [](const auto &a, const auto &b) { return a.net_class == b.net_class; }),
        classes.end());
    sources.insert(sources.end(), classes.begin(), classes.end());
    return sources;
}

std::string obstacle_label(const Board &board, const BoardSpatialObstacle &obstacle,
                           const ResolvedBoardView &resolved) {
    if (obstacle.pad.has_value()) {
        const auto &placement = board.get(obstacle.pad->placement);
        const auto *part = resolved.part(placement.component());
        const auto footprint = resolve_footprint(part->physical_part(), resolved.footprints());
        return board.circuit().get(placement.component()).reference().value() + " pad " +
               footprint.definition()->pad(obstacle.pad->pad).label();
    }
    if (!obstacle.entities.empty()) {
        const auto &entity = obstacle.entities.front();
        return std::string{detail::clearance_kind_name(obstacle.clearance_kind)} + " " +
               std::to_string(entity.index());
    }
    return "staged copper";
}

RouteRuleValue resolve_width(const Board &board, NetId net, BoardLayerId layer,
                             const std::vector<BoardPoint> &vertices,
                             std::optional<double> requested, bool &below_minimum) {
    const auto rules = resolve_net_class_rules(board.circuit(), net);
    const auto &design = board.design_rules();
    const auto rooms = detail::BoardRoomRuleResolver{board};
    const auto base = floor_value(design.minimum_track_width_mm(), rules.track_width_mm, rules,
                                  RouteRuleField::Width);
    auto width = base;
    if (requested.has_value()) {
        auto requirement = base;
        if (const auto room = rooms.track_width_override(layer, vertices, *requested)) {
            requirement.value_mm = std::max(design.minimum_track_width_mm(), room->value_mm);
            requirement.sources.clear();
            if (design.minimum_track_width_mm() == requirement.value_mm)
                requirement.sources.push_back(board_source(RouteRuleField::Width));
            if (room->value_mm == requirement.value_mm)
                requirement.sources.push_back(room_source(room->room, RouteRuleField::Width));
        }
        width.enforced_sources = requirement.sources;
        override_value(width, *requested, RouteRuleField::Width);
        if (width.value_mm + detail::board_drc_epsilon < requirement.value_mm) {
            below_minimum = true;
        }
    } else {
        auto applied = std::vector<detail::RoomRuleValue>{};
        width.value_mm = rooms.effective_track_width_mm(layer, vertices, width.value_mm, &applied);
        if (width.value_mm > base.value_mm)
            width.sources.clear();
        for (const auto &room : applied) {
            if (room.value_mm != width.value_mm)
                continue;
            if (std::none_of(width.sources.begin(), width.sources.end(),
                             [&](const auto &source) { return source.room == room.room; })) {
                width.sources.push_back(room_source(room.room, RouteRuleField::Width));
            }
        }
        std::stable_sort(width.sources.begin(), width.sources.end(),
                         [](const auto &a, const auto &b) {
                             if (a.kind != b.kind)
                                 return a.kind < b.kind;
                             if (a.room && b.room)
                                 return a.room->index() < b.room->index();
                             return false;
                         });
    }
    return width;
}

} // namespace route_detail

using namespace route_detail;

RouteResult BoardRouter::route(const RouteSpec &spec, RoutePolicy policy) {
    return plan_route(spec, policy, false);
}

RouteResult BoardRouter::query_gap(const RouteSpec &prefix) const {
    return plan_route(prefix, RoutePolicy::Report, true);
}

RouteResult BoardRouter::plan_route(const RouteSpec &spec, RoutePolicy policy,
                                    bool gap_query) const {
    static_assert(std::is_nothrow_move_constructible_v<RouteResult>);
    static_assert(std::is_nothrow_destructible_v<BoardSpatialIndex>);
    owner(spec.board == board_);
    if (policy != RoutePolicy::Strict && policy != RoutePolicy::Report)
        invalid("Unknown route policy");
    layer_ref(*board_, spec.layer);
    if (spec.net.has_value()) {
        owner(spec.net->owner == &board_->circuit());
        static_cast<void>(board_->circuit().get(spec.net->net));
    }
    if (spec.width_mm.has_value())
        positive(*spec.width_mm);
    if (spec.via_size.has_value()) {
        positive(spec.via_size->drill_mm);
        positive(spec.via_size->diameter_mm);
        if (spec.via_size->diameter_mm <= spec.via_size->drill_mm)
            invalid("Route via diameter must exceed drill");
    }
    if (gap_query) {
        if (!spec.net || spec.steps.empty() || !std::holds_alternative<RouteGap>(spec.steps.back()))
            invalid("Gap query needs an explicit logical net and must terminate with Gap");
    } else if (spec.steps.empty() || !std::holds_alternative<RouteTo>(spec.steps.back()))
        invalid("Route must terminate with To");
    const auto pads = queries::resolve_pads(resolved_);
    auto endpoints = std::vector<std::optional<EndpointTruth>>(spec.steps.size());
    const auto start = endpoint_truth(*board_, resolved_, spec.start, pads);
    auto active_layer = spec.layer.layer;
    for (std::size_t i = 0; i < spec.steps.size(); ++i) {
        std::visit(
            [&](const auto &step) {
                using T = std::decay_t<decltype(step)>;
                if constexpr (std::same_as<T, RouteThrough> || std::same_as<T, RouteTo>) {
                    corner(step.corner);
                    if constexpr (std::same_as<T, RouteTo>) {
                        if (i + 1U != spec.steps.size())
                            invalid("Route To must be the final step");
                    }
                    endpoints[i] = endpoint_truth(*board_, resolved_, step.endpoint, pads);
                } else if constexpr (std::same_as<T, RouteVia>) {
                    layer_ref(*board_, step.destination);
                    if (step.destination.layer == active_layer)
                        invalid("Route via must change layers");
                    active_layer = step.destination.layer;
                } else if constexpr (std::same_as<T, RouteToX> || std::same_as<T, RouteToY>) {
                    if (!std::isfinite(step.coordinate_mm))
                        invalid("Route axis coordinate must be finite");
                } else {
                    preflight_target(*board_, resolved_, step);
                }
            },
            spec.steps[i]);
    }
    auto result = RouteResult{};
    if (spec.net.has_value())
        result.net = spec.net->net;
    auto net_evidence = [&](const EndpointTruth &truth, std::optional<std::size_t> step) {
        if (truth.net.has_value()) {
            if (result.net.has_value() && result.net != truth.net) {
                throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                          "Route endpoint logical nets conflict"};
            }
            result.net = truth.net;
        }
        if (!truth.failure.empty())
            fail(result, RouteFailureKind::EndpointUnreachable, step, truth.failure);
    };
    net_evidence(start, std::nullopt);
    for (std::size_t i = 0; i < endpoints.size(); ++i)
        if (endpoints[i])
            net_evidence(*endpoints[i], i);
    if (result.failure.has_value() && start.identity && !contains(start.layers, spec.layer.layer))
        fail(result, RouteFailureKind::EndpointUnreachable, std::nullopt,
             "endpoint has no copper on active layer");
    if (!result.net.has_value()) {
        if (result.failure.has_value())
            return reject(*board_, std::move(result), policy);
        invalid("Route needs a connected endpoint or explicit logical net");
    }
    // Physical endpoint failures make a complete connecting geometry unavailable.
    if (result.failure.has_value())
        return reject(*board_, std::move(result), policy);
    const auto net = *result.net;
    const auto rules = resolve_net_class_rules(board_->circuit(), net);
    const auto params = resolve_parameters(net);
    result.layer_rules.source = rules.net_class.has_value()
                                    ? class_source(rules, RouteRuleField::AllowedLayers)
                                    : board_source(RouteRuleField::AllowedLayers);
    result.layer_rules.scope = rules.layer_scope;
    result.layer_rules.requested_names = rules.allowed_layer_names;
    for (std::size_t i = 0; i < board_->all<BoardLayerId>().size(); ++i) {
        const auto id = BoardLayerId{i};
        const auto &layer = board_->get(id);
        if (layer.enabled() && layer.role() == BoardLayerRole::Copper &&
            contains(board_->layer_stack()->layers(), id) && contains(params.allowed_layers, id)) {
            result.layer_rules.allowed.push_back(id);
        } else
            result.layer_rules.excluded.push_back(id);
    }
    const auto &design = board_->design_rules();
    auto drill = floor_value(design.minimum_via_drill_diameter_mm(), rules.via_drill_mm, rules,
                             RouteRuleField::ViaDrill);
    auto diameter = floor_value(design.minimum_via_annular_diameter_mm(), rules.via_diameter_mm,
                                rules, RouteRuleField::ViaDiameter);
    if (spec.via_size.has_value()) {
        override_value(drill, spec.via_size->drill_mm, RouteRuleField::ViaDrill);
        override_value(diameter, spec.via_size->diameter_mm, RouteRuleField::ViaDiameter);
    }
    auto current = *start.position;
    active_layer = spec.layer.layer;
    auto endpoint_layer = [&](const EndpointTruth &truth, BoardLayerId layer,
                              std::optional<std::size_t> step) {
        if (truth.identity && !contains(truth.layers, layer))
            fail(result, RouteFailureKind::EndpointUnreachable, step,
                 "endpoint has no copper on active layer");
    };
    endpoint_layer(start, active_layer, std::nullopt);
    if (!contains(result.layer_rules.allowed, active_layer)) {
        fail(result, RouteFailureKind::LayerNotAllowed, std::nullopt, "start layer is disallowed");
    }
    for (std::size_t i = 0; i < spec.steps.size(); ++i) {
        auto resolved_step = RouteResolvedStep{i, current, current, active_layer, active_layer, {}};
        auto target = current;
        auto style = CornerStyle::Direct;
        const auto *via = std::get_if<RouteVia>(&spec.steps[i]);
        if (via != nullptr) {
            if (diameter.value_mm <= drill.value_mm) {
                fail(result, RouteFailureKind::RuleUnresolvable, i,
                     "inherited via diameter does not exceed drill");
                return reject(*board_, std::move(result), policy);
            }
            const auto ordinal = result.candidates.size();
            result.candidates.push_back(
                {i, BoardVia{net, current, active_layer, via->destination.layer, drill.value_mm,
                             diameter.value_mm}});
            result.resolved_rules.push_back({ordinal, std::nullopt, drill, diameter});
            resolved_step.candidates.push_back(ordinal);
            if (drill.value_mm + detail::board_drc_epsilon < drill.default_mm ||
                diameter.value_mm + detail::board_drc_epsilon < diameter.default_mm) {
                fail(result, RouteFailureKind::RuleViolation, i,
                     "explicit via sizing is below native minimum");
            }
            active_layer = via->destination.layer;
            resolved_step.outgoing_layer = active_layer;
        } else {
            std::visit(
                [&](const auto &step) {
                    using T = std::decay_t<decltype(step)>;
                    if constexpr (std::same_as<T, RouteThrough> || std::same_as<T, RouteTo>) {
                        target = *endpoints[i]->position;
                        style = step.corner;
                        endpoint_layer(*endpoints[i], active_layer, i);
                    } else if constexpr (std::same_as<T, RouteToX>)
                        target = BoardPoint{step.coordinate_mm, current.y_mm()};
                    else if constexpr (std::same_as<T, RouteToY>)
                        target = BoardPoint{current.x_mm(), step.coordinate_mm};
                    else if constexpr (std::same_as<T, RouteExit> || std::same_as<T, RouteBeside> ||
                                       std::same_as<T, RouteGap>) {
                        resolve_target(*board_, resolved_, spec, step, current, active_layer, i,
                                       result);
                        if (!result.target_resolutions.empty() &&
                            result.target_resolutions.back().step_index == i)
                            target = result.target_resolutions.back().position;
                    }
                },
                spec.steps[i]);
            if (is_symbolic(spec.steps[i]) && (result.target_resolutions.empty() ||
                                               result.target_resolutions.back().step_index != i))
                return reject(*board_, std::move(result), policy);
            const auto points = lower(current, target, style);
            if (!points.has_value()) {
                fail(result, RouteFailureKind::GeometryUnresolvable, i,
                     "corner arithmetic is non-finite");
                return reject(*board_, std::move(result), policy);
            }
            for (std::size_t p = 1; p < points->size(); ++p) {
                const auto vertices = std::vector{(*points)[p - 1U], (*points)[p]};
                auto below_minimum = false;
                auto width = resolve_width(*board_, net, active_layer, vertices, spec.width_mm,
                                           below_minimum);
                if (below_minimum)
                    fail(result, RouteFailureKind::RuleViolation, i,
                         "explicit track width is below native minimum");
                const auto ordinal = result.candidates.size();
                result.candidates.push_back(
                    {i, BoardTrack{net, active_layer, vertices, width.value_mm}});
                result.resolved_rules.push_back({ordinal, width, std::nullopt, std::nullopt});
                resolved_step.candidates.push_back(ordinal);
            }
            current = target;
            resolved_step.end = target;
        }
        result.resolved_steps.push_back(std::move(resolved_step));
    }
    result.candidate_complete = true;
    if (!gap_query && result.candidates.empty() && !result.failure.has_value())
        fail(result, RouteFailureKind::NoCandidate, std::nullopt, "route contains no track or via");

    // Fresh private index for each call: candidate insertion never changes canonical Board state.
    auto staged = BoardSpatialIndex{resolved_};
    const auto existing_shapes = staged.shape_count();
    for (std::size_t ordinal = 0; ordinal < result.candidates.size(); ++ordinal) {
        const auto &candidate = result.candidates[ordinal];
        const auto query_shape = shape(candidate.copper, *board_);
        for (const auto layer : query_shape.layers)
            if (!contains(result.layer_rules.allowed, layer)) {
                fail(result, RouteFailureKind::LayerNotAllowed, candidate.step_index,
                     "candidate occupies a disallowed copper layer");
            }
        const auto query = staged.query_legality(query_shape);
        for (const auto &check : query.clearance_checks) {
            const auto obstacle = staged.obstacle(check.shape_index);
            auto clearance =
                RouteRuleValue{check.required_mm,
                               check.required_mm,
                               std::nullopt,
                               clearance_sources(*board_, net, query_shape.clearance_kind, obstacle,
                                                 check.required_mm, check.room),
                               {}};
            result.clearance_checks.push_back(
                {ordinal, check.layer, std::move(clearance), check.actual_mm, obstacle.entities,
                 check.shape_index >= existing_shapes
                     ? std::optional{check.shape_index - existing_shapes}
                     : std::nullopt});
        }
        for (const auto &block : query.blockers) {
            auto out = RouteBlocker{candidate.step_index,
                                    ordinal,
                                    block.kind,
                                    block.layer,
                                    {},
                                    std::nullopt,
                                    std::nullopt,
                                    std::nullopt,
                                    {},
                                    {},
                                    {},
                                    std::nullopt,
                                    block.shape_index.value_or(0)};
            if (block.kind != BoardSpatialBlockerKind::Keepout) {
                out.required_mm = block.required_clearance_mm;
                out.actual_mm = block.actual_clearance_mm;
            }
            if (block.shape_index.has_value()) {
                const auto obstacle = staged.obstacle(*block.shape_index);
                out.entities = obstacle.entities;
                out.sources = clearance_sources(*board_, net, query_shape.clearance_kind, obstacle,
                                                block.required_clearance_mm, block.room);
                if (*block.shape_index >= existing_shapes)
                    out.earlier_candidate = *block.shape_index - existing_shapes;
                out.obstacle_label = obstacle_label(*board_, obstacle, resolved_);
                out.message = "needs " + mm(block.required_clearance_mm) + " mm to " +
                              out.obstacle_label + " (" +
                              board_->circuit().get(obstacle.net).name().value() + "), has " +
                              mm(block.actual_clearance_mm) + " mm";
            } else if (block.keepout.has_value()) {
                auto source = board_source(RouteRuleField::Clearance);
                source.kind = RouteRuleSourceKind::Keepout;
                source.keepout = block.keepout;
                out.sources.push_back(source);
                out.entities.push_back(EntityRef::board_keepout(*block.keepout));
                out.obstacle_order = block.keepout->index();
                out.message = "copper intersects keepout " + std::to_string(block.keepout->index());
            } else {
                auto source = board_source(RouteRuleField::Clearance);
                source.first_kind = query_shape.clearance_kind;
                source.second_kind = block.feature.has_value()
                                         ? BoardClearanceKind::MechanicalOpening
                                         : BoardClearanceKind::BoardEdge;
                out.sources.push_back(source);
                if (block.feature.has_value()) {
                    out.entities.push_back(EntityRef::board_feature(*block.feature));
                    out.obstacle_order = block.feature->index();
                }
                out.message = block.feature.has_value()
                                  ? "copper violates mechanical-opening clearance"
                                  : "copper violates board-outline clearance";
            }
            if (out.layer.has_value())
                out.message = "on " + board_->get(*out.layer).name() + ": " + out.message;
            result.blockers.push_back(std::move(out));
        }
        staged.insert(query_shape);
    }
    std::sort(result.blockers.begin(), result.blockers.end(), [](const auto &a, const auto &b) {
        return std::tuple{a.step_index, a.candidate_ordinal,
                          a.layer.has_value() ? a.layer->index()
                                              : std::numeric_limits<std::size_t>::max(),
                          static_cast<int>(a.kind), a.obstacle_order} <
               std::tuple{b.step_index, b.candidate_ordinal,
                          b.layer.has_value() ? b.layer->index()
                                              : std::numeric_limits<std::size_t>::max(),
                          static_cast<int>(b.kind), b.obstacle_order};
    });
    result.blockers.erase(std::unique(result.blockers.begin(), result.blockers.end(),
                                      [](const auto &a, const auto &b) {
                                          return a.step_index == b.step_index &&
                                                 a.candidate_ordinal == b.candidate_ordinal &&
                                                 a.kind == b.kind && a.layer == b.layer &&
                                                 a.obstacle_order == b.obstacle_order &&
                                                 a.required_mm == b.required_mm &&
                                                 a.actual_mm == b.actual_mm &&
                                                 a.message == b.message;
                                      }),
                          result.blockers.end());
    for (const auto &block : result.blockers)
        fail(result, RouteFailureKind::Blocked, block.step_index, block.message);
    if (result.failure.has_value())
        return reject(*board_, std::move(result), policy);

    if (gap_query) {
        result.message = "route " + board_->circuit().get(net).name().value() +
                         " gap prefix feasible; board unchanged";
        return result;
    }

    // Prepare every result field before Board's no-throw publication boundary.
    auto append = CopperAppendSpec{};
    append.primitives.reserve(result.candidates.size());
    result.committed_order.reserve(result.candidates.size());
    result.tracks.reserve(result.candidates.size());
    result.vias.reserve(result.candidates.size());
    auto track_count = board_->all<BoardTrackId>().size();
    auto via_count = board_->all<BoardViaId>().size();
    for (const auto &candidate : result.candidates) {
        append.primitives.push_back(candidate.copper);
        if (std::holds_alternative<BoardTrack>(candidate.copper)) {
            const auto id = BoardTrackId{track_count++};
            result.tracks.push_back(id);
            result.committed_order.emplace_back(id);
        } else {
            const auto id = BoardViaId{via_count++};
            result.vias.push_back(id);
            result.committed_order.emplace_back(id);
        }
    }
    result.message = "route " + board_->circuit().get(net).name().value() + " committed";
    result.committed = true;
    static_cast<void>(board_->append_copper(append));
    index_.reset();
    return result;
}

} // namespace volt
