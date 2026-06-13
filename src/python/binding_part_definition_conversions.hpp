#pragma once

#include "binding_component_conversions.hpp"
#include "binding_diagnostic_conversions.hpp"

#include <volt/circuit/part_definition.hpp>
#include <volt/io/part_definition_writer.hpp>

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

[[nodiscard]] inline std::vector<volt::PartPin> part_pins_from_list(const py::list &pins) {
    auto result = std::vector<volt::PartPin>{};
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
        result.emplace_back(volt::PinDefinition{spec.name, spec.number, spec.requirement,
                                                spec.terminal_kind, spec.direction,
                                                spec.signal_domain, spec.drive_kind, spec.polarity},
                            std::move(attributes));
    }
    return result;
}

[[nodiscard]] inline std::vector<volt::HashedSchematicSymbolReference>
part_symbols_from_list(const py::list &symbols) {
    auto result = std::vector<volt::HashedSchematicSymbolReference>{};
    result.reserve(static_cast<std::size_t>(py::len(symbols)));
    for (const auto item : symbols) {
        const auto symbol = py::cast<py::dict>(item);
        auto pins = std::vector<volt::PartSymbolPin>{};
        const auto pin_list = required_list_field(symbol, "pins");
        pins.reserve(static_cast<std::size_t>(py::len(pin_list)));
        for (const auto pin_item : pin_list) {
            const auto pin = py::cast<py::dict>(pin_item);
            pins.emplace_back(required_string_field(pin, "name"),
                              required_string_field(pin, "number"));
        }
        result.emplace_back(required_string_field(symbol, "name"),
                            optional_part_string_field(symbol, "variant", "default"),
                            volt::ContentHash{required_string_field(symbol, "hash")},
                            std::move(pins));
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

[[nodiscard]] inline std::vector<volt::OrderablePinPadMapping>
part_pin_pad_mappings_from_list(const py::list &mappings) {
    auto result = std::vector<volt::OrderablePinPadMapping>{};
    result.reserve(static_cast<std::size_t>(py::len(mappings)));
    for (const auto item : mappings) {
        const auto mapping = py::cast<py::dict>(item);
        result.emplace_back(required_string_field(mapping, "pin_number"),
                            required_string_field(mapping, "pad"));
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
        part_pin_pad_mappings_from_list(required_list_field(dict, "pin_pad_mappings")),
        string_vector_from_list(required_list_field(dict, "approved_alternate_mpns")),
        part_model_3d_reference_from_dict(dict, "model_3d"),
        optional_part_footprint_polygon_from_dict(dict, "footprint_courtyard"),
        optional_part_footprint_polygon_from_dict(dict, "footprint_body")};
}

[[nodiscard]] inline volt::PartDefinition part_definition_from_dict(const py::dict &dict) {
    const auto identity = required_dict_field(dict, "identity");
    const auto provenance = required_dict_field(dict, "provenance");
    return volt::PartDefinition{
        volt::PartIdentity{required_string_field(identity, "namespace"),
                           required_string_field(identity, "name"),
                           required_string_field(identity, "version")},
        part_pins_from_list(required_list_field(dict, "pins")),
        volt::ElectricalAttributeMap{},
        volt::PartProvenance{optional_part_string_field(provenance, "datasheet", ""),
                             optional_part_string_field(provenance, "authored_by", ""),
                             optional_part_string_field(provenance, "derived_from", "")},
        part_symbols_from_list(required_list_field(dict, "symbols")),
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
