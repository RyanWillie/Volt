#include <volt/io/capabilities/board_capability_profile.hpp>

#include "board_capability_profile_io.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>

namespace volt::io::detail {
namespace {

void require(bool condition, const std::string &message) {
    if (!condition) {
        throw KernelLogicError{ErrorCode::InvalidArgument, message};
    }
}

[[nodiscard]] const nlohmann::json &field(const nlohmann::json &object, const char *name) {
    require(object.is_object(), "Expected object while reading capability profile");
    const auto it = object.find(name);
    require(it != object.end(), std::string{"Missing required field: "} + name);
    return *it;
}

[[nodiscard]] const nlohmann::json &object_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_object(), std::string{"Expected object field: "} + name);
    return value;
}

[[nodiscard]] const nlohmann::json &array_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_array(), std::string{"Expected array field: "} + name);
    return value;
}

[[nodiscard]] std::string string_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_string(), std::string{"Expected string field: "} + name);
    return value.get<std::string>();
}

[[nodiscard]] double number_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_number(), std::string{"Expected number field: "} + name);
    const auto result = value.get<double>();
    require(std::isfinite(result), std::string{"Expected finite number field: "} + name);
    return result;
}

[[nodiscard]] int int_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_number_integer(), std::string{"Expected integer field: "} + name);
    return value.get<int>();
}

void require_profile_format(const nlohmann::json &object) {
    const auto actual = string_field(object, "format");
    require(actual == capability_profile_format_name(),
            "Unsupported capability profile format: " + actual);
}

void require_profile_version(const nlohmann::json &object) {
    const auto actual = static_cast<std::int64_t>(int_field(object, "version"));
    require(actual == static_cast<std::int64_t>(capability_profile_format_version()),
            "Unsupported capability profile format version: " + std::to_string(actual));
}

void write_clearance_entries(std::ostream &out, const std::vector<BoardClearancePair> &clearances) {
    out << '[';
    for (std::size_t index = 0; index < clearances.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        const auto &entry = clearances[index];
        out << "{\"first\": " << json_string(capability_clearance_kind_name(entry.first))
            << ", \"second\": " << json_string(capability_clearance_kind_name(entry.second))
            << ", \"clearance_mm\": ";
        write_json_number(out, entry.clearance_mm);
        out << '}';
    }
    out << ']';
}

void write_copper_weight_refinements(
    std::ostream &out, const std::vector<BoardCapabilityCopperWeightRefinement> &refinements) {
    out << '[';
    for (std::size_t index = 0; index < refinements.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        const auto &refinement = refinements[index];
        out << "{\"copper_weight_oz\": ";
        write_json_number(out, refinement.copper_weight_oz);
        out << ", \"minimum_track_width_mm\": ";
        write_json_number(out, refinement.minimum_track_width_mm);
        out << ", \"minimum_clearance_mm\": ";
        write_json_number(out, refinement.minimum_clearance_mm);
        out << '}';
    }
    out << ']';
}

void write_supported_layer_counts(std::ostream &out, const std::vector<int> &counts) {
    out << '[';
    for (std::size_t index = 0; index < counts.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        out << counts[index];
    }
    out << ']';
}

void write_double_array(std::ostream &out, const std::vector<double> &values) {
    out << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        write_json_number(out, values[index]);
    }
    out << ']';
}

void write_range(std::ostream &out, BoardCapabilityRange range) {
    out << "{\"minimum_mm\": ";
    write_json_number(out, range.minimum_mm);
    out << ", \"maximum_mm\": ";
    write_json_number(out, range.maximum_mm);
    out << '}';
}

[[nodiscard]] std::vector<int> read_supported_layer_counts(const nlohmann::json &profile_json) {
    const auto it = profile_json.find("supported_copper_layer_counts");
    if (it == profile_json.end()) {
        return {};
    }
    require(it->is_array(), "Capability profile supported copper layer counts must be an array");
    auto counts = std::vector<int>{};
    counts.reserve(it->size());
    for (const auto &entry : *it) {
        require(entry.is_number_integer(),
                "Capability profile supported copper layer count must be an integer");
        counts.push_back(entry.get<int>());
    }
    return counts;
}

