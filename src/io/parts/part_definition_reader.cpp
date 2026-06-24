#include <volt/io/parts/part_definition_reader.hpp>

#include <array>
#include <cstdint>
#include <istream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/io/parts/part_definition_writer.hpp>

namespace volt::io {

namespace {

class PartDefinitionReader {
  public:
    explicit PartDefinitionReader(const nlohmann::json &document) : document_{document} {}

    [[nodiscard]] PartDefinition read();

  private:
    static void require(bool condition, const std::string &message);

    static const nlohmann::json &field(const nlohmann::json &object, const char *name);

    static const nlohmann::json *optional_field(const nlohmann::json &object, const char *name);

    static std::string string_field(const nlohmann::json &object, const char *name);

    static std::string optional_string_field(const nlohmann::json &object, const char *name,
                                             std::string default_value);

    static const nlohmann::json &array_field(const nlohmann::json &object, const char *name);

    static void require_format(const nlohmann::json &object);

    static void require_version(const nlohmann::json &object);

    [[nodiscard]] static double number_field(const nlohmann::json &object, const char *name);

    [[nodiscard]] static ConnectionRequirement connection_requirement(const std::string &value);

    [[nodiscard]] static ElectricalTerminalKind electrical_terminal_kind(const std::string &value);

    [[nodiscard]] static ElectricalDirection electrical_direction(const std::string &value);

    [[nodiscard]] static ElectricalSignalDomain electrical_signal_domain(const std::string &value);

    [[nodiscard]] static ElectricalDriveKind electrical_drive_kind(const std::string &value);

    [[nodiscard]] static ElectricalPolarity electrical_polarity(const std::string &value);

    [[nodiscard]] static UnitDimension unit_dimension(const std::string &value);

    [[nodiscard]] static ToleranceMode tolerance_mode(const std::string &value);

    [[nodiscard]] static ElectricalAttributeValue
    electrical_attribute_value(const nlohmann::json &object);

    [[nodiscard]] static ElectricalAttributeMap
    electrical_attributes(const nlohmann::json &object, ElectricalAttributeOwner owner,
                          ElectricalAttributeKind kind);

    [[nodiscard]] static ElectricalAttributeMap
    optional_electrical_attributes(const nlohmann::json &object, ElectricalAttributeOwner owner,
                                   ElectricalAttributeKind kind);

    [[nodiscard]] static PartIdentity identity(const nlohmann::json &object);

    [[nodiscard]] static std::vector<PartPin> pins(const nlohmann::json &object);

    [[nodiscard]] static PartProvenance provenance(const nlohmann::json &object);

    [[nodiscard]] static std::vector<HashedSchematicSymbolReference>
    symbols(const nlohmann::json &object);

    [[nodiscard]] static std::optional<PartFootprintPadRole>
    part_footprint_pad_role(const nlohmann::json &object);

    [[nodiscard]] static PartFootprintPad part_footprint_pad(const nlohmann::json &object);

    [[nodiscard]] static std::vector<PartFootprintPad> footprint_pads(const nlohmann::json &object);

    [[nodiscard]] static PartFootprintPoint part_footprint_point(const nlohmann::json &object);

    [[nodiscard]] static PartFootprintPolygon part_footprint_polygon(const nlohmann::json &value);

    [[nodiscard]] static std::optional<PartFootprintPolygon>
    optional_part_footprint_polygon(const nlohmann::json &object, const char *name);

    [[nodiscard]] static PartFootprintMarkingKind
    part_footprint_marking_kind(const std::string &value);

    [[nodiscard]] static PartFootprintMarking part_footprint_marking(const nlohmann::json &object);

    [[nodiscard]] static std::vector<PartFootprintMarking>
    part_footprint_markings(const nlohmann::json &object);

    [[nodiscard]] static PartModel3DReference model_3d(const nlohmann::json &object);

    [[nodiscard]] static OrderablePart orderable_part(const nlohmann::json &object);

