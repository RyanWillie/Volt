#include <volt/io/pcb/board_scene.hpp>

#include <utility>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>

namespace volt::io {
namespace {

using Json = nlohmann::ordered_json;

[[nodiscard]] Json point_json(BoardPoint point) {
    return Json{{"x_mm", point.x_mm()}, {"y_mm", point.y_mm()}};
}

[[nodiscard]] Json compiled_identity_json(const CompiledBoardIdentity &identity) {
    return Json{{"board", identity.board().value()},
                {"provenance_digest", identity.provenance_digest().value()}};
}

} // namespace

std::string write_board_scene(const BoardScene &scene) {
    auto models = Json::array();
    for (const auto &model : scene.models()) {
        models.push_back(Json{{"digest", model.reference().digest().value()}});
    }
    auto placements = Json::array();
    for (const auto &placement : scene.placements()) {
        auto transform = Json::array();
        for (const auto value : placement.transform()) {
            transform.push_back(value);
        }
        placements.push_back(Json{{"placement", placement.placement().index()},
                                  {"component", placement.component().index()},
                                  {"reference", placement.reference()},
                                  {"position", point_json(placement.position())},
                                  {"rotation_deg", placement.rotation().degrees()},
                                  {"side", placement.side() == BoardSide::Top ? "top" : "bottom"},
                                  {"transform", std::move(transform)},
                                  {"model_digest", placement.model().has_value()
                                                       ? Json(placement.model()->digest().value())
                                                       : Json(nullptr)}});
    }
    const auto &geometry = scene.geometry();
    auto outline = Json{nullptr};
    if (geometry.outline.has_value()) {
        outline = Json::array();
        for (const auto point : *geometry.outline) {
            outline.push_back(point_json(point));
        }
    }
    auto stackup = Json::array();
    for (const auto &layer : geometry.stackup) {
        stackup.push_back(Json{{"layer", layer.layer.index()},
                               {"order", layer.order},
                               {"name", layer.name},
                               {"z_mm", layer.z_mm},
                               {"thickness_mm", layer.thickness_mm},
                               {"enabled", layer.enabled}});
    }
    return Json{{"format", "volt.board-scene"},
                {"schema_version", 1},
                {"source", compiled_identity_json(scene.source())},
                {"geometry", Json{{"units", "mm"},
                                  {"thickness_mm", geometry.thickness_mm.has_value()
                                                       ? Json(*geometry.thickness_mm)
                                                       : Json(nullptr)},
                                  {"outline", std::move(outline)},
                                  {"stackup", std::move(stackup)}}},
                {"placements", std::move(placements)},
                {"models", std::move(models)}}
        .dump(-1, ' ', false, Json::error_handler_t::strict);
}

BoardScene read_board_scene(std::string_view bytes, const CompiledBoard &compiled) {
    auto scene = BoardScene::from_compiled(compiled);
    if (write_board_scene(scene) != bytes) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "BoardScene payload is not the exact canonical projection of its "
                               "CompiledBoard owner"};
    }
    return scene;
}

} // namespace volt::io
