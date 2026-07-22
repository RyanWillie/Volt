#include <volt/io/parts/footprint_asset.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>

namespace volt::io {
namespace {

using Json = nlohmann::json;

[[nodiscard]] const Json &required(const Json &object, const char *field) {
    const auto match = object.find(field);
    if (match == object.end()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Footprint asset is missing field: " + std::string{field}};
    }
    return *match;
}

[[nodiscard]] std::string required_string(const Json &object, const char *field) {
    const auto &value = required(object, field);
    if (!value.is_string()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Footprint asset field must be a string: " + std::string{field}};
    }
    return value.get<std::string>();
}

[[nodiscard]] double coordinate(const Json &value, const char *label, std::size_t index) {
    if (!value.is_array() || value.size() != 2U || !value[index].is_number()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Footprint asset " + std::string{label} +
                                      " must contain two numeric coordinates"};
    }
    return value[index].get<double>();
}

[[nodiscard]] FootprintPadShape pad_shape(const std::string &value) {
    if (value == "rectangle") {
        return FootprintPadShape::Rectangle;
    }
    if (value == "rounded_rectangle") {
        return FootprintPadShape::RoundedRectangle;
    }
    if (value == "circle") {
        return FootprintPadShape::Circle;
    }
    if (value == "oval") {
        return FootprintPadShape::Oval;
    }
    throw KernelArgumentError{ErrorCode::InvalidArgument,
                              "Footprint asset pad shape is unsupported"};
}

[[nodiscard]] FootprintLayerSet pad_layers(const std::string &value) {
    if (value == "front_smd") {
        return FootprintLayerSet::front_smd();
    }
    if (value == "back_smd") {
        return FootprintLayerSet::back_smd();
    }
    if (value == "through_hole") {
        return FootprintLayerSet::through_hole();
    }
    if (value == "mechanical_hole") {
        return FootprintLayerSet::mechanical_hole();
    }
    throw KernelArgumentError{ErrorCode::InvalidArgument,
                              "Footprint asset pad layer set is unsupported"};
}

[[nodiscard]] std::optional<FootprintPadMechanicalRole> mechanical_role(const Json &value) {
    if (value.is_null()) {
        return std::nullopt;
    }
    if (!value.is_string()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Footprint asset pad mechanical role must be a string or null"};
    }
    const auto role = value.get<std::string>();
    if (role == "mounting" || role == "mechanical") {
        return FootprintPadMechanicalRole::Mounting;
    }
    if (role == "fiducial") {
        return FootprintPadMechanicalRole::Fiducial;
    }
    if (role == "mechanical_support") {
        return FootprintPadMechanicalRole::MechanicalSupport;
    }
    if (role == "thermal") {
        return FootprintPadMechanicalRole::Thermal;
    }
    throw KernelArgumentError{ErrorCode::InvalidArgument,
                              "Footprint asset pad mechanical role is unsupported"};
}

[[nodiscard]] FootprintPadPlating plating(const std::string &value) {
    if (value == "plated") {
        return FootprintPadPlating::Plated;
    }
    if (value == "non_plated") {
        return FootprintPadPlating::NonPlated;
    }
    throw KernelArgumentError{ErrorCode::InvalidArgument,
                              "Footprint asset drill plating is unsupported"};
}

[[nodiscard]] FootprintPad pad(const Json &value) {
    if (!value.is_object()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Footprint asset pad must be an object"};
    }
    const auto label = required_string(value, "label");
    const auto shape = pad_shape(required_string(value, "shape"));
    const auto &position = required(value, "position");
    const auto &size = required(value, "size");
    auto layers = pad_layers(required_string(value, "layers"));
    const auto role = mechanical_role(required(value, "mechanical_role"));
    const auto center =
        FootprintPoint{coordinate(position, "position", 0U), coordinate(position, "position", 1U)};
    const auto dimensions =
        FootprintSize{coordinate(size, "size", 0U), coordinate(size, "size", 1U)};
    const auto kind = required_string(value, "kind");
    if (kind == "surface_mount") {
        if (!required(value, "drill").is_null()) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Surface-mount footprint asset pad must not have a drill"};
        }
        return FootprintPad::surface_mount(label, shape, center, dimensions, std::move(layers),
                                           role);
    }
    if (kind == "through_hole") {
        const auto &drill = required(value, "drill");
        if (!drill.is_object() || !required(drill, "diameter").is_number()) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Through-hole footprint asset pad requires drill data"};
        }
        return FootprintPad::through_hole(
            label, shape, center, dimensions, std::move(layers),
            FootprintDrill{required(drill, "diameter").get<double>(),
                           plating(required_string(drill, "plating"))},
            role);
    }
    throw KernelArgumentError{ErrorCode::InvalidArgument,
                              "Footprint asset pad kind is unsupported"};
}

