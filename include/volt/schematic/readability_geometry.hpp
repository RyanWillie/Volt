#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <volt/circuit/definitions.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/schematic/presentation_geometry.hpp>
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
    /** Logical pin represented by this visual element, if applicable. */
    std::optional<PinId> pin = std::nullopt;
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

[[nodiscard]] inline SchematicBounds
no_connect_marker_collision_bounds(const NoConnectMarker &marker) {
    return padded_bounds(no_connect_marker_bounds(marker), no_connect_marker_clearance);
}

} // namespace detail

} // namespace volt
