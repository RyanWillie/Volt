#pragma once

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <volt/core/ids.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/symbols.hpp>

namespace volt::io {

/** Options that control deterministic schematic SVG rendering. */
struct SchematicSvgOptions {
    /** Include development overlays such as symbol pin anchors and labels. */
    bool debug_overlays = false;
};

/** One production-oriented SVG page exported from a schematic sheet. */
struct SchematicSvgPage {
    /** Source schematic sheet ID. */
    SheetId sheet;
    /** Human-readable sheet name used for filenames and labels. */
    std::string name;
    /** Complete SVG document for the sheet. */
    std::string svg;
};

namespace detail {

inline constexpr double svg_sheet_width = 297.0;
inline constexpr double svg_sheet_height = 210.0;
inline constexpr double svg_sheet_gap = 20.0;
inline constexpr double svg_pi = 3.14159265358979323846;
inline constexpr double title_block_width = 82.0;
inline constexpr double title_block_label_width = 22.0;
inline constexpr double title_block_row_height = 6.0;

/** Sheet-local rectangle used by SVG layout helpers. */
struct SvgRect {
    /** Rectangle x origin. */
    double x;
    /** Rectangle y origin. */
    double y;
    /** Rectangle width. */
    double width;
    /** Rectangle height. */
    double height;
};

[[nodiscard]] inline std::string svg_escape(std::string_view value) {
    auto result = std::string{};
    for (const auto character : value) {
        switch (character) {
        case '&':
            result += "&amp;";
            break;
        case '<':
            result += "&lt;";
            break;
        case '>':
            result += "&gt;";
            break;
        case '"':
            result += "&quot;";
            break;
        case '\'':
            result += "&apos;";
            break;
        default:
            result += character;
            break;
        }
    }
    return result;
}

[[nodiscard]] inline std::string display_scoped_label(std::string_view label) {
    const auto separator = label.find_last_of('/');
    if (separator == std::string_view::npos || separator + 1U >= label.size()) {
        return std::string{label};
    }
    return std::string{label.substr(separator + 1U)};
}

inline void write_svg_number(std::ostream &out, double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument{"SVG numeric values must be finite"};
    }
    if (std::abs(value) < 1e-12) {
        value = 0.0;
    }

