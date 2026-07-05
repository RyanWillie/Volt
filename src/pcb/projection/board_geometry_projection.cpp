#include <volt/pcb/projection/board_geometry_projection.hpp>

#include <volt/core/errors.hpp>

#include <cstddef>
#include <optional>
#include <vector>

namespace volt {
namespace {

[[nodiscard]] double layer_z_mm(const Board &board, const LayerStack &stack,
                                std::size_t stack_index) {
    const auto layer_id = stack.layers()[stack_index];
    const auto &layer = board.layer(layer_id);
    const auto half_thickness = stack.board_thickness_mm() / 2.0;
    switch (layer.side()) {
    case BoardLayerSide::Top:
        return half_thickness;
    case BoardLayerSide::Bottom:
        return -half_thickness;
    case BoardLayerSide::Inner:
    case BoardLayerSide::Both:
    case BoardLayerSide::None:
        break;
    }

    if (stack.layers().size() == 1U) {
        return 0.0;
    }
    return half_thickness - ((stack.board_thickness_mm() * static_cast<double>(stack_index)) /
                             static_cast<double>(stack.layers().size() - 1U));
}

void append_stackup(const Board &board, BoardGeometryProjection &projection) {
    if (!board.layer_stack().has_value()) {
        return;
    }

    const auto &stack = board.layer_stack().value();
    projection.thickness_mm = stack.board_thickness_mm();
    projection.stackup.reserve(stack.layers().size());
    for (std::size_t index = 0; index < stack.layers().size(); ++index) {
        const auto layer_id = stack.layers()[index];
        const auto &layer = board.layer(layer_id);
        projection.stackup.push_back(BoardGeometryStackLayer{
            layer_id,
            index,
            layer.name(),
            layer.role(),
            layer.side(),
            layer_z_mm(board, stack, index),
            layer.thickness_mm(),
            layer.enabled(),
        });
    }
}

void append_feature(BoardGeometryProjection &projection, BoardFeatureId id,
                    const BoardFeature &feature) {
    switch (feature.kind()) {
    case BoardFeatureKind::Hole: {
        const auto &hole = feature.hole();
        projection.openings.push_back(BoardGeometryOpening{
            id,
            feature.label(),
            feature.role(),
            BoardGeometryHoleOpening{
                hole.center(),
                hole.drill_diameter_mm(),
                hole.finished_diameter_mm(),
                hole.plated(),
            },
        });
        return;
    }
    case BoardFeatureKind::Slot: {
        const auto &slot = feature.slot();
        projection.openings.push_back(BoardGeometryOpening{
            id,
            feature.label(),
            feature.role(),
            BoardGeometrySlotOpening{
                slot.start(),
                slot.end(),
                slot.width_mm(),
                slot.plated(),
            },
        });
        return;
    }
    case BoardFeatureKind::Cutout:
        projection.cutouts.push_back(BoardGeometryCutout{
            id,
            feature.label(),
            feature.role(),
            feature.cutout().outline(),
        });
        return;
    case BoardFeatureKind::Circle:
        projection.surface_features.push_back(BoardGeometrySurfaceFeature{
            id,
            feature.kind(),
            feature.label(),
            feature.role(),
            feature.circle().center(),
            feature.circle().diameter_mm(),
            feature.circle().side(),
        });
        return;
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled board feature kind"};
}

void append_features(const Board &board, BoardGeometryProjection &projection) {
    projection.openings.reserve(board.feature_count());
    projection.cutouts.reserve(board.feature_count());
    projection.surface_features.reserve(board.feature_count());
    for (std::size_t index = 0; index < board.feature_count(); ++index) {
        const auto id = BoardFeatureId{index};
        append_feature(projection, id, board.feature(id));
    }
}

} // namespace

[[nodiscard]] BoardGeometryProjection project_board_geometry(const Board &board) {
    auto projection = BoardGeometryProjection{
        board.units(), std::nullopt, std::nullopt, {}, {}, {}, {},
    };
    if (board.outline().has_value()) {
        projection.outline = board.outline()->vertices();
    }
    append_stackup(board, projection);
    append_features(board, projection);
    return projection;
}

} // namespace volt