[[nodiscard]] FootprintPolygon polygon(const Json &value) {
    if (!value.is_array()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Footprint asset polygon must be an array"};
    }
    auto points = std::vector<FootprintPoint>{};
    points.reserve(value.size());
    for (const auto &point : value) {
        points.emplace_back(coordinate(point, "polygon point", 0U),
                            coordinate(point, "polygon point", 1U));
    }
    return FootprintPolygon{std::move(points)};
}

[[nodiscard]] std::optional<FootprintPolygon> optional_polygon(const Json &object,
                                                               const char *field) {
    const auto match = object.find(field);
    if (match == object.end() || match->is_null()) {
        return std::nullopt;
    }
    return polygon(*match);
}

[[nodiscard]] FootprintMarkingKind marking_kind(const std::string &value) {
    if (value == "silkscreen") {
        return FootprintMarkingKind::Silkscreen;
    }
    if (value == "polarity") {
        return FootprintMarkingKind::Polarity;
    }
    if (value == "pin_1") {
        return FootprintMarkingKind::PinOne;
    }
    throw KernelArgumentError{ErrorCode::InvalidArgument,
                              "Footprint asset marking kind is unsupported"};
}

[[nodiscard]] std::vector<FootprintMarking> markings(const Json &object) {
    const auto match = object.find("markings");
    if (match == object.end() || match->is_null()) {
        return {};
    }
    if (!match->is_array()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Footprint asset markings must be an array"};
    }
    auto result = std::vector<FootprintMarking>{};
    result.reserve(match->size());
    for (const auto &entry : *match) {
        if (!entry.is_object()) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Footprint asset marking must be an object"};
        }
        result.emplace_back(marking_kind(required_string(entry, "kind")),
                            polygon(required(entry, "polygon")));
    }
    return result;
}