    auto formatted = std::ostringstream{};
    formatted << std::setprecision(15) << value;
    out << formatted.str();
}

[[nodiscard]] inline std::string svg_component_id(ComponentId id) {
    return "component:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string svg_net_id(NetId id) {
    return "net:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string svg_pin_id(PinId id) {
    return "pin:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string svg_sheet_id(SheetId id) {
    return "sheet:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string svg_sheet_token(SheetId id) {
    return "sheet-" + std::to_string(id.index());
}

[[nodiscard]] inline std::string svg_symbol_def_id(SymbolDefId id) {
    return "symbol_def:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string svg_symbol_instance_id(SymbolInstanceId id) {
    return "symbol_instance:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string power_port_class(PowerPortKind kind) {
    switch (kind) {
    case PowerPortKind::Power:
        return "power";
    case PowerPortKind::Ground:
        return "ground";
    }
    throw std::logic_error{"Unhandled power port kind"};
}

[[nodiscard]] inline std::string sheet_port_class(SheetPortKind kind) {
    switch (kind) {
    case SheetPortKind::Input:
        return "input";
    case SheetPortKind::Output:
        return "output";
    case SheetPortKind::Bidirectional:
        return "bidirectional";
    case SheetPortKind::OffPage:
        return "off-page";
    }
    throw std::logic_error{"Unhandled sheet port kind"};
}

[[nodiscard]] inline double orientation_degrees(SchematicOrientation orientation) {
    switch (orientation) {
    case SchematicOrientation::Right:
        return 0.0;
    case SchematicOrientation::Down:
        return 90.0;
    case SchematicOrientation::Left:
        return 180.0;
    case SchematicOrientation::Up:
        return 270.0;
    }
    throw std::logic_error{"Unhandled schematic orientation"};
}

inline void write_upright_text_transform(std::ostream &out, SchematicOrientation parent_orientation,
                                         Point anchor) {
    const auto degrees = -orientation_degrees(parent_orientation);
    if (std::abs(degrees) < 1e-12) {
        return;
    }

    out << " transform=\"rotate(";
    write_svg_number(out, degrees);
    out << ' ';
    write_svg_number(out, anchor.x());
    out << ' ';
    write_svg_number(out, anchor.y());
    out << ")\"";
}

[[nodiscard]] inline SvgRect drawing_area(const SheetMetadata &metadata) {
    const auto margins = metadata.frame().margins();
    const auto width = std::max(0.0, metadata.size().width() - margins.left() - margins.right());
    const auto height = std::max(0.0, metadata.size().height() - margins.top() - margins.bottom());
    return SvgRect{margins.left(), margins.top(), width, height};
}

[[nodiscard]] inline SvgRect title_block_rect(const SheetMetadata &metadata) {
    const auto area = drawing_area(metadata);
    const auto rows = 1U + metadata.title_block().size();
    const auto height = title_block_row_height * static_cast<double>(rows);
    const auto width = std::min(title_block_width, area.width);
    return SvgRect{area.x + std::max(0.0, area.width - width),
                   area.y + std::max(0.0, area.height - height), width, height};
}

[[nodiscard]] inline std::string zone_row_label(std::size_t row) {
    auto value = row + 1U;
    auto label = std::string{};
    while (value != 0U) {
        --value;
        label.insert(label.begin(), static_cast<char>('A' + (value % 26U)));
        value /= 26U;
    }
    return label;
}

inline void write_xy_attributes(std::ostream &out, Point point, std::string_view x_name,
                                std::string_view y_name) {
    out << ' ' << x_name << "=\"";
    write_svg_number(out, point.x());
    out << "\" " << y_name << "=\"";
    write_svg_number(out, point.y());
    out << '"';
}

[[nodiscard]] inline Point point_on_arc(const SymbolArc &arc, double degrees) {
    const auto radians = degrees * svg_pi / 180.0;
    return Point{arc.center().x() + (arc.radius() * std::cos(radians)),
                 arc.center().y() + (arc.radius() * std::sin(radians))};
}

inline void write_symbol_arc_svg(std::ostream &out, const SymbolArc &arc) {
    const auto sweep_magnitude = std::abs(arc.sweep_degrees());
    const auto segment_count =
        std::max<std::size_t>(1U, static_cast<std::size_t>(std::ceil(sweep_magnitude / 180.0)));
    const auto segment_sweep = arc.sweep_degrees() / static_cast<double>(segment_count);

    out << "      <path class=\"symbol-arc\" d=\"M ";
    const auto start = point_on_arc(arc, arc.start_degrees());
    write_svg_number(out, start.x());
    out << ' ';
    write_svg_number(out, start.y());

    for (std::size_t segment = 0; segment < segment_count; ++segment) {
        const auto end_degrees =
            arc.start_degrees() + (segment_sweep * static_cast<double>(segment + 1U));
        const auto end = point_on_arc(arc, end_degrees);
        const auto large_arc = std::abs(segment_sweep) > 180.0 ? 1 : 0;
        const auto sweep = segment_sweep >= 0.0 ? 1 : 0;

        out << " A ";
        write_svg_number(out, arc.radius());
        out << ' ';
        write_svg_number(out, arc.radius());
        out << " 0 " << large_arc << ' ' << sweep << ' ';
        write_svg_number(out, end.x());
        out << ' ';
        write_svg_number(out, end.y());
    }

    out << "\"/>\n";
}

inline void write_symbol_primitive_svg(std::ostream &out, const SymbolPrimitive &primitive) {
    if (std::holds_alternative<SymbolLine>(primitive)) {
        const auto &line = std::get<SymbolLine>(primitive);
        out << "      <line class=\"symbol-line\"";
        write_xy_attributes(out, line.start(), "x1", "y1");
        write_xy_attributes(out, line.end(), "x2", "y2");
        out << "/>\n";
        return;
    }
    if (std::holds_alternative<SymbolRectangle>(primitive)) {
        const auto &rectangle = std::get<SymbolRectangle>(primitive);
        const auto min_x = std::min(rectangle.first_corner().x(), rectangle.second_corner().x());
        const auto min_y = std::min(rectangle.first_corner().y(), rectangle.second_corner().y());
        const auto width = std::abs(rectangle.second_corner().x() - rectangle.first_corner().x());
        const auto height = std::abs(rectangle.second_corner().y() - rectangle.first_corner().y());
        out << "      <rect class=\"symbol-rectangle\" x=\"";
        write_svg_number(out, min_x);
        out << "\" y=\"";
        write_svg_number(out, min_y);
        out << "\" width=\"";
        write_svg_number(out, width);
        out << "\" height=\"";
        write_svg_number(out, height);
        out << "\"/>\n";
        return;
    }
    if (std::holds_alternative<SymbolCircle>(primitive)) {
        const auto &circle = std::get<SymbolCircle>(primitive);
        out << "      <circle class=\"symbol-circle\" cx=\"";
        write_svg_number(out, circle.center().x());
        out << "\" cy=\"";
        write_svg_number(out, circle.center().y());
        out << "\" r=\"";
        write_svg_number(out, circle.radius());
        out << "\"/>\n";
        return;
    }
    if (std::holds_alternative<SymbolArc>(primitive)) {
        write_symbol_arc_svg(out, std::get<SymbolArc>(primitive));
        return;
    }

    const auto &text = std::get<SymbolText>(primitive);
    out << "      <text class=\"symbol-text\" x=\"";
    write_svg_number(out, text.anchor().x());
    out << "\" y=\"";
    write_svg_number(out, text.anchor().y());
    out << "\" transform=\"rotate(";
    write_svg_number(out, orientation_degrees(text.orientation()));
    out << ' ';
    write_svg_number(out, text.anchor().x());
    out << ' ';
    write_svg_number(out, text.anchor().y());
    out << ")\">" << svg_escape(text.text()) << "</text>\n";
}

inline void write_symbol_pin_svg(std::ostream &out, const SymbolPin &pin) {
    out << "      <circle class=\"pin-anchor\" cx=\"";
    write_svg_number(out, pin.anchor().x());
    out << "\" cy=\"";
    write_svg_number(out, pin.anchor().y());
    out << "\" r=\"1.5\"/>\n";
    out << "      <text class=\"pin-label\" x=\"";
    write_svg_number(out, pin.anchor().x());
    out << "\" y=\"";
    write_svg_number(out, pin.anchor().y() + 4.0);
    out << "\">" << svg_escape(pin.name()) << "</text>\n";
}

inline void write_symbol_instance_svg(std::ostream &out, const Schematic &schematic,
                                      SymbolInstanceId id) {
    const auto &instance = schematic.symbol_instance(id);
    const auto &symbol = schematic.symbol_definition(instance.symbol_definition());
    const auto &component = schematic.circuit().component(instance.component());

    out << "    <g class=\"symbol-instance\" data-component=\""
        << svg_escape(svg_component_id(instance.component())) << "\" data-symbol-definition=\""
        << svg_escape(svg_symbol_def_id(instance.symbol_definition()))
        << "\" transform=\"translate(";
    write_svg_number(out, instance.position().x());
    out << ' ';
    write_svg_number(out, instance.position().y());
    out << ") rotate(";
    write_svg_number(out, orientation_degrees(instance.orientation()));
    out << ")\">\n";
    out << "      <text class=\"reference\" x=\"0\" y=\"-12\"";
    write_upright_text_transform(out, instance.orientation(), Point{0.0, -12.0});
    out << ">" << svg_escape(display_scoped_label(component.reference().value())) << "</text>\n";
    for (const auto &primitive : symbol.primitives()) {
        write_symbol_primitive_svg(out, primitive);
    }
    out << "    </g>\n";
}

inline void write_symbol_debug_overlay_svg(std::ostream &out, const Schematic &schematic,
                                           SymbolInstanceId id) {
    const auto &instance = schematic.symbol_instance(id);
    const auto &symbol = schematic.symbol_definition(instance.symbol_definition());

    out << "      <g class=\"symbol-debug-overlay\" data-component=\""
        << svg_escape(svg_component_id(instance.component())) << "\" data-symbol-definition=\""
        << svg_escape(svg_symbol_def_id(instance.symbol_definition()))
        << "\" transform=\"translate(";
    write_svg_number(out, instance.position().x());
    out << ' ';
    write_svg_number(out, instance.position().y());
    out << ") rotate(";
    write_svg_number(out, orientation_degrees(instance.orientation()));
    out << ")\">\n";
    for (const auto &pin : symbol.pins()) {
        write_symbol_pin_svg(out, pin);
    }
    out << "      </g>\n";
}

inline void write_wire_run_svg(std::ostream &out, const Schematic &schematic, WireRunId id) {
    const auto &wire = schematic.wire_run(id);

    out << "    <polyline class=\"wire-run\" data-net=\"" << svg_escape(svg_net_id(wire.net()))
        << "\" points=\"";
    for (std::size_t index = 0; index < wire.points().size(); ++index) {
        if (index != 0) {
            out << ' ';
        }
        write_svg_number(out, wire.points()[index].x());
        out << ',';
        write_svg_number(out, wire.points()[index].y());
    }
    out << "\"/>\n";
}

inline void write_net_label_svg(std::ostream &out, const Schematic &schematic, NetLabelId id) {
    const auto &label = schematic.net_label(id);
    const auto &net = schematic.circuit().net(label.net());

    out << "    <text class=\"net-label\" data-net=\"" << svg_escape(svg_net_id(label.net()))
        << "\" x=\"";
    write_svg_number(out, label.position().x());
    out << "\" y=\"";
    write_svg_number(out, label.position().y());
    out << "\" transform=\"rotate(";
    write_svg_number(out, orientation_degrees(label.orientation()));
    out << ' ';
    write_svg_number(out, label.position().x());
    out << ' ';
    write_svg_number(out, label.position().y());
    out << ")\">" << svg_escape(display_scoped_label(net.name().value())) << "</text>\n";
}

inline void write_junction_svg(std::ostream &out, const Schematic &schematic, JunctionId id) {
    const auto &junction = schematic.junction(id);

    out << "    <circle class=\"junction\" data-net=\"" << svg_escape(svg_net_id(junction.net()))
        << "\" cx=\"";
    write_svg_number(out, junction.position().x());
    out << "\" cy=\"";
    write_svg_number(out, junction.position().y());
    out << "\" r=\"1.8\"/>\n";
}

inline void write_power_port_svg(std::ostream &out, const Schematic &schematic, PowerPortId id) {
    const auto &port = schematic.power_port(id);
    const auto &net = schematic.circuit().net(port.net());
    const auto kind_class = power_port_class(port.kind());

    out << "    <g class=\"power-port " << kind_class << "\" data-net=\""
        << svg_escape(svg_net_id(port.net())) << "\" transform=\"translate(";
    write_svg_number(out, port.position().x());
    out << ' ';
    write_svg_number(out, port.position().y());
    out << ") rotate(";
    write_svg_number(out, orientation_degrees(port.orientation()));
    out << ")\">\n";
    if (port.kind() == PowerPortKind::Ground) {
        out << "      <line class=\"power-port-line\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"4\"/>\n";
        out << "      <line class=\"ground-bar\" x1=\"-5\" y1=\"4\" x2=\"5\" y2=\"4\"/>\n";
        out << "      <line class=\"ground-bar\" x1=\"-3\" y1=\"6\" x2=\"3\" y2=\"6\"/>\n";
        out << "      <line class=\"ground-bar\" x1=\"-1\" y1=\"8\" x2=\"1\" y2=\"8\"/>\n";
    } else {
        out << "      <line class=\"power-port-line\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"-7\"/>\n";
        out << "      <path class=\"power-port-shape\" d=\"M -4 -7 L 0 -12 L 4 -7 Z\"/>\n";
    }
    out << "      <text class=\"power-port-label\" x=\"0\" y=\"-14\"";
    write_upright_text_transform(out, port.orientation(), Point{0.0, -14.0});
    out << ">" << svg_escape(display_scoped_label(net.name().value())) << "</text>\n";
    out << "    </g>\n";
}

inline void write_no_connect_marker_svg(std::ostream &out, const Schematic &schematic,
                                        NoConnectMarkerId id) {
    const auto &marker = schematic.no_connect_marker(id);

    out << "    <g class=\"no-connect-marker\" data-pin=\"" << svg_escape(svg_pin_id(marker.pin()))
        << "\" transform=\"translate(";
    write_svg_number(out, marker.position().x());
    out << ' ';
    write_svg_number(out, marker.position().y());
    out << ") rotate(";
    write_svg_number(out, orientation_degrees(marker.orientation()));
    out << ")\">\n";
    out << "      <line class=\"no-connect-line\" x1=\"-3\" y1=\"-3\" x2=\"3\" y2=\"3\"/>\n";
    out << "      <line class=\"no-connect-line\" x1=\"-3\" y1=\"3\" x2=\"3\" y2=\"-3\"/>\n";
    out << "    </g>\n";
}

inline void write_sheet_port_svg(std::ostream &out, const Schematic &schematic, SheetPortId id) {
    const auto &port = schematic.sheet_port(id);
    const auto kind_class = sheet_port_class(port.kind());

    out << "    <g class=\"sheet-port " << kind_class << "\" data-net=\""
        << svg_escape(svg_net_id(port.net())) << "\" transform=\"translate(";
    write_svg_number(out, port.position().x());
    out << ' ';
    write_svg_number(out, port.position().y());
    out << ") rotate(";
    write_svg_number(out, orientation_degrees(port.orientation()));
    out << ")\">\n";
    out << "      <path class=\"sheet-port-shape\" d=\"M 0 -4 L 14 -4 L 20 0 L 14 4 L 0 4 Z\"/>\n";
    out << "      <text class=\"sheet-port-label\" x=\"10\" y=\"2\"";
    write_upright_text_transform(out, port.orientation(), Point{10.0, 2.0});
    out << ">" << svg_escape(port.name()) << "</text>\n";
    out << "    </g>\n";
}

inline void write_symbol_field_svg(std::ostream &out, const Schematic &schematic,
                                   SymbolFieldId id) {
    const auto &field = schematic.symbol_field(id);

    out << "    <text class=\"symbol-field\" data-symbol-instance=\""
        << svg_escape(svg_symbol_instance_id(field.symbol_instance())) << "\" data-field=\""
        << svg_escape(field.name()) << "\" x=\"";
    write_svg_number(out, field.position().x());
    out << "\" y=\"";
    write_svg_number(out, field.position().y());
    out << "\" transform=\"rotate(";
    write_svg_number(out, orientation_degrees(field.orientation()));
    out << ' ';
    write_svg_number(out, field.position().x());
    out << ' ';
    write_svg_number(out, field.position().y());
    out << ")\">" << svg_escape(field.value()) << "</text>\n";
}

[[nodiscard]] inline bool region_uses_dashed_frame(const SheetRegion &region) {
    for (const auto &field : region.style()) {
        if (field.key() == "border" && field.value() == "dashed") {
            return true;
        }
    }
    return false;
}

inline void write_sheet_defs_svg(std::ostream &out, SheetId sheet_id, const Sheet &sheet) {
    const auto &metadata = sheet.metadata();
    const auto area = drawing_area(metadata);
    const auto sheet_token = svg_sheet_token(sheet_id);

    out << "    <defs>\n";
    const auto title_block = title_block_rect(metadata);
    out << "      <clipPath id=\"title-block-clip-" << sheet_token << "\">\n";
    out << "        <rect x=\"0\" y=\"0\" width=\"";
    write_svg_number(out, title_block.width);
    out << "\" height=\"";
    write_svg_number(out, title_block.height);
    out << "\"/>\n";
    out << "      </clipPath>\n";

    for (std::size_t index = 0; index < sheet.regions().size(); ++index) {
        const auto bounds = sheet.region(index).bounds();
        out << "      <clipPath id=\"region-title-clip-" << sheet_token << '-' << index << "\">\n";
        out << "        <rect x=\"";
        write_svg_number(out, bounds.x());
        out << "\" y=\"";
        write_svg_number(out, bounds.y());
        out << "\" width=\"";
        write_svg_number(out, bounds.width());
        out << "\" height=\"10\"/>\n";
        out << "      </clipPath>\n";
    }

    if (metadata.grid().has_value() && metadata.grid()->visible()) {
        out << "      <pattern id=\"grid-" << sheet_token << "\" x=\"";
        write_svg_number(out, area.x);
        out << "\" y=\"";
        write_svg_number(out, area.y);
        out << "\" width=\"";
        write_svg_number(out, metadata.grid()->spacing());
        out << "\" height=\"";
        write_svg_number(out, metadata.grid()->spacing());
        out << "\" patternUnits=\"userSpaceOnUse\">\n";
        out << "        <circle class=\"sheet-grid-dot\" cx=\"0\" cy=\"0\" r=\"0.25\"/>\n";
        out << "      </pattern>\n";
    }
    out << "    </defs>\n";
}

inline void write_sheet_grid_svg(std::ostream &out, SheetId sheet_id,
                                 const SheetMetadata &metadata) {
    if (!metadata.grid().has_value() || !metadata.grid()->visible()) {
        return;
    }

    const auto area = drawing_area(metadata);
    out << "    <rect class=\"sheet-grid\" x=\"";
    write_svg_number(out, area.x);
    out << "\" y=\"";
    write_svg_number(out, area.y);
    out << "\" width=\"";
    write_svg_number(out, area.width);
    out << "\" height=\"";
    write_svg_number(out, area.height);
    out << "\" fill=\"url(#grid-" << svg_sheet_token(sheet_id) << ")\"/>\n";
}

inline void write_coordinate_zones_svg(std::ostream &out, const SheetMetadata &metadata) {
    if (!metadata.coordinate_zones().has_value() || !metadata.coordinate_zones()->visible()) {
        return;
    }

    const auto area = drawing_area(metadata);
    if (area.width <= 0.0 || area.height <= 0.0) {
        return;
    }

    const auto &zones = metadata.coordinate_zones().value();
    const auto column_width = area.width / static_cast<double>(zones.columns());
    const auto row_height = area.height / static_cast<double>(zones.rows());

    out << "    <g class=\"coordinate-zones\">\n";
    for (std::size_t column = 0; column < zones.columns(); ++column) {
        const auto x = area.x + (column_width * (static_cast<double>(column) + 0.5));
        out << "      <text class=\"coordinate-zone-label column\" x=\"";
        write_svg_number(out, x);
        out << "\" y=\"";
        write_svg_number(out, metadata.frame().margins().top() / 2.0);
        out << "\">" << column + 1U << "</text>\n";
        out << "      <text class=\"coordinate-zone-label column\" x=\"";
        write_svg_number(out, x);
        out << "\" y=\"";
        write_svg_number(out,
                         metadata.size().height() - (metadata.frame().margins().bottom() / 2.0));
        out << "\">" << column + 1U << "</text>\n";
    }
    for (std::size_t row = 0; row < zones.rows(); ++row) {
        const auto y = area.y + (row_height * (static_cast<double>(row) + 0.5));
        const auto label = zone_row_label(row);
        out << "      <text class=\"coordinate-zone-label row\" x=\"";
        write_svg_number(out, metadata.frame().margins().left() / 2.0);
        out << "\" y=\"";
        write_svg_number(out, y);
        out << "\">" << label << "</text>\n";
        out << "      <text class=\"coordinate-zone-label row\" x=\"";
        write_svg_number(out, metadata.size().width() - (metadata.frame().margins().right() / 2.0));
        out << "\" y=\"";
        write_svg_number(out, y);
        out << "\">" << label << "</text>\n";
    }
    out << "    </g>\n";
}

inline void write_title_block_row_svg(std::ostream &out, std::size_t row, std::string_view key,
                                      std::string_view value) {
    const auto text_y = (static_cast<double>(row) * title_block_row_height) + 4.2;
    out << "      <text class=\"title-block-label\" x=\"2\" y=\"";
    write_svg_number(out, text_y);
    out << "\">" << svg_escape(key) << "</text>\n";
    out << "      <text class=\"title-block-value";
    if (row == 0U) {
        out << " sheet-title";
    }
    out << "\" x=\"";
    write_svg_number(out, title_block_label_width + 2.0);
    out << "\" y=\"";
    write_svg_number(out, text_y);
    out << "\">" << svg_escape(value) << "</text>\n";
}

inline void write_title_block_svg(std::ostream &out, SheetId sheet_id,
                                  const SheetMetadata &metadata) {
    const auto rect = title_block_rect(metadata);
    out << "    <g class=\"title-block\" transform=\"translate(";
    write_svg_number(out, rect.x);
    out << ' ';
    write_svg_number(out, rect.y);
    out << ")\" clip-path=\"url(#title-block-clip-" << svg_sheet_token(sheet_id) << ")\">\n";
    out << "      <rect class=\"title-block-outline\" x=\"0\" y=\"0\" width=\"";
    write_svg_number(out, rect.width);
    out << "\" height=\"";
    write_svg_number(out, rect.height);
    out << "\"/>\n";
    out << "      <line class=\"title-block-rule\" x1=\"";
    write_svg_number(out, title_block_label_width);
    out << "\" y1=\"0\" x2=\"";
    write_svg_number(out, title_block_label_width);
    out << "\" y2=\"";
    write_svg_number(out, rect.height);
    out << "\"/>\n";
    for (std::size_t row = 1; row < 1U + metadata.title_block().size(); ++row) {
        const auto y = title_block_row_height * static_cast<double>(row);
        out << "      <line class=\"title-block-rule\" x1=\"0\" y1=\"";
        write_svg_number(out, y);
        out << "\" x2=\"";
        write_svg_number(out, rect.width);
        out << "\" y2=\"";
        write_svg_number(out, y);
        out << "\"/>\n";
    }

    write_title_block_row_svg(out, 0U, "Title", metadata.title());
    for (std::size_t index = 0; index < metadata.title_block().size(); ++index) {
        const auto &field = metadata.title_block()[index];
        write_title_block_row_svg(out, index + 1U, field.key(), field.value());
    }
    out << "    </g>\n";
}

inline void write_regions_svg(std::ostream &out, SheetId sheet_id, const Sheet &sheet) {
    const auto sheet_token = svg_sheet_token(sheet_id);
    for (std::size_t index = 0; index < sheet.regions().size(); ++index) {
        const auto &region = sheet.region(index);
        const auto bounds = region.bounds();
        out << "    <g class=\"sheet-region\" data-region=\"" << svg_escape(region.name())
            << "\">\n";
        out << "      <rect class=\"sheet-region-frame";
        if (region_uses_dashed_frame(region)) {
            out << " dashed";
        }
        out << "\" data-region=\"" << svg_escape(region.name()) << "\" x=\"";
        write_svg_number(out, bounds.x());
        out << "\" y=\"";
        write_svg_number(out, bounds.y());
        out << "\" width=\"";
        write_svg_number(out, bounds.width());
        out << "\" height=\"";
        write_svg_number(out, bounds.height());
        out << "\"/>\n";
        out << "      <text class=\"sheet-region-title\" x=\"";
        write_svg_number(out, bounds.x() + 3.0);
        out << "\" y=\"";
        write_svg_number(out, bounds.y() + 6.0);
        out << "\" clip-path=\"url(#region-title-clip-" << sheet_token << '-' << index << ")\">"
            << svg_escape(region.title()) << "</text>\n";
        out << "    </g>\n";
    }
}

inline void write_svg_style(std::ostream &out, SchematicSvgOptions options) {
    out << "  <style>"
           ".document-background,.sheet{fill:#fff;stroke:none}"
           ".sheet-border{fill:none;stroke:#111;stroke-width:0.6}"
           ".drawing-frame{fill:none;stroke:#111;stroke-width:0.45}"
           ".sheet-grid{stroke:none}"
           ".sheet-grid-dot{fill:#d7d7d7;stroke:none}"
           ".coordinate-zone-label{font:3.5px sans-serif;fill:#444;text-anchor:middle;"
           "dominant-baseline:middle}"
           ".sheet-title{font-weight:600}"
           ".title-block-outline,.title-block-rule{fill:none;stroke:#111;stroke-width:0.35}"
           ".title-block-label{font:2.8px sans-serif;fill:#444;text-anchor:start}"
           ".title-block-value{font:2.8px sans-serif;fill:#111;text-anchor:start}"
           ".sheet-region-frame{fill:#fff;fill-opacity:0;stroke:#78716c;stroke-width:0.45}"
           ".sheet-region-frame.dashed{stroke-dasharray:3 2}"
           ".sheet-region-title{font:4px sans-serif;fill:#57534e;text-anchor:start;"
           "font-weight:600}"
           ".wire-run{fill:none;stroke:#111;stroke-width:1}"
           ".net-label{font:2.8px sans-serif;fill:#111;text-anchor:start}"
           ".junction{fill:#111;stroke:none}"
           ".power-port-line,.ground-bar,.no-connect-line{stroke:#111;stroke-width:1}"
           ".power-port-shape,.sheet-port-shape{fill:#fff;stroke:#111;stroke-width:1}"
           ".power-port-label,.sheet-port-label,.symbol-field{font:2.8px sans-serif;fill:#111;"
           "text-anchor:middle}"
           ".symbol-line,.symbol-rectangle,.symbol-circle,.symbol-arc{fill:none;stroke:#111;stroke-"
           "width:1}"
           ".symbol-text,.reference{font:2.8px sans-serif;fill:#111;text-anchor:middle}";
    if (options.debug_overlays) {
        out << ".pin-anchor{fill:#fff;stroke:#c2410c;stroke-width:0.8}"
               ".pin-label{font:4px sans-serif;fill:#c2410c;text-anchor:middle}";
    }
    out << "</style>\n";
}

inline void write_sheet_svg(std::ostream &out, const Schematic &schematic, SheetId sheet_id,
                            double y_offset, SchematicSvgOptions options) {
    const auto &sheet = schematic.sheet(sheet_id);
    const auto &metadata = sheet.metadata();
    const auto area = drawing_area(metadata);

    out << "  <g class=\"schematic-sheet\" data-sheet=\"" << svg_escape(svg_sheet_id(sheet_id))
        << "\" transform=\"translate(0 ";
    write_svg_number(out, y_offset);
    out << ")\">\n";
    write_sheet_defs_svg(out, sheet_id, sheet);
    out << "    <rect class=\"sheet\" x=\"0\" y=\"0\" width=\"";
    write_svg_number(out, metadata.size().width());
    out << "\" height=\"";
    write_svg_number(out, metadata.size().height());
    out << "\"/>\n";
    write_sheet_grid_svg(out, sheet_id, metadata);
    if (metadata.frame().visible()) {
        out << "    <rect class=\"sheet-border\" x=\"0\" y=\"0\" width=\"";
        write_svg_number(out, metadata.size().width());
        out << "\" height=\"";
        write_svg_number(out, metadata.size().height());
        out << "\"/>\n";
        out << "    <rect class=\"drawing-frame\" x=\"";
        write_svg_number(out, area.x);
        out << "\" y=\"";
        write_svg_number(out, area.y);
        out << "\" width=\"";
        write_svg_number(out, area.width);
        out << "\" height=\"";
        write_svg_number(out, area.height);
        out << "\"/>\n";
    }
    write_coordinate_zones_svg(out, metadata);
    write_title_block_svg(out, sheet_id, metadata);
    out << "    <g class=\"layer layer-regions\">\n";
    write_regions_svg(out, sheet_id, sheet);
    out << "    </g>\n";
    out << "    <g class=\"layer layer-symbols\">\n";
    for (const auto instance : sheet.symbol_instances()) {
        write_symbol_instance_svg(out, schematic, instance);
    }
    out << "    </g>\n";
    out << "    <g class=\"layer layer-wires\">\n";
    for (const auto wire : sheet.wire_runs()) {
        write_wire_run_svg(out, schematic, wire);
    }
    out << "    </g>\n";
    out << "    <g class=\"layer layer-junctions\">\n";
    for (const auto junction : sheet.junctions()) {
        write_junction_svg(out, schematic, junction);
    }
    out << "    </g>\n";
    out << "    <g class=\"layer layer-ports\">\n";
    for (const auto port : sheet.power_ports()) {
        write_power_port_svg(out, schematic, port);
    }
    for (const auto marker : sheet.no_connect_markers()) {
        write_no_connect_marker_svg(out, schematic, marker);
    }
    for (const auto port : sheet.sheet_ports()) {
        write_sheet_port_svg(out, schematic, port);
    }
    out << "    </g>\n";
    out << "    <g class=\"layer layer-labels\">\n";
    for (const auto label : sheet.net_labels()) {
        write_net_label_svg(out, schematic, label);
    }
    out << "    </g>\n";
    out << "    <g class=\"layer layer-fields\">\n";
    for (const auto field : sheet.symbol_fields()) {
        write_symbol_field_svg(out, schematic, field);
    }
    out << "    </g>\n";
    if (options.debug_overlays) {
        out << "    <g class=\"layer layer-debug\">\n";
        for (const auto instance : sheet.symbol_instances()) {
            write_symbol_debug_overlay_svg(out, schematic, instance);
        }
        out << "    </g>\n";
    }
    out << "  </g>\n";
}

} // namespace detail

