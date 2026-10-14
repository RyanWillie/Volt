#include "completion_detail.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <ranges>
#include <volt/circuit/connectivity/queries.hpp>

namespace volt::detail {
namespace {
constexpr auto epsilon = board_drc_epsilon;
namespace codes = pcb_completion_diagnostic_codes;

[[nodiscard]] bool grid_exact(double value) {
    return std::abs(value - std::round(value * 1.0e6) / 1.0e6) <= 1.0e-12;
}

void unknown(CompletionWork &work, std::string message, std::vector<EntityRef> entities,
             std::optional<DiagnosticMeasurement> measurement = std::nullopt) {
    work.note(BoardCompletionCheck::Geometry, BoardCompletionState::NotEvaluated,
              codes::GeometryNotEvaluated, std::move(message), std::move(entities), measurement);
}

void append(CompletionWork &work, BoardCompletionCopper item) {
    const auto &board = work.compiled.board();
    auto &scope = work.coverage[static_cast<std::size_t>(BoardCompletionCheck::Geometry)].entities;
    const auto entities = completion_entities(item);
    scope.insert(scope.end(), entities.begin(), entities.end());
    if (item.layers.empty()) {
        item.supported = false;
        item.contact_evaluated = false;
    }
    for (const auto layer : item.layers) {
        const auto &definition = board.get(layer);
        if (!definition.enabled() || !board.layer_stack() ||
            std::ranges::find(board.layer_stack()->layers(), layer) ==
                board.layer_stack()->layers().end()) {
            item.supported = false;
            item.contact_evaluated = false;
        }
    }
    if (!item.supported)
        unknown(work,
                "Copper shape/layer is unsupported or lossy; use supported exact emitted geometry",
                entities);
    if (board.outline()) {
        const auto kind = item.pad ? BoardClearanceKind::Pad
                          : item.kind == BoardCompletionCopperKind::Segment
                              ? BoardClearanceKind::Track
                          : item.kind == BoardCompletionCopperKind::Disc ? BoardClearanceKind::Via
                                                                         : BoardClearanceKind::Zone;
        const auto clearance =
            board.design_rules().clearance_mm(kind, BoardClearanceKind::BoardEdge);
        const auto inside =
            item.kind == BoardCompletionCopperKind::Disc
                ? outline_contains_disc(*board.outline(), item.points[0], item.radius_mm, clearance)
            : item.kind == BoardCompletionCopperKind::Segment
                ? outline_contains_segment(*board.outline(), item.points[0], item.points[1],
                                           item.radius_mm, clearance)
                : outline_contains_polygon(*board.outline(), item.points, clearance);
        if (!inside)
            work.note(BoardCompletionCheck::Geometry, BoardCompletionState::Failed,
                      drc_diagnostic_codes::CopperOutsideOutline,
                      "Emitted copper violates native board-edge clearance; keep all copper inside "
                      "the outline",
                      entities);
    }
    auto exact = grid_exact(item.radius_mm * 2.0);
    for (const auto point : item.points)
        exact = exact && grid_exact(point.x_mm()) && grid_exact(point.y_mm());
    if (!exact) {
        unknown(work,
                "Copper is off the native 4.6 export grid; quantized contact is not evaluated",
                entities);
        item.supported = false;
        item.contact_evaluated = false;
    }
    work.copper.push_back(std::move(item));
}

[[nodiscard]] bool layers_overlap(const BoardCompletionCopper &lhs,
                                  const BoardCompletionCopper &rhs) {
    return std::ranges::any_of(lhs.layers, [&](auto layer) {
        return std::ranges::find(rhs.layers, layer) != rhs.layers.end();
    });
}

[[nodiscard]] double gap(const BoardCompletionCopper &lhs, const BoardCompletionCopper &rhs) {
    return primitive_distance(primitive(lhs), primitive(rhs)) - lhs.radius_mm - rhs.radius_mm;
}

[[nodiscard]] bool strictly_inside(BoardPoint point, const BoardCompletionCopper &item) {
    if (item.kind == BoardCompletionCopperKind::Polygon) {
        return polygon_contains_point(item.points, point) &&
               outline_boundary_distance(BoardOutline{item.points}, point) > epsilon;
    }
    const auto points = std::vector{point};
    return primitive_distance({BoardCopperShapeKind::Disc, points}, primitive(item)) <
           item.radius_mm - epsilon;
}

[[nodiscard]] std::vector<BoardPoint> samples(const BoardCompletionCopper &item) {
    auto result = item.points;
    if (item.kind == BoardCompletionCopperKind::Disc) {
        const auto centre = item.points[0];
        const auto radius = item.radius_mm * 0.95;
        for (const auto [x, y] : {std::pair{radius, 0.0}, std::pair{-radius, 0.0},
                                  std::pair{0.0, radius}, std::pair{0.0, -radius}})
            result.emplace_back(centre.x_mm() + x, centre.y_mm() + y);
    } else {
        auto x = 0.0;
        auto y = 0.0;
        for (const auto point : item.points) {
            x += point.x_mm();
            y += point.y_mm();
        }
        result.emplace_back(x / static_cast<double>(item.points.size()),
                            y / static_cast<double>(item.points.size()));
        for (std::size_t i = 1; i < item.points.size(); ++i)
            result.emplace_back((item.points[i - 1].x_mm() + item.points[i].x_mm()) / 2.0,
                                (item.points[i - 1].y_mm() + item.points[i].y_mm()) / 2.0);
    }
    return result;
}

[[nodiscard]] bool outside_openings(BoardPoint point,
                                    const std::vector<CompletionOpening> &openings) {
    const auto points = std::vector{point};
    return std::ranges::all_of(openings, [&](const auto &opening) {
        return primitive_distance({BoardCopperShapeKind::Disc, points},
                                  {opening.kind, opening.points}) > opening.radius_mm + epsilon;
    });
}

[[nodiscard]] bool contact_witness(const BoardCompletionCopper &lhs,
                                   const BoardCompletionCopper &rhs,
                                   const std::vector<CompletionOpening> &openings) {
    for (const auto *item : {&lhs, &rhs})
        for (const auto point : samples(*item))
            if (strictly_inside(point, lhs) && strictly_inside(point, rhs) &&
                outside_openings(point, openings))
                return true;
    return false;
}

[[nodiscard]] bool opening_contained(const CompletionOpening &opening,
                                     const BoardCompletionCopper &item) {
    if (opening.kind != BoardCopperShapeKind::Disc)
        return false;
    const auto point = opening.points[0];
    if (item.kind == BoardCompletionCopperKind::Disc)
        return board_distance(point, item.points[0]) + opening.radius_mm < item.radius_mm - epsilon;
    if (item.kind == BoardCompletionCopperKind::Polygon)
        return polygon_contains_point(item.points, point) &&
               outline_boundary_distance(BoardOutline{item.points}, point) >
                   opening.radius_mm + epsilon;
    return false;
}

} // namespace

CopperPrimitiveView primitive(const BoardCompletionCopper &copper) {
    const auto kind = copper.kind == BoardCompletionCopperKind::Disc ? BoardCopperShapeKind::Disc
                      : copper.kind == BoardCompletionCopperKind::Segment
                          ? BoardCopperShapeKind::Segment
                          : BoardCopperShapeKind::Polygon;
    return {kind, copper.points};
}

void inventory_completion_copper(CompletionWork &work) {
    const auto &board = work.compiled.board();
    const auto &circuit = board.circuit();
    for (std::size_t i = 0; i < board.all<BoardTrackId>().size(); ++i) {
        const auto id = BoardTrackId{i};
        const auto &track = board.get(id);
        for (std::size_t p = 1; p < track.points().size(); ++p)
            append(work, {BoardCompletionCopperKind::Segment,
                          track.net(),
                          {track.layer()},
                          {EntityRef::board_track(id)},
                          {track.points()[p - 1], track.points()[p]},
                          track.width_mm() / 2.0,
                          {},
                          {},
                          {},
                          false,
                          true});
    }
    for (std::size_t i = 0; i < board.all<BoardViaId>().size(); ++i) {
        const auto id = BoardViaId{i};
        const auto &via = board.get(id);
        append(work, {BoardCompletionCopperKind::Disc,
                      via.net(),
                      via_copper_layers(board, via),
                      {EntityRef::board_via(id)},
                      {via.position()},
                      via.annular_diameter_mm() / 2.0,
                      {},
                      {},
                      {},
                      false,
                      true});
        work.openings.push_back({BoardCopperShapeKind::Disc,
                                 {via.position()},
                                 via.drill_diameter_mm() / 2.0,
                                 {EntityRef::board_via(id)}});
    }
    for (std::size_t i = 0; i < board.all<BoardZoneId>().size(); ++i) {
        const auto id = BoardZoneId{i};
        const auto &zone = board.get(id);
        for (const auto layer : zone.layers())
            append(work, {BoardCompletionCopperKind::Polygon,
                          zone.net(),
                          {layer},
                          {EntityRef::board_zone(id)},
                          zone.outline(),
                          0.0,
                          {},
                          {},
                          {},
                          false,
                          zone.fill() == BoardZoneFill::Solid});
    }
    for (const auto &frozen : work.compiled.placements()) {
        if (!frozen.footprint())
            continue;
        const auto id = frozen.placement();
        const auto &placement = board.get(id);
        const auto &footprint = *frozen.footprint();
        for (const auto &resolution : frozen.pad_resolutions()) {
            const auto &pad = footprint.pad(resolution.pad());
            const auto layers = pad_copper_layers(board, pad, placement.side());
            const auto entities = std::vector{EntityRef::component(placement.component()),
                                              EntityRef::component_placement(id),
                                              EntityRef::footprint_pad(resolution.pad())};
            if (pad.drill())
                work.openings.push_back({BoardCopperShapeKind::Disc,
                                         {resolution.position()},
                                         pad.drill()->diameter_mm() / 2.0,
                                         entities});
            if (layers.empty())
                continue;
            if (pad.shape() == FootprintPadShape::Oval) {
                unknown(work,
                        "Oval pads are not emitted by native fabrication; their copper contact is "
                        "not evaluated",
                        entities);
                continue;
            }
            const auto circle = pad.shape() == FootprintPadShape::Circle;
            const auto supported = circle ? pad.size().width_mm() == pad.size().height_mm()
                                          : pad.shape() == FootprintPadShape::Rectangle;
            const auto isolated =
                resolution.pin() &&
                (!queries::net_of(circuit, *resolution.pin()) ||
                 is_no_connect_pin(circuit.get(circuit.get(*resolution.pin()).definition())) ||
                 queries::is_intentional_no_connect_pin(circuit, *resolution.pin()));
            auto item = BoardCompletionCopper{circle ? BoardCompletionCopperKind::Disc
                                                     : BoardCompletionCopperKind::Polygon,
                                              resolution.net(),
                                              layers,
                                              entities,
                                              circle ? std::vector{resolution.position()}
                                                     : transformed_pad_body_corners(placement, pad),
                                              circle ? pad.size().width_mm() / 2.0 : 0.0,
                                              id,
                                              resolution.pad(),
                                              resolution.pin(),
                                              isolated,
                                              supported};
            if (pad.drill() && pad.drill()->plating() == FootprintPadPlating::Plated) {
                append(work, std::move(item));
            } else {
                for (const auto layer : layers) {
                    item.layers = {layer};
                    append(work, item);
                }
            }
        }
    }
    for (const auto &opening : collect_mechanical_openings(board))
        work.openings.push_back({opening.kind,
                                 opening.points,
                                 opening.radius_mm,
                                 {EntityRef::board_feature(opening.feature)}});
}

void connect_completion_copper(CompletionWork &work) {
    auto parents = std::vector<std::size_t>(work.copper.size());
    std::iota(parents.begin(), parents.end(), 0U);
    for (auto &item : work.copper) {
        auto hits = std::vector<std::size_t>{};
        for (std::size_t o = 0; o < work.openings.size(); ++o) {
            const auto &opening = work.openings[o];
            const auto distance =
                primitive_distance(primitive(item), {opening.kind, opening.points}) -
                item.radius_mm - opening.radius_mm;
            if (distance > epsilon)
                continue;
            hits.push_back(o);
            if (!opening_contained(opening, item)) {
                item.supported = false;
                item.contact_evaluated = false;
                auto entities = completion_entities(item);
                entities.insert(entities.end(), opening.entities.begin(), opening.entities.end());
                unknown(work,
                        "An opening may sever this conductor; finished copper is not evaluated",
                        std::move(entities), DiagnosticMeasurement{distance, 0.0});
            }
        }
        for (std::size_t a = 0; a < hits.size(); ++a)
            for (std::size_t b = a + 1; b < hits.size(); ++b) {
                const auto &lhs = work.openings[hits[a]];
                const auto &rhs = work.openings[hits[b]];
                if (primitive_distance({lhs.kind, lhs.points}, {rhs.kind, rhs.points}) <=
                    lhs.radius_mm + rhs.radius_mm + epsilon) {
                    item.supported = false;
                    item.contact_evaluated = false;
                    unknown(work,
                            "Overlapping openings may sever this conductor; finished copper is not "
                            "evaluated",
                            completion_entities(item));
                }
            }
    }
    for (std::size_t a = 0; a < work.copper.size(); ++a)
        for (std::size_t b = a + 1; b < work.copper.size(); ++b) {
            const auto &lhs = work.copper[a];
            const auto &rhs = work.copper[b];
            if (!lhs.contact_evaluated || !rhs.contact_evaluated || !layers_overlap(lhs, rhs))
                continue;
            const auto distance = gap(lhs, rhs);
            if (distance > epsilon)
                continue;
            const auto polygon_contact = lhs.kind == BoardCompletionCopperKind::Polygon &&
                                         rhs.kind == BoardCompletionCopperKind::Polygon;
            const auto robust = polygon_contact
                                    ? polygon_interiors_overlap(lhs.points, rhs.points) &&
                                          contact_witness(lhs, rhs, {})
                                    : distance < -epsilon;
            const auto intersects_opening =
                std::ranges::any_of(work.openings, [&](const auto &opening) {
                    return primitive_distance(primitive(lhs), {opening.kind, opening.points}) <=
                               lhs.radius_mm + opening.radius_mm + epsilon &&
                           primitive_distance(primitive(rhs), {opening.kind, opening.points}) <=
                               rhs.radius_mm + opening.radius_mm + epsilon;
                });
            auto entities = completion_entities(lhs);
            const auto other = completion_entities(rhs);
            entities.insert(entities.end(), other.begin(), other.end());
            if (!robust || (intersects_opening && !contact_witness(lhs, rhs, work.openings))) {
                unknown(work,
                        "Copper contact depends on boundary tolerance or a drilled opening; "
                        "contact is not evaluated",
                        std::move(entities), DiagnosticMeasurement{distance, 0.0});
                continue;
            }
            const auto lhs_root = connectivity_root(parents, a);
            const auto rhs_root = connectivity_root(parents, b);
            parents[std::max(lhs_root, rhs_root)] = std::min(lhs_root, rhs_root);
            if (lhs.isolated || rhs.isolated)
                work.note(BoardCompletionCheck::UnintendedContacts, BoardCompletionState::Failed,
                          codes::IsolatedContact,
                          "Intentionally isolated or disconnected pin copper contacts another "
                          "conductor; remove contact without assigning a logical net",
                          std::move(entities), DiagnosticMeasurement{distance, 0.0});
        }
    auto islands = std::map<std::size_t, std::vector<std::size_t>>{};
    for (std::size_t i = 0; i < parents.size(); ++i)
        islands[connectivity_root(parents, i)].push_back(i);
    for (auto &[index, items] : islands) {
        static_cast<void>(index);
        work.islands.push_back({std::move(items), {}});
    }
    auto &scope =
        work.coverage[static_cast<std::size_t>(BoardCompletionCheck::UnintendedContacts)].entities;
    for (const auto &item : work.copper) {
        const auto entities = completion_entities(item);
        scope.insert(scope.end(), entities.begin(), entities.end());
    }
}
} // namespace volt::detail
