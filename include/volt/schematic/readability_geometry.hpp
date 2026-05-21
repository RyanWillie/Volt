#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <volt/circuit/definitions.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/schematic/schematic.hpp>

namespace volt {

namespace detail {

// These limits are intentionally heuristic: they catch page-level readability smells without
// turning style preferences into mutation-time invariants.
inline constexpr std::size_t repeated_label_warning_threshold = 4U;
inline constexpr std::size_t fragmented_pin_label_threshold = 3U;
inline constexpr std::size_t overlong_label_character_threshold = 24U;
inline constexpr std::size_t dense_no_connect_marker_threshold = 6U;
inline constexpr std::size_t dense_region_port_tag_threshold = 12U;
inline constexpr double dense_no_connect_cluster_radius = 18.0;
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
inline constexpr double tag_stack_min_spacing = 6.0;
inline constexpr double tag_stack_alignment_tolerance = 1.0;
inline constexpr double label_symbol_clearance = 1.5;
inline constexpr double unrelated_text_wire_clearance = 2.0;
inline constexpr double no_connect_marker_clearance = 2.0;
inline constexpr double symbol_field_owner_max_gap = 18.0;
inline constexpr double local_dogleg_endpoint_max_distance = 24.0;
inline constexpr double local_dogleg_min_length = 80.0;
inline constexpr double local_dogleg_min_detour_ratio = 4.0;
inline constexpr double local_label_cluster_max_span = 24.0;
inline constexpr double local_label_alignment_tolerance = 1.0;
inline constexpr std::size_t local_label_cluster_threshold = 3U;
inline constexpr double floating_stub_max_length = 8.0;
inline constexpr double floating_stub_cluster_max_span = 24.0;
inline constexpr std::size_t floating_stub_cluster_threshold = 3U;
inline constexpr double oversized_port_tag_rendered_length = 28.0;
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

/** Broad schematic object kinds used to keep readability checks scoped. */
enum class ReadabilityObjectKind {
    SymbolInstance,
    WireRun,
    NetLabel,
    Junction,
    PowerPort,
    NoConnectMarker,
    SheetPort,
    SymbolField,
};

/** Text-like schematic object kinds used to tune conservative collision checks. */
enum class ReadabilityTextKind {
    NetLabel,
    SymbolField,
    SymbolText,
    PowerPortLabel,
    SheetPortLabel,
};

/** Visual schematic element kinds used by the generic collision backstop. */
enum class ReadabilityCollisionKind {
    SymbolBody,
    WireSegment,
    Text,
    TerminalGlyph,
    Junction,
    NoConnectMarker,
    SheetPort,
};

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

/** Schematic object geometry and context gathered for readability checks. */
struct ReadabilityObject {
    /** Schematic object category. */
    ReadabilityObjectKind kind;
    /** Primary schematic entity referenced by diagnostics. */
    EntityRef entity;
    /** Related logical or schematic entities that explain the object. */
    std::vector<EntityRef> context;
    /** Conservative sheet-space bounds for the object. */
    SchematicBounds bounds;
    /** Sheet-local authored region index, if the object was placed through one. */
    std::optional<std::size_t> authored_region;
    /** Typed symbol instance ID for traversal-only checks. */
    std::optional<SymbolInstanceId> symbol_instance = std::nullopt;
    /** Typed power port ID for traversal-only checks. */
    std::optional<PowerPortId> power_port = std::nullopt;
};

/** Text-like object geometry gathered for conservative collision checks. */
struct ReadabilityTextObject {
    /** Text object category. */
    ReadabilityTextKind kind;
    /** Primary text entity referenced by diagnostics. */
    EntityRef entity;
    /** Related entities that explain the text object. */
    std::vector<EntityRef> context;
    /** Conservative sheet-space text bounds. */
    SchematicBounds bounds;
    /** Authored anchor used to suppress intentional label-on-wire attachment points. */
    Point anchor;
    /** Net named or tagged by the text, if applicable. */
    std::optional<NetId> net;
    /** Symbol instance that owns this text when it is rendered inside a symbol definition. */
    std::optional<SymbolInstanceId> owning_symbol;
};

/** Shape-aware schematic element geometry gathered for generic visual collision checks. */
struct ReadabilityCollisionObject {
    /** Visual element category. */
    ReadabilityCollisionKind kind;
    /** Primary entity referenced by diagnostics. */
    EntityRef entity;
    /** Related entities that explain the visual element. */
    std::vector<EntityRef> context;
    /** Conservative sheet-space bounds for broad-phase collision checks. */
    SchematicBounds bounds;
    /** Exact wire segment geometry for wire elements. */
    std::optional<SchematicSegment> segment;
    /** Sheet-space anchor point for elements that intentionally attach to wires. */
    std::optional<Point> anchor;
    /** Net carried, named, or tagged by this visual element, if applicable. */
    std::optional<NetId> net;
    /** Component represented by this visual element, if applicable. */
    std::optional<ComponentId> component;
    /** Typed symbol instance ID for traversal-only checks. */
    std::optional<SymbolInstanceId> symbol_instance = std::nullopt;
};

/** Rendered tag/port geometry used for stack and density checks. */
struct ReadabilityTagObject {
    /** Primary tag entity referenced by diagnostics. */
    EntityRef entity;
    /** Related logical entities that explain the tag. */
    std::vector<EntityRef> context;
    /** Rendered tag bounds. */
    SchematicBounds bounds;
    /** Sheet-space anchor point. */
    Point position;
    /** Port orientation. */
    SchematicOrientation orientation;
    /** Sheet-local authored region index, if the tag was placed through one. */
    std::optional<std::size_t> authored_region;
};

[[nodiscard]] inline SchematicBounds bounds_from_point(Point point) noexcept {
    return SchematicBounds{point.x(), point.y(), point.x(), point.y()};
}

inline void include_point(SchematicBounds &bounds, Point point) noexcept {
    bounds.min_x = std::min(bounds.min_x, point.x());
    bounds.min_y = std::min(bounds.min_y, point.y());
    bounds.max_x = std::max(bounds.max_x, point.x());
    bounds.max_y = std::max(bounds.max_y, point.y());
}

inline void include_bounds(SchematicBounds &bounds, SchematicBounds other) noexcept {
    bounds.min_x = std::min(bounds.min_x, other.min_x);
    bounds.min_y = std::min(bounds.min_y, other.min_y);
    bounds.max_x = std::max(bounds.max_x, other.max_x);
    bounds.max_y = std::max(bounds.max_y, other.max_y);
}

[[nodiscard]] inline SchematicBounds padded_bounds(SchematicBounds bounds,
                                                   double padding) noexcept {
    return SchematicBounds{bounds.min_x - padding, bounds.min_y - padding, bounds.max_x + padding,
                           bounds.max_y + padding};
}

[[nodiscard]] inline SchematicBounds rect_bounds(double x, double y, double width,
                                                 double height) noexcept {
    return SchematicBounds{x, y, x + width, y + height};
}

[[nodiscard]] inline bool contains_bounds(SchematicBounds outer, SchematicBounds inner) noexcept {
    return inner.min_x >= outer.min_x - schematic_geometry_tolerance &&
           inner.min_y >= outer.min_y - schematic_geometry_tolerance &&
           inner.max_x <= outer.max_x + schematic_geometry_tolerance &&
           inner.max_y <= outer.max_y + schematic_geometry_tolerance;
}

[[nodiscard]] inline bool intersects_bounds(SchematicBounds first,
                                            SchematicBounds second) noexcept {
    return first.min_x <= second.max_x + schematic_geometry_tolerance &&
           first.max_x + schematic_geometry_tolerance >= second.min_x &&
           first.min_y <= second.max_y + schematic_geometry_tolerance &&
           first.max_y + schematic_geometry_tolerance >= second.min_y;
}

[[nodiscard]] inline bool overlaps_bounds_area(SchematicBounds first,
                                               SchematicBounds second) noexcept {
    return first.min_x < second.max_x - schematic_geometry_tolerance &&
           first.max_x > second.min_x + schematic_geometry_tolerance &&
           first.min_y < second.max_y - schematic_geometry_tolerance &&
           first.max_y > second.min_y + schematic_geometry_tolerance;
}

[[nodiscard]] inline double bounds_overlap_width(SchematicBounds first,
                                                 SchematicBounds second) noexcept {
    return std::min(first.max_x, second.max_x) - std::max(first.min_x, second.min_x);
}

[[nodiscard]] inline double bounds_overlap_height(SchematicBounds first,
                                                  SchematicBounds second) noexcept {
    return std::min(first.max_y, second.max_y) - std::max(first.min_y, second.min_y);
}

[[nodiscard]] inline double bounds_width(SchematicBounds bounds) noexcept {
    return bounds.max_x - bounds.min_x;
}

[[nodiscard]] inline double bounds_height(SchematicBounds bounds) noexcept {
    return bounds.max_y - bounds.min_y;
}

[[nodiscard]] inline double bounds_gap(SchematicBounds first, SchematicBounds second) noexcept {
    auto dx = 0.0;
    if (first.max_x < second.min_x) {
        dx = second.min_x - first.max_x;
    } else if (second.max_x < first.min_x) {
        dx = first.min_x - second.max_x;
    }

    auto dy = 0.0;
    if (first.max_y < second.min_y) {
        dy = second.min_y - first.max_y;
    } else if (second.max_y < first.min_y) {
        dy = first.min_y - second.max_y;
    }

    return std::sqrt((dx * dx) + (dy * dy));
}

[[nodiscard]] inline Point bounds_center(SchematicBounds bounds) {
    return Point{(bounds.min_x + bounds.max_x) / 2.0, (bounds.min_y + bounds.max_y) / 2.0};
}

[[nodiscard]] inline double point_distance(Point first, Point second) noexcept {
    const auto dx = first.x() - second.x();
    const auto dy = first.y() - second.y();
    return std::sqrt((dx * dx) + (dy * dy));
}

[[nodiscard]] inline bool point_inside_bounds(Point point, SchematicBounds bounds) noexcept {
    return point.x() >= bounds.min_x - schematic_geometry_tolerance &&
           point.x() <= bounds.max_x + schematic_geometry_tolerance &&
           point.y() >= bounds.min_y - schematic_geometry_tolerance &&
           point.y() <= bounds.max_y + schematic_geometry_tolerance;
}

[[nodiscard]] inline bool segment_intersects_bounds(SchematicSegment segment,
                                                    SchematicBounds bounds) {
    if (point_inside_bounds(segment.start(), bounds) ||
        point_inside_bounds(segment.end(), bounds)) {
        return true;
    }

    const auto top =
        SchematicSegment{Point{bounds.min_x, bounds.min_y}, Point{bounds.max_x, bounds.min_y}};
    const auto right =
        SchematicSegment{Point{bounds.max_x, bounds.min_y}, Point{bounds.max_x, bounds.max_y}};
    const auto bottom =
        SchematicSegment{Point{bounds.max_x, bounds.max_y}, Point{bounds.min_x, bounds.max_y}};
    const auto left =
        SchematicSegment{Point{bounds.min_x, bounds.max_y}, Point{bounds.min_x, bounds.min_y}};
    return classify_segment_relationship(segment, top) != SchematicSegmentRelationship::Disjoint ||
           classify_segment_relationship(segment, right) !=
               SchematicSegmentRelationship::Disjoint ||
           classify_segment_relationship(segment, bottom) !=
               SchematicSegmentRelationship::Disjoint ||
           classify_segment_relationship(segment, left) != SchematicSegmentRelationship::Disjoint;
}

[[nodiscard]] inline SchematicBounds transform_rect_bounds(double min_x, double min_y, double max_x,
                                                           double max_y, Point origin,
                                                           SchematicOrientation orientation) {
    auto result =
        bounds_from_point(transform_schematic_point(Point{min_x, min_y}, origin, orientation));
    include_point(result, transform_schematic_point(Point{max_x, min_y}, origin, orientation));
    include_point(result, transform_schematic_point(Point{min_x, max_y}, origin, orientation));
    include_point(result, transform_schematic_point(Point{max_x, max_y}, origin, orientation));
    return result;
}

[[nodiscard]] inline int orientation_quarter_turns(SchematicOrientation orientation) noexcept {
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
    return 0;
}

[[nodiscard]] inline SchematicOrientation orientation_from_quarter_turns(int turns) {
    const auto normalized = ((turns % 4) + 4) % 4;
    switch (normalized) {
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

[[nodiscard]] inline SchematicOrientation combined_text_orientation(SchematicOrientation parent,
                                                                    SchematicOrientation child) {
    return orientation_from_quarter_turns(orientation_quarter_turns(parent) +
                                          orientation_quarter_turns(child));
}

[[nodiscard]] inline SchematicBounds drawing_area_bounds(const SheetMetadata &metadata) noexcept {
    const auto margins = metadata.frame().margins();
    return rect_bounds(margins.left(), margins.top(),
                       std::max(0.0, metadata.size().width() - margins.left() - margins.right()),
                       std::max(0.0, metadata.size().height() - margins.top() - margins.bottom()));
}

[[nodiscard]] inline SchematicBounds title_block_bounds(const SheetMetadata &metadata) noexcept {
    const auto area = drawing_area_bounds(metadata);
    const auto rows = 1U + metadata.title_block().size();
    const auto height = title_block_row_height * static_cast<double>(rows);
    const auto width = std::min(title_block_width, area.max_x - area.min_x);
    return rect_bounds(area.min_x + std::max(0.0, (area.max_x - area.min_x) - width),
                       area.min_y + std::max(0.0, (area.max_y - area.min_y) - height), width,
                       height);
}

[[nodiscard]] inline SchematicBounds region_bounds(const SheetRegion &region) noexcept {
    const auto bounds = region.bounds();
    return rect_bounds(bounds.x(), bounds.y(), bounds.width(), bounds.height());
}

[[nodiscard]] inline double rendered_text_width(std::string_view text, double font_size) noexcept {
    return std::max(font_size * rendered_text_width_factor,
                    font_size * rendered_text_width_factor * static_cast<double>(text.size()));
}

[[nodiscard]] inline double title_block_rendered_text_width(std::string_view text,
                                                            double font_size) noexcept {
    return font_size * title_block_text_width_factor * static_cast<double>(text.size());
}

[[nodiscard]] inline SchematicBounds text_bounds(Point anchor, SchematicOrientation orientation,
                                                 std::string_view text, double font_size,
                                                 TextHorizontalAlignment horizontal_alignment,
                                                 TextVerticalAlignment vertical_alignment) {
    const auto width = rendered_text_width(text, font_size);
    auto min_x = 0.0;
    auto max_x = width;
    if (horizontal_alignment == TextHorizontalAlignment::Middle) {
        min_x = -width / 2.0;
        max_x = width / 2.0;
    } else if (horizontal_alignment == TextHorizontalAlignment::End) {
        min_x = -width;
        max_x = 0.0;
    }

    const auto height = font_size * (1.0 + rendered_text_descent_factor);
    auto min_y = -font_size;
    auto max_y = font_size * rendered_text_descent_factor;
    if (vertical_alignment == TextVerticalAlignment::Top) {
        min_y = 0.0;
        max_y = height;
    } else if (vertical_alignment == TextVerticalAlignment::Middle) {
        min_y = -height / 2.0;
        max_y = height / 2.0;
    } else if (vertical_alignment == TextVerticalAlignment::Bottom) {
        min_y = -height;
        max_y = 0.0;
    }
    return transform_rect_bounds(min_x, min_y, max_x, max_y, anchor, orientation);
}

[[nodiscard]] inline SchematicBounds text_bounds(Point anchor, SchematicOrientation orientation,
                                                 std::string_view text, double font_size,
                                                 bool centered) {
    return text_bounds(anchor, orientation, text, font_size,
                       centered ? TextHorizontalAlignment::Middle : TextHorizontalAlignment::Start,
                       TextVerticalAlignment::Baseline);
}

[[nodiscard]] inline double text_style_font_size(SchematicTextStyle style,
                                                 double default_font_size) noexcept {
    return style.font_size().value_or(default_font_size);
}

[[nodiscard]] inline SchematicBounds text_bounds(Point anchor, SchematicOrientation orientation,
                                                 std::string_view text, SchematicTextStyle style,
                                                 double default_font_size) {
    return text_bounds(anchor, orientation, text, text_style_font_size(style, default_font_size),
                       style.horizontal_alignment(), style.vertical_alignment());
}

[[nodiscard]] inline bool wire_covers_point(const WireRun &wire, Point point) noexcept {
    for (std::size_t index = 1; index < wire.points().size(); ++index) {
        if (point_on_schematic_segment(
                point, SchematicSegment{wire.points()[index - 1U], wire.points()[index]})) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool sheet_has_net_label_at_point(const Schematic &schematic,
                                                       const Sheet &sheet, NetId net, Point point) {
    for (const auto label_id : sheet.net_labels()) {
        const auto &label = schematic.net_label(label_id);
        if (label.net() == net && same_schematic_point(label.position(), point)) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] inline bool sheet_visually_covers_net_at_pin(const Schematic &schematic,
                                                           const Sheet &sheet, NetId net,
                                                           Point point) {
    for (const auto wire_id : sheet.wire_runs()) {
        const auto &wire = schematic.wire_run(wire_id);
        if (wire.net() == net && wire_covers_point(wire, point)) {
            return true;
        }
    }

    if (sheet_has_net_label_at_point(schematic, sheet, net, point)) {
        return true;
    }

    for (const auto port_id : sheet.power_ports()) {
        const auto &port = schematic.power_port(port_id);
        if (port.net() == net && same_schematic_point(port.position(), point)) {
            return true;
        }
    }

    for (const auto port_id : sheet.sheet_ports()) {
        const auto &port = schematic.sheet_port(port_id);
        if (port.net() == net && same_schematic_point(port.position(), point)) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] inline bool sheet_has_coincident_same_net_symbol_pin(const Schematic &schematic,
                                                                   const Sheet &sheet, NetId net,
                                                                   PinId pin_id, Point point) {
    const auto &circuit = schematic.circuit();

    for (const auto other_instance_id : sheet.symbol_instances()) {
        const auto &other_instance = schematic.symbol_instance(other_instance_id);
        const auto &other_symbol = schematic.symbol_definition(other_instance.symbol_definition());
        for (const auto &other_symbol_pin : other_symbol.pins()) {
            const auto other_pin =
                circuit.pin_by_number(other_instance.component(), other_symbol_pin.number());
            if (!other_pin.has_value() || other_pin.value() == pin_id) {
                continue;
            }
            const auto other_net = circuit.net_of(other_pin.value());
            if (!other_net.has_value() || other_net.value() != net) {
                continue;
            }
            const auto other_point = transform_schematic_point(
                other_symbol_pin.anchor(), other_instance.position(), other_instance.orientation());
            if (same_schematic_point(other_point, point)) {
                return true;
            }
        }
    }

    return false;
}

[[nodiscard]] inline bool schematic_readiness_exempts_pin(const Circuit &circuit, PinId pin_id,
                                                          PinDefId pin_def_id) {
    const auto &definition = circuit.pin_definition(pin_def_id);
    return definition.role() == PinRole::NoConnect ||
           definition.connection_requirement() == ConnectionRequirement::MustNotConnect ||
           circuit.is_intentional_no_connect_pin(pin_id);
}

[[nodiscard]] inline std::optional<SymbolPin> symbol_pin_by_number(const SymbolDefinition &symbol,
                                                                   const std::string &number) {
    for (const auto &symbol_pin : symbol.pins()) {
        if (symbol_pin.number() == number) {
            return symbol_pin;
        }
    }

    return std::nullopt;
}

[[nodiscard]] inline std::vector<SymbolInstanceId>
symbol_instances_for_component(const Schematic &schematic, ComponentId component) {
    auto result = std::vector<SymbolInstanceId>{};
    for (std::size_t sheet_index = 0; sheet_index < schematic.sheet_count(); ++sheet_index) {
        const auto &sheet = schematic.sheet(SheetId{sheet_index});
        for (const auto instance_id : sheet.symbol_instances()) {
            if (schematic.symbol_instance(instance_id).component() == component) {
                result.push_back(instance_id);
            }
        }
    }

    return result;
}

[[nodiscard]] inline bool component_is_schematic_relevant(const Circuit &circuit,
                                                          ComponentId component) {
    const auto &component_instance = circuit.component(component);
    const auto &definition = circuit.component_definition(component_instance.definition());
    const auto category = PropertyKey{"category"};
    if (definition.properties().contains(category)) {
        const auto &value = definition.properties().get(category);
        if (value.kind() == PropertyValueKind::String && value.as_string() == "mechanical") {
            return false;
        }
    }

    for (const auto pin_id : circuit.pins_for(component)) {
        if (circuit.net_of(pin_id).has_value() || circuit.is_intentional_no_connect_pin(pin_id)) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] inline bool sheet_has_junction_on_segments(const Schematic &schematic,
                                                         const Sheet &sheet, SchematicSegment first,
                                                         SchematicSegment second, NetId net) {
    for (const auto junction_id : sheet.junctions()) {
        const auto &junction = schematic.junction(junction_id);
        if (junction.net() == net && point_on_schematic_segment(junction.position(), first) &&
            point_on_schematic_segment(junction.position(), second)) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] inline bool point_inside_sheet(const Sheet &sheet, Point point) noexcept {
    const auto size = sheet.metadata().size();
    return point.x() >= 0.0 && point.y() >= 0.0 && point.x() <= size.width() &&
           point.y() <= size.height();
}

[[nodiscard]] inline SchematicBounds transform_symbol_point_bounds(Point point,
                                                                   const SymbolInstance &instance) {
    return bounds_from_point(
        transform_schematic_point(point, instance.position(), instance.orientation()));
}

[[nodiscard]] inline SchematicBounds symbol_primitive_bounds(const SymbolPrimitive &primitive,
                                                             const SymbolInstance &instance) {
    if (std::holds_alternative<SymbolLine>(primitive)) {
        const auto &line = std::get<SymbolLine>(primitive);
        auto bounds = transform_symbol_point_bounds(line.start(), instance);
        include_bounds(bounds, transform_symbol_point_bounds(line.end(), instance));
        return padded_bounds(bounds, 0.5);
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
            0.5);
    }
    if (std::holds_alternative<SymbolCircle>(primitive)) {
        const auto &circle = std::get<SymbolCircle>(primitive);
        return transform_rect_bounds(
            circle.center().x() - circle.radius(), circle.center().y() - circle.radius(),
            circle.center().x() + circle.radius(), circle.center().y() + circle.radius(),
            instance.position(), instance.orientation());
    }
    if (std::holds_alternative<SymbolArc>(primitive)) {
        const auto &arc = std::get<SymbolArc>(primitive);
        return transform_rect_bounds(
            arc.center().x() - arc.radius(), arc.center().y() - arc.radius(),
            arc.center().x() + arc.radius(), arc.center().y() + arc.radius(), instance.position(),
            instance.orientation());
    }

    const auto &text = std::get<SymbolText>(primitive);
    const auto anchor =
        transform_schematic_point(text.anchor(), instance.position(), instance.orientation());
    return text_bounds(anchor,
                       combined_text_orientation(instance.orientation(), text.orientation()),
                       text.text(), text.style(), symbol_text_rendered_font_size);
}

[[nodiscard]] inline SchematicBounds symbol_instance_bounds(const Schematic &schematic,
                                                            SymbolInstanceId id) {
    const auto &instance = schematic.symbol_instance(id);
    const auto &symbol = schematic.symbol_definition(instance.symbol_definition());
    auto bounds = bounds_from_point(instance.position());
    for (const auto &pin : symbol.pins()) {
        include_bounds(bounds, transform_symbol_point_bounds(pin.anchor(), instance));
    }
    for (const auto &primitive : symbol.primitives()) {
        include_bounds(bounds, symbol_primitive_bounds(primitive, instance));
    }
    return bounds;
}

[[nodiscard]] inline SchematicBounds symbol_instance_geometry_bounds(const Schematic &schematic,
                                                                     SymbolInstanceId id) {
    const auto &instance = schematic.symbol_instance(id);
    const auto &symbol = schematic.symbol_definition(instance.symbol_definition());
    auto bounds = bounds_from_point(instance.position());
    for (const auto &pin : symbol.pins()) {
        include_bounds(bounds, transform_symbol_point_bounds(pin.anchor(), instance));
    }
    for (const auto &primitive : symbol.primitives()) {
        include_bounds(bounds, symbol_primitive_bounds(primitive, instance));
    }
    return bounds;
}

[[nodiscard]] inline std::optional<SchematicBounds>
symbol_instance_body_bounds(const Schematic &schematic, SymbolInstanceId id) {
    const auto &instance = schematic.symbol_instance(id);
    const auto &symbol = schematic.symbol_definition(instance.symbol_definition());
    const auto has_body_primitive = std::any_of(
        symbol.primitives().begin(), symbol.primitives().end(), [](const auto &primitive) {
            return std::holds_alternative<SymbolRectangle>(primitive) ||
                   std::holds_alternative<SymbolCircle>(primitive) ||
                   std::holds_alternative<SymbolArc>(primitive);
        });
    const auto has_line_body_primitive = std::any_of(
        symbol.primitives().begin(), symbol.primitives().end(), [](const auto &primitive) {
            return std::holds_alternative<SymbolLine>(primitive) &&
                   std::get<SymbolLine>(primitive).role() == SymbolLineRole::Normal;
        });
    auto bounds = std::optional<SchematicBounds>{};
    for (const auto &primitive : symbol.primitives()) {
        if (std::holds_alternative<SymbolText>(primitive)) {
            continue;
        }
        if (has_body_primitive && std::holds_alternative<SymbolLine>(primitive)) {
            continue;
        }
        if (!has_body_primitive && has_line_body_primitive &&
            std::holds_alternative<SymbolLine>(primitive) &&
            std::get<SymbolLine>(primitive).role() != SymbolLineRole::Normal) {
            continue;
        }
        const auto primitive_bounds = symbol_primitive_bounds(primitive, instance);
        if (bounds.has_value()) {
            include_bounds(bounds.value(), primitive_bounds);
        } else {
            bounds = primitive_bounds;
        }
    }
    return bounds;
}

[[nodiscard]] inline bool symbol_instances_share_same_net_pin_point(const Schematic &schematic,
                                                                    SymbolInstanceId first_id,
                                                                    SymbolInstanceId second_id) {
    const auto &circuit = schematic.circuit();
    const auto &first = schematic.symbol_instance(first_id);
    const auto &second = schematic.symbol_instance(second_id);
    const auto &first_symbol = schematic.symbol_definition(first.symbol_definition());
    const auto &second_symbol = schematic.symbol_definition(second.symbol_definition());
    for (const auto &first_pin : first_symbol.pins()) {
        const auto first_concrete_pin =
            circuit.pin_by_number(first.component(), first_pin.number());
        if (!first_concrete_pin.has_value()) {
            continue;
        }
        const auto first_net = circuit.net_of(first_concrete_pin.value());
        if (!first_net.has_value()) {
            continue;
        }
        const auto first_point =
            transform_schematic_point(first_pin.anchor(), first.position(), first.orientation());
        for (const auto &second_pin : second_symbol.pins()) {
            const auto second_concrete_pin =
                circuit.pin_by_number(second.component(), second_pin.number());
            if (!second_concrete_pin.has_value()) {
                continue;
            }
            const auto second_net = circuit.net_of(second_concrete_pin.value());
            if (!second_net.has_value() || second_net.value() != first_net.value()) {
                continue;
            }
            const auto second_point = transform_schematic_point(
                second_pin.anchor(), second.position(), second.orientation());
            if (same_schematic_point(first_point, second_point)) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] inline bool symbol_overlap_is_shared_pin_contact(const Schematic &schematic,
                                                               SymbolInstanceId first_id,
                                                               SchematicBounds first_bounds,
                                                               SymbolInstanceId second_id,
                                                               SchematicBounds second_bounds) {
    constexpr auto contact_overlap_tolerance = 1.25;
    if (!symbol_instances_share_same_net_pin_point(schematic, first_id, second_id)) {
        return false;
    }
    return bounds_overlap_width(first_bounds, second_bounds) <= contact_overlap_tolerance ||
           bounds_overlap_height(first_bounds, second_bounds) <= contact_overlap_tolerance;
}

[[nodiscard]] inline SchematicBounds wire_run_bounds(const WireRun &wire) {
    auto bounds = bounds_from_point(wire.points().front());
    for (const auto point : wire.points()) {
        include_point(bounds, point);
    }
    return padded_bounds(bounds, 0.5);
}

[[nodiscard]] inline SchematicBounds segment_bounds(SchematicSegment segment) {
    auto bounds = bounds_from_point(segment.start());
    include_point(bounds, segment.end());
    return padded_bounds(bounds, 0.5);
}

[[nodiscard]] inline SchematicOrientation
power_port_glyph_orientation(PowerPortKind kind, SchematicOrientation orientation) {
    switch (kind) {
    case PowerPortKind::Power:
        switch (orientation) {
        case SchematicOrientation::Right:
            return SchematicOrientation::Down;
        case SchematicOrientation::Down:
            return SchematicOrientation::Left;
        case SchematicOrientation::Left:
            return SchematicOrientation::Up;
        case SchematicOrientation::Up:
            return SchematicOrientation::Right;
        }
        break;
    case PowerPortKind::Ground:
        switch (orientation) {
        case SchematicOrientation::Right:
            return SchematicOrientation::Up;
        case SchematicOrientation::Down:
            return SchematicOrientation::Right;
        case SchematicOrientation::Left:
            return SchematicOrientation::Down;
        case SchematicOrientation::Up:
            return SchematicOrientation::Left;
        }
        break;
    }
    throw std::logic_error{"Unhandled power port orientation"};
}

[[nodiscard]] inline Point transformed_port_anchor(const PowerPort &port, Point local_anchor) {
    return transform_schematic_point(local_anchor, port.position(),
                                     power_port_glyph_orientation(port.kind(), port.orientation()));
}

[[nodiscard]] inline SchematicBounds power_port_label_bounds(const PowerPort &port,
                                                             std::string_view label) {
    const auto label_y =
        port.kind() == PowerPortKind::Ground ? ground_port_label_offset : -power_port_label_offset;
    return text_bounds(
        port.explicit_label_position().value_or(transformed_port_anchor(port, Point{0.0, label_y})),
        SchematicOrientation::Right, label, sheet_port_rendered_label_font_size, true);
}

[[nodiscard]] inline SchematicBounds power_port_glyph_bounds(const PowerPort &port) {
    const auto glyph_orientation = power_port_glyph_orientation(port.kind(), port.orientation());
    return port.kind() == PowerPortKind::Ground
               ? transform_rect_bounds(-3.6, 0.0, 3.6, 6.0, port.position(), glyph_orientation)
               : transform_rect_bounds(-power_port_half_width, -power_port_tip_offset,
                                       power_port_half_width, 0.0, port.position(),
                                       glyph_orientation);
}

[[nodiscard]] inline SchematicBounds power_port_bounds(const PowerPort &port,
                                                       std::string_view label) {
    auto bounds = power_port_glyph_bounds(port);
    include_bounds(bounds, power_port_label_bounds(port, label));
    return bounds;
}

[[nodiscard]] inline SchematicBounds no_connect_marker_bounds(const NoConnectMarker &marker) {
    return transform_rect_bounds(-4.0, -4.0, 4.0, 4.0, marker.position(), marker.orientation());
}

[[nodiscard]] inline SchematicBounds
no_connect_marker_collision_bounds(const NoConnectMarker &marker) {
    return padded_bounds(no_connect_marker_bounds(marker), no_connect_marker_clearance);
}

[[nodiscard]] inline double sheet_port_rendered_body_length(std::string_view label) {
    const auto label_width = static_cast<double>(label.size()) *
                             sheet_port_rendered_label_font_size * rendered_text_width_factor;
    return std::max(sheet_port_rendered_min_body_length,
                    label_width + (sheet_port_rendered_label_padding * 2.0));
}

[[nodiscard]] inline Point transformed_port_anchor(const SheetPort &port, Point local_anchor) {
    return transform_schematic_point(local_anchor, port.position(), port.orientation());
}

[[nodiscard]] inline SchematicBounds sheet_port_label_bounds(const SheetPort &port) {
    const auto body_length = sheet_port_rendered_body_length(port.name());
    return text_bounds(transformed_port_anchor(port, Point{body_length * 0.5, 0.9}),
                       SchematicOrientation::Right, port.name(),
                       sheet_port_rendered_label_font_size, true);
}

[[nodiscard]] inline SchematicBounds sheet_port_bounds(const SheetPort &port) {
    const auto max_x =
        sheet_port_rendered_body_length(port.name()) + sheet_port_rendered_tip_length;
    auto bounds =
        transform_rect_bounds(0.0, -sheet_port_rendered_half_height, max_x,
                              sheet_port_rendered_half_height, port.position(), port.orientation());
    include_bounds(bounds, sheet_port_label_bounds(port));
    return bounds;
}

} // namespace detail

} // namespace volt