/** Write a deterministic SVG rendering of a schematic projection. */
inline void write_schematic_svg(std::ostream &out, const Schematic &schematic,
                                SchematicSvgOptions options = {}) {
    const auto sheet_count = schematic.sheet_count();
    auto width = detail::svg_sheet_width;
    auto height = sheet_count == 0 ? detail::svg_sheet_height : 0.0;
    for (std::size_t sheet_index = 0; sheet_index < sheet_count; ++sheet_index) {
        const auto &metadata = schematic.sheet(SheetId{sheet_index}).metadata();
        width = std::max(width, metadata.size().width());
        if (sheet_index != 0) {
            height += detail::svg_sheet_gap;
        }
        height += metadata.size().height();
    }

    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 ";
    detail::write_svg_number(out, width);
    out << ' ';
    detail::write_svg_number(out, height);
    out << "\" width=\"";
    detail::write_svg_number(out, width);
    out << "\" height=\"";
    detail::write_svg_number(out, height);
    out << "\">\n";
    detail::write_svg_style(out, options);
    out << "  <rect class=\"document-background\" x=\"0\" y=\"0\" width=\"";
    detail::write_svg_number(out, width);
    out << "\" height=\"";
    detail::write_svg_number(out, height);
    out << "\"/>\n";

    auto y_offset = 0.0;
    for (std::size_t sheet_index = 0; sheet_index < sheet_count; ++sheet_index) {
        const auto sheet_id = SheetId{sheet_index};
        detail::write_sheet_svg(out, schematic, sheet_id, y_offset, options);
        y_offset += schematic.sheet(sheet_id).metadata().size().height() + detail::svg_sheet_gap;
    }

    out << "</svg>\n";
}

