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

#include <volt/core/ids.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/symbols.hpp>

namespace volt::io {

/** Options that control deterministic schematic SVG rendering. */
struct SchematicSvgOptions {
    /** Include development overlays such as symbol pin anchors and labels. */
    bool debug_overlays = false;
};

namespace detail {

inline constexpr double svg_sheet_width = 297.0;
inline constexpr double svg_sheet_height = 210.0;
inline constexpr double svg_sheet_gap = 20.0;
inline constexpr double svg_pi = 3.14159265358979323846;

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
    out << "      <text class=\"reference\" x=\"0\" y=\"-12\">"
        << svg_escape(component.reference().value()) << "</text>\n";
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
    out << ")\">" << svg_escape(net.name().value()) << "</text>\n";
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
    out << "      <text class=\"power-port-label\" x=\"0\" y=\"-14\">"
        << svg_escape(net.name().value()) << "</text>\n";
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
    out << "      <text class=\"sheet-port-label\" x=\"4\" y=\"2\">" << svg_escape(port.name())
        << "</text>\n";
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

inline void write_title_block_svg(std::ostream &out, const SheetMetadata &metadata) {
    auto y = metadata.size().height() - 8.0;
    for (const auto &field : metadata.title_block()) {
        out << "    <text class=\"title-block-field\" x=\"";
        write_svg_number(out, metadata.size().width() - 72.0);
        out << "\" y=\"";
        write_svg_number(out, y);
        out << "\">" << svg_escape(field.key()) << ": " << svg_escape(field.value()) << "</text>\n";
        y -= 6.0;
    }
}

inline void write_svg_style(std::ostream &out, SchematicSvgOptions options) {
    out << "  <style>"
           ".sheet{fill:#fff;stroke:#111;stroke-width:0.5}"
           ".sheet-title{font:6px sans-serif;fill:#111}"
           ".title-block-field{font:4px sans-serif;fill:#111;text-anchor:start}"
           ".wire-run{fill:none;stroke:#111;stroke-width:1}"
           ".net-label{font:5px sans-serif;fill:#111;text-anchor:start}"
           ".junction{fill:#111;stroke:none}"
           ".power-port-line,.ground-bar,.no-connect-line{stroke:#111;stroke-width:1}"
           ".power-port-shape,.sheet-port-shape{fill:#fff;stroke:#111;stroke-width:1}"
           ".power-port-label,.sheet-port-label,.symbol-field{font:5px sans-serif;fill:#111;"
           "text-anchor:middle}"
           ".symbol-line,.symbol-rectangle,.symbol-circle,.symbol-arc{fill:none;stroke:#111;stroke-"
           "width:1}"
           ".symbol-text,.reference{font:5px sans-serif;fill:#111;text-anchor:middle}";
    if (options.debug_overlays) {
        out << ".pin-anchor{fill:#fff;stroke:#c2410c;stroke-width:0.8}"
               ".pin-label{font:4px sans-serif;fill:#c2410c;text-anchor:middle}";
    }
    out << "</style>\n";
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

    auto y_offset = 0.0;
    for (std::size_t sheet_index = 0; sheet_index < sheet_count; ++sheet_index) {
        const auto sheet_id = SheetId{sheet_index};
        const auto &sheet = schematic.sheet(sheet_id);
        const auto &metadata = sheet.metadata();

        out << "  <g class=\"schematic-sheet\" data-sheet=\""
            << detail::svg_escape(detail::svg_sheet_id(sheet_id)) << "\" transform=\"translate(0 ";
        detail::write_svg_number(out, y_offset);
        out << ")\">\n";
        out << "    <rect class=\"sheet\" x=\"0\" y=\"0\" width=\"";
        detail::write_svg_number(out, metadata.size().width());
        out << "\" height=\"";
        detail::write_svg_number(out, metadata.size().height());
        out << "\"/>\n";
        out << "    <text class=\"sheet-title\" x=\"10\" y=\"16\">"
            << detail::svg_escape(metadata.title()) << "</text>\n";
        detail::write_title_block_svg(out, metadata);
        out << "    <g class=\"layer layer-symbols\">\n";
        for (const auto instance : sheet.symbol_instances()) {
            detail::write_symbol_instance_svg(out, schematic, instance);
        }
        out << "    </g>\n";
        out << "    <g class=\"layer layer-wires\">\n";
        for (const auto wire : sheet.wire_runs()) {
            detail::write_wire_run_svg(out, schematic, wire);
        }
        out << "    </g>\n";
        out << "    <g class=\"layer layer-junctions\">\n";
        for (const auto junction : sheet.junctions()) {
            detail::write_junction_svg(out, schematic, junction);
        }
        out << "    </g>\n";
        out << "    <g class=\"layer layer-ports\">\n";
        for (const auto port : sheet.power_ports()) {
            detail::write_power_port_svg(out, schematic, port);
        }
        for (const auto marker : sheet.no_connect_markers()) {
            detail::write_no_connect_marker_svg(out, schematic, marker);
        }
        for (const auto port : sheet.sheet_ports()) {
            detail::write_sheet_port_svg(out, schematic, port);
        }
        out << "    </g>\n";
        out << "    <g class=\"layer layer-labels\">\n";
        for (const auto label : sheet.net_labels()) {
            detail::write_net_label_svg(out, schematic, label);
        }
        out << "    </g>\n";
        out << "    <g class=\"layer layer-fields\">\n";
        for (const auto field : sheet.symbol_fields()) {
            detail::write_symbol_field_svg(out, schematic, field);
        }
        out << "    </g>\n";
        if (options.debug_overlays) {
            out << "    <g class=\"layer layer-debug\">\n";
            for (const auto instance : sheet.symbol_instances()) {
                detail::write_symbol_debug_overlay_svg(out, schematic, instance);
            }
            out << "    </g>\n";
        }
        out << "  </g>\n";
        y_offset += metadata.size().height() + detail::svg_sheet_gap;
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

} // namespace volt::io
