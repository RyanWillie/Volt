#include "pcb_feature_io.hpp"

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>
#include <volt/io/detail/typed_id.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/io/pcb/pcb_schema.hpp>
#include <volt/io/pcb/pcb_writer.hpp>

namespace volt::io::detail {
namespace {

void require(bool condition, const std::string &message) {
    if (!condition) {
        throw KernelLogicError{ErrorCode::InvalidArgument, message};
    }
}

const nlohmann::json &field(const nlohmann::json &object, const char *name) {
    require(object.is_object(), "Expected object while reading PCB projection");
    const auto it = object.find(name);
    require(it != object.end(), std::string{"Missing required field: "} + name);
    return *it;
}

const nlohmann::json *optional_field(const nlohmann::json &object, const char *name) {
    require(object.is_object(), "Expected object while reading PCB projection");
    const auto it = object.find(name);
    if (it == object.end()) {
        return nullptr;
    }
    return &*it;
}

const nlohmann::json &array_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_array(), std::string{"Expected array field: "} + name);
    return value;
}

std::string string_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_string(), std::string{"Expected string field: "} + name);
    return value.get<std::string>();
}

bool bool_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_boolean(), std::string{"Expected boolean field: "} + name);
    return value.get<bool>();
}

double number_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_number(), std::string{"Expected number field: "} + name);
    return value.get<double>();
}

template <typename Id> Id typed_id(const nlohmann::json &object, const char *name) {
    return decode_local_id<Id>(string_field(object, name));
}

void require_sequential_id(const nlohmann::json &object, const char *name, BoardFeatureId expected,
                           const std::string &message) {
    require(typed_id<BoardFeatureId>(object, name) == expected, message);
}

[[nodiscard]] BoardPoint board_point(const nlohmann::json &value) {
    require(value.is_array(), "PCB point must be an array");
    require(value.size() == 2U, "PCB point must contain two numbers");
    require(value[0].is_number() && value[1].is_number(), "PCB point values must be numbers");
    return BoardPoint{value[0].get<double>(), value[1].get<double>()};
}

[[nodiscard]] std::vector<BoardPoint> board_points(const nlohmann::json &value) {
    require(value.is_array(), "PCB point list must be an array");
    auto points = std::vector<BoardPoint>{};
    points.reserve(value.size());
    for (const auto &point : value) {
        points.push_back(board_point(point));
    }
    return points;
}

[[nodiscard]] std::string optional_string_field(const nlohmann::json &object, const char *name) {
    const auto *value = optional_field(object, name);
    return value == nullptr ? std::string{} : string_field(object, name);
}

[[nodiscard]] std::optional<double> optional_finished_diameter(const nlohmann::json &feature) {
    const auto *value = optional_field(feature, "finished_diameter_mm");
    if (value == nullptr || value->is_null()) {
        return std::nullopt;
    }
    require(value->is_number(), "PCB board hole finished diameter must be a number");
    return value->get<double>();
}

[[nodiscard]] double drill_diameter(const nlohmann::json &feature) {
    const auto *drill = optional_field(feature, "drill_diameter_mm");
    return drill == nullptr ? number_field(feature, "diameter_mm")
                            : number_field(feature, "drill_diameter_mm");
}

[[nodiscard]] bool optional_plated(const nlohmann::json &feature) {
    return optional_field(feature, "plated") != nullptr && bool_field(feature, "plated");
}

[[nodiscard]] BoardFeature read_hole_feature(const nlohmann::json &feature,
                                             const std::string &label, const std::string &role) {
    const auto position = board_point(field(feature, "position"));
    const auto drill = drill_diameter(feature);
    const auto finished = optional_finished_diameter(feature);
    return BoardFeature::hole(label, position, drill, optional_plated(feature), role, finished);
}

[[nodiscard]] BoardFeature read_feature(const nlohmann::json &feature) {
    const auto kind = board_feature_kind_from_name(string_field(feature, "kind"));
    const auto label = optional_string_field(feature, "label");
    const auto role = optional_string_field(feature, "role");

    switch (kind) {
    case BoardFeatureKind::Hole:
        return read_hole_feature(feature, label, role);
    case BoardFeatureKind::Slot:
        return BoardFeature::slot(
            label, board_point(field(feature, "start")), board_point(field(feature, "end")),
            number_field(feature, "width_mm"), optional_plated(feature), role);
    case BoardFeatureKind::Cutout:
        return BoardFeature::cutout(label, board_points(field(feature, "outline")), role);
    case BoardFeatureKind::Circle:
        return BoardFeature::circle(label, board_point(field(feature, "position")),
                                    number_field(feature, "diameter_mm"),
                                    optional_field(feature, "side") == nullptr
                                        ? BoardSide::Top
                                        : board_side_from_name(string_field(feature, "side")),
                                    role);
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled PCB board feature kind"};
}

} // namespace

void read_features(Board &board, const nlohmann::json &board_json) {
    const auto &features = array_field(board_json, "features");
    for (std::size_t index = 0; index < features.size(); ++index) {
        const auto &feature = features[index];
        require(feature.is_object(), "PCB board feature must be an object");
        const auto expected = BoardFeatureId{index};
        require_sequential_id(feature, "id", expected, "PCB board feature IDs must be sequential");
        const auto id = board.add_feature(read_feature(feature));
        require(id == expected, "PCB board feature IDs must be sequential");
    }
}

void write_features(std::ostream &out, const Board &board) {
    out << "    \"features\": [\n";
    for (std::size_t index = 0; index < board.feature_count(); ++index) {
        const auto id = BoardFeatureId{index};
        const auto &feature = board.feature(id);
        out << "      {\"id\": " << json_string(encode_local_id(id))
            << ", \"kind\": " << json_string(board_feature_kind_name(feature.kind()))
            << ", \"label\": " << json_string(feature.label());
        switch (feature.kind()) {
        case BoardFeatureKind::Hole: {
            const auto &hole = feature.hole();
            out << ", \"position\": ";
            write_board_point(out, hole.center());
            out << ", \"drill_diameter_mm\": ";
            write_number(out, hole.drill_diameter_mm());
            out << ", \"finished_diameter_mm\": ";
            if (hole.finished_diameter_mm().has_value()) {
                write_number(out, hole.finished_diameter_mm().value());
            } else {
                out << "null";
            }
            out << ", \"plated\": " << (hole.plated() ? "true" : "false")
                << ", \"role\": " << json_string(feature.role());
            break;
        }
        case BoardFeatureKind::Slot: {
            const auto &slot = feature.slot();
            out << ", \"start\": ";
            write_board_point(out, slot.start());
            out << ", \"end\": ";
            write_board_point(out, slot.end());
            out << ", \"width_mm\": ";
            write_number(out, slot.width_mm());
            out << ", \"plated\": " << (slot.plated() ? "true" : "false")
                << ", \"role\": " << json_string(feature.role());
            break;
        }
        case BoardFeatureKind::Cutout:
            out << ", \"outline\": ";
            write_board_points(out, feature.cutout().outline());
            out << ", \"role\": " << json_string(feature.role());
            break;
        case BoardFeatureKind::Circle:
            out << ", \"position\": ";
            write_board_point(out, feature.circle().center());
            out << ", \"diameter_mm\": ";
            write_number(out, feature.circle().diameter_mm());
            out << ", \"side\": " << json_string(board_side_name(feature.circle().side()))
                << ", \"role\": " << json_string(feature.role());
            break;
        }
        out << '}';
        if (index + 1U != board.feature_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ],\n";
}

} // namespace volt::io::detail