[[nodiscard]] std::vector<double> read_double_array(const nlohmann::json &profile_json,
                                                    const char *field_name) {
    const auto it = profile_json.find(field_name);
    if (it == profile_json.end()) {
        return {};
    }
    require(it->is_array(), std::string{"Capability profile "} + field_name + " must be an array");
    auto values = std::vector<double>{};
    values.reserve(it->size());
    for (const auto &entry : *it) {
        require(entry.is_number(), std::string{"Capability profile "} + field_name +
                                       " entries must be finite numbers");
        const auto value = entry.get<double>();
        require(std::isfinite(value), std::string{"Capability profile "} + field_name +
                                          " entries must be finite numbers");
        values.push_back(value);
    }
    return values;
}

[[nodiscard]] std::optional<BoardCapabilityRange>
read_optional_range(const nlohmann::json &profile_json, const char *field_name) {
    const auto it = profile_json.find(field_name);
    if (it == profile_json.end()) {
        return std::nullopt;
    }
    require(it->is_object(),
            std::string{"Capability profile "} + field_name + " must be an object");
    return BoardCapabilityRange{
        number_field(*it, "minimum_mm"),
        number_field(*it, "maximum_mm"),
    };
}

[[nodiscard]] std::vector<BoardClearancePair>
read_clearance_entries(const nlohmann::json &profile_json) {
    const auto &clearance_json = array_field(profile_json, "minimum_clearances");
    auto clearances = std::vector<BoardClearancePair>{};
    clearances.reserve(clearance_json.size());
    auto seen_pairs = std::set<std::pair<int, int>>{};
    for (const auto &entry : clearance_json) {
        require(entry.is_object(), "Capability profile clearance entry must be an object");
        const auto first = capability_clearance_kind_from_name(string_field(entry, "first"));
        const auto second = capability_clearance_kind_from_name(string_field(entry, "second"));
        const auto low = std::min(static_cast<int>(first), static_cast<int>(second));
        const auto high = std::max(static_cast<int>(first), static_cast<int>(second));
        if (!seen_pairs.emplace(low, high).second) {
            throw KernelLogicError{ErrorCode::DuplicateName,
                                   "Duplicate capability profile clearance pair"};
        }
        clearances.push_back(
            BoardClearancePair{first, second, number_field(entry, "clearance_mm")});
    }
    return clearances;
}

[[nodiscard]] std::vector<BoardCapabilityCopperWeightRefinement>
read_copper_weight_refinements(const nlohmann::json &profile_json) {
    const auto it = profile_json.find("copper_weight_refinements");
    if (it == profile_json.end()) {
        return {};
    }
    require(it->is_array(), "Capability profile copper-weight refinements must be an array");
    auto refinements = std::vector<BoardCapabilityCopperWeightRefinement>{};
    refinements.reserve(it->size());
    for (const auto &entry : *it) {
        require(entry.is_object(), "Capability profile copper-weight refinement must be an object");
        refinements.push_back(BoardCapabilityCopperWeightRefinement{
            number_field(entry, "copper_weight_oz"),
            number_field(entry, "minimum_track_width_mm"),
            number_field(entry, "minimum_clearance_mm"),
        });
    }
    return refinements;
}

} // namespace

