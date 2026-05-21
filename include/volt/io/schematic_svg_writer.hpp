#pragma once

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
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

/** Options that control content-tight schematic body SVG rendering. */
struct SchematicSvgBodyOptions {
    /** Shared SVG rendering options for schematic drawing content. */
    SchematicSvgOptions svg;
    /** Sheet-space margin added around the computed content bounds. */
    double margin = 4.0;
    /** Include authored sheet region frames in the body output and bounds. */
    bool include_regions = false;
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
inline constexpr double title_block_label_x = 2.0;
inline constexpr double title_block_value_x = title_block_label_width + 2.0;
inline constexpr double title_block_right_padding = 2.0;
inline constexpr double title_block_text_width_factor = 0.64;
inline constexpr double title_block_min_compression_scale = 0.82;

/** Production schematic rendering scale: page chrome stays quiet, circuit marks stay primary.
 *
 * Values are tuned from dense schematic visual inspection so page chrome is lighter than circuit
 * content, tags are lighter than wires and symbols, and labels stay readable without becoming the
 * dominant marks on the sheet.
 */
struct SchematicSvgVisualScale {
    /** Outer sheet border stroke width. */
    double sheet_border_stroke_width = 0.45;
    /** Inner drawing-frame stroke width. */
    double drawing_frame_stroke_width = 0.35;
    /** Title-block outline and rule stroke width. */
    double title_block_stroke_width = 0.3;
    /** Functional-region frame stroke width. */
    double region_frame_stroke_width = 0.35;
    /** Logical wire stroke width. */
    double wire_stroke_width = 0.6;
    /** Symbol primitive stroke width. */
    double symbol_stroke_width = 0.6;
    /** Sheet, power, and ground tag stroke width. */
    double tag_port_stroke_width = 0.5;
    /** No-connect marker stroke width. */
    double no_connect_stroke_width = 0.55;
    /** Junction dot radius. */
    double junction_radius = 0.85;
    /** Coordinate-zone label font size. */
    double coordinate_zone_font_size = 3.0;
    /** Title-block font size. */
    double title_block_font_size = 2.5;
    /** Functional-region title font size. */
    double region_title_font_size = 3.2;
    /** Local net label font size. */
    double net_label_font_size = 2.8;
    /** Sheet, power, and ground tag label font size. */
    double tag_port_label_font_size = 2.45;
    /** Symbol primitive text font size. */
    double symbol_text_font_size = 3.0;
    /** Symbol field/value font size. */
    double symbol_field_font_size = 2.5;
    /** Debug pin anchor radius. */
    double pin_anchor_radius = 1.2;
    /** Debug pin label font size. */
    double pin_label_font_size = 3.0;
    /** Debug pin overlay stroke width. */
    double pin_overlay_stroke_width = 0.7;
};

inline constexpr SchematicSvgVisualScale schematic_svg_visual_scale{};
inline constexpr double power_port_stem_length = 4.2;
inline constexpr double power_port_tip_offset = 7.6;
inline constexpr double power_port_half_width = 3.0;
inline constexpr double power_port_label_offset = 9.4;
inline constexpr double ground_port_stem_length = 3.0;
inline constexpr double ground_port_label_offset = 8.2;
inline constexpr double sheet_port_half_height = 2.4;
inline constexpr double sheet_port_min_body_length = 7.0;
inline constexpr double sheet_port_tip_length = 3.2;
inline constexpr double sheet_port_label_padding = 2.1;
inline constexpr double debug_pin_label_offset = 4.0;
/** Deterministic average sans-serif character width used instead of browser font metrics. */
inline constexpr double sheet_port_text_width_factor = 0.56;
inline constexpr double sheet_port_label_baseline = 0.9;

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

/** Sheet-local bounds used by content-tight SVG body export. */
struct SvgBounds {
    /** Minimum sheet-space x coordinate. */
    double min_x;
    /** Minimum sheet-space y coordinate. */
    double min_y;
    /** Maximum sheet-space x coordinate. */
    double max_x;
    /** Maximum sheet-space y coordinate. */
    double max_y;
};

/** Deterministic title-block text fit result. */
struct SvgTitleBlockTextFit {
    /** Text emitted visibly in the SVG. */
    std::string text;
    /** Whether the original value needed deterministic fitting. */
    bool fitted;
    /** Whether SVG textLength compression is used instead of abbreviation. */
    bool compressed;
    /** Width used for SVG textLength when compressed. */
    double text_length;
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

[[nodiscard]] inline double power_port_glyph_degrees(PowerPortKind kind,
                                                     SchematicOrientation orientation) {
    const auto degrees = orientation_degrees(orientation);
    switch (kind) {
    case PowerPortKind::Power:
        return std::fmod(degrees + 90.0, 360.0);
    case PowerPortKind::Ground:
        return std::fmod(degrees + 270.0, 360.0);
    }
    throw std::logic_error{"Unhandled power port kind"};
}

inline void write_upright_text_transform_degrees(std::ostream &out, double parent_degrees,
                                                 Point anchor) {
    const auto degrees = -parent_degrees;
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

inline void write_upright_text_transform(std::ostream &out, SchematicOrientation parent_orientation,
                                         Point anchor) {
    write_upright_text_transform_degrees(out, orientation_degrees(parent_orientation), anchor);
}

inline void write_css_stroke_width(std::ostream &out, double width) {
    out << "stroke-width:";
    write_svg_number(out, width);
}

inline void write_css_font(std::ostream &out, double size) {
    out << "font:";
    write_svg_number(out, size);
    out << "px sans-serif";
}

[[nodiscard]] inline std::string_view svg_text_anchor(TextHorizontalAlignment alignment) {
    switch (alignment) {
    case TextHorizontalAlignment::Start:
        return "start";
    case TextHorizontalAlignment::Middle:
        return "middle";
    case TextHorizontalAlignment::End:
        return "end";
    }
    throw std::logic_error{"Unhandled text horizontal alignment"};
}

[[nodiscard]] inline std::string_view svg_dominant_baseline(TextVerticalAlignment alignment) {
    switch (alignment) {
    case TextVerticalAlignment::Top:
        return "text-before-edge";
    case TextVerticalAlignment::Middle:
        return "middle";
    case TextVerticalAlignment::Bottom:
        return "text-after-edge";
    case TextVerticalAlignment::Baseline:
        return "alphabetic";
    }
    throw std::logic_error{"Unhandled text vertical alignment"};
}

inline void write_text_presentation_attributes(std::ostream &out, SchematicTextStyle style) {
    out << " text-anchor=\"" << svg_text_anchor(style.horizontal_alignment())
        << "\" dominant-baseline=\"" << svg_dominant_baseline(style.vertical_alignment()) << '"';
    if (style.font_size().has_value()) {
        out << " style=\"font-size:";
        write_svg_number(out, style.font_size().value());
        out << "px\"";
    }
}

[[nodiscard]] inline double title_block_rendered_text_width(std::string_view text,
                                                            double font_size) noexcept {
    return font_size * title_block_text_width_factor * static_cast<double>(text.size());
}

[[nodiscard]] inline SvgBounds bounds_from_point(Point point) noexcept {
    return SvgBounds{point.x(), point.y(), point.x(), point.y()};
}

inline void include_point(SvgBounds &bounds, Point point) noexcept {
    bounds.min_x = std::min(bounds.min_x, point.x());
    bounds.min_y = std::min(bounds.min_y, point.y());
    bounds.max_x = std::max(bounds.max_x, point.x());
    bounds.max_y = std::max(bounds.max_y, point.y());
}

inline void include_bounds(SvgBounds &bounds, SvgBounds other) noexcept {
    bounds.min_x = std::min(bounds.min_x, other.min_x);
    bounds.min_y = std::min(bounds.min_y, other.min_y);
    bounds.max_x = std::max(bounds.max_x, other.max_x);
    bounds.max_y = std::max(bounds.max_y, other.max_y);
}

[[nodiscard]] inline SvgBounds padded_bounds(SvgBounds bounds, double padding) noexcept {
    return SvgBounds{bounds.min_x - padding, bounds.min_y - padding, bounds.max_x + padding,
                     bounds.max_y + padding};
}

[[nodiscard]] inline SvgBounds rect_bounds(double x, double y, double width,
                                           double height) noexcept {
    return SvgBounds{x, y, x + width, y + height};
}

[[nodiscard]] inline double bounds_width(SvgBounds bounds) noexcept {
    return bounds.max_x - bounds.min_x;
}

[[nodiscard]] inline double bounds_height(SvgBounds bounds) noexcept {
    return bounds.max_y - bounds.min_y;
}

[[nodiscard]] inline SvgBounds transform_rect_bounds(double min_x, double min_y, double max_x,
                                                     double max_y, Point origin,
                                                     SchematicOrientation orientation) {
    auto result =
        bounds_from_point(transform_schematic_point(Point{min_x, min_y}, origin, orientation));
    include_point(result, transform_schematic_point(Point{max_x, min_y}, origin, orientation));
    include_point(result, transform_schematic_point(Point{min_x, max_y}, origin, orientation));
    include_point(result, transform_schematic_point(Point{max_x, max_y}, origin, orientation));
    return result;
}

[[nodiscard]] inline double rendered_text_width(std::string_view text, double font_size) noexcept {
    return std::max(font_size * sheet_port_text_width_factor,
                    font_size * sheet_port_text_width_factor * static_cast<double>(text.size()));
}

[[nodiscard]] inline double rendered_text_height(double font_size) noexcept {
    return font_size * 1.25;
}

[[nodiscard]] inline int orientation_quarter_turns(SchematicOrientation orientation) {
    switch (orientation) {
    case SchematicOrientation::Right:
        return 0;
    case SchematicOrientation::Down:
        return 1;
    case SchematicOrientation::Left:
        return 2;
    case SchematicOrientation::Up:
        return 3;
    }
    throw std::logic_error{"Unhandled schematic orientation"};
}

[[nodiscard]] inline SchematicOrientation orientation_from_quarter_turns(int turns) {
    switch ((turns % 4 + 4) % 4) {
    case 0:
        return SchematicOrientation::Right;
    case 1:
        return SchematicOrientation::Down;
    case 2:
        return SchematicOrientation::Left;
    case 3:
        return SchematicOrientation::Up;
    }
    throw std::logic_error{"Unhandled schematic orientation"};
}

[[nodiscard]] inline SchematicOrientation combine_orientations(SchematicOrientation parent,
                                                               SchematicOrientation child) {
    return orientation_from_quarter_turns(orientation_quarter_turns(parent) +
                                          orientation_quarter_turns(child));
}

[[nodiscard]] inline double text_style_font_size(SchematicTextStyle style,
                                                 double default_font_size) noexcept {
    return style.font_size().value_or(default_font_size);
}

[[nodiscard]] inline SvgBounds text_bounds(Point anchor, SchematicOrientation orientation,
                                           std::string_view text, SchematicTextStyle style,
                                           double default_font_size) {
    const auto font_size = text_style_font_size(style, default_font_size);
    const auto width = rendered_text_width(text, font_size);
    auto min_x = 0.0;
    auto max_x = width;
    if (style.horizontal_alignment() == TextHorizontalAlignment::Middle) {
        min_x = -width / 2.0;
        max_x = width / 2.0;
    } else if (style.horizontal_alignment() == TextHorizontalAlignment::End) {
        min_x = -width;
        max_x = 0.0;
    }

    const auto height = rendered_text_height(font_size);
    auto min_y = -font_size;
    auto max_y = height - font_size;
    if (style.vertical_alignment() == TextVerticalAlignment::Top) {
        min_y = 0.0;
        max_y = height;
    } else if (style.vertical_alignment() == TextVerticalAlignment::Middle) {
        min_y = -height / 2.0;
        max_y = height / 2.0;
    } else if (style.vertical_alignment() == TextVerticalAlignment::Bottom) {
        min_y = -height;
        max_y = 0.0;
    }
    return transform_rect_bounds(min_x, min_y, max_x, max_y, anchor, orientation);
}

[[nodiscard]] inline std::string
abbreviate_middle_to_fit(std::string_view text, double available_width, double font_size) {
    const auto character_width = font_size * title_block_text_width_factor;
    if (available_width <= 0.0 || character_width <= 0.0) {
        return {};
    }

    const auto character_limit =
        static_cast<std::size_t>(std::floor(available_width / character_width));
    if (text.size() <= character_limit) {
        return std::string{text};
    }
    if (character_limit <= 3U) {
        return std::string{text.substr(0U, character_limit)};
    }

    const auto retained = character_limit - 3U;
    const auto prefix = (retained + 1U) / 2U;
    const auto suffix = retained - prefix;
    auto result = std::string{text.substr(0U, prefix)};
    result += "...";
    if (suffix != 0U) {
        result += text.substr(text.size() - suffix);
    }
    return result;
}

[[nodiscard]] inline SvgTitleBlockTextFit fit_title_block_text(std::string_view text,
                                                               double available_width) {
    const auto font_size = schematic_svg_visual_scale.title_block_font_size;
    const auto rendered_width = title_block_rendered_text_width(text, font_size);
    if (rendered_width <= available_width + 1e-12) {
        return SvgTitleBlockTextFit{std::string{text}, false, false, 0.0};
    }

    const auto scale = available_width <= 0.0 ? 0.0 : available_width / rendered_width;
    if (scale >= title_block_min_compression_scale) {
        return SvgTitleBlockTextFit{std::string{text}, true, true, available_width};
    }

    return SvgTitleBlockTextFit{abbreviate_middle_to_fit(text, available_width, font_size), true,
                                false, 0.0};
}

[[nodiscard]] inline double sheet_port_body_length(std::string_view label) {
    const auto label_width = static_cast<double>(label.size()) *
                             schematic_svg_visual_scale.tag_port_label_font_size *
                             sheet_port_text_width_factor;
    return std::max(sheet_port_min_body_length, label_width + (sheet_port_label_padding * 2.0));
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
    out << '"';
    write_text_presentation_attributes(out, text.style());
    out << " transform=\"rotate(";
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
    out << "\" r=\"";
    write_svg_number(out, schematic_svg_visual_scale.pin_anchor_radius);
    out << "\"/>\n";
    out << "      <text class=\"pin-label\" x=\"";
    write_svg_number(out, pin.anchor().x());
    out << "\" y=\"";
    write_svg_number(out, pin.anchor().y() + debug_pin_label_offset);
    out << "\">" << svg_escape(pin.name()) << "</text>\n";
}

inline void write_symbol_instance_svg(std::ostream &out, const Schematic &schematic,
                                      SymbolInstanceId id) {
    const auto &instance = schematic.symbol_instance(id);
    const auto &symbol = schematic.symbol_definition(instance.symbol_definition());

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
    const auto &text = label.label().value_or(net.name().value());

    out << "    <text class=\"net-label\" data-net=\"" << svg_escape(svg_net_id(label.net()))
        << "\" x=\"";
    write_svg_number(out, label.position().x());
    out << "\" y=\"";
    write_svg_number(out, label.position().y());
    out << '"';
    write_text_presentation_attributes(out, label.style());
    out << " transform=\"rotate(";
    write_svg_number(out, orientation_degrees(label.orientation()));
    out << ' ';
    write_svg_number(out, label.position().x());
    out << ' ';
    write_svg_number(out, label.position().y());
    out << ")\">" << svg_escape(text) << "</text>\n";
}

inline void write_junction_svg(std::ostream &out, const Schematic &schematic, JunctionId id) {
    const auto &junction = schematic.junction(id);

    out << "    <circle class=\"junction\" data-net=\"" << svg_escape(svg_net_id(junction.net()))
        << "\" cx=\"";
    write_svg_number(out, junction.position().x());
    out << "\" cy=\"";
    write_svg_number(out, junction.position().y());
    out << "\" r=\"";
    write_svg_number(out, schematic_svg_visual_scale.junction_radius);
    out << "\"/>\n";
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
    out << ")";
    const auto glyph_degrees = power_port_glyph_degrees(port.kind(), port.orientation());
    if (std::abs(glyph_degrees) >= 1e-12) {
        out << " rotate(";
        write_svg_number(out, glyph_degrees);
        out << ")";
    }
    out << "\">\n";
    if (port.kind() == PowerPortKind::Ground) {
        out << "      <line class=\"power-port-line\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"";
        write_svg_number(out, ground_port_stem_length);
        out << "\"/>\n";
        out << "      <line class=\"ground-bar\" x1=\"-3.6\" y1=\"";
        write_svg_number(out, ground_port_stem_length);
        out << "\" x2=\"3.6\" y2=\"";
        write_svg_number(out, ground_port_stem_length);
        out << "\"/>\n";
        out << "      <line class=\"ground-bar\" x1=\"-2.2\" y1=\"4.6\" x2=\"2.2\" y2=\"4.6\"/>\n";
        out << "      <line class=\"ground-bar\" x1=\"-0.9\" y1=\"6\" x2=\"0.9\" y2=\"6\"/>\n";
    } else {
        out << "      <line class=\"power-port-line\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"-";
        write_svg_number(out, power_port_stem_length);
        out << "\"/>\n";
        out << "      <path class=\"power-port-shape\" d=\"M -";
        write_svg_number(out, power_port_half_width);
        out << " -";
        write_svg_number(out, power_port_stem_length);
        out << " L 0 -";
        write_svg_number(out, power_port_tip_offset);
        out << " L ";
        write_svg_number(out, power_port_half_width);
        out << " -";
        write_svg_number(out, power_port_stem_length);
        out << " Z\"/>\n";
    }
    const auto label_y =
        port.kind() == PowerPortKind::Ground ? ground_port_label_offset : -power_port_label_offset;
    out << "      <text class=\"power-port-label\" x=\"0\" y=\"";
    write_svg_number(out, label_y);
    out << "\"";
    write_upright_text_transform_degrees(out, glyph_degrees, Point{0.0, label_y});
    const auto port_label = port.label().value_or(net.name().value());
    out << ">" << svg_escape(port_label) << "</text>\n";
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
    const auto body_length = sheet_port_body_length(port.name());
    const auto tip_x = body_length + sheet_port_tip_length;
    out << "      <path class=\"sheet-port-shape\" d=\"M 0 -";
    write_svg_number(out, sheet_port_half_height);
    out << " L ";
    write_svg_number(out, body_length);
    out << " -";
    write_svg_number(out, sheet_port_half_height);
    out << " L ";
    write_svg_number(out, tip_x);
    out << " 0 L ";
    write_svg_number(out, body_length);
    out << ' ';
    write_svg_number(out, sheet_port_half_height);
    out << " L 0 ";
    write_svg_number(out, sheet_port_half_height);
    out << " Z\"/>\n";
    const auto label_x = body_length * 0.5;
    const auto label_anchor = Point{label_x, sheet_port_label_baseline};
    out << "      <text class=\"sheet-port-label\" x=\"";
    write_svg_number(out, label_x);
    out << "\" y=\"";
    write_svg_number(out, sheet_port_label_baseline);
    out << "\"";
    write_upright_text_transform(out, port.orientation(), label_anchor);
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
    out << '"';
    write_text_presentation_attributes(out, field.style());
    out << " transform=\"rotate(";
    write_svg_number(out, orientation_degrees(field.orientation()));
    out << ' ';
    write_svg_number(out, field.position().x());
    out << ' ';
    write_svg_number(out, field.position().y());
    out << ")\">" << svg_escape(field.value()) << "</text>\n";
}

[[nodiscard]] inline SvgBounds symbol_primitive_bounds(const SymbolPrimitive &primitive,
                                                       const SymbolInstance &instance) {
    const auto stroke_padding = schematic_svg_visual_scale.symbol_stroke_width / 2.0;
    if (std::holds_alternative<SymbolLine>(primitive)) {
        const auto &line = std::get<SymbolLine>(primitive);
        auto bounds = bounds_from_point(
            transform_schematic_point(line.start(), instance.position(), instance.orientation()));
        include_point(bounds, transform_schematic_point(line.end(), instance.position(),
                                                        instance.orientation()));
        return padded_bounds(bounds, stroke_padding);
    }
    if (std::holds_alternative<SymbolRectangle>(primitive)) {
        const auto &rectangle = std::get<SymbolRectangle>(primitive);
        return padded_bounds(
            transform_rect_bounds(
                std::min(rectangle.first_corner().x(), rectangle.second_corner().x()),
                std::min(rectangle.first_corner().y(), rectangle.second_corner().y()),
                std::max(rectangle.first_corner().x(), rectangle.second_corner().x()),
                std::max(rectangle.first_corner().y(), rectangle.second_corner().y()),
                instance.position(), instance.orientation()),
            stroke_padding);
    }
    if (std::holds_alternative<SymbolCircle>(primitive)) {
        const auto &circle = std::get<SymbolCircle>(primitive);
        return padded_bounds(transform_rect_bounds(circle.center().x() - circle.radius(),
                                                   circle.center().y() - circle.radius(),
                                                   circle.center().x() + circle.radius(),
                                                   circle.center().y() + circle.radius(),
                                                   instance.position(), instance.orientation()),
                             stroke_padding);
    }
    if (std::holds_alternative<SymbolArc>(primitive)) {
        const auto &arc = std::get<SymbolArc>(primitive);
        return padded_bounds(
            transform_rect_bounds(arc.center().x() - arc.radius(), arc.center().y() - arc.radius(),
                                  arc.center().x() + arc.radius(), arc.center().y() + arc.radius(),
                                  instance.position(), instance.orientation()),
            stroke_padding);
    }

    const auto &text = std::get<SymbolText>(primitive);
    const auto anchor =
        transform_schematic_point(text.anchor(), instance.position(), instance.orientation());
    return text_bounds(anchor, combine_orientations(instance.orientation(), text.orientation()),
                       text.text(), text.style(), schematic_svg_visual_scale.symbol_text_font_size);
}

[[nodiscard]] inline SvgBounds symbol_instance_bounds(const Schematic &schematic,
                                                      SymbolInstanceId id) {
    const auto &instance = schematic.symbol_instance(id);
    const auto &symbol = schematic.symbol_definition(instance.symbol_definition());
    auto bounds = bounds_from_point(instance.position());
    for (const auto &pin : symbol.pins()) {
        include_point(bounds, transform_schematic_point(pin.anchor(), instance.position(),
                                                        instance.orientation()));
    }
    for (const auto &primitive : symbol.primitives()) {
        include_bounds(bounds, symbol_primitive_bounds(primitive, instance));
    }
    return bounds;
}

[[nodiscard]] inline SvgBounds symbol_debug_overlay_bounds(const Schematic &schematic,
                                                           SymbolInstanceId id) {
    const auto &instance = schematic.symbol_instance(id);
    const auto &symbol = schematic.symbol_definition(instance.symbol_definition());
    auto bounds = bounds_from_point(instance.position());
    const auto pin_anchor_padding = schematic_svg_visual_scale.pin_anchor_radius +
                                    (schematic_svg_visual_scale.pin_overlay_stroke_width / 2.0);
    for (const auto &pin : symbol.pins()) {
        const auto anchor =
            transform_schematic_point(pin.anchor(), instance.position(), instance.orientation());
        include_bounds(bounds, padded_bounds(bounds_from_point(anchor), pin_anchor_padding));
        const auto label_anchor = transform_schematic_point(
            Point{pin.anchor().x(), pin.anchor().y() + debug_pin_label_offset}, instance.position(),
            instance.orientation());
        include_bounds(bounds, text_bounds(label_anchor, instance.orientation(), pin.name(),
                                           SchematicTextStyle{},
                                           schematic_svg_visual_scale.pin_label_font_size));
    }
    return bounds;
}

[[nodiscard]] inline SvgBounds wire_run_bounds(const WireRun &wire) {
    auto bounds = bounds_from_point(wire.points().front());
    for (const auto point : wire.points()) {
        include_point(bounds, point);
    }
    return padded_bounds(bounds, schematic_svg_visual_scale.wire_stroke_width / 2.0);
}

[[nodiscard]] inline SchematicOrientation
power_port_bounds_orientation(PowerPortKind kind, SchematicOrientation orientation) {
    switch (kind) {
    case PowerPortKind::Power:
        return orientation_from_quarter_turns(orientation_quarter_turns(orientation) + 1);
    case PowerPortKind::Ground:
        return orientation_from_quarter_turns(orientation_quarter_turns(orientation) - 1);
    }
    throw std::logic_error{"Unhandled power port kind"};
}

[[nodiscard]] inline Point transformed_power_port_anchor(const PowerPort &port,
                                                         Point local_anchor) {
    return transform_schematic_point(
        local_anchor, port.position(),
        power_port_bounds_orientation(port.kind(), port.orientation()));
}

[[nodiscard]] inline SvgBounds power_port_label_bounds(const PowerPort &port,
                                                       std::string_view label) {
    const auto label_y =
        port.kind() == PowerPortKind::Ground ? ground_port_label_offset : -power_port_label_offset;
    return text_bounds(transformed_power_port_anchor(port, Point{0.0, label_y}),
                       SchematicOrientation::Right, label, SchematicTextStyle{},
                       schematic_svg_visual_scale.tag_port_label_font_size);
}

[[nodiscard]] inline SvgBounds power_port_bounds(const PowerPort &port, std::string_view label) {
    const auto glyph_orientation = power_port_bounds_orientation(port.kind(), port.orientation());
    const auto stroke_padding = schematic_svg_visual_scale.tag_port_stroke_width / 2.0;
    auto bounds =
        port.kind() == PowerPortKind::Ground
            ? transform_rect_bounds(-3.6, 0.0, 3.6, 6.0, port.position(), glyph_orientation)
            : transform_rect_bounds(-power_port_half_width, -power_port_tip_offset,
                                    power_port_half_width, 0.0, port.position(), glyph_orientation);
    bounds = padded_bounds(bounds, stroke_padding);
    include_bounds(bounds, power_port_label_bounds(port, label));
    return bounds;
}

[[nodiscard]] inline SvgBounds no_connect_marker_bounds(const NoConnectMarker &marker) {
    return transform_rect_bounds(-3.0 - (schematic_svg_visual_scale.no_connect_stroke_width / 2.0),
                                 -3.0 - (schematic_svg_visual_scale.no_connect_stroke_width / 2.0),
                                 3.0 + (schematic_svg_visual_scale.no_connect_stroke_width / 2.0),
                                 3.0 + (schematic_svg_visual_scale.no_connect_stroke_width / 2.0),
                                 marker.position(), marker.orientation());
}

[[nodiscard]] inline Point transformed_sheet_port_anchor(const SheetPort &port,
                                                         Point local_anchor) {
    return transform_schematic_point(local_anchor, port.position(), port.orientation());
}

[[nodiscard]] inline SvgBounds sheet_port_label_bounds(const SheetPort &port) {
    const auto body_length = sheet_port_body_length(port.name());
    return text_bounds(
        transformed_sheet_port_anchor(port, Point{body_length * 0.5, sheet_port_label_baseline}),
        SchematicOrientation::Right, port.name(), SchematicTextStyle{},
        schematic_svg_visual_scale.tag_port_label_font_size);
}

[[nodiscard]] inline SvgBounds sheet_port_bounds(const SheetPort &port) {
    const auto body_length = sheet_port_body_length(port.name());
    auto bounds =
        transform_rect_bounds(0.0, -sheet_port_half_height, body_length + sheet_port_tip_length,
                              sheet_port_half_height, port.position(), port.orientation());
    bounds = padded_bounds(bounds, schematic_svg_visual_scale.tag_port_stroke_width / 2.0);
    include_bounds(bounds, sheet_port_label_bounds(port));
    return bounds;
}

[[nodiscard]] inline std::optional<SvgBounds>
sheet_content_bounds(const Schematic &schematic, SheetId sheet_id,
                     SchematicSvgBodyOptions options) {
    const auto &sheet = schematic.sheet(sheet_id);
    auto bounds = std::optional<SvgBounds>{};
    const auto include = [&bounds](SvgBounds next) {
        if (bounds.has_value()) {
            include_bounds(bounds.value(), next);
        } else {
            bounds = next;
        }
    };

    if (options.include_regions) {
        for (std::size_t index = 0; index < sheet.regions().size(); ++index) {
            const auto region = sheet.region(index).bounds();
            include(rect_bounds(region.x(), region.y(), region.width(), region.height()));
        }
    }
    for (const auto instance : sheet.symbol_instances()) {
        include(symbol_instance_bounds(schematic, instance));
    }
    for (const auto wire : sheet.wire_runs()) {
        include(wire_run_bounds(schematic.wire_run(wire)));
    }
    for (const auto junction : sheet.junctions()) {
        include(padded_bounds(bounds_from_point(schematic.junction(junction).position()),
                              schematic_svg_visual_scale.junction_radius));
    }
    for (const auto port_id : sheet.power_ports()) {
        const auto &port = schematic.power_port(port_id);
        const auto &net = schematic.circuit().net(port.net());
        include(power_port_bounds(port, port.label().value_or(net.name().value())));
    }
    for (const auto marker : sheet.no_connect_markers()) {
        include(no_connect_marker_bounds(schematic.no_connect_marker(marker)));
    }
    for (const auto port : sheet.sheet_ports()) {
        include(sheet_port_bounds(schematic.sheet_port(port)));
    }
    for (const auto label_id : sheet.net_labels()) {
        const auto &label = schematic.net_label(label_id);
        const auto &net = schematic.circuit().net(label.net());
        include(text_bounds(label.position(), label.orientation(),
                            label.label().value_or(net.name().value()), label.style(),
                            schematic_svg_visual_scale.net_label_font_size));
    }
    for (const auto field_id : sheet.symbol_fields()) {
        const auto &field = schematic.symbol_field(field_id);
        include(text_bounds(field.position(), field.orientation(), field.value(), field.style(),
                            schematic_svg_visual_scale.symbol_field_font_size));
    }
    if (options.svg.debug_overlays) {
        for (const auto instance : sheet.symbol_instances()) {
            include(symbol_debug_overlay_bounds(schematic, instance));
        }
    }
    return bounds;
}

[[nodiscard]] inline SvgBounds expanded_body_bounds(const Schematic &schematic, SheetId sheet_id,
                                                    SchematicSvgBodyOptions options) {
    if (!std::isfinite(options.margin) || options.margin < 0.0) {
        throw std::invalid_argument{"Schematic SVG body margin must be finite and non-negative"};
    }
    auto bounds =
        sheet_content_bounds(schematic, sheet_id, options).value_or(SvgBounds{0.0, 0.0, 1.0, 1.0});
    return padded_bounds(bounds, options.margin);
}

[[nodiscard]] inline bool region_uses_dashed_frame(const SheetRegion &region) {
    for (const auto &field : region.style()) {
        if (field.key() == "border" && field.value() == "dashed") {
            return true;
        }
    }
    return false;
}

inline void write_region_title_clip_defs_svg(std::ostream &out, SheetId sheet_id,
                                             const Sheet &sheet) {
    const auto sheet_token = svg_sheet_token(sheet_id);
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

    write_region_title_clip_defs_svg(out, sheet_id, sheet);

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

inline void write_title_block_text_svg(std::ostream &out, std::string_view css_class, double x,
                                       double y, std::string_view text, double available_width) {
    const auto fit = fit_title_block_text(text, available_width);

    out << "      <text class=\"" << css_class << "\" x=\"";
    write_svg_number(out, x);
    out << "\" y=\"";
    write_svg_number(out, y);
    out << "\"";
    if (fit.fitted) {
        out << " data-full-text=\"" << svg_escape(text) << "\"";
    }
    if (fit.compressed) {
        out << " textLength=\"";
        write_svg_number(out, fit.text_length);
        out << "\" lengthAdjust=\"spacingAndGlyphs\"";
    }
    out << ">" << svg_escape(fit.text) << "</text>\n";
}

inline void write_title_block_row_svg(std::ostream &out, const SvgRect &rect, std::size_t row,
                                      std::string_view key, std::string_view value) {
    const auto text_y = (static_cast<double>(row) * title_block_row_height) + 4.2;
    const auto label_available_width =
        std::max(0.0, std::min(title_block_label_width, rect.width) - title_block_label_x - 1.0);
    const auto value_available_width =
        std::max(0.0, rect.width - title_block_value_x - title_block_right_padding);
    write_title_block_text_svg(out, "title-block-label", title_block_label_x, text_y, key,
                               label_available_width);
    auto value_class = std::string{"title-block-value"};
    if (row == 0U) {
        value_class += " sheet-title";
    }
    write_title_block_text_svg(out, value_class, title_block_value_x, text_y, value,
                               value_available_width);
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

    write_title_block_row_svg(out, rect, 0U, "Title", metadata.title());
    for (std::size_t index = 0; index < metadata.title_block().size(); ++index) {
        const auto &field = metadata.title_block()[index];
        write_title_block_row_svg(out, rect, index + 1U, field.key(), field.value());
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
    const auto &scale = schematic_svg_visual_scale;

    out << "  <style>"
           ".document-background,.sheet{fill:#fff;stroke:none}"
           ".sheet-border{fill:none;stroke:#111;";
    write_css_stroke_width(out, scale.sheet_border_stroke_width);
    out << "}.drawing-frame{fill:none;stroke:#111;";
    write_css_stroke_width(out, scale.drawing_frame_stroke_width);
    out << "}.sheet-grid{stroke:none}"
           ".sheet-grid-dot{fill:#d7d7d7;stroke:none}"
           ".coordinate-zone-label{";
    write_css_font(out, scale.coordinate_zone_font_size);
    out << ";fill:#444;text-anchor:middle;dominant-baseline:middle}"
           ".sheet-title{font-weight:600}"
           ".title-block-outline,.title-block-rule{fill:none;stroke:#111;";
    write_css_stroke_width(out, scale.title_block_stroke_width);
    out << "}.title-block-label{";
    write_css_font(out, scale.title_block_font_size);
    out << ";fill:#444;text-anchor:start}"
           ".title-block-value{";
    write_css_font(out, scale.title_block_font_size);
    out << ";fill:#111;text-anchor:start}"
           ".sheet-region-frame{fill:#fff;fill-opacity:0;stroke:#78716c;";
    write_css_stroke_width(out, scale.region_frame_stroke_width);
    out << "}.sheet-region-frame.dashed{stroke-dasharray:3 2}"
           ".sheet-region-title{";
    write_css_font(out, scale.region_title_font_size);
    out << ";fill:#57534e;text-anchor:start;font-weight:600}"
           ".wire-run{fill:none;stroke:#111;";
    write_css_stroke_width(out, scale.wire_stroke_width);
    out << ";stroke-linecap:round;stroke-linejoin:round}.net-label{";
    write_css_font(out, scale.net_label_font_size);
    out << ";fill:#111}"
           ".junction{fill:#111;stroke:none}"
           ".power-port-line,.ground-bar{stroke:#111;";
    write_css_stroke_width(out, scale.tag_port_stroke_width);
    out << ";stroke-linecap:round;stroke-linejoin:round}.no-connect-line{stroke:#111;";
    write_css_stroke_width(out, scale.no_connect_stroke_width);
    out << ";stroke-linecap:round}"
           ".power-port-shape,.sheet-port-shape{fill:#fff;stroke:#111;";
    write_css_stroke_width(out, scale.tag_port_stroke_width);
    out << ";stroke-linejoin:round}";
    out << ".power-port-label,.sheet-port-label{";
    write_css_font(out, scale.tag_port_label_font_size);
    out << ";fill:#111;text-anchor:middle}"
           ".symbol-field{";
    write_css_font(out, scale.symbol_field_font_size);
    out << ";fill:#111}"
           ".symbol-line,.symbol-rectangle,.symbol-circle,.symbol-arc{fill:none;stroke:#111;";
    write_css_stroke_width(out, scale.symbol_stroke_width);
    out << ";stroke-linecap:round;stroke-linejoin:round}.symbol-text{";
    write_css_font(out, scale.symbol_text_font_size);
    out << ";fill:#111}";
    if (options.debug_overlays) {
        out << ".pin-anchor{fill:#fff;stroke:#c2410c;";
        write_css_stroke_width(out, scale.pin_overlay_stroke_width);
        out << "}.pin-label{";
        write_css_font(out, scale.pin_label_font_size);
        out << ";fill:#c2410c;text-anchor:middle}";
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

inline void write_body_content_layers_svg(std::ostream &out, const Schematic &schematic,
                                          SheetId sheet_id, SchematicSvgBodyOptions options) {
    const auto &sheet = schematic.sheet(sheet_id);

    if (options.include_regions) {
        out << "    <g class=\"layer layer-regions\">\n";
        write_regions_svg(out, sheet_id, sheet);
        out << "    </g>\n";
    }
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
    if (options.svg.debug_overlays) {
        out << "    <g class=\"layer layer-debug\">\n";
        for (const auto instance : sheet.symbol_instances()) {
            write_symbol_debug_overlay_svg(out, schematic, instance);
        }
        out << "    </g>\n";
    }
}

} // namespace detail

/** Write one content-tight SVG body for a single schematic sheet. */
inline void write_schematic_body_svg(std::ostream &out, const Schematic &schematic,
                                     SheetId sheet_id, SchematicSvgBodyOptions options = {}) {
    const auto bounds = detail::expanded_body_bounds(schematic, sheet_id, options);
    const auto width = detail::bounds_width(bounds);
    const auto height = detail::bounds_height(bounds);

    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"";
    detail::write_svg_number(out, bounds.min_x);
    out << ' ';
    detail::write_svg_number(out, bounds.min_y);
    out << ' ';
    detail::write_svg_number(out, width);
    out << ' ';
    detail::write_svg_number(out, height);
    out << "\" width=\"";
    detail::write_svg_number(out, width);
    out << "\" height=\"";
    detail::write_svg_number(out, height);
    out << "\">\n";
    detail::write_svg_style(out, options.svg);
    const auto &sheet = schematic.sheet(sheet_id);
    if (options.include_regions && !sheet.regions().empty()) {
        out << "  <defs>\n";
        detail::write_region_title_clip_defs_svg(out, sheet_id, sheet);
        out << "  </defs>\n";
    }
    out << "  <g class=\"schematic-body\" data-sheet=\""
        << detail::svg_escape(detail::svg_sheet_id(sheet_id)) << "\">\n";
    detail::write_body_content_layers_svg(out, schematic, sheet_id, options);
    out << "  </g>\n";
    out << "</svg>\n";
}

/** Return one content-tight SVG body for a single schematic sheet. */
[[nodiscard]] inline std::string write_schematic_body_svg(const Schematic &schematic,
                                                          SheetId sheet_id,
                                                          SchematicSvgBodyOptions options = {}) {
    auto out = std::ostringstream{};
    write_schematic_body_svg(out, schematic, sheet_id, options);
    return out.str();
}

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
