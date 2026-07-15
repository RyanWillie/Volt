#pragma once

#include <cstddef>
#include <vector>

#include <volt/pcb/routing/board_spatial_index.hpp>

namespace volt::detail {

struct BoardSpatialIndexBox {
    double min_x_mm = 0.0;
    double min_y_mm = 0.0;
    double max_x_mm = 0.0;
    double max_y_mm = 0.0;
};

struct BoardSpatialIndexCell {
    BoardLayerId layer;
    long long x = 0;
    long long y = 0;
    std::vector<std::size_t> shape_indices;
};

struct BoardSpatialIndexState {
    const Board *board = nullptr;
    std::vector<BoardCopperShape> shapes;
    std::vector<BoardSpatialIndexBox> boxes;
    std::vector<BoardSpatialIndexCell> cells;
    double conservative_clearance_mm = 0.0;
    double cell_size_mm = 1.0;
    std::optional<board_entity_range_t<BoardLayerId>> geometry_snapshot;
    std::size_t expected_track_count = 0;
    std::size_t expected_via_count = 0;
};

} // namespace volt::detail
