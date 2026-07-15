#include <volt/pcb/routing/board_router.hpp>

#include <volt/core/errors.hpp>
#include <volt/pcb/queries/board_queries.hpp>
#include <volt/pcb/routing/board_spatial_index.hpp>

#include "board_spatial_index_storage.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace volt {

namespace {

[[nodiscard]] detail::BoardCopperShapeKind copper_shape_kind(BoardSpatialQueryShapeKind kind) {
    switch (kind) {
    case BoardSpatialQueryShapeKind::Disc:
        return detail::BoardCopperShapeKind::Disc;
    case BoardSpatialQueryShapeKind::Segment:
        return detail::BoardCopperShapeKind::Segment;
    case BoardSpatialQueryShapeKind::Polygon:
        return detail::BoardCopperShapeKind::Polygon;
    }
    return detail::BoardCopperShapeKind::Segment;
}

[[nodiscard]] std::optional<double>
shape_outline_actual_clearance(const detail::BoardCopperShape &shape, const BoardOutline &outline) {
    if (shape.kind == detail::BoardCopperShapeKind::Disc) {
        const auto distance = detail::outline_boundary_distance(outline, shape.points[0]);
        if (!outline.contains(shape.points[0])) {
            return -distance - shape.radius_mm;
        }
        return distance - shape.radius_mm;
    }
    if (shape.kind == detail::BoardCopperShapeKind::Segment) {
        const auto distance =
            detail::segment_outline_boundary_distance(outline, shape.points[0], shape.points[1]);
        if (!outline.contains(shape.points[0]) || !outline.contains(shape.points[1])) {
            return -distance - shape.radius_mm;
        }
        return distance - shape.radius_mm;
    }
    const auto distance = detail::polygon_outline_boundary_distance(outline, shape.points);
    for (const auto point : shape.points) {
        if (!outline.contains(point)) {
            return -distance;
        }
    }
    return distance;
}

} // namespace

BoardSpatialIndex::BoardSpatialIndex(const BoardSpatialIndex &other)
    : state_{std::make_unique<detail::BoardSpatialIndexState>(other.state())} {}

BoardSpatialIndex::BoardSpatialIndex(BoardSpatialIndex &&other) noexcept = default;

BoardSpatialIndex &BoardSpatialIndex::operator=(const BoardSpatialIndex &other) {
    if (this != &other) {
        state_ = std::make_unique<detail::BoardSpatialIndexState>(other.state());
    }
    return *this;
}

BoardSpatialIndex &BoardSpatialIndex::operator=(BoardSpatialIndex &&other) noexcept = default;

BoardSpatialIndex::~BoardSpatialIndex() = default;

[[nodiscard]] double BoardSpatialIndex::conservative_clearance_mm() const noexcept {
    return state().conservative_clearance_mm;
}

[[nodiscard]] bool BoardSpatialIndex::cell_less(const detail::BoardSpatialIndexCell &lhs,
                                                const detail::BoardSpatialIndexCell &rhs) {
    if (lhs.layer.index() != rhs.layer.index()) {
        return lhs.layer.index() < rhs.layer.index();
    }
    if (lhs.x != rhs.x) {
        return lhs.x < rhs.x;
    }
    return lhs.y < rhs.y;
}

[[nodiscard]] bool BoardSpatialIndex::same_cell_key(const detail::BoardSpatialIndexCell &lhs,
                                                    const detail::BoardSpatialIndexCell &rhs) {
    return lhs.layer == rhs.layer && lhs.x == rhs.x && lhs.y == rhs.y;
}