[[nodiscard]] std::string capability_clearance_kind_name(BoardClearanceKind kind) {
    switch (kind) {
    case BoardClearanceKind::Track:
        return "track";
    case BoardClearanceKind::Pad:
        return "pad";
    case BoardClearanceKind::Via:
        return "via";
    case BoardClearanceKind::Zone:
        return "zone";
    case BoardClearanceKind::BoardEdge:
        return "board_edge";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled capability clearance kind"};
}

[[nodiscard]] BoardClearanceKind capability_clearance_kind_from_name(const std::string &value) {
    if (value == "track") {
        return BoardClearanceKind::Track;
    }
    if (value == "pad") {
        return BoardClearanceKind::Pad;
    }
    if (value == "via") {
        return BoardClearanceKind::Via;
    }
    if (value == "zone") {
        return BoardClearanceKind::Zone;
    }
    if (value == "board_edge") {
        return BoardClearanceKind::BoardEdge;
    }
    throw KernelLogicError{ErrorCode::InvalidArgument,
                           "Unknown capability profile clearance kind: " + value};
}

void write_capability_profile_payload(std::ostream &out, const BoardCapabilityProfile &profile) {
    out << "{\"name\": " << json_string(profile.name())
        << ", \"provenance\": {\"source\": " << json_string(profile.provenance().source)
        << ", \"as_of\": " << json_string(profile.provenance().as_of)
        << "}, \"minimum_track_width_mm\": ";
    write_json_number(out, profile.minimum_track_width_mm());
    out << ", \"minimum_via_drill_mm\": ";
    write_json_number(out, profile.minimum_via_drill_mm());
    out << ", \"minimum_via_annular_mm\": ";
    write_json_number(out, profile.minimum_via_annular_mm());
    if (!profile.supported_copper_layer_counts().empty()) {
        out << ", \"supported_copper_layer_counts\": ";
        write_supported_layer_counts(out, profile.supported_copper_layer_counts());
    }
    if (profile.board_thickness_range_mm().has_value()) {
        out << ", \"board_thickness_range_mm\": ";
        write_range(out, profile.board_thickness_range_mm().value());
    }
    if (!profile.available_copper_weights_oz().empty()) {
        out << ", \"available_copper_weights_oz\": ";
        write_double_array(out, profile.available_copper_weights_oz());
    }
    if (profile.drill_diameter_range_mm().has_value()) {
        out << ", \"drill_diameter_range_mm\": ";
        write_range(out, profile.drill_diameter_range_mm().value());
    }
    out << ", \"minimum_clearances\": ";
    write_clearance_entries(out, profile.minimum_clearances());
    if (!profile.copper_weight_refinements().empty()) {
        out << ", \"copper_weight_refinements\": ";
        write_copper_weight_refinements(out, profile.copper_weight_refinements());
    }
    out << '}';
}

[[nodiscard]] BoardCapabilityProfile
read_capability_profile_payload(const nlohmann::json &profile_json) {
    require(profile_json.is_object(), "Capability profile payload must be an object");
    const auto &provenance = object_field(profile_json, "provenance");
    return BoardCapabilityProfile{
        string_field(profile_json, "name"),
        BoardCapabilityProvenance{
            string_field(provenance, "source"),
            string_field(provenance, "as_of"),
        },
        number_field(profile_json, "minimum_track_width_mm"),
        number_field(profile_json, "minimum_via_drill_mm"),
        number_field(profile_json, "minimum_via_annular_mm"),
        read_clearance_entries(profile_json),
        read_copper_weight_refinements(profile_json),
        read_supported_layer_counts(profile_json),
        read_optional_range(profile_json, "board_thickness_range_mm"),
        read_double_array(profile_json, "available_copper_weights_oz"),
        read_optional_range(profile_json, "drill_diameter_range_mm"),
    };
}

[[nodiscard]] BoardCapabilityProfile
read_capability_profile_document(const nlohmann::json &document) {
    require_profile_format(document);
    require_profile_version(document);
    return read_capability_profile_payload(object_field(document, "profile"));
}

} // namespace volt::io::detail

namespace volt::io {

void write_capability_profile(std::ostream &out, const BoardCapabilityProfile &profile) {
    out << "{\n";
    out << "  \"format\": " << detail::json_string(capability_profile_format_name()) << ",\n";
    out << "  \"version\": " << capability_profile_format_version() << ",\n";
    out << "  \"profile\": ";
    detail::write_capability_profile_payload(out, profile);
    out << "\n}\n";
}

[[nodiscard]] std::string write_capability_profile(const BoardCapabilityProfile &profile) {
    auto out = std::ostringstream{};
    write_capability_profile(out, profile);
    return out.str();
}

[[nodiscard]] BoardCapabilityProfile read_capability_profile_text(std::string_view text) {
    auto document = nlohmann::json{};
    try {
        document = nlohmann::json::parse(text.begin(), text.end());
    } catch (const nlohmann::json::parse_error &error) {
        throw KernelLogicError{ErrorCode::InvalidArgument,
                               std::string{"Invalid capability profile JSON: "} + error.what()};
    }
    return detail::read_capability_profile_document(document);
}

[[nodiscard]] BoardCapabilityProfile read_capability_profile(std::istream &input) {
    auto buffer = std::ostringstream{};
    buffer << input.rdbuf();
    return read_capability_profile_text(buffer.str());
}

} // namespace volt::io
