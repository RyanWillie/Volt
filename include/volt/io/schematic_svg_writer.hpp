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

[[nodiscard]] inline std::string svg_sheet_id(SheetId id) {
    return "sheet:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string svg_symbol_def_id(SymbolDefId id) {
    return "symbol_def:" + std::to_string(id.index());
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
    for (const auto &pin : symbol.pins()) {
        write_symbol_pin_svg(out, pin);
    }
    out << "    </g>\n";
}

} // namespace detail

/** Write a deterministic SVG rendering of a schematic projection. */
inline void write_schematic_svg(std::ostream &out, const Schematic &schematic) {
    const auto sheet_count = schematic.sheet_count();
    const auto rendered_sheet_count = sheet_count == 0 ? 1U : sheet_count;
    const auto height = (static_cast<double>(rendered_sheet_count) * detail::svg_sheet_height) +
                        (static_cast<double>(rendered_sheet_count - 1U) * detail::svg_sheet_gap);

    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 ";
    detail::write_svg_number(out, detail::svg_sheet_width);
    out << ' ';
    detail::write_svg_number(out, height);
    out << "\" width=\"";
    detail::write_svg_number(out, detail::svg_sheet_width);
    out << "\" height=\"";
    detail::write_svg_number(out, height);
    out << "\">\n";
    out << "  <style>"
           ".sheet{fill:#fff;stroke:#c7c7c7;stroke-width:0.5}"
           ".sheet-title{font:6px sans-serif;fill:#333}"
           ".symbol-line,.symbol-rectangle,.symbol-circle,.symbol-arc{fill:none;stroke:#111;stroke-"
           "width:1.2}"
           ".symbol-text,.reference,.pin-label{font:5px sans-serif;fill:#111;text-anchor:middle}"
           ".pin-anchor{fill:#fff;stroke:#111;stroke-width:0.8}"
           "</style>\n";

    for (std::size_t sheet_index = 0; sheet_index < sheet_count; ++sheet_index) {
        const auto sheet_id = SheetId{sheet_index};
        const auto &sheet = schematic.sheet(sheet_id);
        const auto y_offset =
            static_cast<double>(sheet_index) * (detail::svg_sheet_height + detail::svg_sheet_gap);

        out << "  <g class=\"schematic-sheet\" data-sheet=\""
            << detail::svg_escape(detail::svg_sheet_id(sheet_id)) << "\" transform=\"translate(0 ";
        detail::write_svg_number(out, y_offset);
        out << ")\">\n";
        out << "    <rect class=\"sheet\" x=\"0\" y=\"0\" width=\"";
        detail::write_svg_number(out, detail::svg_sheet_width);
        out << "\" height=\"";
        detail::write_svg_number(out, detail::svg_sheet_height);
        out << "\"/>\n";
        out << "    <text class=\"sheet-title\" x=\"10\" y=\"16\">"
            << detail::svg_escape(sheet.name()) << "</text>\n";
        for (const auto instance : sheet.symbol_instances()) {
            detail::write_symbol_instance_svg(out, schematic, instance);
        }
        out << "  </g>\n";
    }

    out << "</svg>\n";
}

/** Return a deterministic SVG rendering of a schematic projection. */
[[nodiscard]] inline std::string write_schematic_svg(const Schematic &schematic) {
    auto out = std::ostringstream{};
    write_schematic_svg(out, schematic);
    return out.str();
}

} // namespace volt::io