    const nlohmann::json &document_;
};

[[nodiscard]] PartDefinition PartDefinitionReader::read() {
    require(document_.is_object(), "Part definition document must be an object");
    require_format(document_);
    require_version(document_);
    return PartDefinition{
        identity(field(document_, "identity")),
        pins(document_),
        optional_electrical_attributes(document_, ElectricalAttributeOwner::PartDefinition,
                                       ElectricalAttributeKind::DesignInput),
        provenance(document_),
        symbols(document_),
        orderable_part(field(document_, "orderable_part")),
    };
}

void PartDefinitionReader::require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::logic_error{message};
    }
}

const nlohmann::json &PartDefinitionReader::field(const nlohmann::json &object, const char *name) {
    require(object.is_object(), "Expected object while reading part definition");
    const auto it = object.find(name);
    require(it != object.end(), std::string{"Missing required field: "} + name);
    return *it;
}

const nlohmann::json *PartDefinitionReader::optional_field(const nlohmann::json &object,
                                                           const char *name) {
    require(object.is_object(), "Expected object while reading part definition");
    const auto it = object.find(name);
    return it == object.end() ? nullptr : &*it;
}

std::string PartDefinitionReader::string_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_string(), std::string{"Expected string field: "} + name);
    return value.get<std::string>();
}

std::string PartDefinitionReader::optional_string_field(const nlohmann::json &object,
                                                        const char *name,
                                                        std::string default_value) {
    const auto *value = optional_field(object, name);
    if (value == nullptr) {
        return default_value;
    }
    require(value->is_string(), std::string{"Expected string field: "} + name);
    return value->get<std::string>();
}

const nlohmann::json &PartDefinitionReader::array_field(const nlohmann::json &object,
                                                        const char *name) {
    const auto &value = field(object, name);
    require(value.is_array(), std::string{"Expected array field: "} + name);
    return value;
}

void PartDefinitionReader::require_format(const nlohmann::json &object) {
    const auto actual = string_field(object, "format");
    require(actual == part_definition_format_name(),
            "Unsupported part definition format: " + actual);
}

void PartDefinitionReader::require_version(const nlohmann::json &object) {
    const auto &value = field(object, "version");
    require(value.is_number_integer(), "Expected integer field: version");
    const auto actual = value.get<std::int64_t>();
    require(actual == static_cast<std::int64_t>(part_definition_format_version()),
            "Unsupported part definition format version: " + std::to_string(actual));
}

[[nodiscard]] double PartDefinitionReader::number_field(const nlohmann::json &object,
                                                        const char *name) {
    const auto &value = field(object, name);
    require(value.is_number(), std::string{"Expected number field: "} + name);
    return value.get<double>();
}

[[nodiscard]] ConnectionRequirement
PartDefinitionReader::connection_requirement(const std::string &value) {
    if (value == "Optional")
        return ConnectionRequirement::Optional;
    if (value == "Required")
        return ConnectionRequirement::Required;
    if (value == "MustNotConnect")
        return ConnectionRequirement::MustNotConnect;
    throw std::logic_error{"Invalid ConnectionRequirement value"};
}

[[nodiscard]] ElectricalTerminalKind
PartDefinitionReader::electrical_terminal_kind(const std::string &value) {
    if (value == "Unspecified")
        return ElectricalTerminalKind::Unspecified;
    if (value == "Passive")
        return ElectricalTerminalKind::Passive;
    if (value == "Signal")
        return ElectricalTerminalKind::Signal;
    if (value == "Power")
        return ElectricalTerminalKind::Power;
    if (value == "Ground")
        return ElectricalTerminalKind::Ground;
    if (value == "NoConnect")
        return ElectricalTerminalKind::NoConnect;
    throw std::logic_error{"Invalid ElectricalTerminalKind value"};
}

[[nodiscard]] ElectricalDirection
PartDefinitionReader::electrical_direction(const std::string &value) {
    if (value == "Unspecified")
        return ElectricalDirection::Unspecified;
    if (value == "Input")
        return ElectricalDirection::Input;
    if (value == "Output")
        return ElectricalDirection::Output;
    if (value == "Bidirectional")
        return ElectricalDirection::Bidirectional;
    if (value == "Passive")
        return ElectricalDirection::Passive;
    throw std::logic_error{"Invalid ElectricalDirection value"};
}

