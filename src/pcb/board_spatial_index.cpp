#include <volt/pcb/board_spatial_index.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
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

[[nodiscard]] bool BoardSpatialIndex::cell_less(const Cell &lhs, const Cell &rhs) {
    if (lhs.layer.index() != rhs.layer.index()) {
        return lhs.layer.index() < rhs.layer.index();
    }
    if (lhs.x != rhs.x) {
        return lhs.x < rhs.x;
    }
    return lhs.y < rhs.y;
}

[[nodiscard]] bool BoardSpatialIndex::same_cell_key(const Cell &lhs, const Cell &rhs) {
    return lhs.layer == rhs.layer && lhs.x == rhs.x && lhs.y == rhs.y;
}

[[nodiscard]] BoardSpatialIndex::Box
BoardSpatialIndex::shape_box(const detail::BoardCopperShape &shape) {
    auto box = Box{
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

[[nodiscard]] BoardSpatialIndex::Box BoardSpatialIndex::outline_box(const BoardOutline &outline) {
    auto box = Box{
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

[[nodiscard]] BoardSpatialIndex::Box BoardSpatialIndex::merge_box(Box lhs, Box rhs) {
    lhs.min_x_mm = std::min(lhs.min_x_mm, rhs.min_x_mm);
    lhs.min_y_mm = std::min(lhs.min_y_mm, rhs.min_y_mm);
    lhs.max_x_mm = std::max(lhs.max_x_mm, rhs.max_x_mm);
    lhs.max_y_mm = std::max(lhs.max_y_mm, rhs.max_y_mm);
    return lhs;
}

[[nodiscard]] BoardSpatialIndex::Box BoardSpatialIndex::expanded_box(Box box, double expansion_mm) {
    box.min_x_mm -= expansion_mm;
    box.min_y_mm -= expansion_mm;
    box.max_x_mm += expansion_mm;
    box.max_y_mm += expansion_mm;
    return box;
}

[[nodiscard]] bool BoardSpatialIndex::boxes_intersect(Box lhs, Box rhs) {
    return lhs.min_x_mm <= rhs.max_x_mm && lhs.max_x_mm >= rhs.min_x_mm &&
           lhs.min_y_mm <= rhs.max_y_mm && lhs.max_y_mm >= rhs.min_y_mm;
}

[[nodiscard]] long long BoardSpatialIndex::cell_key(double value, double cell_size_mm) {
    return static_cast<long long>(std::floor(value / cell_size_mm));
}

[[nodiscard]] double BoardSpatialIndex::extent_cell_size(const Board &board,
                                                         const std::vector<Box> &boxes) {
    auto extent = std::optional<Box>{};
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
                                detail::board_resolution_footprints(board, footprints);
                            return detail::collect_copper_shapes(
                                board, resolution_footprints,
                                board.resolve_pads(resolution_footprints));
                        }()} {}

BoardSpatialIndex::BoardSpatialIndex(const Board &board,
                                     std::vector<detail::BoardCopperShape> shapes)
    : board_{&board}, shapes_{std::move(shapes)},
      conservative_clearance_mm_{detail::maximum_required_copper_clearance(board)},
      cell_size_mm_{1.0}, expected_geometry_mutation_count_{board.geometry_mutation_count()} {
    boxes_.reserve(shapes_.size());
    for (const auto &shape : shapes_) {
        validate_shape(shape);
        boxes_.push_back(shape_box(shape));
    }
    cell_size_mm_ = extent_cell_size(board, boxes_);
    for (std::size_t index = 0; index < shapes_.size(); ++index) {
        index_shape(index);
    }
}

void BoardSpatialIndex::ensure_conservative_bound_current() const {
    const auto current = detail::maximum_required_copper_clearance(*board_);
    if (current > conservative_clearance_mm_ + detail::board_drc_epsilon) {
        throw std::logic_error{
            "Board spatial index clearance bound is stale; rebuild after board rule changes"};
    }
}

void BoardSpatialIndex::ensure_geometry_current() const {
    if (board_->geometry_mutation_count() != expected_geometry_mutation_count_) {
        throw std::logic_error{
            "Board spatial index is stale; board geometry changed outside the index"};
    }
}

void BoardSpatialIndex::validate_shape(const detail::BoardCopperShape &shape) const {
    if (shape.net.index() >= board_->circuit().net_count()) {
        throw std::invalid_argument{"Board spatial index shape net must belong to the board"};
    }
    if (shape.layers.empty()) {
        throw std::invalid_argument{"Board spatial index shape layers must not be empty"};
    }
    if (!std::isfinite(shape.radius_mm) || shape.radius_mm < 0.0) {
        throw std::invalid_argument{
            "Board spatial index shape radius must be finite and non-negative"};
    }
    if ((shape.kind == detail::BoardCopperShapeKind::Disc && shape.points.size() != 1U) ||
        (shape.kind == detail::BoardCopperShapeKind::Segment && shape.points.size() != 2U) ||
        (shape.kind == detail::BoardCopperShapeKind::Polygon && shape.points.size() < 3U)) {
        throw std::invalid_argument{"Board spatial index shape has invalid geometry"};
    }

    auto sorted_layers = shape.layers;
    std::sort(sorted_layers.begin(), sorted_layers.end(),
              [](BoardLayerId lhs, BoardLayerId rhs) { return lhs.index() < rhs.index(); });
    if (std::adjacent_find(sorted_layers.begin(), sorted_layers.end()) != sorted_layers.end()) {
        throw std::invalid_argument{"Board spatial index shape layers must be unique"};
    }
    for (const auto layer : shape.layers) {
        if (layer.index() >= board_->layer_count() ||
            board_->layer(layer).role() != BoardLayerRole::Copper) {
            throw std::invalid_argument{
                "Board spatial index shape layers must be board copper layers"};
        }
    }
}

void BoardSpatialIndex::index_shape(std::size_t shape_index) {
    const auto &shape = shapes_[shape_index];
    const auto box = boxes_[shape_index];
    const auto min_x = cell_key(box.min_x_mm, cell_size_mm_);
    const auto max_x = cell_key(box.max_x_mm, cell_size_mm_);
    const auto min_y = cell_key(box.min_y_mm, cell_size_mm_);
    const auto max_y = cell_key(box.max_y_mm, cell_size_mm_);

    for (const auto layer : shape.layers) {
        for (auto x = min_x; x <= max_x; ++x) {
            for (auto y = min_y; y <= max_y; ++y) {
                auto key = Cell{layer, x, y, {}};
                auto position = std::lower_bound(cells_.begin(), cells_.end(), key, cell_less);
                if (position == cells_.end() || !same_cell_key(*position, key)) {
                    position = cells_.insert(position, std::move(key));
                }
                position->shape_indices.push_back(shape_index);
            }
        }
    }
}

void BoardSpatialIndex::insert(BoardSpatialQueryShape shape) {
    insert(to_copper_shape(std::move(shape)));
}

void BoardSpatialIndex::insert(detail::BoardCopperShape shape) {
    ensure_conservative_bound_current();
    validate_shape(shape);
    const auto shape_index = shapes_.size();
    boxes_.push_back(shape_box(shape));
    shapes_.push_back(std::move(shape));
    index_shape(shape_index);
    expected_geometry_mutation_count_ = board_->geometry_mutation_count();
}

[[nodiscard]] std::vector<std::size_t>
BoardSpatialIndex::candidate_obstacles(const BoardSpatialQueryShape &candidate) const {
    return candidate_obstacles(to_copper_shape(candidate));
}

[[nodiscard]] std::vector<std::size_t>
BoardSpatialIndex::candidate_obstacles(const detail::BoardCopperShape &candidate) const {
    ensure_conservative_bound_current();
    ensure_geometry_current();
    validate_shape(candidate);
    const auto query_box = expanded_box(shape_box(candidate), conservative_clearance_mm_);
    const auto min_x = cell_key(query_box.min_x_mm, cell_size_mm_);
    const auto max_x = cell_key(query_box.max_x_mm, cell_size_mm_);
    const auto min_y = cell_key(query_box.min_y_mm, cell_size_mm_);
    const auto max_y = cell_key(query_box.max_y_mm, cell_size_mm_);

    auto candidates = std::vector<std::size_t>{};
    for (const auto layer : candidate.layers) {
        for (auto x = min_x; x <= max_x; ++x) {
            for (auto y = min_y; y <= max_y; ++y) {
                const auto key = Cell{layer, x, y, {}};
                const auto position =
                    std::lower_bound(cells_.begin(), cells_.end(), key, cell_less);
                if (position == cells_.end() || !same_cell_key(*position, key)) {
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
                                        return !detail::layers_overlap(candidate, shapes_[index]) ||
                                               !boxes_intersect(query_box, boxes_[index]);
                                    }),
                     candidates.end());
    return candidates;
}

[[nodiscard]] std::vector<BoardSpatialCandidatePair>
BoardSpatialIndex::copper_clearance_candidates() const {
    ensure_geometry_current();
    auto pairs = std::vector<BoardSpatialCandidatePair>{};
    for (std::size_t lhs_index = 0; lhs_index < shapes_.size(); ++lhs_index) {
        const auto obstacles = candidate_obstacles(shapes_[lhs_index]);
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
        const auto &obstacle = shapes_[obstacle_index];
        const auto check = detail::check_copper_clearance(
            *board_, candidate, candidate_kind, obstacle, detail::shape_clearance_kind(obstacle));
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

    if (board_->outline().has_value()) {
        const auto &outline = board_->outline().value();
        const auto outline_clearance =
            board_->design_rules().clearance_mm(candidate_kind, BoardClearanceKind::BoardEdge);
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

    for (std::size_t keepout_index = 0; keepout_index < board_->keepout_count(); ++keepout_index) {
        const auto keepout_id = BoardKeepoutId{keepout_index};
        const auto &keepout = board_->keepout(keepout_id);
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

namespace detail {

[[nodiscard]] double maximum_required_copper_clearance(const Board &board) {
    auto result = board.design_rules().copper_clearance_mm();
    for (const auto &entry : board.design_rules().clearance_matrix()) {
        result = std::max(result, entry.clearance_mm);
    }
    for (std::size_t index = 0; index < board.circuit().net_class_count(); ++index) {
        const auto clearance = board.circuit().net_class(NetClassId{index}).copper_clearance_mm();
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