/** Return a deterministic SVG rendering of a schematic projection. */
[[nodiscard]] inline std::string write_schematic_svg(const Schematic &schematic,
                                                     SchematicSvgOptions options = {}) {
    auto out = std::ostringstream{};
    write_schematic_svg(out, schematic, options);
    return out.str();
}

/** Write one deterministic SVG page for a single schematic sheet. */
inline void write_schematic_sheet_svg(std::ostream &out, const Schematic &schematic,
                                      SheetId sheet_id, SchematicSvgOptions options = {}) {
    const auto &metadata = schematic.sheet(sheet_id).metadata();
    const auto width = metadata.size().width();
    const auto height = metadata.size().height();

    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 ";
    detail::write_svg_number(out, width);
    out << ' ';
    detail::write_svg_number(out, height);
    out << "\" width=\"";
    detail::write_svg_number(out, width);
    out << "\" height=\"";
    detail::write_svg_number(out, height);
    out << "\">\n";
    detail::write_svg_style(out, options);
    out << "  <rect class=\"document-background\" x=\"0\" y=\"0\" width=\"";
    detail::write_svg_number(out, width);
    out << "\" height=\"";
    detail::write_svg_number(out, height);
    out << "\"/>\n";
    detail::write_sheet_svg(out, schematic, sheet_id, 0.0, options);
    out << "</svg>\n";
}

/** Return one deterministic SVG page for a single schematic sheet. */
[[nodiscard]] inline std::string write_schematic_sheet_svg(const Schematic &schematic,
                                                           SheetId sheet_id,
                                                           SchematicSvgOptions options = {}) {
    auto out = std::ostringstream{};
    write_schematic_sheet_svg(out, schematic, sheet_id, options);
    return out.str();
}

/** Return separate SVG pages for production-oriented multi-sheet export. */
[[nodiscard]] inline std::vector<SchematicSvgPage>
write_schematic_svg_pages(const Schematic &schematic, SchematicSvgOptions options = {}) {
    auto pages = std::vector<SchematicSvgPage>{};
    pages.reserve(schematic.sheet_count());
    for (std::size_t sheet_index = 0; sheet_index < schematic.sheet_count(); ++sheet_index) {
        const auto sheet_id = SheetId{sheet_index};
        const auto &sheet = schematic.sheet(sheet_id);
        pages.push_back(SchematicSvgPage{
            sheet_id,
            sheet.name(),
            write_schematic_sheet_svg(schematic, sheet_id, options),
        });
    }
    return pages;
}

} // namespace volt::io
