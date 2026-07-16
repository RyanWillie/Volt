#pragma once

#include "binding_component_conversions.hpp"
#include "binding_diagnostic_conversions.hpp"

#include <algorithm>
#include <map>
#include <ranges>

#include <volt/circuit/parts/part_definition.hpp>
#include <volt/io/parts/part_definition_writer.hpp>

namespace volt::python {

namespace {

[[nodiscard]] inline py::dict required_dict_field(const py::dict &dict, const char *name) {
    if (!dict.contains(name)) {
        throw std::invalid_argument{std::string{"Part artifact payload missing field: "} + name};
    }
    return py::cast<py::dict>(dict[name]);
}

[[nodiscard]] inline py::list required_list_field(const py::dict &dict, const char *name) {
    if (!dict.contains(name)) {
        throw std::invalid_argument{std::string{"Part artifact payload missing field: "} + name};
    }
    return py::cast<py::list>(dict[name]);
}

[[nodiscard]] inline std::string required_string_field(const py::dict &dict, const char *name) {
    if (!dict.contains(name)) {
        throw std::invalid_argument{std::string{"Part artifact payload missing field: "} + name};
    }
    return py::cast<std::string>(dict[name]);
}

[[nodiscard]] inline std::string optional_part_string_field(const py::dict &dict, const char *name,
                                                            std::string default_value) {
    if (!dict.contains(name) || dict[name].is_none()) {
        return default_value;
    }
    return py::cast<std::string>(dict[name]);
}

[[nodiscard]] inline std::vector<std::string> string_vector_from_list(const py::list &values) {
    auto result = std::vector<std::string>{};
    result.reserve(static_cast<std::size_t>(py::len(values)));
    for (const auto item : values) {
        result.push_back(py::cast<std::string>(item));
    }
    return result;
}

[[nodiscard]] inline std::vector<volt::PinDefinition> part_pins_from_list(const py::list &pins) {
    auto result = std::vector<volt::PinDefinition>{};
    result.reserve(static_cast<std::size_t>(py::len(pins)));
    for (const auto item : pins) {
        const auto spec = pin_spec_from_dict(py::cast<py::dict>(item));
        auto attributes = volt::ElectricalAttributeMap{};
        if (spec.voltage_range.has_value()) {
            attributes.set(
                volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{"voltage_range"},
                                              volt::ElectricalAttributeOwner::PinSpec,
                                              volt::ElectricalAttributeKind::Constraint,
                                              volt::UnitDimension::Voltage},
                volt::ElectricalAttributeValue{spec.voltage_range.value()});
        }
        result.emplace_back(spec.name, spec.number, spec.requirement, spec.terminal_kind,
                            spec.direction, spec.signal_domain, spec.drive_kind, spec.polarity,
                            std::move(attributes));
    }
    return result;
}

[[nodiscard]] inline std::vector<volt::PartSchematicAssetReference>
part_assets_from_list(const py::list &symbols,
                      const std::vector<volt::PinDefinition> &component_pins) {
    auto result = std::vector<volt::PartSchematicAssetReference>{};
    result.reserve(static_cast<std::size_t>(py::len(symbols)));
    for (const auto item : symbols) {
        const auto symbol = py::cast<py::dict>(item);
        auto seen = std::vector<bool>(component_pins.size(), false);
        const auto pin_list = required_list_field(symbol, "pins");
        for (const auto pin_item : pin_list) {
            const auto pin = py::cast<py::dict>(pin_item);
            const auto name = required_string_field(pin, "name");
            const auto number = required_string_field(pin, "number");
            const auto match =
                std::ranges::find(component_pins, number, &volt::PinDefinition::number);
            if (match == component_pins.end() || match->name() != name) {
                throw std::invalid_argument{
                    "Part artifact schematic symbol pin is outside component pins"};
            }
            const auto index =
                static_cast<std::size_t>(std::distance(component_pins.begin(), match));
            if (seen[index]) {
                throw std::invalid_argument{
                    "Part artifact schematic symbol contains duplicate component pin"};
            }
            seen[index] = true;
        }
        if (!std::ranges::all_of(seen, [](const auto value) { return value; })) {
            throw std::invalid_argument{
                "Part artifact schematic symbol must contain every component pin"};
        }
        result.emplace_back(required_string_field(symbol, "name"),
                            optional_part_string_field(symbol, "variant", "default"),
                            volt::ContentHash{required_string_field(symbol, "hash")});
    }
    return result;
}

[[nodiscard]] inline std::vector<volt::SchematicSymbolReference>
component_symbol_references(const std::vector<volt::PartSchematicAssetReference> &assets) {
    auto result = std::vector<volt::SchematicSymbolReference>{};
    result.reserve(assets.size());
    for (const auto &asset : assets) {
        result.emplace_back(asset.name(), asset.variant());
    }
    return result;
}

[[nodiscard]] inline volt::PartFootprintPadRole
part_footprint_pad_role_from_string(const std::string &role) {
    if (role == "mechanical") {
        return volt::PartFootprintPadRole::Mechanical;
    }
    if (role == "thermal") {
        return volt::PartFootprintPadRole::Thermal;
    }
    throw std::invalid_argument{"Part artifact footprint pad role must be mechanical or thermal"};
}

[[nodiscard]] inline std::vector<volt::PartFootprintPad>
part_footprint_pads_from_list(const py::list &pads) {
    auto result = std::vector<volt::PartFootprintPad>{};
    result.reserve(static_cast<std::size_t>(py::len(pads)));
    for (const auto item : pads) {
        const auto pad = py::cast<py::dict>(item);
        const auto label = required_string_field(pad, "label");
        const auto x = py::cast<double>(pad["x_mm"]);
        const auto y = py::cast<double>(pad["y_mm"]);
        const auto width = py::cast<double>(pad["width_mm"]);
        const auto height = py::cast<double>(pad["height_mm"]);
        if (pad.contains("role") && !pad["role"].is_none()) {
            result.emplace_back(
                label, x, y, width, height,
                part_footprint_pad_role_from_string(py::cast<std::string>(pad["role"])));
        } else {
            result.emplace_back(label, x, y, width, height);
        }
    }
    return result;
}

[[nodiscard]] inline volt::PartFootprintPoint part_footprint_point_from_dict(const py::dict &dict) {
    return volt::PartFootprintPoint{py::cast<double>(dict["x_mm"]), py::cast<double>(dict["y_mm"])};
}

[[nodiscard]] inline std::optional<volt::PartFootprintPolygon>
optional_part_footprint_polygon_from_object(py::handle value) {
    if (value.is_none()) {
        return std::nullopt;
    }
    const auto points = py::cast<py::list>(value);
    auto vertices = std::vector<volt::PartFootprintPoint>{};
    vertices.reserve(static_cast<std::size_t>(py::len(points)));
    for (const auto item : points) {
        vertices.push_back(part_footprint_point_from_dict(py::cast<py::dict>(item)));
    }
    return volt::PartFootprintPolygon{std::move(vertices)};
}

[[nodiscard]] inline std::optional<volt::PartFootprintPolygon>
optional_part_footprint_polygon_from_dict(const py::dict &dict, const char *name) {
    if (!dict.contains(name) || dict[name].is_none()) {
        return std::nullopt;
    }
    return optional_part_footprint_polygon_from_object(dict[name]);
}

[[nodiscard]] inline volt::PartFootprintMarkingKind
part_footprint_marking_kind_from_string(const std::string &kind) {
    if (kind == "silkscreen") {
        return volt::PartFootprintMarkingKind::Silkscreen;
    }
    if (kind == "polarity") {
        return volt::PartFootprintMarkingKind::Polarity;
    }
    if (kind == "pin_1") {
        return volt::PartFootprintMarkingKind::PinOne;
    }
    throw std::invalid_argument{
        "Part artifact footprint marking kind must be silkscreen, polarity, or pin_1"};
}

[[nodiscard]] inline volt::PartFootprintMarking
part_footprint_marking_from_dict(const py::dict &dict) {
    return volt::PartFootprintMarking{
        part_footprint_marking_kind_from_string(required_string_field(dict, "kind")),
        optional_part_footprint_polygon_from_object(dict["polygon"]).value()};
}

[[nodiscard]] inline std::vector<volt::PartFootprintMarking>
part_footprint_markings_from_dict(const py::dict &dict, const char *name) {
    if (!dict.contains(name) || dict[name].is_none()) {
        return {};
    }
    const auto values = py::cast<py::list>(dict[name]);
    auto markings = std::vector<volt::PartFootprintMarking>{};
    markings.reserve(static_cast<std::size_t>(py::len(values)));
    for (const auto item : values) {
        markings.push_back(part_footprint_marking_from_dict(py::cast<py::dict>(item)));
    }
    return markings;
}

[[nodiscard]] inline std::vector<volt::PackageTerminalPadMapping>
part_terminal_pad_mappings_from_list(const py::list &mappings) {
    auto pads_by_terminal = std::map<std::string, std::vector<volt::FootprintPadKey>>{};
    for (const auto item : mappings) {
        const auto mapping = py::cast<py::dict>(item);
        pads_by_terminal[required_string_field(mapping, "pin_number")].emplace_back(
            required_string_field(mapping, "pad"));
    }
    auto result = std::vector<volt::PackageTerminalPadMapping>{};
    result.reserve(pads_by_terminal.size());
    for (auto &[terminal, pads] : pads_by_terminal) {
        result.emplace_back(volt::PackageTerminalKey{terminal}, std::move(pads));
    }
    return result;
}

[[nodiscard]] inline std::optional<volt::PartModel3DReference>
part_model_3d_reference_from_object(py::handle value) {
    if (value.is_none()) {
        return std::nullopt;
    }
    const auto dict = py::cast<py::dict>(value);
    const auto translation = py::cast<std::array<double, 3>>(dict["translation_mm"]);
    for (const auto coordinate : translation) {
        require_finite(coordinate, "Part artifact 3D model translation must be finite");
    }
    const auto rotation = py::cast<double>(dict["rotation_deg"]);
    require_finite(rotation, "Part artifact 3D model rotation must be finite");
    return volt::PartModel3DReference{
        required_string_field(dict, "format"), required_string_field(dict, "file_name"),
        volt::ContentHash{required_string_field(dict, "hash")}, translation, rotation};
}

[[nodiscard]] inline std::optional<volt::PartModel3DReference>
part_model_3d_reference_from_dict(const py::dict &dict, const char *name) {
    if (!dict.contains(name) || dict[name].is_none()) {
        return std::nullopt;
    }
    return part_model_3d_reference_from_object(dict[name]);
}

[[nodiscard]] inline volt::OrderablePart orderable_part_from_dict(const py::dict &dict) {
    return volt::OrderablePart{
        volt::ManufacturerPart{required_string_field(dict, "manufacturer"),
                               required_string_field(dict, "mpn")},
        volt::PackageRef{required_string_field(dict, "package")},
        volt::HashedFootprintReference{
            volt::FootprintRef{required_string_field(dict, "footprint_library"),
                               required_string_field(dict, "footprint_name")},
            volt::ContentHash{required_string_field(dict, "footprint_hash")}},
        part_footprint_pads_from_list(required_list_field(dict, "footprint_pads")),
        part_terminal_pad_mappings_from_list(required_list_field(dict, "pin_pad_mappings")),
        string_vector_from_list(required_list_field(dict, "approved_alternate_mpns")),
        part_model_3d_reference_from_dict(dict, "model_3d"),
        optional_part_footprint_polygon_from_dict(dict, "footprint_courtyard"),
        optional_part_footprint_polygon_from_dict(dict, "footprint_body"),
        optional_part_footprint_polygon_from_dict(dict, "footprint_fabrication_outline"),
        optional_part_footprint_polygon_from_dict(dict, "footprint_assembly_outline"),
        part_footprint_markings_from_dict(dict, "footprint_markings")};
}

[[nodiscard]] inline volt::PartDefinition part_definition_from_dict(const py::dict &dict) {
    const auto identity = required_dict_field(dict, "identity");
    const auto provenance = required_dict_field(dict, "provenance");
    const auto pins = part_pins_from_list(required_list_field(dict, "pins"));
    const auto assets = part_assets_from_list(required_list_field(dict, "symbols"), pins);
    if (assets.empty()) {
        throw std::invalid_argument{
            "Part artifact requires at least one schematic symbol projection"};
    }
    auto pin_ids = std::vector<volt::PinDefId>{};
    pin_ids.reserve(pins.size());
    for (std::size_t index = 0; index < pins.size(); ++index) {
        pin_ids.emplace_back(index);
    }
    const auto component = volt::ComponentDefinition::make(
        required_string_field(identity, "name"), pins, std::move(pin_ids), {},
        volt::DefinitionSource{required_string_field(identity, "namespace"),
                               required_string_field(identity, "name"),
                               required_string_field(identity, "version")},
        component_symbol_references(assets));
    auto pin_mappings = std::vector<volt::PinPackageTerminalMapping>{};
    pin_mappings.reserve(pins.size());
    for (std::size_t index = 0; index < pins.size(); ++index) {
        pin_mappings.emplace_back(component.contract().pin_keys()[index],
                                  std::vector{volt::PackageTerminalKey{pins[index].number()}});
    }
    return volt::PartDefinition{
        component,
        volt::PartIdentity{required_string_field(identity, "namespace"),
                           required_string_field(identity, "name"),
                           required_string_field(identity, "version")},
        volt::ElectricalRecordSet{pins.size()},
        std::move(pin_mappings),
        {},
        volt::PartProvenance{optional_part_string_field(provenance, "datasheet", ""),
                             optional_part_string_field(provenance, "authored_by", ""),
                             optional_part_string_field(provenance, "derived_from", "")},
        assets,
        orderable_part_from_dict(required_dict_field(dict, "orderable_part"))};
}

[[nodiscard]] inline py::dict part_definition_artifact_from_dict(const py::dict &dict) {
    const auto part = part_definition_from_dict(dict);
    const auto bytes = volt::io::write_part_definition(part);
    auto result = py::dict{};
    result["bytes"] = py::bytes{bytes};
    result["sha256"] = volt::io::part_definition_content_hash(part).value();
    result["diagnostics"] = diagnostics_to_list(volt::validate_part_lineup(part));
    return result;
}

} // namespace

} // namespace volt::python
