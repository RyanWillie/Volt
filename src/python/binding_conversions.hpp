#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <volt/authoring/component_library.hpp>
#include <volt/authoring/reference_designators.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/circuit/validation.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/properties.hpp>
#include <volt/io/logical_circuit_writer.hpp>
#include <volt/io/schematic_reader.hpp>
#include <volt/io/schematic_svg_writer.hpp>
#include <volt/io/schematic_writer.hpp>
#include <volt/schematic/default_symbols.hpp>
#include <volt/schematic/layout.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/schematic_document.hpp>
#include <volt/schematic/symbols.hpp>
#include <volt/schematic/validation.hpp>

namespace py = pybind11;

namespace volt::python {

namespace {

[[nodiscard]] volt::ComponentDefId component_def_id(std::size_t index) {
    return volt::ComponentDefId{index};
}

[[nodiscard]] volt::ComponentId component_id(std::size_t index) { return volt::ComponentId{index}; }

[[nodiscard]] volt::PinId pin_id(std::size_t index) { return volt::PinId{index}; }

[[nodiscard]] volt::NetId net_id(std::size_t index) { return volt::NetId{index}; }

[[nodiscard]] volt::SheetId sheet_id(std::size_t index) { return volt::SheetId{index}; }

[[nodiscard]] volt::ModuleDefId module_def_id(std::size_t index) {
    return volt::ModuleDefId{index};
}

[[nodiscard]] volt::TemplateNetDefId template_net_def_id(std::size_t index) {
    return volt::TemplateNetDefId{index};
}

[[nodiscard]] volt::PortDefId port_def_id(std::size_t index) { return volt::PortDefId{index}; }

[[nodiscard]] volt::ModuleComponentId module_component_id(std::size_t index) {
    return volt::ModuleComponentId{index};
}

[[nodiscard]] volt::ModuleInstanceId module_instance_id(std::size_t index) {
    return volt::ModuleInstanceId{index};
}

[[nodiscard]] volt::PinRole parse_pin_role(const std::string &value) {
    if (value == "passive" || value == "Passive") {
        return volt::PinRole::Passive;
    }
    if (value == "input" || value == "digital_input" || value == "DigitalInput") {
        return volt::PinRole::DigitalInput;
    }
    if (value == "output" || value == "digital_output" || value == "DigitalOutput") {
        return volt::PinRole::DigitalOutput;
    }
    if (value == "analog_input" || value == "AnalogInput") {
        return volt::PinRole::AnalogInput;
    }
    if (value == "analog_output" || value == "AnalogOutput") {
        return volt::PinRole::AnalogOutput;
    }
    if (value == "bidirectional" || value == "Bidirectional") {
        return volt::PinRole::Bidirectional;
    }
    if (value == "power" || value == "power_input" || value == "PowerInput") {
        return volt::PinRole::PowerInput;
    }
    if (value == "power_output" || value == "PowerOutput") {
        return volt::PinRole::PowerOutput;
    }
    if (value == "ground" || value == "Ground") {
        return volt::PinRole::Ground;
    }
    if (value == "no_connect" || value == "no-connect" || value == "NoConnect") {
        return volt::PinRole::NoConnect;
    }

    throw std::invalid_argument{"Unknown pin role"};
}

[[nodiscard]] volt::ConnectionRequirement parse_connection_requirement(const std::string &value) {
    if (value == "required" || value == "Required") {
        return volt::ConnectionRequirement::Required;
    }
    if (value == "optional" || value == "Optional") {
        return volt::ConnectionRequirement::Optional;
    }
    if (value == "must_not_connect" || value == "must-not-connect" || value == "MustNotConnect") {
        return volt::ConnectionRequirement::MustNotConnect;
    }

    throw std::invalid_argument{"Unknown connection requirement"};
}

[[nodiscard]] volt::ElectricalTerminalKind parse_terminal_kind(const std::string &value) {
    if (value == "unspecified" || value == "Unspecified") {
        return volt::ElectricalTerminalKind::Unspecified;
    }
    if (value == "passive" || value == "Passive") {
        return volt::ElectricalTerminalKind::Passive;
    }
    if (value == "signal" || value == "Signal") {
        return volt::ElectricalTerminalKind::Signal;
    }
    if (value == "power" || value == "Power") {
        return volt::ElectricalTerminalKind::Power;
    }
    if (value == "ground" || value == "Ground") {
        return volt::ElectricalTerminalKind::Ground;
    }
    if (value == "no_connect" || value == "no-connect" || value == "NoConnect") {
        return volt::ElectricalTerminalKind::NoConnect;
    }

    throw std::invalid_argument{"Unknown electrical terminal kind"};
}

[[nodiscard]] volt::ElectricalDirection parse_direction(const std::string &value) {
    if (value == "unspecified" || value == "Unspecified") {
        return volt::ElectricalDirection::Unspecified;
    }
    if (value == "input" || value == "Input") {
        return volt::ElectricalDirection::Input;
    }
    if (value == "output" || value == "Output") {
        return volt::ElectricalDirection::Output;
    }
    if (value == "bidirectional" || value == "Bidirectional") {
        return volt::ElectricalDirection::Bidirectional;
    }
    if (value == "passive" || value == "Passive") {
        return volt::ElectricalDirection::Passive;
    }

    throw std::invalid_argument{"Unknown electrical direction"};
}

[[nodiscard]] volt::ElectricalSignalDomain parse_signal_domain(const std::string &value) {
    if (value == "unspecified" || value == "Unspecified") {
        return volt::ElectricalSignalDomain::Unspecified;
    }
    if (value == "digital" || value == "Digital") {
        return volt::ElectricalSignalDomain::Digital;
    }
    if (value == "analog" || value == "Analog") {
        return volt::ElectricalSignalDomain::Analog;
    }
    if (value == "mixed" || value == "Mixed") {
        return volt::ElectricalSignalDomain::Mixed;
    }

    throw std::invalid_argument{"Unknown electrical signal domain"};
}

[[nodiscard]] volt::ElectricalDriveKind parse_drive_kind(const std::string &value) {
    if (value == "unspecified" || value == "Unspecified") {
        return volt::ElectricalDriveKind::Unspecified;
    }
    if (value == "push_pull" || value == "push-pull" || value == "PushPull") {
        return volt::ElectricalDriveKind::PushPull;
    }
    if (value == "open_collector" || value == "open-collector" || value == "OpenCollector") {
        return volt::ElectricalDriveKind::OpenCollector;
    }
    if (value == "open_drain" || value == "open-drain" || value == "OpenDrain") {
        return volt::ElectricalDriveKind::OpenDrain;
    }
    if (value == "high_impedance" || value == "high-impedance" || value == "HighImpedance") {
        return volt::ElectricalDriveKind::HighImpedance;
    }
    if (value == "passive" || value == "Passive") {
        return volt::ElectricalDriveKind::Passive;
    }

    throw std::invalid_argument{"Unknown electrical drive kind"};
}

[[nodiscard]] volt::ElectricalPolarity parse_polarity(const std::string &value) {
    if (value == "none" || value == "None") {
        return volt::ElectricalPolarity::None;
    }
    if (value == "active_high" || value == "active-high" || value == "ActiveHigh") {
        return volt::ElectricalPolarity::ActiveHigh;
    }
    if (value == "active_low" || value == "active-low" || value == "ActiveLow") {
        return volt::ElectricalPolarity::ActiveLow;
    }

    throw std::invalid_argument{"Unknown electrical polarity"};
}

[[nodiscard]] volt::NetKind parse_net_kind(const std::string &value) {
    if (value == "signal" || value == "Signal") {
        return volt::NetKind::Signal;
    }
    if (value == "power" || value == "Power") {
        return volt::NetKind::Power;
    }
    if (value == "ground" || value == "Ground") {
        return volt::NetKind::Ground;
    }
    if (value == "clock" || value == "Clock") {
        return volt::NetKind::Clock;
    }
    if (value == "analog" || value == "Analog") {
        return volt::NetKind::Analog;
    }
    if (value == "high_current" || value == "high-current" || value == "HighCurrent") {
        return volt::NetKind::HighCurrent;
    }

    throw std::invalid_argument{"Unknown net kind"};
}

[[nodiscard]] volt::PortRole parse_port_role(const std::string &value) {
    if (value == "passive" || value == "Passive") {
        return volt::PortRole::Passive;
    }
    if (value == "input" || value == "Input") {
        return volt::PortRole::Input;
    }
    if (value == "output" || value == "Output") {
        return volt::PortRole::Output;
    }
    if (value == "bidirectional" || value == "Bidirectional") {
        return volt::PortRole::Bidirectional;
    }
    if (value == "power" || value == "power_input" || value == "PowerInput") {
        return volt::PortRole::PowerInput;
    }
    if (value == "power_output" || value == "PowerOutput") {
        return volt::PortRole::PowerOutput;
    }
    if (value == "ground" || value == "Ground") {
        return volt::PortRole::Ground;
    }

    throw std::invalid_argument{"Unknown port role"};
}

[[nodiscard]] std::string net_kind_name(volt::NetKind kind) {
    switch (kind) {
    case volt::NetKind::Signal:
        return "Signal";
    case volt::NetKind::Power:
        return "Power";
    case volt::NetKind::Ground:
        return "Ground";
    case volt::NetKind::Clock:
        return "Clock";
    case volt::NetKind::Analog:
        return "Analog";
    case volt::NetKind::HighCurrent:
        return "HighCurrent";
    }

    throw std::logic_error{"Unhandled net kind"};
}

[[nodiscard]] std::string port_role_name(volt::PortRole role) {
    switch (role) {
    case volt::PortRole::Passive:
        return "Passive";
    case volt::PortRole::Input:
        return "Input";
    case volt::PortRole::Output:
        return "Output";
    case volt::PortRole::Bidirectional:
        return "Bidirectional";
    case volt::PortRole::PowerInput:
        return "PowerInput";
    case volt::PortRole::PowerOutput:
        return "PowerOutput";
    case volt::PortRole::Ground:
        return "Ground";
    }

    throw std::logic_error{"Unhandled port role"};
}

[[nodiscard]] std::string severity_name(volt::Severity severity) {
    switch (severity) {
    case volt::Severity::Info:
        return "info";
    case volt::Severity::Warning:
        return "warning";
    case volt::Severity::Error:
        return "error";
    }

    throw std::logic_error{"Unhandled diagnostic severity"};
}

void require_finite(double value, const char *message) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument{message};
    }
}