[[nodiscard]] ElectricalSignalDomain
PartDefinitionReader::electrical_signal_domain(const std::string &value) {
    if (value == "Unspecified")
        return ElectricalSignalDomain::Unspecified;
    if (value == "Digital")
        return ElectricalSignalDomain::Digital;
    if (value == "Analog")
        return ElectricalSignalDomain::Analog;
    if (value == "Mixed")
        return ElectricalSignalDomain::Mixed;
    throw std::logic_error{"Invalid ElectricalSignalDomain value"};
}

[[nodiscard]] ElectricalDriveKind
PartDefinitionReader::electrical_drive_kind(const std::string &value) {
    if (value == "Unspecified")
        return ElectricalDriveKind::Unspecified;
    if (value == "PushPull")
        return ElectricalDriveKind::PushPull;
    if (value == "OpenCollector")
        return ElectricalDriveKind::OpenCollector;
    if (value == "OpenDrain")
        return ElectricalDriveKind::OpenDrain;
    if (value == "HighImpedance")
        return ElectricalDriveKind::HighImpedance;
    if (value == "Passive")
        return ElectricalDriveKind::Passive;
    throw std::logic_error{"Invalid ElectricalDriveKind value"};
}

[[nodiscard]] ElectricalPolarity
PartDefinitionReader::electrical_polarity(const std::string &value) {
    if (value == "None")
        return ElectricalPolarity::None;
    if (value == "ActiveHigh")
        return ElectricalPolarity::ActiveHigh;
    if (value == "ActiveLow")
        return ElectricalPolarity::ActiveLow;
    throw std::logic_error{"Invalid ElectricalPolarity value"};
}

[[nodiscard]] UnitDimension PartDefinitionReader::unit_dimension(const std::string &value) {
    if (value == "resistance")
        return UnitDimension::Resistance;
    if (value == "capacitance")
        return UnitDimension::Capacitance;
    if (value == "inductance")
        return UnitDimension::Inductance;
    if (value == "voltage")
        return UnitDimension::Voltage;
    if (value == "current")
        return UnitDimension::Current;
    if (value == "power")
        return UnitDimension::Power;
    if (value == "frequency")
        return UnitDimension::Frequency;
    if (value == "time")
        return UnitDimension::Time;
    if (value == "temperature")
        return UnitDimension::Temperature;
    if (value == "ratio")
        return UnitDimension::Ratio;
    throw std::logic_error{"Invalid unit dimension value"};
}

[[nodiscard]] ToleranceMode PartDefinitionReader::tolerance_mode(const std::string &value) {
    if (value == "absolute")
        return ToleranceMode::Absolute;
    if (value == "percent")
        return ToleranceMode::Percent;
    throw std::logic_error{"Invalid tolerance mode value"};
}

[[nodiscard]] ElectricalAttributeValue
PartDefinitionReader::electrical_attribute_value(const nlohmann::json &object) {
    require(object.is_object(), "Electrical attribute value must be an object");
    const auto type = string_field(object, "type");
    const auto dimension = unit_dimension(string_field(object, "dimension"));
    if (type == "quantity") {
        return ElectricalAttributeValue{Quantity{dimension, number_field(object, "value")}};
    }
    if (type == "tolerance") {
        const auto mode = tolerance_mode(string_field(object, "mode"));
        if (mode == ToleranceMode::Percent) {
            require(dimension == UnitDimension::Ratio, "Percent tolerance dimension must be ratio");
            return ElectricalAttributeValue{
                Tolerance::percent(number_field(object, "minus"), number_field(object, "plus"))};
        }
        return ElectricalAttributeValue{
            Tolerance::absolute(Quantity{dimension, number_field(object, "minus")},
                                Quantity{dimension, number_field(object, "plus")})};
    }
    if (type == "range") {
        const auto *minimum = optional_field(object, "minimum");
        const auto *maximum = optional_field(object, "maximum");
        require(minimum != nullptr || maximum != nullptr,
                "Quantity range must contain at least one bound");
        if (minimum != nullptr) {
            require(minimum->is_number(), "Quantity range minimum must be a number");
        }
        if (maximum != nullptr) {
            require(maximum->is_number(), "Quantity range maximum must be a number");
        }
        if (minimum != nullptr && maximum != nullptr) {
            return ElectricalAttributeValue{
                QuantityRange::bounded(Quantity{dimension, minimum->get<double>()},
                                       Quantity{dimension, maximum->get<double>()})};
        }
        if (minimum != nullptr) {
            return ElectricalAttributeValue{
                QuantityRange::minimum(Quantity{dimension, minimum->get<double>()})};
        }
        return ElectricalAttributeValue{
            QuantityRange::maximum(Quantity{dimension, maximum->get<double>()})};
    }
    throw std::logic_error{"Invalid electrical attribute value type"};
}

