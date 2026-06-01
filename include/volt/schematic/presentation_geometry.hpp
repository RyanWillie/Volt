#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <variant>

#include <volt/circuit/definitions.hpp>
#include <volt/schematic/schematic.hpp>

namespace volt {

namespace detail {

inline constexpr double title_block_width = 82.0;
inline constexpr double title_block_label_width = 22.0;
inline constexpr double title_block_row_height = 6.0;
inline constexpr double title_block_label_x = 2.0;
inline constexpr double title_block_value_x = title_block_label_width + 2.0;
inline constexpr double title_block_right_padding = 2.0;
inline constexpr double title_block_text_width_factor = 0.64;
inline constexpr double rendered_text_width_factor = 0.56;
inline constexpr double rendered_text_descent_factor = 0.25;
inline constexpr double title_block_rendered_font_size = 2.5;
inline constexpr double net_label_rendered_font_size = 2.8;
inline constexpr double symbol_text_rendered_font_size = 3.0;
inline constexpr double symbol_field_rendered_font_size = 2.5;
inline constexpr double power_port_stem_length = 4.2;
inline constexpr double power_port_tip_offset = 7.6;
inline constexpr double power_port_half_width = 3.0;
inline constexpr double power_port_label_offset = 9.4;
inline constexpr double ground_port_stem_length = 3.0;
inline constexpr double ground_port_label_offset = 8.2;
inline constexpr double sheet_port_rendered_half_height = 2.4;
inline constexpr double sheet_port_rendered_min_body_length = 7.0;
inline constexpr double sheet_port_rendered_tip_length = 3.2;
inline constexpr double sheet_port_rendered_label_padding = 2.1;
inline constexpr double sheet_port_rendered_label_font_size = 2.45;

/** Conservative sheet-space bounds used by schematic readability diagnostics. */
struct SchematicBounds {
    /** Minimum sheet-space x coordinate. */
    double min_x;
    /** Minimum sheet-space y coordinate. */
    double min_y;
    /** Maximum sheet-space x coordinate. */
    double max_x;
    /** Maximum sheet-space y coordinate. */
    double max_y;
};

[[nodiscard]] SchematicBounds bounds_from_point(Point point) noexcept;

void include_point(SchematicBounds &bounds, Point point) noexcept;

void include_bounds(SchematicBounds &bounds, SchematicBounds other) noexcept;

[[nodiscard]] SchematicBounds padded_bounds(SchematicBounds bounds, double padding) noexcept;

[[nodiscard]] SchematicBounds rect_bounds(double x, double y, double width, double height) noexcept;

[[nodiscard]] bool contains_bounds(SchematicBounds outer, SchematicBounds inner) noexcept;

[[nodiscard]] bool intersects_bounds(SchematicBounds first, SchematicBounds second) noexcept;

[[nodiscard]] bool overlaps_bounds_area(SchematicBounds first, SchematicBounds second) noexcept;

[[nodiscard]] double bounds_overlap_width(SchematicBounds first, SchematicBounds second) noexcept;

[[nodiscard]] double bounds_overlap_height(SchematicBounds first, SchematicBounds second) noexcept;

[[nodiscard]] double bounds_width(SchematicBounds bounds) noexcept;

[[nodiscard]] double bounds_height(SchematicBounds bounds) noexcept;

[[nodiscard]] double bounds_gap(SchematicBounds first, SchematicBounds second) noexcept;

[[nodiscard]] Point bounds_center(SchematicBounds bounds);

[[nodiscard]] double point_distance(Point first, Point second) noexcept;

[[nodiscard]] bool point_inside_bounds(Point point, SchematicBounds bounds) noexcept;

[[nodiscard]] bool segment_intersects_bounds(SchematicSegment segment, SchematicBounds bounds);

[[nodiscard]] SchematicBounds transform_rect_bounds(double min_x, double min_y, double max_x,
                                                    double max_y, Point origin,
                                                    SchematicOrientation orientation);

[[nodiscard]] int orientation_quarter_turns(SchematicOrientation orientation) noexcept;

[[nodiscard]] SchematicOrientation orientation_from_quarter_turns(int turns);

[[nodiscard]] SchematicOrientation combined_text_orientation(SchematicOrientation parent,
                                                             SchematicOrientation child);

[[nodiscard]] SchematicOrientation combine_orientations(SchematicOrientation parent,
                                                        SchematicOrientation child);

[[nodiscard]] SchematicBounds drawing_area_bounds(const SheetMetadata &metadata) noexcept;

[[nodiscard]] SchematicBounds title_block_bounds(const SheetMetadata &metadata) noexcept;

[[nodiscard]] SchematicBounds region_bounds(const SheetRegion &region) noexcept;

[[nodiscard]] double rendered_text_width(std::string_view text, double font_size) noexcept;

[[nodiscard]] double title_block_rendered_text_width(std::string_view text,
                                                     double font_size) noexcept;

[[nodiscard]] SchematicBounds text_bounds(Point anchor, SchematicOrientation orientation,
                                          std::string_view text, double font_size,
                                          TextHorizontalAlignment horizontal_alignment,
                                          TextVerticalAlignment vertical_alignment);

[[nodiscard]] SchematicBounds text_bounds(Point anchor, SchematicOrientation orientation,
                                          std::string_view text, double font_size, bool centered);

[[nodiscard]] double text_style_font_size(SchematicTextStyle style,
                                          double default_font_size) noexcept;

[[nodiscard]] SchematicBounds text_bounds(Point anchor, SchematicOrientation orientation,
                                          std::string_view text, SchematicTextStyle style,
                                          double default_font_size);

[[nodiscard]] SchematicBounds transform_symbol_point_bounds(Point point,
                                                            const SymbolInstance &instance);

[[nodiscard]] SchematicBounds
symbol_primitive_bounds(const SymbolPrimitive &primitive, const SymbolInstance &instance,
                        double line_padding = 0.5, double closed_shape_padding = 0.0,
                        double text_font_size = symbol_text_rendered_font_size);

[[nodiscard]] SchematicBounds
symbol_instance_bounds(const Schematic &schematic, SymbolInstanceId id, double line_padding = 0.5,
                       double closed_shape_padding = 0.0,
                       double text_font_size = symbol_text_rendered_font_size);

[[nodiscard]] std::optional<SchematicBounds> symbol_instance_body_bounds(const Schematic &schematic,
                                                                         SymbolInstanceId id);

[[nodiscard]] bool symbol_instances_share_same_net_pin_point(const Schematic &schematic,
                                                             SymbolInstanceId first_id,
                                                             SymbolInstanceId second_id);

[[nodiscard]] bool symbol_overlap_is_shared_pin_contact(const Schematic &schematic,
                                                        SymbolInstanceId first_id,
                                                        SchematicBounds first_bounds,
                                                        SymbolInstanceId second_id,
                                                        SchematicBounds second_bounds);

[[nodiscard]] SchematicBounds wire_run_bounds(const WireRun &wire, double padding = 0.5);

[[nodiscard]] SchematicBounds segment_bounds(SchematicSegment segment);

[[nodiscard]] SchematicOrientation power_port_glyph_orientation(PowerPortKind kind,
                                                                SchematicOrientation orientation);

[[nodiscard]] Point transformed_port_anchor(const PowerPort &port, Point local_anchor);

[[nodiscard]] SchematicBounds
power_port_label_bounds(const PowerPort &port, std::string_view label,
                        double font_size = sheet_port_rendered_label_font_size);

[[nodiscard]] SchematicBounds power_port_glyph_bounds(const PowerPort &port,
                                                      double stroke_padding = 0.0);

[[nodiscard]] SchematicBounds
power_port_bounds(const PowerPort &port, std::string_view label, double stroke_padding = 0.0,
                  double label_font_size = sheet_port_rendered_label_font_size);

[[nodiscard]] SchematicBounds no_connect_marker_bounds(const NoConnectMarker &marker,
                                                       double half_size = 4.0);

[[nodiscard]] double sheet_port_rendered_body_length(std::string_view label);

[[nodiscard]] double sheet_port_body_length(std::string_view label);

[[nodiscard]] Point transformed_port_anchor(const SheetPort &port, Point local_anchor);

[[nodiscard]] SchematicBounds
sheet_port_label_bounds(const SheetPort &port,
                        double font_size = sheet_port_rendered_label_font_size);

[[nodiscard]] SchematicBounds
sheet_port_bounds(const SheetPort &port, double stroke_padding = 0.0,
                  double label_font_size = sheet_port_rendered_label_font_size);

} // namespace detail

} // namespace volt