[[nodiscard]] volt::UnitDimension parse_dimension(const std::string &value) {
    if (value == "resistance") {
        return volt::UnitDimension::Resistance;
    }
    if (value == "capacitance") {
        return volt::UnitDimension::Capacitance;
    }
    if (value == "inductance") {
        return volt::UnitDimension::Inductance;
    }
    if (value == "voltage") {
        return volt::UnitDimension::Voltage;
    }
    if (value == "power") {
        return volt::UnitDimension::Power;
    }
    if (value == "ratio") {
        return volt::UnitDimension::Ratio;
    }

    throw std::invalid_argument{"Unknown electrical attribute dimension"};
}

[[nodiscard]] volt::ElectricalAttributeSpec component_quantity_spec(const std::string &name,
                                                                    volt::UnitDimension dimension) {
    return volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{name},
                                         volt::ElectricalAttributeOwner::ComponentInstance,
                                         volt::ElectricalAttributeKind::DesignInput, dimension};
}

[[nodiscard]] volt::ElectricalAttributeSpec
selected_part_quantity_spec(const std::string &name, volt::UnitDimension dimension) {
    return volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{name},
                                         volt::ElectricalAttributeOwner::SelectedPart,
                                         volt::ElectricalAttributeKind::DesignInput, dimension};
}

