#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <volt/core/ids.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/board_features.hpp>
#include <volt/pcb/board_geometry.hpp>
#include <volt/pcb/board_layers.hpp>
#include <volt/pcb/board_outline.hpp>

namespace volt {

/** A derived, render-oriented view of one board layer in the physical stack. */
struct BoardGeometryStackLayer {
    BoardLayerId layer;
    std::size_t order;
    std::string name;
    BoardLayerRole role;
    BoardLayerSide side;
    double z_mm;
    double thickness_mm;
    bool enabled;
};

/** Derived geometry for a through-board circular opening. */
struct BoardGeometryHoleOpening {
    BoardPoint center;
    double drill_diameter_mm;
    std::optional<double> finished_diameter_mm;
    bool plated;
};

/** Derived geometry for a through-board slotted opening. */
struct BoardGeometrySlotOpening {
    BoardPoint start;
    BoardPoint end;
    double width_mm;
    bool plated;
};

using BoardGeometryOpeningShape = std::variant<BoardGeometryHoleOpening, BoardGeometrySlotOpening>;

/** A derived through-board opening suitable for a bare-board 3D projection. */
struct BoardGeometryOpening {
    BoardFeatureId feature;
    std::string label;
    std::string role;
    BoardGeometryOpeningShape shape;
};

/** A derived through-board polygon cutout suitable for a bare-board 3D projection. */
struct BoardGeometryCutout {
    BoardFeatureId feature;
    std::string label;
    std::string role;
    std::vector<BoardPoint> outline;
};

/** A derived surface board feature suitable for a bare-board 3D projection. */
struct BoardGeometrySurfaceFeature {
    BoardFeatureId feature;
    BoardFeatureKind kind;
    std::string label;
    std::string role;
    BoardPoint center;
    double diameter_mm;
    BoardSide side;
};

/** Derived board-only 3D geometry. This is projection data, not canonical board state. */
struct BoardGeometryProjection {
    BoardUnits units;
    std::optional<double> thickness_mm;
    std::optional<std::vector<BoardPoint>> outline;
    std::vector<BoardGeometryStackLayer> stackup;
    std::vector<BoardGeometryOpening> openings;
    std::vector<BoardGeometryCutout> cutouts;
    std::vector<BoardGeometrySurfaceFeature> surface_features;
};

[[nodiscard]] BoardGeometryProjection project_board_geometry(const Board &board);

} // namespace volt
