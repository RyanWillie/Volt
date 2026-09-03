#include <volt/io/parts/part_definition_reader.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <istream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>
#include <volt/electrical/passive_model.hpp>
#include <volt/io/parts/electrical_records_io.hpp>
#include <volt/io/parts/part_definition_writer.hpp>

namespace volt::io {

namespace {

using Json = nlohmann::json;

void require(bool condition, const std::string &message) {
    if (!condition) {
        throw KernelLogicError{ErrorCode::InvalidArgument, message};
    }
}

void require_fields(const Json &object, std::initializer_list<std::string_view> allowed,
                    std::string_view label) {
    require(object.is_object(), std::string{label} + " must be an object");
    for (const auto &[name, value] : object.items()) {
        static_cast<void>(value);
        require(std::find(allowed.begin(), allowed.end(), name) != allowed.end(),
                std::string{label} + " contains unknown field: " + name);
    }
}

const Json &field(const Json &object, const char *name) {
    require(object.is_object(), "Expected object while reading part definition");
    const auto iterator = object.find(name);
    require(iterator != object.end(), std::string{"Missing required field: "} + name);
    return *iterator;
}

const Json *optional_field(const Json &object, const char *name) {
    require(object.is_object(), "Expected object while reading part definition");
    const auto iterator = object.find(name);
    return iterator == object.end() ? nullptr : &*iterator;
}

std::string string_field(const Json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_string(), std::string{"Expected string field: "} + name);
    return value.get<std::string>();
}

const Json &array_field(const Json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_array(), std::string{"Expected array field: "} + name);
    return value;
}

double number_field(const Json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_number(), std::string{"Expected number field: "} + name);
    return value.get<double>();
}

void require_format_version(const Json &document, std::int64_t expected_version) {
    require(document.is_object(), "Part definition document must be an object");
    require(string_field(document, "format") == part_definition_format_name(),
            "Unsupported part definition format");
    const auto &version = field(document, "version");
    require(version.is_number_integer(), "Expected integer field: version");
    const auto actual = version.get<std::int64_t>();
    require(actual == expected_version,
            "Unsupported part definition format version: " + std::to_string(actual));
}

PartIdentity identity(const Json &object) {
    require_fields(object, {"namespace", "name", "version"}, "Part identity");
    return PartIdentity{string_field(object, "namespace"), string_field(object, "name"),
                        string_field(object, "version")};
}

PartProvenance provenance(const Json &document) {
    const auto &value = field(document, "provenance");
    require_fields(value, {"datasheet", "authored_by", "derived_from"}, "Part provenance");
    return PartProvenance{string_field(value, "datasheet"), string_field(value, "authored_by"),
                          string_field(value, "derived_from")};
}

UnitDimension model_dimension(const std::string &value) {
    if (value == "resistance") {
        return UnitDimension::Resistance;
    }
    if (value == "capacitance") {
        return UnitDimension::Capacitance;
    }
    if (value == "inductance") {
        return UnitDimension::Inductance;
    }
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid electrical-model dimension"};
}

Quantity model_quantity(const Json &object) {
    require_fields(object, {"dimension", "value"}, "Electrical-model quantity");
    return Quantity{model_dimension(string_field(object, "dimension")),
                    number_field(object, "value")};
}

ModelParameter model_parameter(const Json &object) {
    require_fields(object, {"nominal", "tolerance", "evidence"}, "Electrical-model parameter");
    auto tolerance = std::optional<Tolerance>{};
    const auto &value = field(object, "tolerance");
    if (!value.is_null()) {
        require_fields(value, {"minus", "plus"}, "Electrical-model tolerance");
        tolerance = Tolerance::absolute(model_quantity(field(value, "minus")),
                                        model_quantity(field(value, "plus")));
    }
    auto evidence = std::vector<ContentHash>{};
    for (const auto &reference : array_field(object, "evidence")) {
        require(reference.is_string(), "Electrical-model evidence must be a content hash");
        evidence.emplace_back(reference.get<std::string>());
    }
    return ModelParameter{model_quantity(field(object, "nominal")), tolerance, std::move(evidence)};
}

ModelEndpoint model_endpoint(const Json &object) {
    require_fields(object, {"kind", "key"}, "Electrical-model endpoint");
    const auto kind = string_field(object, "kind");
    if (kind == "terminal") {
        return ModelTerminalKey{string_field(object, "key")};
    }
    if (kind == "internal_node") {
        return ModelInternalNodeKey{string_field(object, "key")};
    }
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid electrical-model endpoint kind"};
}

