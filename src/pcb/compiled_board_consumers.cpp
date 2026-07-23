#include <volt/pcb/compiled/board_consumers.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <utility>

#include <volt/core/errors.hpp>
#include <volt/pcb/queries/board_queries.hpp>

namespace volt {
namespace {

using Matrix4 = std::array<double, 16>;

[[nodiscard]] Matrix4 identity_matrix() noexcept {
    return {1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0};
}

[[nodiscard]] Matrix4 multiply(const Matrix4 &lhs, const Matrix4 &rhs) noexcept {
    auto result = Matrix4{};
    for (std::size_t row = 0; row < 4U; ++row) {
        for (std::size_t column = 0; column < 4U; ++column) {
            for (std::size_t inner = 0; inner < 4U; ++inner) {
                result[(row * 4U) + column] += lhs[(row * 4U) + inner] * rhs[(inner * 4U) + column];
            }
        }
    }
    return result;
}

[[nodiscard]] Matrix4 translation(double x, double y, double z) noexcept {
    auto result = identity_matrix();
    result[3] = x;
    result[7] = y;
    result[11] = z;
    return result;
}

[[nodiscard]] Matrix4 rotation_z(double degrees) noexcept {
    constexpr auto pi = 3.14159265358979323846;
    const auto radians = degrees * pi / 180.0;
    const auto cosine = std::cos(radians);
    const auto sine = std::sin(radians);
    return {cosine, -sine, 0.0, 0.0, sine, cosine, 0.0, 0.0,
            0.0,    0.0,   1.0, 0.0, 0.0,  0.0,    0.0, 1.0};
}

[[nodiscard]] Matrix4 scale(double x, double y, double z) noexcept {
    return {x, 0.0, 0.0, 0.0, 0.0, y, 0.0, 0.0, 0.0, 0.0, z, 0.0, 0.0, 0.0, 0.0, 1.0};
}

[[nodiscard]] double surface_z(const BoardGeometryProjection &geometry, BoardSide side) noexcept {
    const auto expected = side == BoardSide::Top ? BoardLayerSide::Top : BoardLayerSide::Bottom;
    const auto match =
        std::ranges::find(geometry.stackup, expected, &BoardGeometryStackLayer::side);
    return match == geometry.stackup.end() ? 0.0 : match->z_mm;
}

[[nodiscard]] const ResolvedBoardPart *part_for_component(const CompiledBoard &compiled,
                                                          ComponentId component) noexcept {
    const auto parts = compiled.parts();
    const auto match = std::ranges::find(parts, component, &ResolvedBoardPart::component);
    return match != parts.end() && match->component() == component ? &*match : nullptr;
}

[[noreturn]] void reject_scene_reference(std::string message) {
    throw KernelLogicError{ErrorCode::CrossReferenceViolation, message};
}

[[nodiscard]] ContentHash checked_consumed_glb_digest(const CompiledBoard &compiled,
                                                      ComponentId component) {
    const auto *part = part_for_component(compiled, component);
    if (!compiled.capabilities().has(BoardAssetCapability::Models3D) || part == nullptr ||
        !part->physical_part().model_3d().has_value() ||
        part->physical_part().model_3d()->format() != "glb" ||
        !part->model_3d_bytes().has_value()) {
        reject_scene_reference(
            "BoardScene model reference has no consumed GLB in the exact CompiledBoard");
    }
    return sha256_content_hash(*part->model_3d_bytes());
}

[[nodiscard]] Matrix4 placement_transform(const ComponentPlacement &placement, double z,
                                          const PartModel3D *model) noexcept {
    auto result = multiply(translation(placement.position().x_mm(), placement.position().y_mm(), z),
                           rotation_z(placement.rotation().degrees()));
    if (placement.side() == BoardSide::Bottom) {
        result = multiply(result, scale(-1.0, 1.0, -1.0));
    }
    if (model != nullptr) {
        result =
            multiply(result, translation(model->translation_mm()[0], model->translation_mm()[1],
                                         model->translation_mm()[2]));
        result = multiply(result, rotation_z(model->rotation_deg()));
    }
    for (auto &value : result) {
        if (std::abs(value) < 1.0e-12) {
            value = 0.0;
        }
    }
    return result;
}

} // namespace

CompiledBoardValidation::CompiledBoardValidation(CompiledBoardIdentity source,
                                                 DiagnosticReport diagnostics)
    : source_{std::move(source)}, diagnostics_{std::move(diagnostics)} {}

CompiledBoardRatsnest::CompiledBoardRatsnest(CompiledBoardIdentity source,
                                             std::vector<RatsnestEdge> edges)
    : source_{std::move(source)}, edges_{std::move(edges)} {}

CompiledBoardCpl::CompiledBoardCpl(CompiledBoardIdentity source, Cpl cpl)
    : source_{std::move(source)}, cpl_{std::move(cpl)} {}

BoardSceneModelRef::BoardSceneModelRef(const CompiledBoard &compiled, ComponentId component)
    : source_{compiled.identity()}, digest_{checked_consumed_glb_digest(compiled, component)} {}

bool BoardSceneModelRef::operator==(const BoardSceneModelRef &other) const noexcept {
    return source_ == other.source_ && digest_ == other.digest_;
}

BoardSceneModel::BoardSceneModel(BoardSceneModelRef reference) : reference_{std::move(reference)} {}

BoardScenePlacement::BoardScenePlacement(ComponentPlacementId placement, ComponentId component,
                                         std::string reference, BoardPoint position,
                                         BoardRotation rotation, BoardSide side,
                                         std::array<double, 16> transform,
                                         std::optional<BoardSceneModelRef> model)
    : placement_{placement}, component_{component}, reference_{std::move(reference)},
      position_{position}, rotation_{rotation}, side_{side}, transform_{transform},
      model_{std::move(model)} {}

BoardScene::BoardScene(CompiledBoardIdentity source, BoardGeometryProjection geometry,
                       std::vector<BoardScenePlacement> placements,
                       std::vector<BoardSceneModel> models)
    : source_{std::move(source)}, geometry_{std::move(geometry)},
      placements_{std::move(placements)}, models_{std::move(models)} {}

BoardScene BoardScene::from_compiled(const CompiledBoard &compiled) {
    auto models = std::vector<BoardSceneModel>{};
    auto component_models = std::vector<std::pair<ComponentId, BoardSceneModelRef>>{};
    if (compiled.capabilities().has(BoardAssetCapability::Models3D)) {
        models.reserve(compiled.parts().size());
        component_models.reserve(compiled.parts().size());
        for (const auto &part : compiled.parts()) {
            const auto &model = part.physical_part().model_3d();
            if (!model.has_value() || model->format() != "glb") {
                continue;
            }
            if (!part.model_3d_bytes().has_value()) {
                throw KernelLogicError{
                    ErrorCode::InvalidState,
                    "CompiledBoard models3d closure is missing bytes required by BoardScene",
                    EntityRef::component(part.component())};
            }
            const auto reference = BoardSceneModelRef{compiled, part.component()};
            const auto duplicate =
                std::ranges::any_of(models, [&](const BoardSceneModel &candidate) {
                    return candidate.reference().digest() == reference.digest();
                });
            if (!duplicate) {
                models.emplace_back(reference);
            }
            component_models.emplace_back(part.component(), reference);
        }
    }

    auto geometry = project_board_geometry(compiled.board());
    auto placements = std::vector<BoardScenePlacement>{};
    placements.reserve(compiled.board().all<ComponentPlacementId>().size());
    for (std::size_t index = 0; index < compiled.board().all<ComponentPlacementId>().size();
         ++index) {
        const auto placement_id = ComponentPlacementId{index};
        const auto &placement = compiled.board().get(placement_id);
        const auto model_match = std::ranges::find_if(component_models, [&](const auto &candidate) {
            return candidate.first == placement.component();
        });
        const auto model_reference = model_match == component_models.end()
                                         ? std::optional<BoardSceneModelRef>{}
                                         : std::optional<BoardSceneModelRef>{model_match->second};
        const auto *part = part_for_component(compiled, placement.component());
        const auto *model = part != nullptr && part->physical_part().model_3d().has_value() &&
                                    part->physical_part().model_3d()->format() == "glb"
                                ? &*part->physical_part().model_3d()
                                : nullptr;
        placements.emplace_back(
            placement_id, placement.component(),
            compiled.board().circuit().get(placement.component()).reference().value(),
            placement.position(), placement.rotation(), placement.side(),
            placement_transform(placement, surface_z(geometry, placement.side()), model),
            model_reference);
    }

    return BoardScene{compiled.identity(), std::move(geometry), std::move(placements),
                      std::move(models)};
}

CompiledBoardValidation validate_board(const CompiledBoard &compiled) {
    return CompiledBoardValidation{compiled.identity(),
                                   validate_board(compiled.board(), compiled.footprints())};
}

CompiledBoardRatsnest compute_ratsnest(const CompiledBoard &compiled) {
    return CompiledBoardRatsnest{
        compiled.identity(),
        queries::ratsnest_edges(compiled.board(), compiled.footprints()),
    };
}

CompiledBoardCpl project_cpl(const CompiledBoard &compiled) {
    return project_cpl(compiled, CplProjectionOptions{});
}

CompiledBoardCpl project_cpl(const CompiledBoard &compiled, const CplProjectionOptions &options) {
    return CompiledBoardCpl{
        compiled.identity(),
        project_cpl(compiled.board(), compiled.footprints(), options),
    };
}

BoardScene prepare_board_scene(const CompiledBoard &compiled) {
    return BoardScene::from_compiled(compiled);
}

std::string_view resolve_board_scene_model(const BoardScene &scene, const CompiledBoard &compiled,
                                           const BoardSceneModelRef &reference) {
    if (!(scene.source() == compiled.identity())) {
        reject_scene_reference("BoardScene belongs to a different CompiledBoard revision");
    }
    if (!(reference.source() == scene.source())) {
        reject_scene_reference("BoardScene model reference belongs to a foreign or stale revision");
    }
    const auto scene_model =
        std::ranges::find(scene.models(), reference, &BoardSceneModel::reference);
    if (scene_model == scene.models().end()) {
        reject_scene_reference("BoardScene model reference is not in the exact consumed GLB set");
    }
    const auto part =
        std::ranges::find_if(compiled.parts(), [&](const ResolvedBoardPart &candidate) {
            return candidate.physical_part().model_3d().has_value() &&
                   candidate.physical_part().model_3d()->format() == "glb" &&
                   candidate.model_3d_bytes().has_value() &&
                   sha256_content_hash(*candidate.model_3d_bytes()) == reference.digest();
        });
    if (part == compiled.parts().end()) {
        reject_scene_reference("BoardScene model reference has no consumed GLB in CompiledBoard");
    }
    return *part->model_3d_bytes();
}

} // namespace volt