[[nodiscard]] ElectricalAttributeMap PartDefinitionReader::electrical_attributes(
    const nlohmann::json &object, ElectricalAttributeOwner owner, ElectricalAttributeKind kind) {
    require(object.is_object(), "Electrical attributes must be an object");
    auto result = ElectricalAttributeMap{};
    for (const auto &[name, value] : object.items()) {
        const auto attribute = electrical_attribute_value(value);
        result.set(ElectricalAttributeSpec{ElectricalAttributeName{name}, owner, kind,
                                           attribute.dimension()},
                   attribute);
    }
    return result;
}

[[nodiscard]] ElectricalAttributeMap PartDefinitionReader::optional_electrical_attributes(
    const nlohmann::json &object, ElectricalAttributeOwner owner, ElectricalAttributeKind kind) {
    const auto *attributes = optional_field(object, "electrical_attributes");
    if (attributes == nullptr) {
        return {};
    }
    return electrical_attributes(*attributes, owner, kind);
}

[[nodiscard]] PartIdentity PartDefinitionReader::identity(const nlohmann::json &object) {
    return PartIdentity{string_field(object, "namespace"), string_field(object, "name"),
                        string_field(object, "version")};
}

[[nodiscard]] std::vector<PartPin> PartDefinitionReader::pins(const nlohmann::json &object) {
    auto result = std::vector<PartPin>{};
    const auto &pin_array = array_field(object, "pins");
    result.reserve(pin_array.size());
    for (const auto &pin : pin_array) {
        require(pin.is_object(), "Part pin must be an object");
        require(pin.find("role") == pin.end(),
                "Part pin role is not supported; use canonical electrical fields");
        result.emplace_back(
            PinDefinition{
                string_field(pin, "name"), string_field(pin, "number"),
                connection_requirement(string_field(pin, "connection_requirement")),
                electrical_terminal_kind(
                    optional_string_field(pin, "terminal_kind", "Unspecified")),
                electrical_direction(optional_string_field(pin, "direction", "Unspecified")),
                electrical_signal_domain(
                    optional_string_field(pin, "signal_domain", "Unspecified")),
                electrical_drive_kind(optional_string_field(pin, "drive_kind", "Unspecified")),
                electrical_polarity(optional_string_field(pin, "polarity", "None"))},
            optional_electrical_attributes(pin, ElectricalAttributeOwner::PinSpec,
                                           ElectricalAttributeKind::Constraint));
    }
    return result;
}

[[nodiscard]] PartProvenance PartDefinitionReader::provenance(const nlohmann::json &object) {
    const auto *value = optional_field(object, "provenance");
    if (value == nullptr) {
        return {};
    }
    return PartProvenance{optional_string_field(*value, "datasheet", ""),
                          optional_string_field(*value, "authored_by", ""),
                          optional_string_field(*value, "derived_from", "")};
}

