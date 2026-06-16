#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <volt/io/detail/typed_id.hpp>
#include <volt/schematic/presentation_geometry.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/symbols.hpp>

namespace volt::io::detail {

inline constexpr double svg_sheet_width = 297.0;
inline constexpr double svg_sheet_height = 210.0;
inline constexpr double svg_sheet_gap = 20.0;
inline constexpr double svg_pi = 3.14159265358979323846;
inline constexpr double title_block_width = ::volt::detail::title_block_width;
inline constexpr double title_block_label_width = ::volt::detail::title_block_label_width;
inline constexpr double title_block_row_height = ::volt::detail::title_block_row_height;
inline constexpr double title_block_label_x = ::volt::detail::title_block_label_x;
inline constexpr double title_block_value_x = ::volt::detail::title_block_value_x;
inline constexpr double title_block_right_padding = ::volt::detail::title_block_right_padding;
inline constexpr double title_block_text_width_factor =
    ::volt::detail::title_block_text_width_factor;
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
    double title_block_font_size = ::volt::detail::title_block_rendered_font_size;
    /** Functional-region title font size. */
    double region_title_font_size = 3.2;
    /** Local net label font size. */
    double net_label_font_size = ::volt::detail::net_label_rendered_font_size;
    /** Sheet, power, and ground tag label font size. */
    double tag_port_label_font_size = ::volt::detail::sheet_port_rendered_label_font_size;
    /** Symbol primitive text font size. */
    double symbol_text_font_size = ::volt::detail::symbol_text_rendered_font_size;
    /** Symbol field/value font size. */
    double symbol_field_font_size = ::volt::detail::symbol_field_rendered_font_size;
    /** Debug pin anchor radius. */
    double pin_anchor_radius = 1.2;
    /** Debug pin label font size. */
    double pin_label_font_size = 3.0;
    /** Debug pin overlay stroke width. */
    double pin_overlay_stroke_width = 0.7;
};

inline constexpr SchematicSvgVisualScale schematic_svg_visual_scale{};
inline constexpr double power_port_stem_length = ::volt::detail::power_port_stem_length;
inline constexpr double power_port_tip_offset = ::volt::detail::power_port_tip_offset;
inline constexpr double power_port_half_width = ::volt::detail::power_port_half_width;
inline constexpr double power_port_label_offset = ::volt::detail::power_port_label_offset;
inline constexpr double ground_port_stem_length = ::volt::detail::ground_port_stem_length;
inline constexpr double ground_port_label_offset = ::volt::detail::ground_port_label_offset;
inline constexpr double sheet_port_half_height = ::volt::detail::sheet_port_rendered_half_height;
inline constexpr double sheet_port_min_body_length =
    ::volt::detail::sheet_port_rendered_min_body_length;
inline constexpr double sheet_port_tip_length = ::volt::detail::sheet_port_rendered_tip_length;
inline constexpr double sheet_port_label_padding =
    ::volt::detail::sheet_port_rendered_label_padding;
inline constexpr double debug_pin_label_offset = 4.0;
/** Deterministic average sans-serif character width used instead of browser font metrics. */
inline constexpr double sheet_port_text_width_factor = ::volt::detail::rendered_text_width_factor;
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
using SvgBounds = ::volt::detail::SchematicBounds;

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

[[nodiscard]] std::string svg_escape(std::string_view value);

void write_svg_number(std::ostream &out, double value);

[[nodiscard]] inline std::string svg_component_id(ComponentId id) { return encode_local_id(id); }

[[nodiscard]] inline std::string svg_net_id(NetId id) { return encode_local_id(id); }

[[nodiscard]] inline std::string svg_pin_id(PinId id) { return encode_local_id(id); }

[[nodiscard]] inline std::string svg_sheet_id(SheetId id) { return encode_local_id(id); }

[[nodiscard]] std::string svg_sheet_token(SheetId id);

[[nodiscard]] inline std::string svg_symbol_def_id(SymbolDefId id) { return encode_local_id(id); }

[[nodiscard]] std::string svg_symbol_instance_id(SymbolInstanceId id);

[[nodiscard]] std::string power_port_class(PowerPortKind kind);

[[nodiscard]] std::string sheet_port_class(SheetPortKind kind);

[[nodiscard]] double orientation_degrees(SchematicOrientation orientation);

[[nodiscard]] double power_port_glyph_degrees(PowerPortKind kind, SchematicOrientation orientation);

void write_upright_text_transform_degrees(std::ostream &out, double parent_degrees, Point anchor);

void write_upright_text_transform(std::ostream &out, SchematicOrientation parent_orientation,
                                  Point anchor);

void write_css_stroke_width(std::ostream &out, double width);

void write_css_font(std::ostream &out, double size);

[[nodiscard]] std::string_view svg_text_anchor(TextHorizontalAlignment alignment);

[[nodiscard]] std::string_view svg_dominant_baseline(TextVerticalAlignment alignment);

void write_text_presentation_attributes(std::ostream &out, SchematicTextStyle style);

using ::volt::detail::bounds_from_point;
using ::volt::detail::bounds_height;
using ::volt::detail::bounds_width;
using ::volt::detail::combine_orientations;
using ::volt::detail::include_bounds;
using ::volt::detail::include_point;
using ::volt::detail::orientation_from_quarter_turns;
using ::volt::detail::orientation_quarter_turns;
using ::volt::detail::padded_bounds;
using ::volt::detail::rect_bounds;
using ::volt::detail::rendered_text_width;
using ::volt::detail::sheet_port_body_length;
using ::volt::detail::text_bounds;
using ::volt::detail::text_style_font_size;
using ::volt::detail::title_block_rendered_text_width;
using ::volt::detail::transform_rect_bounds;

[[nodiscard]] std::string abbreviate_middle_to_fit(std::string_view text, double available_width,
                                                   double font_size);

[[nodiscard]] SvgTitleBlockTextFit fit_title_block_text(std::string_view text,
                                                        double available_width);

[[nodiscard]] SvgRect drawing_area(const SheetMetadata &metadata);

[[nodiscard]] SvgRect title_block_rect(const SheetMetadata &metadata);

[[nodiscard]] std::string zone_row_label(std::size_t row);

void write_xy_attributes(std::ostream &out, Point point, std::string_view x_name,
                         std::string_view y_name);

[[nodiscard]] Point point_on_arc(const SymbolArc &arc, double degrees);

} // namespace volt::io::detail