ModelElement model_element(const Json &object) {
    require_fields(object, {"kind", "key", "from", "to", "parameter"}, "Electrical-model element");
    const auto kind = string_field(object, "kind");
    const auto key = ModelElementKey{string_field(object, "key")};
    const auto from = model_endpoint(field(object, "from"));
    const auto to = model_endpoint(field(object, "to"));
    const auto parameter = model_parameter(field(object, "parameter"));
    if (kind == "resistance") {
        return ResistanceElement{key, from, to, parameter};
    }
    if (kind == "capacitance") {
        return CapacitanceElement{key, from, to, parameter};
    }
    if (kind == "inductance") {
        return InductanceElement{key, from, to, parameter};
    }
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid electrical-model element kind"};
}

std::optional<PartElectricalModel> electrical_model(const Json &document,
                                                    const ComponentDefinition &component) {
    const auto &object = field(document, "electrical_model");
    if (object.is_null()) {
        return std::nullopt;
    }
    require_fields(object, {"implements", "terminals", "internal_nodes", "elements"},
                   "Part electrical model");
    require(string_field(object, "implements") == component.content_identity().value(),
            "Electrical-model component digest mismatch");
    auto terminals = std::vector<ModelTerminal>{};
    for (const auto &terminal : array_field(object, "terminals")) {
        require_fields(terminal, {"key", "pin_key"}, "Electrical-model terminal");
        terminals.emplace_back(ModelTerminalKey{string_field(terminal, "key")},
                               PinKey{string_field(terminal, "pin_key")});
    }
    auto nodes = std::vector<ModelInternalNode>{};
    for (const auto &node : array_field(object, "internal_nodes")) {
        require_fields(node, {"key"}, "Electrical-model internal node");
        nodes.emplace_back(ModelInternalNodeKey{string_field(node, "key")});
    }
    auto elements = std::vector<ModelElement>{};
    for (const auto &element : array_field(object, "elements")) {
        elements.push_back(model_element(element));
    }
    return PartElectricalModel{component, std::move(terminals), std::move(nodes),
                               std::move(elements)};
}

std::optional<PartFootprintPadRole> part_footprint_pad_role(const Json &object,
                                                            bool allow_non_electrical) {
    const auto *role = optional_field(object, "role");
    if (role == nullptr) {
        return std::nullopt;
    }
    require(role->is_string(), "Footprint pad role must be a string");
    const auto value = role->get<std::string>();
    if (value == "mechanical")
        return PartFootprintPadRole::Mechanical;
    if (value == "thermal")
        return PartFootprintPadRole::Thermal;
    if (allow_non_electrical && value == "non_electrical")
        return PartFootprintPadRole::NonElectrical;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid footprint pad role"};
}

PartFootprintPad part_footprint_pad(const Json &object, bool allow_non_electrical) {
    require(object.is_object(), "Footprint pad must be an object");
    require_fields(object, {"label", "x_mm", "y_mm", "width_mm", "height_mm", "role"},
                   "Footprint pad");
    const auto role = part_footprint_pad_role(object, allow_non_electrical);
    if (role.has_value()) {
        return PartFootprintPad{string_field(object, "label"),     number_field(object, "x_mm"),
                                number_field(object, "y_mm"),      number_field(object, "width_mm"),
                                number_field(object, "height_mm"), *role};
    }
    return PartFootprintPad{string_field(object, "label"), number_field(object, "x_mm"),
                            number_field(object, "y_mm"), number_field(object, "width_mm"),
                            number_field(object, "height_mm")};
}

std::vector<PartFootprintPad> footprint_pads(const Json &object, bool allow_non_electrical) {
    auto result = std::vector<PartFootprintPad>{};
    for (const auto &pad : array_field(object, "pads")) {
        result.push_back(part_footprint_pad(pad, allow_non_electrical));
    }
    return result;
}

PartFootprintPoint part_footprint_point(const Json &object) {
    require_fields(object, {"x_mm", "y_mm"}, "Footprint polygon point");
    return PartFootprintPoint{number_field(object, "x_mm"), number_field(object, "y_mm")};
}