[[nodiscard]] detail::BoardSpatialIndexBox
BoardSpatialIndex::shape_box(const detail::BoardCopperShape &shape) {
    auto box = detail::BoardSpatialIndexBox{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    for (const auto point : shape.points) {
        box.min_x_mm = std::min(box.min_x_mm, point.x_mm() - shape.radius_mm);
        box.min_y_mm = std::min(box.min_y_mm, point.y_mm() - shape.radius_mm);
        box.max_x_mm = std::max(box.max_x_mm, point.x_mm() + shape.radius_mm);
        box.max_y_mm = std::max(box.max_y_mm, point.y_mm() + shape.radius_mm);
    }
    return box;
}

[[nodiscard]] detail::BoardSpatialIndexBox
BoardSpatialIndex::outline_box(const BoardOutline &outline) {
    auto box = detail::BoardSpatialIndexBox{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    for (const auto point : outline.vertices()) {
        box.min_x_mm = std::min(box.min_x_mm, point.x_mm());
        box.min_y_mm = std::min(box.min_y_mm, point.y_mm());
        box.max_x_mm = std::max(box.max_x_mm, point.x_mm());
        box.max_y_mm = std::max(box.max_y_mm, point.y_mm());
    }
    return box;
}

[[nodiscard]] detail::BoardSpatialIndexBox
BoardSpatialIndex::merge_box(detail::BoardSpatialIndexBox lhs, detail::BoardSpatialIndexBox rhs) {
    lhs.min_x_mm = std::min(lhs.min_x_mm, rhs.min_x_mm);
    lhs.min_y_mm = std::min(lhs.min_y_mm, rhs.min_y_mm);
    lhs.max_x_mm = std::max(lhs.max_x_mm, rhs.max_x_mm);
    lhs.max_y_mm = std::max(lhs.max_y_mm, rhs.max_y_mm);
    return lhs;
}

[[nodiscard]] detail::BoardSpatialIndexBox
BoardSpatialIndex::expanded_box(detail::BoardSpatialIndexBox box, double expansion_mm) {
    box.min_x_mm -= expansion_mm;
    box.min_y_mm -= expansion_mm;
    box.max_x_mm += expansion_mm;
    box.max_y_mm += expansion_mm;
    return box;
}

[[nodiscard]] bool BoardSpatialIndex::boxes_intersect(detail::BoardSpatialIndexBox lhs,
                                                      detail::BoardSpatialIndexBox rhs) {
    return lhs.min_x_mm <= rhs.max_x_mm && lhs.max_x_mm >= rhs.min_x_mm &&
           lhs.min_y_mm <= rhs.max_y_mm && lhs.max_y_mm >= rhs.min_y_mm;
}

[[nodiscard]] long long BoardSpatialIndex::cell_key(double value, double cell_size_mm) {
    return static_cast<long long>(std::floor(value / cell_size_mm));
}

[[nodiscard]] double
BoardSpatialIndex::extent_cell_size(const Board &board,
                                    const std::vector<detail::BoardSpatialIndexBox> &boxes) {
    auto extent = std::optional<detail::BoardSpatialIndexBox>{};
    if (board.outline().has_value()) {
        extent = outline_box(board.outline().value());
    }
    for (const auto box : boxes) {
        extent = extent.has_value() ? merge_box(extent.value(), box) : box;
    }
    if (!extent.has_value()) {
        return 1.0;
    }

    const auto width = extent->max_x_mm - extent->min_x_mm;
    const auto height = extent->max_y_mm - extent->min_y_mm;
    const auto span = std::max(width, height);
    if (!std::isfinite(span) || span <= detail::board_drc_epsilon) {
        return 1.0;
    }
    return std::max(span / 32.0, 0.25);
}

[[nodiscard]] detail::BoardCopperShape
BoardSpatialIndex::to_copper_shape(BoardSpatialQueryShape candidate) {
    return detail::BoardCopperShape{
        copper_shape_kind(candidate.kind),
        candidate.net,
        std::move(candidate.layers),
        {},
        std::move(candidate.points),
        candidate.radius_mm,
        std::nullopt,
    };
}

BoardSpatialIndex::BoardSpatialIndex(const Board &board)
    : BoardSpatialIndex{board, std::vector<detail::BoardCopperShape>{}} {}

BoardSpatialIndex::BoardSpatialIndex(const Board &board, const FootprintLibrary &footprints)
    : BoardSpatialIndex{board, [&board, &footprints]() {
                            const auto resolution_footprints =
                                queries::board_resolution_footprints(board, footprints);
                            return detail::collect_copper_shapes(
                                board, resolution_footprints,
                                queries::resolve_pads(board, resolution_footprints));
                        }()} {}

BoardSpatialIndex::BoardSpatialIndex(const Board &board,
                                     std::vector<detail::BoardCopperShape> shapes)
    : state_{std::make_unique<detail::BoardSpatialIndexState>()} {
    mutable_state().board = &board;
    mutable_state().shapes = std::move(shapes);
    mutable_state().conservative_clearance_mm = detail::maximum_required_copper_clearance(board);
    mutable_state().cell_size_mm = 1.0;
    mutable_state().expected_geometry_mutation_count = board.geometry_mutation_count();

    mutable_state().boxes.reserve(state().shapes.size());
    for (const auto &shape : state().shapes) {
        validate_shape(shape);
        mutable_state().boxes.push_back(shape_box(shape));
    }
    mutable_state().cell_size_mm = extent_cell_size(board, state().boxes);
    for (std::size_t index = 0; index < state().shapes.size(); ++index) {
        index_shape(index);
    }
}

void BoardSpatialIndex::ensure_conservative_bound_current() const {
    const auto current = detail::maximum_required_copper_clearance(*state().board);
    if (current > state().conservative_clearance_mm + detail::board_drc_epsilon) {
        throw KernelLogicError{
            ErrorCode::InvalidState,
            "Board spatial index clearance bound is stale; rebuild after board rule changes"};
    }
}

void BoardSpatialIndex::ensure_geometry_current() const {
    if (state().board->geometry_mutation_count() != state().expected_geometry_mutation_count) {
        throw KernelLogicError{
            ErrorCode::InvalidState,
            "Board spatial index is stale; board geometry changed outside the index"};
    }
}

void BoardSpatialIndex::validate_shape(const detail::BoardCopperShape &shape) const {
    if (shape.net.index() >= state().board->circuit().all<NetId>().size()) {
        throw KernelArgumentError{ErrorCode::UnknownEntity,
                                  "Board spatial index shape net must belong to the board",
                                  EntityRef::net(shape.net)};
    }
    if (shape.layers.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Board spatial index shape layers must not be empty"};
    }
    if (!std::isfinite(shape.radius_mm) || shape.radius_mm < 0.0) {
        throw KernelArgumentError{
            ErrorCode::InvalidArgument,
            "Board spatial index shape radius must be finite and non-negative"};
    }
    if ((shape.kind == detail::BoardCopperShapeKind::Disc && shape.points.size() != 1U) ||
        (shape.kind == detail::BoardCopperShapeKind::Segment && shape.points.size() != 2U) ||
        (shape.kind == detail::BoardCopperShapeKind::Polygon && shape.points.size() < 3U)) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Board spatial index shape has invalid geometry"};
    }

    auto sorted_layers = shape.layers;
    std::sort(sorted_layers.begin(), sorted_layers.end(),
              [](BoardLayerId lhs, BoardLayerId rhs) { return lhs.index() < rhs.index(); });
    if (std::adjacent_find(sorted_layers.begin(), sorted_layers.end()) != sorted_layers.end()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Board spatial index shape layers must be unique"};
    }
    for (const auto layer : shape.layers) {
        if (layer.index() >= state().board->layer_count() ||
            state().board->layer(layer).role() != BoardLayerRole::Copper) {
            throw KernelArgumentError{
                ErrorCode::CrossReferenceViolation,
                "Board spatial index shape layers must be board copper layers",
                EntityRef::board_layer(layer)};
        }
    }
}

void BoardSpatialIndex::index_shape(std::size_t shape_index) {
    const auto &shape = state().shapes[shape_index];
    const auto box = state().boxes[shape_index];
    const auto cell_key_for = [](double value, double cell_size_mm) {
        return static_cast<long long>(std::floor(value / cell_size_mm));
    };
    const auto cell_less_for = [](const detail::BoardSpatialIndexCell &lhs,
                                  const detail::BoardSpatialIndexCell &rhs) {
        if (lhs.layer.index() != rhs.layer.index()) {
            return lhs.layer.index() < rhs.layer.index();
        }
        if (lhs.x != rhs.x) {
            return lhs.x < rhs.x;
        }
        return lhs.y < rhs.y;
    };
    const auto same_cell_key_for = [](const detail::BoardSpatialIndexCell &lhs,
                                      const detail::BoardSpatialIndexCell &rhs) {
        return lhs.layer == rhs.layer && lhs.x == rhs.x && lhs.y == rhs.y;
    };

    const auto min_x = cell_key_for(box.min_x_mm, state().cell_size_mm);
    const auto max_x = cell_key_for(box.max_x_mm, state().cell_size_mm);
    const auto min_y = cell_key_for(box.min_y_mm, state().cell_size_mm);
    const auto max_y = cell_key_for(box.max_y_mm, state().cell_size_mm);

    for (const auto layer : shape.layers) {
        for (auto x = min_x; x <= max_x; ++x) {
            for (auto y = min_y; y <= max_y; ++y) {
                auto key = detail::BoardSpatialIndexCell{layer, x, y, {}};
                auto position = std::lower_bound(mutable_state().cells.begin(),
                                                 mutable_state().cells.end(), key, cell_less_for);
                if (position == mutable_state().cells.end() || !same_cell_key_for(*position, key)) {
                    position = mutable_state().cells.insert(position, std::move(key));
                }
                position->shape_indices.push_back(shape_index);
            }
        }
    }
}

void BoardSpatialIndex::insert(BoardSpatialQueryShape shape) {
    insert(to_copper_shape(std::move(shape)));
}

void BoardSpatialIndex::append_shape(detail::BoardCopperShape shape) {
    ensure_conservative_bound_current();
    validate_shape(shape);
    const auto shape_index = state().shapes.size();
    mutable_state().boxes.push_back(shape_box(shape));
    mutable_state().shapes.push_back(std::move(shape));
    index_shape(shape_index);
}

void BoardSpatialIndex::insert(detail::BoardCopperShape shape) {
    ensure_geometry_current();
    append_shape(std::move(shape));
}

void BoardRouter::SpatialIndexStorage::insert_after_board_mutation(
    BoardSpatialQueryShape shape, std::size_t previous_geometry_mutation_count) {
    detail::insert_after_board_mutation(*this, std::move(shape), previous_geometry_mutation_count);
}

namespace detail {

void insert_after_board_mutation(BoardSpatialIndex &index, BoardSpatialQueryShape shape,
                                 std::size_t previous_geometry_mutation_count) {
    if (index.state().expected_geometry_mutation_count != previous_geometry_mutation_count ||
        index.state().board->geometry_mutation_count() != previous_geometry_mutation_count + 1U) {
        throw KernelLogicError{
            ErrorCode::InvalidState,
            "Board spatial index mirror insert must follow exactly one board geometry mutation"};
    }

    index.append_shape(BoardSpatialIndex::to_copper_shape(std::move(shape)));
    index.mutable_state().expected_geometry_mutation_count =
        index.state().board->geometry_mutation_count();
}

} // namespace detail

[[nodiscard]] std::vector<std::size_t>
BoardSpatialIndex::candidate_obstacles(const BoardSpatialQueryShape &candidate) const {
    return candidate_obstacles(to_copper_shape(candidate));
}

[[nodiscard]] std::vector<std::size_t>
BoardSpatialIndex::candidate_obstacles(const detail::BoardCopperShape &candidate) const {
    ensure_conservative_bound_current();
    ensure_geometry_current();
    validate_shape(candidate);
    const auto query_box = expanded_box(shape_box(candidate), state().conservative_clearance_mm);
    const auto min_x = cell_key(query_box.min_x_mm, state().cell_size_mm);
    const auto max_x = cell_key(query_box.max_x_mm, state().cell_size_mm);
    const auto min_y = cell_key(query_box.min_y_mm, state().cell_size_mm);
    const auto max_y = cell_key(query_box.max_y_mm, state().cell_size_mm);

    auto candidates = std::vector<std::size_t>{};
    for (const auto layer : candidate.layers) {
        for (auto x = min_x; x <= max_x; ++x) {
            for (auto y = min_y; y <= max_y; ++y) {
                const auto key = detail::BoardSpatialIndexCell{layer, x, y, {}};
                const auto position =
                    std::lower_bound(state().cells.begin(), state().cells.end(), key, cell_less);
                if (position == state().cells.end() || !same_cell_key(*position, key)) {
                    continue;
                }
                candidates.insert(candidates.end(), position->shape_indices.begin(),
                                  position->shape_indices.end());
            }
        }
    }

    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                    [this, &candidate, query_box](auto index) {
                                        return !detail::layers_overlap(candidate,
                                                                       state().shapes[index]) ||
                                               !boxes_intersect(query_box, state().boxes[index]);
                                    }),
                     candidates.end());
    return candidates;
}