[[nodiscard]] volt::ElectricalAttributeSpec net_quantity_spec(const std::string &name,
                                                              volt::UnitDimension dimension) {
    return volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{name},
                                         volt::ElectricalAttributeOwner::Net,
                                         volt::ElectricalAttributeKind::DesignInput, dimension};
}

[[nodiscard]] std::string required_string_field(const py::dict &dict, const char *field,
                                                const char *context) {
    if (!dict.contains(field)) {
        throw std::invalid_argument{std::string{context} + " must include " + field};
    }
    return py::cast<std::string>(dict[field]);
}

[[nodiscard]] std::string optional_string_field(const py::dict &dict, const char *field,
                                                std::string default_value) {
    if (!dict.contains(field)) {
        return default_value;
    }
    return py::cast<std::string>(dict[field]);
}

[[nodiscard]] py::dict required_dict_field(const py::dict &dict, const char *field,
                                           const char *context) {
    if (!dict.contains(field)) {
        throw std::invalid_argument{std::string{context} + " must include " + field};
    }
    return py::cast<py::dict>(dict[field]);
}

[[nodiscard]] double required_number_field(const py::dict &dict, const char *field,
                                           const char *context) {
    if (!dict.contains(field)) {
        throw std::invalid_argument{std::string{context} + " must include " + field};
    }
    const auto value = py::cast<double>(dict[field]);
    require_finite(value, "Schematic symbol numbers must be finite");
    return value;
}

