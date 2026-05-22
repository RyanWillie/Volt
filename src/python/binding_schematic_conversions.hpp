#pragma once

#include "binding_enum_conversions.hpp"

namespace volt::python {

namespace {

[[nodiscard]] inline std::string required_string_field(const py::dict &dict, const char *field,
                                                       const char *context) {
    if (!dict.contains(field)) {
        throw std::invalid_argument{std::string{context} + " must include " + field};
    }
    return py::cast<std::string>(dict[field]);
}

[[nodiscard]] inline std::string optional_string_field(const py::dict &dict, const char *field,
                                                       std::string default_value) {
    if (!dict.contains(field)) {
        return default_value;
    }
    return py::cast<std::string>(dict[field]);
}

[[nodiscard]] inline py::dict required_dict_field(const py::dict &dict, const char *field,
                                                  const char *context) {
    if (!dict.contains(field)) {
        throw std::invalid_argument{std::string{context} + " must include " + field};
    }
    return py::cast<py::dict>(dict[field]);
}

[[nodiscard]] inline double required_number_field(const py::dict &dict, const char *field,
                                                  const char *context) {
    if (!dict.contains(field)) {
        throw std::invalid_argument{std::string{context} + " must include " + field};
    }
    const auto value = py::cast<double>(dict[field]);
    require_finite(value, "Schematic symbol numbers must be finite");
    return value;
}

[[nodiscard]] inline double required_finite_number_field(const py::dict &dict, const char *field,
                                                         const char *context) {
    if (!dict.contains(field)) {
        throw std::invalid_argument{std::string{context} + " must include " + field};
    }
    const auto value = py::cast<double>(dict[field]);
    const auto message = std::string{context} + " numbers must be finite";
    require_finite(value, message.c_str());
    return value;
}

[[nodiscard]] inline bool optional_bool_field(const py::dict &dict, const char *field,
                                              bool default_value) {
    if (!dict.contains(field)) {
        return default_value;
    }
    return py::cast<bool>(dict[field]);
}

[[nodiscard]] inline std::optional<double>
optional_positive_number_field(const py::dict &dict, const char *field, const char *context) {
    if (!dict.contains(field)) {
        return std::nullopt;
    }
    const auto value = py::cast<double>(dict[field]);
    const auto message = std::string{context} + " numbers must be finite";
    require_finite(value, message.c_str());
    if (value <= 0.0) {
        throw std::invalid_argument{std::string{context} + " numbers must be positive"};
    }
    return value;
}

[[nodiscard]] inline std::size_t required_size_field(const py::dict &dict, const char *field,
                                                     const char *context) {
    if (!dict.contains(field)) {
        throw std::invalid_argument{std::string{context} + " must include " + field};
    }
    const auto value = py::cast<std::size_t>(dict[field]);
    if (value == 0U) {
        throw std::invalid_argument{std::string{context} + " counts must be positive"};
    }
    return value;
}

[[nodiscard]] inline volt::Point point_from_dict(const py::dict &dict) {
    return volt::Point{required_number_field(dict, "x", "Symbol point"),
                       required_number_field(dict, "y", "Symbol point")};
}

[[nodiscard]] inline volt::SheetSize sheet_size_from_dict(const py::dict &dict) {
    return volt::SheetSize{required_finite_number_field(dict, "width", "Sheet size"),
                           required_finite_number_field(dict, "height", "Sheet size")};
}

[[nodiscard]] inline std::vector<volt::TitleBlockField>
title_block_from_list(const py::list &fields) {
    auto result = std::vector<volt::TitleBlockField>{};
    result.reserve(static_cast<std::size_t>(py::len(fields)));
    for (const auto item : fields) {
        const auto field = py::cast<py::dict>(item);
        result.emplace_back(required_string_field(field, "key", "Title block field"),
                            required_string_field(field, "value", "Title block field"));
    }
    return result;
}

[[nodiscard]] inline volt::SheetMargins sheet_margins_from_dict(const py::dict &dict) {
    return volt::SheetMargins{required_finite_number_field(dict, "left", "Sheet margins"),
                              required_finite_number_field(dict, "top", "Sheet margins"),
                              required_finite_number_field(dict, "right", "Sheet margins"),
                              required_finite_number_field(dict, "bottom", "Sheet margins")};
}

[[nodiscard]] inline volt::SheetFrame sheet_frame_from_dict(const py::dict &dict) {
    auto margins = volt::SheetMargins{};
    if (dict.contains("margins")) {
        margins = sheet_margins_from_dict(py::cast<py::dict>(dict["margins"]));
    }
    return volt::SheetFrame{optional_bool_field(dict, "visible", true), margins};
}

[[nodiscard]] inline std::optional<volt::SheetCoordinateZones>
sheet_coordinate_zones_from_object(const py::object &object) {
    if (object.is_none()) {
        return std::nullopt;
    }
    const auto dict = py::cast<py::dict>(object);
    return volt::SheetCoordinateZones{required_size_field(dict, "columns", "Coordinate zones"),
                                      required_size_field(dict, "rows", "Coordinate zones"),
                                      optional_bool_field(dict, "visible", true)};
}

[[nodiscard]] inline std::optional<volt::SheetGrid>
sheet_grid_from_object(const py::object &object) {
    if (object.is_none()) {
        return std::nullopt;
    }
    const auto dict = py::cast<py::dict>(object);
    return volt::SheetGrid{required_finite_number_field(dict, "spacing", "Sheet grid"),
                           optional_bool_field(dict, "visible", true)};
}

[[nodiscard]] inline volt::SheetMetadata
sheet_metadata_from_dict(const py::dict &dict, const std::string &fallback_title) {
    if (py::len(dict) == 0) {
        return volt::SheetMetadata{fallback_title};
    }

    auto size = volt::SheetSize{};
    if (dict.contains("size")) {
        size = sheet_size_from_dict(py::cast<py::dict>(dict["size"]));
    }
    auto title_block = std::vector<volt::TitleBlockField>{};
    if (dict.contains("title_block")) {
        title_block = title_block_from_list(py::cast<py::list>(dict["title_block"]));
    }
    auto frame = volt::SheetFrame{};
    if (dict.contains("frame")) {
        frame = sheet_frame_from_dict(py::cast<py::dict>(dict["frame"]));
    }
    auto coordinate_zones = std::optional<volt::SheetCoordinateZones>{};
    if (dict.contains("coordinate_zones")) {
        coordinate_zones = sheet_coordinate_zones_from_object(
            py::reinterpret_borrow<py::object>(dict["coordinate_zones"]));
    }
    auto grid = std::optional<volt::SheetGrid>{};
    if (dict.contains("grid")) {
        grid = sheet_grid_from_object(py::reinterpret_borrow<py::object>(dict["grid"]));
    }
    return volt::SheetMetadata{
        optional_string_field(dict, "title", fallback_title),
        size,
        std::move(title_block),
        sheet_orientation_from_string(optional_string_field(dict, "orientation", "Landscape")),
        frame,
        coordinate_zones,
        grid};
}

[[nodiscard]] inline std::vector<volt::SheetRegionStyleField>
region_style_from_dict(const py::dict &dict) {
    auto result = std::vector<volt::SheetRegionStyleField>{};
    result.reserve(static_cast<std::size_t>(py::len(dict)));
    for (const auto item : dict) {
        result.emplace_back(py::cast<std::string>(item.first), py::cast<std::string>(item.second));
    }
    return result;
}

[[nodiscard]] inline volt::SheetRegion sheet_region_from_dict(const py::dict &dict) {
    const auto bounds = required_dict_field(dict, "bounds", "Sheet region");
    auto style = std::vector<volt::SheetRegionStyleField>{};
    if (dict.contains("style")) {
        style = region_style_from_dict(py::cast<py::dict>(dict["style"]));
    }
    return volt::SheetRegion{
        required_string_field(dict, "name", "Sheet region"),
        required_string_field(dict, "title", "Sheet region"),
        volt::SheetRegionBounds{
            required_finite_number_field(bounds, "x", "Sheet region bounds"),
            required_finite_number_field(bounds, "y", "Sheet region bounds"),
            required_finite_number_field(bounds, "width", "Sheet region bounds"),
            required_finite_number_field(bounds, "height", "Sheet region bounds")},
        std::move(style)};
}

[[nodiscard]] inline volt::SchematicTextStyle
text_style_from_dict(const py::dict &dict, volt::SchematicTextStyle defaults) {
    auto font_size = optional_positive_number_field(dict, "font_size", "Schematic text");
    if (!font_size.has_value()) {
        font_size = defaults.font_size();
    }
    return volt::SchematicTextStyle{
        text_horizontal_alignment_from_string(
            optional_string_field(dict, "horizontal_alignment",
                                  std::string{volt::io::text_horizontal_alignment_name(
                                      defaults.horizontal_alignment())})),
        text_vertical_alignment_from_string(optional_string_field(
            dict, "vertical_alignment",
            std::string{volt::io::text_vertical_alignment_name(defaults.vertical_alignment())})),
        font_size};
}

[[nodiscard]] inline volt::SchematicTextStyle
text_style_from_strings(const std::string &horizontal_alignment,
                        const std::string &vertical_alignment, std::optional<double> font_size) {
    return volt::SchematicTextStyle{text_horizontal_alignment_from_string(horizontal_alignment),
                                    text_vertical_alignment_from_string(vertical_alignment),
                                    font_size};
}

[[nodiscard]] inline volt::SymbolPin symbol_pin_from_dict(const py::dict &dict) {
    return volt::SymbolPin{required_string_field(dict, "name", "Symbol pin"),
                           required_string_field(dict, "number", "Symbol pin"),
                           point_from_dict(required_dict_field(dict, "anchor", "Symbol pin")),
                           schematic_orientation_from_string(
                               required_string_field(dict, "orientation", "Symbol pin"))};
}

[[nodiscard]] inline volt::SymbolPrimitive symbol_primitive_from_dict(const py::dict &dict) {
    const auto type = required_string_field(dict, "type", "Symbol primitive");
    if (type == "line") {
        return volt::SymbolLine{
            point_from_dict(required_dict_field(dict, "start", "Symbol line")),
            point_from_dict(required_dict_field(dict, "end", "Symbol line")),
            symbol_line_role_from_string(optional_string_field(dict, "role", "Normal"))};
    }
    if (type == "rectangle") {
        return volt::SymbolRectangle{
            point_from_dict(required_dict_field(dict, "first_corner", "Symbol rectangle")),
            point_from_dict(required_dict_field(dict, "second_corner", "Symbol rectangle"))};
    }
    if (type == "circle") {
        return volt::SymbolCircle{
            point_from_dict(required_dict_field(dict, "center", "Symbol circle")),
            required_number_field(dict, "radius", "Symbol circle")};
    }
    if (type == "arc") {
        return volt::SymbolArc{point_from_dict(required_dict_field(dict, "center", "Symbol arc")),
                               required_number_field(dict, "radius", "Symbol arc"),
                               required_number_field(dict, "start_degrees", "Symbol arc"),
                               required_number_field(dict, "sweep_degrees", "Symbol arc")};
    }
    if (type == "text") {
        return volt::SymbolText{required_string_field(dict, "text", "Symbol text"),
                                point_from_dict(required_dict_field(dict, "anchor", "Symbol text")),
                                schematic_orientation_from_string(
                                    required_string_field(dict, "orientation", "Symbol text")),
                                text_style_from_dict(dict, volt::SchematicTextStyle{})};
    }

    throw std::invalid_argument{"Unknown schematic symbol primitive"};
}

[[nodiscard]] inline volt::SymbolDefinition symbol_definition_from_dict(const py::dict &dict) {
    auto symbol = volt::SymbolDefinition{required_string_field(dict, "name", "Symbol definition")};
    if (!dict.contains("pins")) {
        throw std::invalid_argument{"Symbol definition must include pins"};
    }
    if (!dict.contains("primitives")) {
        throw std::invalid_argument{"Symbol definition must include primitives"};
    }

    for (const auto item : py::cast<py::list>(dict["pins"])) {
        symbol.add_pin(symbol_pin_from_dict(py::cast<py::dict>(item)));
    }
    for (const auto item : py::cast<py::list>(dict["primitives"])) {
        symbol.add_primitive(symbol_primitive_from_dict(py::cast<py::dict>(item)));
    }
    return symbol;
}

[[nodiscard]] inline std::vector<volt::SchematicSymbolReference>
schematic_symbol_references_from_list(const py::list &symbols) {
    auto result = std::vector<volt::SchematicSymbolReference>{};
    result.reserve(static_cast<std::size_t>(py::len(symbols)));
    for (const auto item : symbols) {
        const auto symbol = py::cast<py::dict>(item);
        result.emplace_back(required_string_field(symbol, "name", "Schematic symbol reference"),
                            optional_string_field(symbol, "variant", "default"));
    }
    return result;
}

[[nodiscard]] inline std::optional<volt::SymbolDefinition>
built_in_symbol(const std::string &name) {
    return volt::default_schematic_symbol(name);
}

} // namespace

} // namespace volt::python