[[nodiscard]] std::vector<BoardSpatialCandidatePair>
BoardSpatialIndex::copper_clearance_candidates() const {
    ensure_geometry_current();
    auto pairs = std::vector<BoardSpatialCandidatePair>{};
    for (std::size_t lhs_index = 0; lhs_index < state().shapes.size(); ++lhs_index) {
        const auto obstacles = candidate_obstacles(state().shapes[lhs_index]);
        for (const auto rhs_index : obstacles) {
            if (rhs_index <= lhs_index) {
                continue;
            }
            pairs.push_back(BoardSpatialCandidatePair{lhs_index, rhs_index});
        }
    }

    std::sort(pairs.begin(), pairs.end(), [](const auto &lhs, const auto &rhs) {
        if (lhs.lhs_index != rhs.lhs_index) {
            return lhs.lhs_index < rhs.lhs_index;
        }
        return lhs.rhs_index < rhs.rhs_index;
    });
    pairs.erase(std::unique(pairs.begin(), pairs.end(),
                            [](const auto &lhs, const auto &rhs) {
                                return lhs.lhs_index == rhs.lhs_index &&
                                       lhs.rhs_index == rhs.rhs_index;
                            }),
                pairs.end());
    return pairs;
}

[[nodiscard]] BoardSpatialQueryResult
BoardSpatialIndex::query_legality(const BoardSpatialQueryShape &candidate) const {
    const auto clearance_kind = candidate.clearance_kind;
    const auto keepout_restriction = candidate.keepout_restriction;
    return query_legality(to_copper_shape(candidate), clearance_kind, keepout_restriction);
}