[[nodiscard]] double required_finite_number_field(const py::dict &dict, const char *field,
                                                  const char *context) {
    if (!dict.contains(field)) {
        throw std::invalid_argument{std::string{context} + " must include " + field};
    }
    const auto value = py::cast<double>(dict[field]);
    const auto message = std::string{context} + " numbers must be finite";
    require_finite(value, message.c_str());
    return value;
}

[[nodiscard]] bool optional_bool_field(const py::dict &dict, const char *field,
                                       bool default_value) {
    if (!dict.contains(field)) {
        return default_value;
    }
    return py::cast<bool>(dict[field]);
}

[[nodiscard]] std::optional<double>
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

[[nodiscard]] std::size_t required_size_field(const py::dict &dict, const char *field,
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

[[nodiscard]] volt::Point point_from_dict(const py::dict &dict) {
    return volt::Point{required_number_field(dict, "x", "Symbol point"),
                       required_number_field(dict, "y", "Symbol point")};
}

[[nodiscard]] volt::SheetOrientation sheet_orientation_from_string(const std::string &value) {
    if (value == "Portrait") {
        return volt::SheetOrientation::Portrait;
    }
    if (value == "Landscape") {
        return volt::SheetOrientation::Landscape;
    }
    throw std::invalid_argument{"Unknown schematic sheet orientation"};
}

[[nodiscard]] volt::SheetSize sheet_size_from_dict(const py::dict &dict) {
    return volt::SheetSize{required_finite_number_field(dict, "width", "Sheet size"),
                           required_finite_number_field(dict, "height", "Sheet size")};
}

[[nodiscard]] std::vector<volt::TitleBlockField> title_block_from_list(const py::list &fields) {
    auto result = std::vector<volt::TitleBlockField>{};
    result.reserve(static_cast<std::size_t>(py::len(fields)));
    for (const auto item : fields) {
        const auto field = py::cast<py::dict>(item);
        result.emplace_back(required_string_field(field, "key", "Title block field"),
                            required_string_field(field, "value", "Title block field"));
    }
    return result;
}

[[nodiscard]] volt::SheetMargins sheet_margins_from_dict(const py::dict &dict) {
    return volt::SheetMargins{required_finite_number_field(dict, "left", "Sheet margins"),
                              required_finite_number_field(dict, "top", "Sheet margins"),
                              required_finite_number_field(dict, "right", "Sheet margins"),
                              required_finite_number_field(dict, "bottom", "Sheet margins")};
}

[[nodiscard]] volt::SheetFrame sheet_frame_from_dict(const py::dict &dict) {
    auto margins = volt::SheetMargins{};
    if (dict.contains("margins")) {
        margins = sheet_margins_from_dict(py::cast<py::dict>(dict["margins"]));
    }
    return volt::SheetFrame{optional_bool_field(dict, "visible", true), margins};
}

[[nodiscard]] std::optional<volt::SheetCoordinateZones>
sheet_coordinate_zones_from_object(const py::object &object) {
    if (object.is_none()) {
        return std::nullopt;
    }
    const auto dict = py::cast<py::dict>(object);
    return volt::SheetCoordinateZones{required_size_field(dict, "columns", "Coordinate zones"),
                                      required_size_field(dict, "rows", "Coordinate zones"),
                                      optional_bool_field(dict, "visible", true)};
}

[[nodiscard]] std::optional<volt::SheetGrid> sheet_grid_from_object(const py::object &object) {
    if (object.is_none()) {
        return std::nullopt;
    }
    const auto dict = py::cast<py::dict>(object);
    return volt::SheetGrid{required_finite_number_field(dict, "spacing", "Sheet grid"),
                           optional_bool_field(dict, "visible", true)};
}

[[nodiscard]] volt::SheetMetadata sheet_metadata_from_dict(const py::dict &dict,
                                                           const std::string &fallback_title) {
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

[[nodiscard]] std::vector<volt::SheetRegionStyleField>
region_style_from_dict(const py::dict &dict) {
    auto result = std::vector<volt::SheetRegionStyleField>{};
    result.reserve(static_cast<std::size_t>(py::len(dict)));
    for (const auto item : dict) {
        result.emplace_back(py::cast<std::string>(item.first), py::cast<std::string>(item.second));
    }
    return result;
}

[[nodiscard]] volt::SheetRegion sheet_region_from_dict(const py::dict &dict) {
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

[[nodiscard]] volt::SchematicOrientation
schematic_orientation_from_string(const std::string &value) {
    if (value == "Right") {
        return volt::SchematicOrientation::Right;
    }
    if (value == "Down") {
        return volt::SchematicOrientation::Down;
    }
    if (value == "Left") {
        return volt::SchematicOrientation::Left;
    }
    if (value == "Up") {
        return volt::SchematicOrientation::Up;
    }
    throw std::invalid_argument{"Unknown schematic orientation"};
}

[[nodiscard]] std::string schematic_orientation_name(volt::SchematicOrientation value) {
    switch (value) {
    case volt::SchematicOrientation::Right:
        return "Right";
    case volt::SchematicOrientation::Down:
        return "Down";
    case volt::SchematicOrientation::Left:
        return "Left";
    case volt::SchematicOrientation::Up:
        return "Up";
    }
    throw std::logic_error{"Unhandled schematic orientation"};
}

[[nodiscard]] volt::SymbolLineRole symbol_line_role_from_string(const std::string &value) {
    if (value == "Normal") {
        return volt::SymbolLineRole::Normal;
    }
    if (value == "TerminalLeadStart") {
        return volt::SymbolLineRole::TerminalLeadStart;
    }
    if (value == "TerminalLeadEnd") {
        return volt::SymbolLineRole::TerminalLeadEnd;
    }
    throw std::invalid_argument{"Unknown symbol line role"};
}

[[nodiscard]] int schematic_orientation_quarter_turns(volt::SchematicOrientation value) {
    switch (value) {
    case volt::SchematicOrientation::Right:
        return 0;
    case volt::SchematicOrientation::Down:
        return 1;
    case volt::SchematicOrientation::Left:
        return 2;
    case volt::SchematicOrientation::Up:
        return 3;
    }
    throw std::logic_error{"Unhandled schematic orientation"};
}

[[nodiscard]] volt::SchematicOrientation schematic_orientation_from_quarter_turns(int value) {
    switch (value % 4) {
    case 0:
        return volt::SchematicOrientation::Right;
    case 1:
        return volt::SchematicOrientation::Down;
    case 2:
        return volt::SchematicOrientation::Left;
    case 3:
        return volt::SchematicOrientation::Up;
    }
    throw std::logic_error{"Unhandled schematic orientation turn count"};
}

[[nodiscard]] volt::SchematicOrientation
rotated_schematic_orientation(volt::SchematicOrientation local,
                              volt::SchematicOrientation instance) {
    return schematic_orientation_from_quarter_turns(schematic_orientation_quarter_turns(local) +
                                                    schematic_orientation_quarter_turns(instance));
}

[[nodiscard]] volt::RouteIntent route_intent_from_string(const std::string &value) {
    if (value == "Direct") {
        return volt::RouteIntent::Direct;
    }
    if (value == "Orthogonal") {
        return volt::RouteIntent::Orthogonal;
    }
    throw std::invalid_argument{"Unknown schematic route intent"};
}

[[nodiscard]] volt::PowerPortKind power_port_kind_from_string(const std::string &value) {
    if (value == "Power") {
        return volt::PowerPortKind::Power;
    }
    if (value == "Ground") {
        return volt::PowerPortKind::Ground;
    }
    throw std::invalid_argument{"Unknown schematic power port kind"};
}

[[nodiscard]] volt::SheetPortKind sheet_port_kind_from_string(const std::string &value) {
    if (value == "Input") {
        return volt::SheetPortKind::Input;
    }
    if (value == "Output") {
        return volt::SheetPortKind::Output;
    }
    if (value == "Bidirectional") {
        return volt::SheetPortKind::Bidirectional;
    }
    if (value == "OffPage") {
        return volt::SheetPortKind::OffPage;
    }
    throw std::invalid_argument{"Unknown schematic sheet port kind"};
}

[[nodiscard]] volt::TextHorizontalAlignment
text_horizontal_alignment_from_string(const std::string &value) {
    if (value == "Start") {
        return volt::TextHorizontalAlignment::Start;
    }
    if (value == "Middle") {
        return volt::TextHorizontalAlignment::Middle;
    }
    if (value == "End") {
        return volt::TextHorizontalAlignment::End;
    }
    throw std::invalid_argument{"Unknown schematic text horizontal alignment"};
}

[[nodiscard]] volt::TextVerticalAlignment
text_vertical_alignment_from_string(const std::string &value) {
    if (value == "Top") {
        return volt::TextVerticalAlignment::Top;
    }
    if (value == "Middle") {
        return volt::TextVerticalAlignment::Middle;
    }
    if (value == "Bottom") {
        return volt::TextVerticalAlignment::Bottom;
    }
    if (value == "Baseline") {
        return volt::TextVerticalAlignment::Baseline;
    }
    throw std::invalid_argument{"Unknown schematic text vertical alignment"};
}

[[nodiscard]] volt::SchematicTextStyle text_style_from_dict(const py::dict &dict,
                                                            volt::SchematicTextStyle defaults) {
    auto font_size = optional_positive_number_field(dict, "font_size", "Schematic text");
    if (!font_size.has_value()) {
        font_size = defaults.font_size();
    }
    return volt::SchematicTextStyle{
        text_horizontal_alignment_from_string(optional_string_field(
            dict, "horizontal_alignment",
            volt::io::detail::text_horizontal_alignment_name(defaults.horizontal_alignment()))),
        text_vertical_alignment_from_string(optional_string_field(
            dict, "vertical_alignment",
            volt::io::detail::text_vertical_alignment_name(defaults.vertical_alignment()))),
        font_size};
}

[[nodiscard]] volt::SchematicTextStyle
text_style_from_strings(const std::string &horizontal_alignment,
                        const std::string &vertical_alignment, std::optional<double> font_size) {
    return volt::SchematicTextStyle{text_horizontal_alignment_from_string(horizontal_alignment),
                                    text_vertical_alignment_from_string(vertical_alignment),
                                    font_size};
}

[[nodiscard]] volt::SymbolPin symbol_pin_from_dict(const py::dict &dict) {
    return volt::SymbolPin{required_string_field(dict, "name", "Symbol pin"),
                           required_string_field(dict, "number", "Symbol pin"),
                           point_from_dict(required_dict_field(dict, "anchor", "Symbol pin")),
                           schematic_orientation_from_string(
                               required_string_field(dict, "orientation", "Symbol pin"))};
}

[[nodiscard]] volt::SymbolPrimitive symbol_primitive_from_dict(const py::dict &dict) {
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

[[nodiscard]] volt::SymbolDefinition symbol_definition_from_dict(const py::dict &dict) {
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

[[nodiscard]] std::vector<volt::SchematicSymbolReference>
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

[[nodiscard]] std::optional<volt::SymbolDefinition> built_in_symbol(const std::string &name) {
    return volt::default_schematic_symbol(name);
}

[[nodiscard]] std::string entity_kind_name(volt::EntityKind kind) {
    switch (kind) {
    case volt::EntityKind::ComponentDef:
        return "component_definition";
    case volt::EntityKind::Component:
        return "component";
    case volt::EntityKind::PinDef:
        return "pin_definition";
    case volt::EntityKind::Pin:
        return "pin";
    case volt::EntityKind::Net:
        return "net";
    case volt::EntityKind::ModuleDef:
        return "module_definition";
    case volt::EntityKind::ModuleInstance:
        return "module_instance";
    case volt::EntityKind::PortDef:
        return "port_definition";
    case volt::EntityKind::SymbolDef:
        return "symbol_definition";
    case volt::EntityKind::Sheet:
        return "sheet";
    case volt::EntityKind::SymbolInstance:
        return "symbol_instance";
    case volt::EntityKind::WireRun:
        return "wire_run";
    case volt::EntityKind::NetLabel:
        return "net_label";
    case volt::EntityKind::Junction:
        return "junction";
    case volt::EntityKind::PowerPort:
        return "power_port";
    case volt::EntityKind::NoConnectMarker:
        return "no_connect_marker";
    case volt::EntityKind::SheetPort:
        return "sheet_port";
    case volt::EntityKind::SymbolField:
        return "symbol_field";
    }

    throw std::logic_error{"Unhandled diagnostic entity kind"};
}

[[nodiscard]] volt::PropertyMap properties_from_dict(const py::dict &dict) {
    auto properties = volt::PropertyMap{};

    for (const auto item : dict) {
        if (!py::isinstance<py::str>(item.first)) {
            throw std::invalid_argument{"Property keys must be strings"};
        }

        auto key = volt::PropertyKey{py::cast<std::string>(item.first)};
        const auto value = item.second;

        if (py::isinstance<py::bool_>(value)) {
            properties.set(std::move(key), volt::PropertyValue{py::cast<bool>(value)});
        } else if (py::isinstance<py::int_>(value)) {
            properties.set(std::move(key), volt::PropertyValue{static_cast<std::int64_t>(
                                               py::cast<long long>(value))});
        } else if (py::isinstance<py::float_>(value)) {
            const auto number = py::cast<double>(value);
            if (!std::isfinite(number)) {
                throw std::invalid_argument{"Property numbers must be finite"};
            }
            properties.set(std::move(key), volt::PropertyValue{number});
        } else if (py::isinstance<py::str>(value)) {
            properties.set(std::move(key), volt::PropertyValue{py::cast<std::string>(value)});
        } else {
            throw std::invalid_argument{
                "Property values must be strings, booleans, ints, or floats"};
        }
    }

    return properties;
}

[[nodiscard]] volt::authoring::PinSpec pin_spec_from_dict(const py::dict &dict) {
    if (!dict.contains("name")) {
        throw std::invalid_argument{"Pin specs must include a name"};
    }
    if (!dict.contains("number")) {
        throw std::invalid_argument{"Pin specs must include a number"};
    }

    auto role = volt::PinRole::Passive;
    if (dict.contains("role")) {
        role = parse_pin_role(py::cast<std::string>(dict["role"]));
    }

    auto requirement = volt::ConnectionRequirement::Required;
    if (dict.contains("requirement")) {
        requirement = parse_connection_requirement(py::cast<std::string>(dict["requirement"]));
    }

    auto terminal_kind = volt::ElectricalTerminalKind::Unspecified;
    if (dict.contains("terminal")) {
        terminal_kind = parse_terminal_kind(py::cast<std::string>(dict["terminal"]));
    }

    auto direction = volt::ElectricalDirection::Unspecified;
    if (dict.contains("direction")) {
        direction = parse_direction(py::cast<std::string>(dict["direction"]));
    }

    auto signal_domain = volt::ElectricalSignalDomain::Unspecified;
    if (dict.contains("signal")) {
        signal_domain = parse_signal_domain(py::cast<std::string>(dict["signal"]));
    }

    auto drive_kind = volt::ElectricalDriveKind::Unspecified;
    if (dict.contains("drive")) {
        drive_kind = parse_drive_kind(py::cast<std::string>(dict["drive"]));
    }

    auto polarity = volt::ElectricalPolarity::None;
    if (dict.contains("polarity")) {
        polarity = parse_polarity(py::cast<std::string>(dict["polarity"]));
    }

    auto voltage_range = std::optional<volt::QuantityRange>{};
    if (dict.contains("voltage_range")) {
        const auto range = py::cast<std::pair<std::optional<double>, std::optional<double>>>(
            dict["voltage_range"]);
        if (!range.first.has_value() && !range.second.has_value()) {
            throw std::invalid_argument{"voltage_range must include at least one bound"};
        }
        if (range.first.has_value()) {
            require_finite(range.first.value(), "Voltage range bounds must be finite");
        }
        if (range.second.has_value()) {
            require_finite(range.second.value(), "Voltage range bounds must be finite");
        }
        if (range.first.has_value() && range.second.has_value()) {
            voltage_range = volt::QuantityRange::bounded(
                volt::Quantity{volt::UnitDimension::Voltage, range.first.value()},
                volt::Quantity{volt::UnitDimension::Voltage, range.second.value()});
        } else if (range.first.has_value()) {
            voltage_range = volt::QuantityRange::minimum(
                volt::Quantity{volt::UnitDimension::Voltage, range.first.value()});
        } else {
            voltage_range = volt::QuantityRange::maximum(
                volt::Quantity{volt::UnitDimension::Voltage, range.second.value()});
        }
    }

    return volt::authoring::PinSpec{py::cast<std::string>(dict["name"]),
                                    py::cast<std::string>(dict["number"]),
                                    role,
                                    requirement,
                                    terminal_kind,
                                    direction,
                                    signal_domain,
                                    drive_kind,
                                    polarity,
                                    voltage_range};
}

[[nodiscard]] std::vector<volt::authoring::PinSpec> pin_specs_from_list(const py::list &pins) {
    auto specs = std::vector<volt::authoring::PinSpec>{};
    specs.reserve(static_cast<std::size_t>(py::len(pins)));
    for (const auto item : pins) {
        specs.push_back(pin_spec_from_dict(py::cast<py::dict>(item)));
    }
    return specs;
}

[[nodiscard]] std::string string_from_pin_key(const py::handle &key) {
    if (py::isinstance<py::int_>(key) || py::isinstance<py::str>(key)) {
        return py::cast<std::string>(py::str(key));
    }

    throw std::invalid_argument{"Pin-pad mapping keys must be pin numbers or names"};
}

[[nodiscard]] py::dict diagnostic_to_dict(const volt::Diagnostic &diagnostic) {
    auto result = py::dict{};
    result["severity"] = severity_name(diagnostic.severity());
    result["code"] = diagnostic.code().value();
    result["message"] = diagnostic.message();

    auto entities = py::list{};
    for (const auto entity : diagnostic.entities()) {
        auto entity_dict = py::dict{};
        entity_dict["kind"] = entity_kind_name(entity.kind());
        entity_dict["index"] = entity.index();
        entities.append(std::move(entity_dict));
    }
    result["entities"] = std::move(entities);

    return result;
}

[[nodiscard]] py::list diagnostics_to_list(const volt::DiagnosticReport &report) {
    auto diagnostics = py::list{};
    for (const auto &diagnostic : report.diagnostics()) {
        diagnostics.append(diagnostic_to_dict(diagnostic));
    }

    return diagnostics;
}

} // namespace

} // namespace volt::python
