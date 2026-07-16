#include <volt/io/parts/part_definition_reader.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <istream>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>
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

std::string optional_string_field(const Json &object, const char *name, std::string default_value) {
    const auto *value = optional_field(object, name);
    if (value == nullptr) {
        return default_value;
    }
    require(value->is_string(), std::string{"Expected string field: "} + name);
    return value->get<std::string>();
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

ConnectionRequirement connection_requirement(const std::string &value) {
    if (value == "Optional")
        return ConnectionRequirement::Optional;
    if (value == "Required")
        return ConnectionRequirement::Required;
    if (value == "MustNotConnect")
        return ConnectionRequirement::MustNotConnect;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid ConnectionRequirement value"};
}

ElectricalTerminalKind electrical_terminal_kind(const std::string &value) {
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
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid ElectricalTerminalKind value"};
}

ElectricalDirection electrical_direction(const std::string &value) {
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
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid ElectricalDirection value"};
}

ElectricalSignalDomain electrical_signal_domain(const std::string &value) {
    if (value == "Unspecified")
        return ElectricalSignalDomain::Unspecified;
    if (value == "Digital")
        return ElectricalSignalDomain::Digital;
    if (value == "Analog")
        return ElectricalSignalDomain::Analog;
    if (value == "Mixed")
        return ElectricalSignalDomain::Mixed;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid ElectricalSignalDomain value"};
}

ElectricalDriveKind electrical_drive_kind(const std::string &value) {
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
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid ElectricalDriveKind value"};
}

ElectricalPolarity electrical_polarity(const std::string &value) {
    if (value == "None")
        return ElectricalPolarity::None;
    if (value == "ActiveHigh")
        return ElectricalPolarity::ActiveHigh;
    if (value == "ActiveLow")
        return ElectricalPolarity::ActiveLow;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid ElectricalPolarity value"};
}

UnitDimension unit_dimension(const std::string &value) {
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
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid unit dimension value"};
}

ToleranceMode tolerance_mode(const std::string &value) {
    if (value == "absolute")
        return ToleranceMode::Absolute;
    if (value == "percent")
        return ToleranceMode::Percent;
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid tolerance mode value"};
}

ElectricalAttributeValue electrical_attribute_value(const Json &object) {
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
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid electrical attribute value type"};
}

ElectricalAttributeMap electrical_attributes(const Json &object, ElectricalAttributeOwner owner,
                                             ElectricalAttributeKind kind) {
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

ElectricalAttributeMap optional_electrical_attributes(const Json &object,
                                                      ElectricalAttributeOwner owner,
                                                      ElectricalAttributeKind kind) {
    const auto *attributes = optional_field(object, "electrical_attributes");
    return attributes == nullptr ? ElectricalAttributeMap{}
                                 : electrical_attributes(*attributes, owner, kind);
}

PartIdentity identity(const Json &object) {
    return PartIdentity{string_field(object, "namespace"), string_field(object, "name"),
                        string_field(object, "version")};
}

PartProvenance provenance(const Json &document) {
    const auto *value = optional_field(document, "provenance");
    if (value == nullptr) {
        return {};
    }
    return PartProvenance{optional_string_field(*value, "datasheet", ""),
                          optional_string_field(*value, "authored_by", ""),
                          optional_string_field(*value, "derived_from", "")};
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
    require(object.is_object(), "Footprint polygon point must be an object");
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
        require(marking.is_object(), "Footprint marking must be an object");
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

OrderablePart orderable_part_v5(const Json &object) {
    require(object.is_object(), "Orderable part must be an object");
    const auto &footprint = field(object, "footprint");
    auto mappings = std::vector<PackageTerminalPadMapping>{};
    for (const auto &mapping : array_field(object, "terminal_pad_mappings")) {
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

std::vector<PinPackageTerminalMapping> pin_terminal_mappings_v5(const Json &document) {
    auto mappings = std::vector<PinPackageTerminalMapping>{};
    for (const auto &mapping : array_field(document, "pin_terminal_mappings")) {
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

std::vector<DisposedPackageTerminal> terminal_dispositions_v5(const Json &document) {
    auto dispositions = std::vector<DisposedPackageTerminal>{};
    for (const auto &value : array_field(document, "terminal_dispositions")) {
        dispositions.emplace_back(PackageTerminalKey{string_field(value, "terminal")},
                                  terminal_disposition(string_field(value, "disposition")));
    }
    return dispositions;
}

std::vector<PartSchematicAssetReference> schematic_assets_v5(const Json &document) {
    auto assets = std::vector<PartSchematicAssetReference>{};
    for (const auto &asset : array_field(document, "schematic_assets")) {
        assets.emplace_back(string_field(asset, "name"),
                            optional_string_field(asset, "variant", "default"),
                            ContentHash{string_field(asset, "hash")});
    }
    return assets;
}

PartDefinition read_v5_document(const Json &document, const ComponentDefinition &component) {
    require_format_version(document, part_definition_format_version());
    require(string_field(document, "implements") == component.content_identity().value(),
            "Part definition component digest mismatch");
    auto part = PartDefinition{
        component,
        identity(field(document, "identity")),
        read_electrical_records_text(field(document, "electrical_records").dump()),
        pin_terminal_mappings_v5(document),
        terminal_dispositions_v5(document),
        provenance(document),
        schematic_assets_v5(document),
        orderable_part_v5(field(document, "orderable_part")),
    };
    require(string_field(document, "content_identity") == part.content_identity().value(),
            "Part definition content identity mismatch");
    return part;
}

struct LegacySymbol {
    std::string name;
    std::string variant;
    ContentHash hash;
    std::vector<std::pair<std::string, std::string>> pins;
};

struct LegacyPinPadMapping {
    std::string pin_number;
    std::string pad;
};

struct LegacyPart {
    PartIdentity identity;
    std::vector<PinDefinition> pins;
    ElectricalAttributeMap electrical_attributes;
    PartProvenance provenance;
    std::vector<LegacySymbol> symbols;
    ManufacturerPart manufacturer_part;
    PackageRef package;
    HashedFootprintReference footprint;
    std::vector<PartFootprintPad> footprint_pads;
    std::vector<LegacyPinPadMapping> pin_pad_mappings;
    std::vector<std::string> approved_alternate_mpns;
    std::optional<PartModel3DReference> model;
    std::optional<PartFootprintPolygon> courtyard;
    std::optional<PartFootprintPolygon> body;
    std::optional<PartFootprintPolygon> fabrication_outline;
    std::optional<PartFootprintPolygon> assembly_outline;
    std::vector<PartFootprintMarking> markings;
};

std::vector<PinDefinition> legacy_pins(const Json &document) {
    auto pins = std::vector<PinDefinition>{};
    for (const auto &pin : array_field(document, "pins")) {
        require(pin.is_object(), "Part pin must be an object");
        require(pin.find("role") == pin.end(),
                "Part pin role is not supported; use canonical electrical fields");
        pins.emplace_back(
            string_field(pin, "name"), string_field(pin, "number"),
            connection_requirement(string_field(pin, "connection_requirement")),
            electrical_terminal_kind(optional_string_field(pin, "terminal_kind", "Unspecified")),
            electrical_direction(optional_string_field(pin, "direction", "Unspecified")),
            electrical_signal_domain(optional_string_field(pin, "signal_domain", "Unspecified")),
            electrical_drive_kind(optional_string_field(pin, "drive_kind", "Unspecified")),
            electrical_polarity(optional_string_field(pin, "polarity", "None")),
            optional_electrical_attributes(pin, ElectricalAttributeOwner::PinSpec,
                                           ElectricalAttributeKind::Constraint));
    }
    require(!pins.empty(), "Legacy part definition must contain pins");
    auto numbers = std::set<std::string>{};
    for (const auto &pin : pins) {
        require(numbers.insert(pin.number()).second,
                "Legacy part definition contains duplicate pin numbers");
    }
    return pins;
}

std::vector<LegacySymbol> legacy_symbols(const Json &document,
                                         const std::vector<PinDefinition> &pins) {
    auto symbols = std::vector<LegacySymbol>{};
    for (const auto &symbol : array_field(document, "symbols")) {
        auto symbol_pins = std::vector<std::pair<std::string, std::string>>{};
        for (const auto &pin : array_field(symbol, "pins")) {
            symbol_pins.emplace_back(string_field(pin, "name"), string_field(pin, "number"));
        }
        symbols.push_back(LegacySymbol{
            string_field(symbol, "name"), optional_string_field(symbol, "variant", "default"),
            ContentHash{string_field(symbol, "hash")}, std::move(symbol_pins)});
    }
    require(!symbols.empty(), "Legacy part definition must contain schematic symbols");
    for (const auto &symbol : symbols) {
        auto counts = std::vector<std::size_t>(pins.size(), 0U);
        for (const auto &[name, number] : symbol.pins) {
            const auto pin = std::ranges::find(pins, number, &PinDefinition::number);
            require(pin != pins.end() && pin->name() == name,
                    "Legacy schematic symbol pin is outside the part definition pin map");
            ++counts[static_cast<std::size_t>(std::distance(pins.begin(), pin))];
        }
        require(std::ranges::all_of(counts, [](auto count) { return count == 1U; }),
                "Legacy schematic symbol must reference every part pin exactly once");
    }
    return symbols;
}

LegacyPart parse_v4_document(const Json &document) {
    require_format_version(document, 4);
    auto pins = legacy_pins(document);
    auto symbols = legacy_symbols(document, pins);
    const auto &orderable = field(document, "orderable_part");
    const auto &footprint = field(orderable, "footprint");
    auto pads = footprint_pads(footprint, false);
    auto pad_labels = std::set<std::string>{};
    for (const auto &pad : pads) {
        require(pad_labels.insert(pad.label()).second,
                "Legacy footprint contains duplicate pad labels");
    }
    auto pin_numbers = std::set<std::string>{};
    for (const auto &pin : pins) {
        pin_numbers.insert(pin.number());
    }
    auto mappings = std::vector<LegacyPinPadMapping>{};
    auto mapped_pads = std::set<std::string>{};
    for (const auto &mapping : array_field(orderable, "pin_pad_mappings")) {
        auto pin_number = string_field(mapping, "pin_number");
        auto pad = string_field(mapping, "pad");
        require(pin_numbers.contains(pin_number),
                "Legacy pin-pad mapping references a foreign pin");
        require(pad.find(',') == std::string::npos,
                "Legacy multi-pad mappings require one entry per pad");
        require(pad_labels.contains(pad), "Legacy pin-pad mapping references a foreign pad");
        require(mapped_pads.insert(pad).second,
                "Legacy footprint pad has duplicate logical ownership");
        mappings.push_back(LegacyPinPadMapping{std::move(pin_number), std::move(pad)});
    }
    return LegacyPart{
        identity(field(document, "identity")),
        std::move(pins),
        optional_electrical_attributes(document, ElectricalAttributeOwner::PartDefinition,
                                       ElectricalAttributeKind::DesignInput),
        provenance(document),
        std::move(symbols),
        ManufacturerPart{string_field(orderable, "manufacturer"), string_field(orderable, "mpn")},
        PackageRef{string_field(orderable, "package")},
        HashedFootprintReference{
            FootprintRef{string_field(footprint, "library"), string_field(footprint, "name")},
            ContentHash{string_field(footprint, "hash")}},
        std::move(pads),
        std::move(mappings),
        approved_alternates(orderable),
        model_3d(orderable),
        optional_part_footprint_polygon(footprint, "courtyard"),
        optional_part_footprint_polygon(footprint, "body"),
        optional_part_footprint_polygon(footprint, "fabrication_outline"),
        optional_part_footprint_polygon(footprint, "assembly_outline"),
        part_footprint_markings(footprint),
    };
}

ComponentContractSpec contract_spec(const ComponentContract &contract) {
    return ComponentContractSpec{contract.key(),
                                 contract.pin_keys(),
                                 contract.framed_pins(),
                                 contract.relations(),
                                 contract.supply_domains(),
                                 contract.feature_schemas(),
                                 contract.feature_bindings()};
}

ComponentDefinition legacy_component(const LegacyPart &legacy,
                                     const ComponentDefinition &component) {
    auto pin_ids = std::vector<PinDefId>{};
    pin_ids.reserve(legacy.pins.size());
    for (std::size_t index = 0; index < legacy.pins.size(); ++index) {
        pin_ids.emplace_back(index);
    }
    return ComponentDefinition::make(component.name(), legacy.pins, std::move(pin_ids), {},
                                     component.source(), component.schematic_symbols(),
                                     contract_spec(component.contract()));
}

void require_legacy_assets_match_component(const LegacyPart &legacy,
                                           const ComponentDefinition &component) {
    require(legacy.symbols.size() == component.schematic_symbols().size(),
            "Legacy schematic assets do not match the component definition");
    for (const auto &symbol : component.schematic_symbols()) {
        const auto match = std::ranges::find_if(legacy.symbols, [&](const auto &legacy_symbol) {
            return legacy_symbol.name == symbol.name() && legacy_symbol.variant == symbol.variant();
        });
        require(match != legacy.symbols.end(),
                "Legacy schematic asset is outside the component definition");
    }
}

PartDefinition convert_v4_document(const Json &document, const ComponentDefinition &component,
                                   ElectricalRecordSet electrical_records) {
    auto legacy = parse_v4_document(document);
    require(legacy_component(legacy, component).content_identity() == component.content_identity(),
            "Legacy part pins do not implement the supplied component digest");
    require_legacy_assets_match_component(legacy, component);
    require(legacy.electrical_attributes.empty() || !electrical_records.records().empty(),
            "Legacy part-level electrical attributes require explicit canonical P1 records");

    auto pin_mappings = std::vector<PinPackageTerminalMapping>{};
    for (std::size_t index = 0; index < legacy.pins.size(); ++index) {
        pin_mappings.emplace_back(component.contract().pin_keys()[index],
                                  std::vector{PackageTerminalKey{legacy.pins[index].number()}});
    }

    auto pads_by_terminal = std::map<std::string, std::vector<FootprintPadKey>>{};
    for (const auto &mapping : legacy.pin_pad_mappings) {
        pads_by_terminal[mapping.pin_number].emplace_back(mapping.pad);
    }
    auto terminal_mappings = std::vector<PackageTerminalPadMapping>{};
    for (auto &[terminal, pads] : pads_by_terminal) {
        terminal_mappings.emplace_back(PackageTerminalKey{terminal}, std::move(pads));
    }

    auto assets = std::vector<PartSchematicAssetReference>{};
    for (auto &symbol : legacy.symbols) {
        assets.emplace_back(std::move(symbol.name), std::move(symbol.variant),
                            std::move(symbol.hash));
    }

    auto orderable = OrderablePart{std::move(legacy.manufacturer_part),
                                   std::move(legacy.package),
                                   std::move(legacy.footprint),
                                   std::move(legacy.footprint_pads),
                                   std::move(terminal_mappings),
                                   std::move(legacy.approved_alternate_mpns),
                                   std::move(legacy.model),
                                   std::move(legacy.courtyard),
                                   std::move(legacy.body),
                                   std::move(legacy.fabrication_outline),
                                   std::move(legacy.assembly_outline),
                                   std::move(legacy.markings)};
    return PartDefinition{component,
                          std::move(legacy.identity),
                          std::move(electrical_records),
                          std::move(pin_mappings),
                          {},
                          std::move(legacy.provenance),
                          std::move(assets),
                          std::move(orderable)};
}

Json parse_document(std::string_view text) {
    try {
        return Json::parse(text.begin(), text.end());
    } catch (const Json::exception &error) {
        throw KernelLogicError{ErrorCode::InvalidArgument, error.what()};
    }
}

} // namespace

PartDefinitionV4::PartDefinitionV4(std::string normalized_json)
    : normalized_json_{std::move(normalized_json)} {
    if (normalized_json_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Legacy part definition JSON must not be empty"};
    }
}

PartDefinition read_part_definition_text(std::string_view text,
                                         const ComponentDefinition &component) {
    return read_v5_document(parse_document(text), component);
}

PartDefinition read_part_definition(std::istream &input, const ComponentDefinition &component) {
    auto buffer = std::ostringstream{};
    buffer << input.rdbuf();
    return read_part_definition_text(buffer.str(), component);
}

PartDefinitionV4 PartDefinitionV4::read_text(std::string_view text) {
    const auto document = parse_document(text);
    static_cast<void>(parse_v4_document(document));
    return PartDefinitionV4{document.dump()};
}

PartDefinitionV4 PartDefinitionV4::read(std::istream &input) {
    auto buffer = std::ostringstream{};
    buffer << input.rdbuf();
    return read_text(buffer.str());
}

PartDefinition PartDefinitionV4::convert(const ComponentDefinition &component,
                                         ElectricalRecordSet electrical_records) const {
    return convert_v4_document(parse_document(normalized_json_), component,
                               std::move(electrical_records));
}

} // namespace volt::io