[[nodiscard]] const char *pad_shape_name(FootprintPadShape shape) {
    switch (shape) {
    case FootprintPadShape::Rectangle:
        return "rectangle";
    case FootprintPadShape::RoundedRectangle:
        return "rounded_rectangle";
    case FootprintPadShape::Circle:
        return "circle";
    case FootprintPadShape::Oval:
        return "oval";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Footprint pad shape is unsupported"};
}

[[nodiscard]] const char *pad_layers_name(const FootprintLayerSet &layers) {
    if (layers.is_front_smd()) {
        return "front_smd";
    }
    if (layers.is_back_smd()) {
        return "back_smd";
    }
    if (layers.is_through_hole()) {
        return "through_hole";
    }
    if (layers.is_mechanical_hole()) {
        return "mechanical_hole";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Footprint pad layer set is unsupported"};
}

[[nodiscard]] Json mechanical_role_json(const std::optional<FootprintPadMechanicalRole> &role) {
    if (!role.has_value()) {
        return nullptr;
    }
    switch (*role) {
    case FootprintPadMechanicalRole::Mounting:
        return "mounting";
    case FootprintPadMechanicalRole::Fiducial:
        return "fiducial";
    case FootprintPadMechanicalRole::MechanicalSupport:
        return "mechanical_support";
    case FootprintPadMechanicalRole::Thermal:
        return "thermal";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Footprint pad mechanical role is unsupported"};
}

[[nodiscard]] Json polygon_json(const FootprintPolygon &polygon) {
    auto result = Json::array();
    for (const auto &point : polygon.vertices()) {
        result.push_back(Json::array({point.x_mm(), point.y_mm()}));
    }
    return result;
}

void add_optional_polygon(Json &document, const char *name,
                          const std::optional<FootprintPolygon> &polygon) {
    if (polygon.has_value()) {
        document[name] = polygon_json(*polygon);
    }
}

[[nodiscard]] const char *marking_kind_name(FootprintMarkingKind kind) {
    switch (kind) {
    case FootprintMarkingKind::Silkscreen:
        return "silkscreen";
    case FootprintMarkingKind::Polarity:
        return "polarity";
    case FootprintMarkingKind::PinOne:
        return "pin_1";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Footprint marking kind is unsupported"};
}

} // namespace

FootprintDefinition read_footprint_asset(std::string_view bytes) {
    try {
        const auto document = Json::parse(bytes);
        if (!document.is_object()) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Footprint asset root must be an object"};
        }
        const auto &reference = required(document, "ref");
        if (!reference.is_array() || reference.size() != 2U || !reference[0].is_string() ||
            !reference[1].is_string()) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Footprint asset ref must contain library and name"};
        }
        const auto &pad_values = required(document, "pads");
        if (!pad_values.is_array()) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Footprint asset pads must be an array"};
        }
        auto pads = std::vector<FootprintPad>{};
        pads.reserve(pad_values.size());
        for (const auto &value : pad_values) {
            pads.push_back(pad(value));
        }
        return FootprintDefinition{
            FootprintRef{reference[0].get<std::string>(), reference[1].get<std::string>()},
            std::move(pads),
            FootprintPackageGeometry{
                optional_polygon(document, "courtyard"), optional_polygon(document, "body"),
                optional_polygon(document, "fabrication_outline"),
                optional_polygon(document, "assembly_outline"), markings(document)}};
    } catch (const KernelError &) {
        throw;
    } catch (const std::exception &error) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Footprint asset JSON is invalid: " + std::string{error.what()}};
    }
}

std::string write_footprint_asset(const FootprintDefinition &definition) {
    auto document = Json::object();
    document["ref"] = Json::array({definition.ref().library(), definition.ref().name()});
    document["pads"] = Json::array();
    for (const auto &pad : definition.pads()) {
        auto item = Json::object();
        item["kind"] =
            pad.kind() == FootprintPadKind::SurfaceMount ? "surface_mount" : "through_hole";
        item["label"] = pad.label();
        item["shape"] = pad_shape_name(pad.shape());
        item["position"] = Json::array({pad.position().x_mm(), pad.position().y_mm()});
        item["size"] = Json::array({pad.size().width_mm(), pad.size().height_mm()});
        item["layers"] = pad_layers_name(pad.layers());
        item["mechanical_role"] = mechanical_role_json(pad.mechanical_role());
        if (pad.drill().has_value()) {
            item["drill"] = Json{{"diameter", pad.drill()->diameter_mm()},
                                 {"plating", pad.drill()->plating() == FootprintPadPlating::Plated
                                                 ? "plated"
                                                 : "non_plated"}};
        } else {
            item["drill"] = nullptr;
        }
        document["pads"].push_back(std::move(item));
    }
    add_optional_polygon(document, "courtyard", definition.courtyard());
    add_optional_polygon(document, "body", definition.body());
    add_optional_polygon(document, "fabrication_outline", definition.fabrication_outline());
    add_optional_polygon(document, "assembly_outline", definition.assembly_outline());
    if (!definition.markings().empty()) {
        document["markings"] = Json::array();
        for (const auto &marking : definition.markings()) {
            document["markings"].push_back(Json{{"kind", marking_kind_name(marking.kind())},
                                                {"polygon", polygon_json(marking.polygon())}});
        }
    }
    return document.dump();
}

} // namespace volt::io