[[nodiscard]] std::vector<HashedSchematicSymbolReference>
PartDefinitionReader::symbols(const nlohmann::json &object) {
    auto result = std::vector<HashedSchematicSymbolReference>{};
    const auto &symbol_array = array_field(object, "symbols");
    result.reserve(symbol_array.size());
    for (const auto &symbol : symbol_array) {
        require(symbol.is_object(), "Schematic symbol reference must be an object");
        auto pins = std::vector<PartSymbolPin>{};
        const auto &pin_array = array_field(symbol, "pins");
        pins.reserve(pin_array.size());
        for (const auto &pin : pin_array) {
            require(pin.is_object(), "Schematic symbol pin must be an object");
            pins.emplace_back(string_field(pin, "name"), string_field(pin, "number"));
        }
        result.emplace_back(string_field(symbol, "name"),
                            optional_string_field(symbol, "variant", "default"),
                            ContentHash{string_field(symbol, "hash")}, std::move(pins));
    }
    return result;
}

[[nodiscard]] std::optional<PartFootprintPadRole>
PartDefinitionReader::part_footprint_pad_role(const nlohmann::json &object) {
    const auto *role = optional_field(object, "role");
    if (role == nullptr) {
        return std::nullopt;
    }
    require(role->is_string(), "Footprint pad role must be a string");
    const auto value = role->get<std::string>();
    if (value == "mechanical") {
        return PartFootprintPadRole::Mechanical;
    }
    if (value == "thermal") {
        return PartFootprintPadRole::Thermal;
    }
    throw std::logic_error{"Invalid footprint pad role"};
}

[[nodiscard]] PartFootprintPad
PartDefinitionReader::part_footprint_pad(const nlohmann::json &object) {
    require(object.is_object(), "Footprint pad must be an object");
    const auto role = part_footprint_pad_role(object);
    if (role.has_value()) {
        return PartFootprintPad{string_field(object, "label"),     number_field(object, "x_mm"),
                                number_field(object, "y_mm"),      number_field(object, "width_mm"),
                                number_field(object, "height_mm"), role.value()};
    }
    return PartFootprintPad{string_field(object, "label"), number_field(object, "x_mm"),
                            number_field(object, "y_mm"), number_field(object, "width_mm"),
                            number_field(object, "height_mm")};
}

[[nodiscard]] std::vector<PartFootprintPad>
PartDefinitionReader::footprint_pads(const nlohmann::json &object) {
    auto result = std::vector<PartFootprintPad>{};
    const auto &pads = array_field(object, "pads");
    result.reserve(pads.size());
    for (const auto &pad : pads) {
        result.push_back(part_footprint_pad(pad));
    }
    return result;
}

[[nodiscard]] PartFootprintPoint
PartDefinitionReader::part_footprint_point(const nlohmann::json &object) {
    require(object.is_object(), "Footprint polygon point must be an object");
    return PartFootprintPoint{number_field(object, "x_mm"), number_field(object, "y_mm")};
}

[[nodiscard]] PartFootprintPolygon
PartDefinitionReader::part_footprint_polygon(const nlohmann::json &value) {
    require(value.is_array(), "Footprint polygon must be an array");
    auto vertices = std::vector<PartFootprintPoint>{};
    vertices.reserve(value.size());
    for (const auto &point : value) {
        vertices.push_back(part_footprint_point(point));
    }
    return PartFootprintPolygon{std::move(vertices)};
}

[[nodiscard]] std::optional<PartFootprintPolygon>
PartDefinitionReader::optional_part_footprint_polygon(const nlohmann::json &object,
                                                      const char *name) {
    const auto *value = optional_field(object, name);
    if (value == nullptr) {
        return std::nullopt;
    }
    return part_footprint_polygon(*value);
}

[[nodiscard]] PartFootprintMarkingKind
PartDefinitionReader::part_footprint_marking_kind(const std::string &value) {
    if (value == "silkscreen") {
        return PartFootprintMarkingKind::Silkscreen;
    }
    if (value == "polarity") {
        return PartFootprintMarkingKind::Polarity;
    }
    if (value == "pin_1") {
        return PartFootprintMarkingKind::PinOne;
    }
    throw std::logic_error{"Invalid footprint marking kind"};
}