[[nodiscard]] BoardSpatialQueryResult
BoardSpatialIndex::query_legality(const detail::BoardCopperShape &candidate,
                                  BoardClearanceKind candidate_kind,
                                  BoardKeepoutRestriction keepout_restriction) const {
    ensure_geometry_current();
    validate_shape(candidate);
    auto result = BoardSpatialQueryResult{};
    result.legal = true;

    const auto obstacles = candidate_obstacles(candidate);
    for (const auto obstacle_index : obstacles) {
        const auto &obstacle = state().shapes[obstacle_index];
        const auto check =
            detail::check_copper_clearance(*state().board, candidate, candidate_kind, obstacle,
                                           detail::shape_clearance_kind(obstacle));
        if (!check.violates) {
            continue;
        }
        result.blockers.push_back(BoardSpatialBlocker{
            BoardSpatialBlockerKind::CopperClearance,
            obstacle_index,
            std::nullopt,
            check.layer,
            check.required_clearance_mm,
            check.actual_clearance_mm,
            check.room,
        });
    }

    if (state().board->outline().has_value()) {
        const auto &outline = state().board->outline().value();
        const auto outline_clearance = state().board->design_rules().clearance_mm(
            candidate_kind, BoardClearanceKind::BoardEdge);
        if (!detail::shape_satisfies_outline(candidate, outline, outline_clearance)) {
            result.blockers.push_back(BoardSpatialBlocker{
                BoardSpatialBlockerKind::BoardOutline,
                std::nullopt,
                std::nullopt,
                candidate.layers.empty() ? std::optional<BoardLayerId>{}
                                         : std::optional<BoardLayerId>{candidate.layers.front()},
                outline_clearance,
                shape_outline_actual_clearance(candidate, outline).value_or(0.0),
                std::nullopt,
            });
        }
    }

    for (std::size_t keepout_index = 0; keepout_index < state().board->keepout_count();
         ++keepout_index) {
        const auto keepout_id = BoardKeepoutId{keepout_index};
        const auto &keepout = state().board->keepout(keepout_id);
        if (!detail::keepout_restricts(keepout, keepout_restriction)) {
            continue;
        }
        const auto layer = detail::first_common_keepout_layer(keepout, candidate.layers);
        if (!layer.has_value() || !detail::shape_violates_keepout(candidate, keepout)) {
            continue;
        }
        result.blockers.push_back(BoardSpatialBlocker{
            BoardSpatialBlockerKind::Keepout,
            std::nullopt,
            keepout_id,
            layer,
            0.0,
            0.0,
            std::nullopt,
        });
    }

    result.legal = result.blockers.empty();
    return result;
}

[[nodiscard]] detail::BoardSpatialIndexState &BoardSpatialIndex::mutable_state() noexcept {
    return *state_;
}

[[nodiscard]] const detail::BoardSpatialIndexState &BoardSpatialIndex::state() const noexcept {
    return *state_;
}

namespace detail {

[[nodiscard]] double maximum_required_copper_clearance(const Board &board) {
    auto result = board.design_rules().copper_clearance_mm();
    for (const auto &entry : board.design_rules().clearance_matrix()) {
        result = std::max(result, entry.clearance_mm);
    }
    for (std::size_t index = 0; index < board.circuit().all<volt::NetClassId>().size(); ++index) {
        const auto clearance = board.circuit().get(NetClassId{index}).copper_clearance_mm();
        if (clearance.has_value()) {
            result = std::max(result, clearance.value());
        }
    }
    for (std::size_t index = 0; index < board.room_count(); ++index) {
        const auto clearance = board.room(BoardRoomId{index}).copper_clearance_mm();
        if (clearance.has_value()) {
            result = std::max(result, clearance.value());
        }
    }
    return result;
}

} // namespace detail

} // namespace volt
