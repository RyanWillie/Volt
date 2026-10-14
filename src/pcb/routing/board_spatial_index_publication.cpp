#include "board_spatial_index_storage.hpp"

#include <algorithm>
#include <numeric>
#include <tuple>

namespace volt {

void BoardSpatialIndex::prepare_route_publication(std::size_t existing_shapes,
                                                  const CopperAppendSpec &append) {
    auto &staged = mutable_state();
    auto track = staged.expected_track_count;
    auto via = staged.expected_via_count;
    for (std::size_t i = 0; i < append.primitives.size(); ++i) {
        auto &shape = staged.shapes.at(existing_shapes + i);
        shape.primary_entities = std::holds_alternative<BoardTrack>(append.primitives[i])
                                     ? std::vector{EntityRef::board_track(BoardTrackId{track++})}
                                     : std::vector{EntityRef::board_via(BoardViaId{via++})};
    }
    // Native fresh order is tracks, vias, zones, then placed pads. Stable sorting preserves
    // each polyline's segment order and each placement's pad order. Remap existing cells;
    // neither copper resolution nor geometric indexing is repeated.
    const auto rank = [](const detail::BoardCopperShape &shape) {
        const auto &entity = shape.primary_entities.front();
        const auto family = entity.kind() == EntityKind::BoardTrack  ? 0
                            : entity.kind() == EntityKind::BoardVia  ? 1
                            : entity.kind() == EntityKind::BoardZone ? 2
                                                                     : 3;
        return std::pair{family, entity.index()};
    };
    auto order = std::vector<std::size_t>(staged.shapes.size());
    std::iota(order.begin(), order.end(), 0U);
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
        return rank(staged.shapes[a]) < rank(staged.shapes[b]);
    });
    auto mapping = std::vector<std::size_t>(order.size());
    auto shapes = std::vector<detail::BoardCopperShape>{};
    auto boxes = std::vector<detail::BoardSpatialIndexBox>{};
    shapes.reserve(order.size());
    boxes.reserve(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        mapping[order[i]] = i;
        shapes.push_back(std::move(staged.shapes[order[i]]));
        boxes.push_back(staged.boxes[order[i]]);
    }
    for (auto &cell : staged.cells) {
        for (auto &index : cell.shape_indices)
            index = mapping[index];
        std::sort(cell.shape_indices.begin(), cell.shape_indices.end());
    }
    staged.shapes = std::move(shapes);
    staged.boxes = std::move(boxes);
    staged.expected_track_count = track;
    staged.expected_via_count = via;
}

void BoardSpatialIndex::adopt_route_publication() noexcept {
    mutable_state().geometry_snapshot = state().board->all<BoardLayerId>();
}

} // namespace volt