[[nodiscard]] PartFootprintMarking
PartDefinitionReader::part_footprint_marking(const nlohmann::json &object) {
    require(object.is_object(), "Footprint marking must be an object");
    return PartFootprintMarking{part_footprint_marking_kind(string_field(object, "kind")),
                                part_footprint_polygon(field(object, "polygon"))};
}

[[nodiscard]] std::vector<PartFootprintMarking>
PartDefinitionReader::part_footprint_markings(const nlohmann::json &object) {
    const auto *value = optional_field(object, "markings");
    if (value == nullptr) {
        return {};
    }
    require(value->is_array(), "Footprint markings must be an array");
    auto markings = std::vector<PartFootprintMarking>{};
    markings.reserve(value->size());
    for (const auto &marking : *value) {
        markings.push_back(part_footprint_marking(marking));
    }
    return markings;
}

[[nodiscard]] PartModel3DReference PartDefinitionReader::model_3d(const nlohmann::json &object) {
    const auto &translation = array_field(object, "translation_mm");
    require(translation.size() == 3U, "3D model translation must contain three numbers");
    for (const auto &coordinate : translation) {
        require(coordinate.is_number(), "3D model translation entries must be numbers");
    }
    return PartModel3DReference{string_field(object, "format"), string_field(object, "file_name"),
                                ContentHash{string_field(object, "hash")},
                                std::array<double, 3>{translation[0].get<double>(),
                                                      translation[1].get<double>(),
                                                      translation[2].get<double>()},
                                number_field(object, "rotation_deg")};
}

[[nodiscard]] OrderablePart PartDefinitionReader::orderable_part(const nlohmann::json &object) {
    require(object.is_object(), "Orderable part must be an object");
    const auto &footprint = field(object, "footprint");
    auto mappings = std::vector<OrderablePinPadMapping>{};
    for (const auto &mapping : array_field(object, "pin_pad_mappings")) {
        require(mapping.is_object(), "Orderable part pin-pad mapping must be an object");
        mappings.emplace_back(string_field(mapping, "pin_number"), string_field(mapping, "pad"));
    }
    auto alternates = std::vector<std::string>{};
    for (const auto &mpn : array_field(object, "approved_alternate_mpns")) {
        require(mpn.is_string(), "Approved alternate MPN must be a string");
        alternates.push_back(mpn.get<std::string>());
    }
    auto model = std::optional<PartModel3DReference>{};
    const auto *model_value = optional_field(object, "model_3d");
    if (model_value != nullptr) {
        model = model_3d(*model_value);
    }
    return OrderablePart{
        ManufacturerPart{string_field(object, "manufacturer"), string_field(object, "mpn")},
        PackageRef{string_field(object, "package")},
        HashedFootprintReference{
            FootprintRef{string_field(footprint, "library"), string_field(footprint, "name")},
            ContentHash{string_field(footprint, "hash")}},
        footprint_pads(footprint),
        std::move(mappings),
        std::move(alternates),
        std::move(model),
        optional_part_footprint_polygon(footprint, "courtyard"),
        optional_part_footprint_polygon(footprint, "body"),
        optional_part_footprint_polygon(footprint, "fabrication_outline"),
        optional_part_footprint_polygon(footprint, "assembly_outline"),
        part_footprint_markings(footprint)};
}

[[nodiscard]] PartDefinition read_part_definition_document(const nlohmann::json &document) {
    try {
        return PartDefinitionReader{document}.read();
    } catch (const std::invalid_argument &error) {
        throw std::logic_error{error.what()};
    } catch (const std::out_of_range &error) {
        throw std::logic_error{error.what()};
    }
}

} // namespace

[[nodiscard]] PartDefinition read_part_definition_text(std::string_view text) {
    return read_part_definition_document(nlohmann::json::parse(text.begin(), text.end()));
}

[[nodiscard]] PartDefinition read_part_definition(std::istream &input) {
    auto buffer = std::ostringstream{};
    buffer << input.rdbuf();
    return read_part_definition_text(buffer.str());
}

} // namespace volt::io