PartFootprintPolygon part_footprint_polygon(const Json &value) {
    require(value.is_array(), "Footprint polygon must be an array");
    auto vertices = std::vector<PartFootprintPoint>{};
    vertices.reserve(value.size());
    for (const auto &point : value) {
        vertices.push_back(part_footprint_point(point));
    }
    return PartFootprintPolygon{std::move(vertices)};
}

std::optional<PartFootprintPolygon> optional_part_footprint_polygon(const Json &object,
                                                                    const char *name) {
    const auto *value = optional_field(object, name);
    return value == nullptr ? std::nullopt
                            : std::optional<PartFootprintPolygon>{part_footprint_polygon(*value)};
}

PartFootprintMarkingKind part_footprint_marking_kind(const std::string &value) {
    if (value == "silkscreen")
        return PartFootprintMarkingKind::Silkscreen;
    if (value == "polarity")
        return PartFootprintMarkingKind::Polarity;
    if (value == "pin_1")
        return PartFootprintMarkingKind::PinOne;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid footprint marking kind"};
}

std::vector<PartFootprintMarking> part_footprint_markings(const Json &object) {
    const auto *value = optional_field(object, "markings");
    if (value == nullptr) {
        return {};
    }
    require(value->is_array(), "Footprint markings must be an array");
    auto markings = std::vector<PartFootprintMarking>{};
    for (const auto &marking : *value) {
        require_fields(marking, {"kind", "polygon"}, "Footprint marking");
        markings.emplace_back(part_footprint_marking_kind(string_field(marking, "kind")),
                              part_footprint_polygon(field(marking, "polygon")));
    }
    return markings;
}

std::optional<PartModel3DReference> model_3d(const Json &object) {
    const auto *model = optional_field(object, "model_3d");
    if (model == nullptr) {
        return std::nullopt;
    }
    require_fields(*model, {"format", "file_name", "hash", "translation_mm", "rotation_deg"},
                   "Part 3D model");
    const auto &translation = array_field(*model, "translation_mm");
    require(translation.size() == 3U, "3D model translation must contain three numbers");
    for (const auto &coordinate : translation) {
        require(coordinate.is_number(), "3D model translation entries must be numbers");
    }
    return PartModel3DReference{string_field(*model, "format"), string_field(*model, "file_name"),
                                ContentHash{string_field(*model, "hash")},
                                std::array<double, 3>{translation[0].get<double>(),
                                                      translation[1].get<double>(),
                                                      translation[2].get<double>()},
                                number_field(*model, "rotation_deg")};
}

std::vector<std::string> approved_alternates(const Json &object) {
    auto alternates = std::vector<std::string>{};
    for (const auto &value : array_field(object, "approved_alternate_mpns")) {
        require(value.is_string(), "Approved alternate MPN must be a string");
        alternates.push_back(value.get<std::string>());
    }
    return alternates;
}

OrderablePart orderable_part(const Json &object) {
    require_fields(object,
                   {"manufacturer", "mpn", "package", "footprint", "terminal_pad_mappings",
                    "approved_alternate_mpns", "model_3d"},
                   "Orderable part");
    const auto &footprint = field(object, "footprint");
    require_fields(footprint,
                   {"library", "name", "hash", "pads", "courtyard", "body", "fabrication_outline",
                    "assembly_outline", "markings"},
                   "Part footprint");
    auto mappings = std::vector<PackageTerminalPadMapping>{};
    for (const auto &mapping : array_field(object, "terminal_pad_mappings")) {
        require_fields(mapping, {"terminal", "pads"}, "Terminal-pad mapping");
        auto pads = std::vector<FootprintPadKey>{};
        for (const auto &pad : array_field(mapping, "pads")) {
            require(pad.is_string(), "Footprint pad key must be a string");
            pads.emplace_back(pad.get<std::string>());
        }
        mappings.emplace_back(PackageTerminalKey{string_field(mapping, "terminal")},
                              std::move(pads));
    }
    return OrderablePart{
        ManufacturerPart{string_field(object, "manufacturer"), string_field(object, "mpn")},
        PackageRef{string_field(object, "package")},
        HashedFootprintReference{
            FootprintRef{string_field(footprint, "library"), string_field(footprint, "name")},
            ContentHash{string_field(footprint, "hash")}},
        footprint_pads(footprint, true),
        std::move(mappings),
        approved_alternates(object),
        model_3d(object),
        optional_part_footprint_polygon(footprint, "courtyard"),
        optional_part_footprint_polygon(footprint, "body"),
        optional_part_footprint_polygon(footprint, "fabrication_outline"),
        optional_part_footprint_polygon(footprint, "assembly_outline"),
        part_footprint_markings(footprint)};
}

