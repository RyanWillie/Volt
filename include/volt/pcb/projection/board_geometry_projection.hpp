#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <volt/core/ids.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/features/board_features.hpp>
#include <volt/pcb/geometry/board_geometry.hpp>
#include <volt/pcb/geometry/board_outline.hpp>
#include <volt/pcb/layers/board_layers.hpp>

namespace volt {

/** A derived, render-oriented view of one board layer in the physical stack. */
struct BoardGeometryStackLayer {
    /** Stable board-layer ID from the canonical layer stack. */
    BoardLayerId layer;
    /** Zero-based physical stack order. */
    std::size_t order;
    /** Human-facing layer name. */
    std::string name;
    /** Electrical or mechanical role assigned to the layer. */
    BoardLayerRole role;
    /** Board side associated with the layer. */
    BoardLayerSide side;
    /** Bottom-relative layer origin for 3D projection. */
    double z_mm;
    /** Layer thickness used by renderers. */
    double thickness_mm;
    /** Whether the source layer is enabled. */
    bool enabled;
};

/** Derived geometry for a through-board circular opening. */
struct BoardGeometryHoleOpening {
    /** Hole center in board coordinates. */
    BoardPoint center;
    /** Drill diameter before plating or finishing. */
    double drill_diameter_mm;
    /** Finished opening diameter when the source feature declares one. */
    std::optional<double> finished_diameter_mm;
    /** Whether the opening is plated. */
    bool plated;
};

/** Derived geometry for a through-board slotted opening. */
struct BoardGeometrySlotOpening {
    /** Slot start point in board coordinates. */
    BoardPoint start;
    /** Slot end point in board coordinates. */
    BoardPoint end;
    /** Slot width. */
    double width_mm;
    /** Whether the slot is plated. */
    bool plated;
};

/** Shape payload for a derived through-board opening. */
using BoardGeometryOpeningShape = std::variant<BoardGeometryHoleOpening, BoardGeometrySlotOpening>;

/** A derived through-board opening suitable for a bare-board 3D projection. */
struct BoardGeometryOpening {
    /** Source board feature ID. */
    BoardFeatureId feature;
    /** Human-facing source feature label. */
    std::string label;
    /** Source feature role, normalized for serialization. */
    std::string role;
    /** Geometry payload for the opening. */
    BoardGeometryOpeningShape shape;
};

/** A derived through-board polygon cutout suitable for a bare-board 3D projection. */
struct BoardGeometryCutout {
    /** Source board feature ID. */
    BoardFeatureId feature;
    /** Human-facing source feature label. */
    std::string label;
    /** Source feature role, normalized for serialization. */
    std::string role;
    /** Cutout outline in board coordinates. */
    std::vector<BoardPoint> outline;
};

/** A derived surface board feature suitable for a bare-board 3D projection. */
struct BoardGeometrySurfaceFeature {
    /** Source board feature ID. */
    BoardFeatureId feature;
    /** Source board feature kind. */
    BoardFeatureKind kind;
    /** Human-facing source feature label. */
    std::string label;
    /** Source feature role, normalized for serialization. */
    std::string role;
    /** Surface feature center in board coordinates. */
    BoardPoint center;
    /** Surface feature diameter. */
    double diameter_mm;
    /** Physical board side where the feature appears. */
    BoardSide side;
};

/** Derived board-only 3D geometry. This is projection data, not canonical board state. */
struct BoardGeometryProjection {
    /** Units used by the source board. */
    BoardUnits units;
    /** Board thickness when the stack declares one. */
    std::optional<double> thickness_mm;
    /** Board outline when one has been authored. */
    std::optional<std::vector<BoardPoint>> outline;
    /** Physical layer stack projected for renderers. */
    std::vector<BoardGeometryStackLayer> stackup;
    /** Through-board circular and slotted openings. */
    std::vector<BoardGeometryOpening> openings;
    /** Through-board polygon cutouts. */
    std::vector<BoardGeometryCutout> cutouts;
    /** Surface-only board features. */
    std::vector<BoardGeometrySurfaceFeature> surface_features;
};

/** Project canonical board data into derived board-only 3D geometry. */
[[nodiscard]] BoardGeometryProjection project_board_geometry(const Board &board);

} // namespace volt