std::vector<PinPackageTerminalMapping> pin_terminal_mappings(const Json &document) {
    auto mappings = std::vector<PinPackageTerminalMapping>{};
    for (const auto &mapping : array_field(document, "pin_terminal_mappings")) {
        require_fields(mapping, {"pin_key", "terminals"}, "Pin-terminal mapping");
        auto terminals = std::vector<PackageTerminalKey>{};
        for (const auto &terminal : array_field(mapping, "terminals")) {
            require(terminal.is_string(), "Package terminal key must be a string");
            terminals.emplace_back(terminal.get<std::string>());
        }
        mappings.emplace_back(PinKey{string_field(mapping, "pin_key")}, std::move(terminals));
    }
    return mappings;
}

PackageTerminalDisposition terminal_disposition(const std::string &value) {
    if (value == "no_connect")
        return PackageTerminalDisposition::NoConnect;
    if (value == "non_electrical")
        return PackageTerminalDisposition::NonElectrical;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid package terminal disposition"};
}

std::vector<DisposedPackageTerminal> terminal_dispositions(const Json &document) {
    auto dispositions = std::vector<DisposedPackageTerminal>{};
    for (const auto &value : array_field(document, "terminal_dispositions")) {
        require_fields(value, {"terminal", "disposition"}, "Terminal disposition");
        dispositions.emplace_back(PackageTerminalKey{string_field(value, "terminal")},
                                  terminal_disposition(string_field(value, "disposition")));
    }
    return dispositions;
}

std::vector<PartSchematicAssetReference> schematic_assets(const Json &document) {
    auto assets = std::vector<PartSchematicAssetReference>{};
    for (const auto &asset : array_field(document, "schematic_assets")) {
        require_fields(asset, {"name", "variant", "hash"}, "Schematic asset");
        assets.emplace_back(string_field(asset, "name"), string_field(asset, "variant"),
                            ContentHash{string_field(asset, "hash")});
    }
    return assets;
}

PartDefinition read_document(const Json &document, const ComponentDefinition &component) {
    require_format_version(document, part_definition_format_version());
    require_fields(document,
                   {"format", "version", "content_identity", "implements", "identity",
                    "electrical_records", "electrical_model", "pin_terminal_mappings",
                    "terminal_dispositions", "provenance", "schematic_assets", "orderable_part"},
                   "Part definition");
    require(string_field(document, "implements") == component.content_identity().value(),
            "Part definition component digest mismatch");
    auto part = PartDefinition{
        component,
        identity(field(document, "identity")),
        read_electrical_records_text(field(document, "electrical_records").dump()),
        pin_terminal_mappings(document),
        terminal_dispositions(document),
        provenance(document),
        schematic_assets(document),
        orderable_part(field(document, "orderable_part")),
        electrical_model(document, component),
    };
    require(string_field(document, "content_identity") == part.content_identity().value(),
            "Part definition content identity mismatch");
    return part;
}

Json parse_document(std::string_view text) {
    auto object_keys = std::vector<std::set<std::string>>{};
    const auto callback = [&](int, Json::parse_event_t event, Json &parsed) {
        if (event == Json::parse_event_t::object_start) {
            object_keys.emplace_back();
        } else if (event == Json::parse_event_t::key) {
            if (object_keys.empty() ||
                !object_keys.back().insert(parsed.get<std::string>()).second) {
                throw KernelLogicError{ErrorCode::InvalidArgument,
                                       "Part definition contains a duplicate JSON object key"};
            }
        } else if (event == Json::parse_event_t::object_end) {
            object_keys.pop_back();
        }
        return true;
    };
    try {
        return Json::parse(text.begin(), text.end(), callback);
    } catch (const Json::exception &error) {
        throw KernelLogicError{ErrorCode::InvalidArgument, error.what()};
    }
}

} // namespace

PartDefinition read_part_definition_text(std::string_view text,
                                         const ComponentDefinition &component) {
    return read_document(parse_document(text), component);
}

PartDefinition read_part_definition(std::istream &input, const ComponentDefinition &component) {
    auto buffer = std::ostringstream{};
    buffer << input.rdbuf();
    return read_part_definition_text(buffer.str(), component);
}

} // namespace volt::io
