#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <volt/circuit/definitions.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/schematic/schematic.hpp>

namespace volt {

namespace detail {

inline constexpr std::size_t repeated_label_warning_threshold = 4U;
inline constexpr std::size_t fragmented_pin_label_threshold = 3U;
inline constexpr std::size_t overlong_label_character_threshold = 24U;
inline constexpr std::size_t dense_no_connect_marker_threshold = 6U;
inline constexpr double dense_no_connect_cluster_radius = 18.0;
inline constexpr double title_block_width = 82.0;
inline constexpr double title_block_row_height = 6.0;
inline constexpr double conservative_text_character_width = 3.0;
inline constexpr double conservative_text_height = 6.0;

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
    /** Primary schematic entity referenced by diagnostics. */
    EntityRef entity;
    /** Related logical or schematic entities that explain the object. */
    std::vector<EntityRef> context;
    /** Conservative sheet-space bounds for the object. */
    SchematicBounds bounds;
    /** Sheet-local authored region index, if the object was placed through one. */
    std::optional<std::size_t> authored_region;
};

/** Text-like object geometry gathered for conservative collision checks. */
struct ReadabilityTextObject {
    /** Primary text entity referenced by diagnostics. */
    EntityRef entity;
    /** Related entities that explain the text object. */
    std::vector<EntityRef> context;
    /** Conservative sheet-space text bounds. */
    SchematicBounds bounds;
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

[[nodiscard]] inline SchematicBounds text_bounds(Point anchor, SchematicOrientation orientation,
                                                 const std::string &text, bool centered) {
    const auto width =
        std::max(conservative_text_character_width,
                 conservative_text_character_width * static_cast<double>(text.size()));
    const auto min_x = centered ? -width / 2.0 : 0.0;
    const auto max_x = centered ? width / 2.0 : width;
    return transform_rect_bounds(min_x, -conservative_text_height, max_x, 1.0, anchor, orientation);
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
    return text_bounds(anchor, instance.orientation(), text.text(), true);
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
    const auto &component = schematic.circuit().component(instance.component());
    include_bounds(bounds,
                   text_bounds(transform_schematic_point(Point{0.0, -12.0}, instance.position(),
                                                         instance.orientation()),
                               instance.orientation(), component.reference().value(), true));
    return bounds;
}

[[nodiscard]] inline SchematicBounds wire_run_bounds(const WireRun &wire) {
    auto bounds = bounds_from_point(wire.points().front());
    for (const auto point : wire.points()) {
        include_point(bounds, point);
    }
    return padded_bounds(bounds, 0.5);
}

[[nodiscard]] inline SchematicBounds power_port_bounds(const PowerPort &port) {
    return transform_rect_bounds(-7.0, -16.0, 7.0, 9.0, port.position(), port.orientation());
}

[[nodiscard]] inline SchematicBounds no_connect_marker_bounds(const NoConnectMarker &marker) {
    return transform_rect_bounds(-4.0, -4.0, 4.0, 4.0, marker.position(), marker.orientation());
}

[[nodiscard]] inline SchematicBounds sheet_port_bounds(const SheetPort &port) {
    return transform_rect_bounds(0.0, -6.0, 22.0, 6.0, port.position(), port.orientation());
}

inline void add_readability_diagnostic(DiagnosticReport &report, Severity severity,
                                       const std::string &code, std::string message,
                                       SheetId sheet_id, EntityRef object,
                                       const std::vector<EntityRef> &context) {
    auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id), object};
    refs.insert(refs.end(), context.begin(), context.end());
    report.add(Diagnostic{severity, DiagnosticCode{code}, std::move(message), std::move(refs)});
}

inline void add_outside_sheet_diagnostic(DiagnosticReport &report, SheetId sheet_id,
                                         EntityRef object, std::vector<EntityRef> entities) {
    auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id), object};
    refs.insert(refs.end(), entities.begin(), entities.end());
    report.add(Diagnostic{
        Severity::Warning,
        DiagnosticCode{"SCHEMATIC_OBJECT_OUTSIDE_SHEET_BOUNDS"},
        "Schematic object is outside the sheet drawing bounds",
        std::move(refs),
    });
}

inline void validate_component_placement_coverage(const Schematic &schematic, const Sheet &sheet,
                                                  SheetId sheet_id, SymbolInstanceId instance_id,
                                                  DiagnosticReport &report) {
    const auto &circuit = schematic.circuit();
    const auto &instance = schematic.symbol_instance(instance_id);
    const auto &component = circuit.component(instance.component());
    const auto &symbol = schematic.symbol_definition(instance.symbol_definition());

    for (const auto &symbol_pin : symbol.pins()) {
        const auto pin = circuit.pin_by_number(instance.component(), symbol_pin.number());
        if (!pin.has_value()) {
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"SCHEMATIC_SYMBOL_PIN_NOT_ON_COMPONENT"},
                "Schematic symbol pin " + symbol_pin.number() + " does not map to component " +
                    component.reference().value(),
                std::vector{EntityRef::component(instance.component()),
                            EntityRef::symbol_def(instance.symbol_definition()),
                            EntityRef::symbol_instance(instance_id)},
            });
        }
    }

    for (const auto pin_id : circuit.pins_for(instance.component())) {
        const auto &pin_instance = circuit.pin(pin_id);
        const auto pin_def_id = pin_instance.definition();
        const auto net = circuit.net_of(pin_id);
        if (!net.has_value() || schematic_readiness_exempts_pin(circuit, pin_id, pin_def_id)) {
            continue;
        }
        const auto &pin_definition = circuit.pin_definition(pin_def_id);
        const auto symbol_pin = symbol_pin_by_number(symbol, pin_definition.number());
        if (!symbol_pin.has_value()) {
            const auto &net_model = circuit.net(net.value());
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"SCHEMATIC_CONNECTED_PIN_MISSING_SYMBOL_PIN"},
                "Schematic symbol for " + component.reference().value() + " omits connected pin " +
                    pin_definition.number() + " (" + pin_definition.name() + ") on " +
                    net_model.name().value(),
                std::vector{EntityRef::component(instance.component()), EntityRef::pin(pin_id),
                            EntityRef::pin_def(pin_def_id), EntityRef::net(net.value()),
                            EntityRef::symbol_instance(instance_id)},
            });
            continue;
        }

        const auto pin_point = transform_schematic_point(
            symbol_pin.value().anchor(), instance.position(), instance.orientation());
        if (sheet_visually_covers_net_at_pin(schematic, sheet, net.value(), pin_point)) {
            continue;
        }

        const auto &net_model = circuit.net(net.value());
        report.add(Diagnostic{
            Severity::Error,
            DiagnosticCode{"SCHEMATIC_PIN_NET_NOT_VISUALLY_COVERED"},
            "Schematic sheet '" + sheet.name() + "' omits visual net coverage for " +
                component.reference().value() + " pin " + pin_definition.number() + " (" +
                pin_definition.name() + ") on " + net_model.name().value(),
            std::vector{EntityRef::sheet(sheet_id), EntityRef::component(instance.component()),
                        EntityRef::pin(pin_id), EntityRef::pin_def(pin_def_id),
                        EntityRef::net(net.value())},
        });
    }
}

inline void validate_component_placements(const Schematic &schematic, DiagnosticReport &report) {
    const auto &circuit = schematic.circuit();
    for (std::size_t component_index = 0; component_index < circuit.component_count();
         ++component_index) {
        const auto component_id = ComponentId{component_index};
        const auto placements = symbol_instances_for_component(schematic, component_id);
        if (placements.empty()) {
            if (component_is_schematic_relevant(circuit, component_id)) {
                const auto &component = circuit.component(component_id);
                report.add(Diagnostic{
                    Severity::Error,
                    DiagnosticCode{"SCHEMATIC_COMPONENT_NOT_PLACED"},
                    "Schematic omits placement for " + component.reference().value(),
                    std::vector{EntityRef::component(component_id),
                                EntityRef::component_def(component.definition())},
                });
            }
            continue;
        }

        if (placements.size() > 1U) {
            auto refs = std::vector<EntityRef>{EntityRef::component(component_id)};
            for (const auto instance_id : placements) {
                refs.push_back(EntityRef::symbol_instance(instance_id));
            }
            const auto &component = circuit.component(component_id);
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"SCHEMATIC_COMPONENT_DUPLICATE_PLACEMENT"},
                "Schematic places " + component.reference().value() +
                    " more than once; split placements are not supported yet",
                std::move(refs),
            });
        }
    }
}

inline void validate_repeated_labels(const Schematic &schematic, SheetId sheet_id,
                                     const Sheet &sheet, DiagnosticReport &report) {
    const auto &circuit = schematic.circuit();
    for (std::size_t net_index = 0; net_index < circuit.net_count(); ++net_index) {
        const auto net_id = NetId{net_index};
        auto count = std::size_t{0};
        for (const auto label_id : sheet.net_labels()) {
            if (schematic.net_label(label_id).net() == net_id) {
                ++count;
            }
        }
        if (count > repeated_label_warning_threshold) {
            const auto &net = circuit.net(net_id);
            report.add(Diagnostic{
                Severity::Warning,
                DiagnosticCode{"SCHEMATIC_REPEATED_NET_LABELS"},
                "Schematic sheet repeats label " + net.name().value() + " " +
                    std::to_string(count) + " times",
                std::vector{EntityRef::sheet(sheet_id), EntityRef::net(net_id)},
            });
        }
    }
}

inline void validate_fragmented_pin_labels_for_net(const Schematic &schematic, SheetId sheet_id,
                                                   const Sheet &sheet, NetId net_id,
                                                   DiagnosticReport &report) {
    const auto &circuit = schematic.circuit();
    const auto &net = circuit.net(net_id);
    if (net.pins().size() < fragmented_pin_label_threshold) {
        return;
    }

    auto labelled_pin_anchors = std::size_t{0};
    for (const auto instance_id : sheet.symbol_instances()) {
        const auto &instance = schematic.symbol_instance(instance_id);
        const auto &symbol = schematic.symbol_definition(instance.symbol_definition());
        for (const auto &symbol_pin : symbol.pins()) {
            const auto pin = circuit.pin_by_number(instance.component(), symbol_pin.number());
            if (!pin.has_value()) {
                continue;
            }
            const auto pin_net = circuit.net_of(pin.value());
            if (!pin_net.has_value() || pin_net.value() != net_id) {
                continue;
            }
            const auto pin_point = transform_schematic_point(
                symbol_pin.anchor(), instance.position(), instance.orientation());
            if (sheet_has_net_label_at_point(schematic, sheet, net_id, pin_point)) {
                ++labelled_pin_anchors;
            }
        }
    }

    if (labelled_pin_anchors >= fragmented_pin_label_threshold) {
        report.add(Diagnostic{
            Severity::Warning,
            DiagnosticCode{"SCHEMATIC_NET_FRAGMENTED_PIN_LABELS"},
            "Schematic covers " + net.name().value() +
                " with repeated pin-local labels instead of readable shared routing",
            std::vector{EntityRef::sheet(sheet_id), EntityRef::net(net_id)},
        });
    }
}

inline void validate_fragmented_pin_labels(const Schematic &schematic, SheetId sheet_id,
                                           const Sheet &sheet, DiagnosticReport &report) {
    const auto &circuit = schematic.circuit();
    for (std::size_t net_index = 0; net_index < circuit.net_count(); ++net_index) {
        validate_fragmented_pin_labels_for_net(schematic, sheet_id, sheet, NetId{net_index},
                                               report);
    }
}

inline void validate_outside_sheet_objects(const Schematic &schematic, SheetId sheet_id,
                                           const Sheet &sheet, DiagnosticReport &report) {
    for (const auto wire_id : sheet.wire_runs()) {
        const auto &wire = schematic.wire_run(wire_id);
        for (const auto point : wire.points()) {
            if (!point_inside_sheet(sheet, point)) {
                add_outside_sheet_diagnostic(report, sheet_id, EntityRef::wire_run(wire_id),
                                             std::vector{EntityRef::net(wire.net())});
                break;
            }
        }
    }

    for (const auto label_id : sheet.net_labels()) {
        const auto &label = schematic.net_label(label_id);
        if (!point_inside_sheet(sheet, label.position())) {
            add_outside_sheet_diagnostic(report, sheet_id, EntityRef::net_label(label_id),
                                         std::vector{EntityRef::net(label.net())});
        }
    }

    for (const auto junction_id : sheet.junctions()) {
        const auto &junction = schematic.junction(junction_id);
        if (!point_inside_sheet(sheet, junction.position())) {
            add_outside_sheet_diagnostic(report, sheet_id, EntityRef::junction(junction_id),
                                         std::vector{EntityRef::net(junction.net())});
        }
    }

    for (const auto port_id : sheet.power_ports()) {
        const auto &port = schematic.power_port(port_id);
        if (!point_inside_sheet(sheet, port.position())) {
            add_outside_sheet_diagnostic(report, sheet_id, EntityRef::power_port(port_id),
                                         std::vector{EntityRef::net(port.net())});
        }
    }

    for (const auto marker_id : sheet.no_connect_markers()) {
        const auto &marker = schematic.no_connect_marker(marker_id);
        if (!point_inside_sheet(sheet, marker.position())) {
            add_outside_sheet_diagnostic(report, sheet_id, EntityRef::no_connect_marker(marker_id),
                                         std::vector{EntityRef::pin(marker.pin())});
        }
    }

    for (const auto port_id : sheet.sheet_ports()) {
        const auto &port = schematic.sheet_port(port_id);
        if (!point_inside_sheet(sheet, port.position())) {
            add_outside_sheet_diagnostic(report, sheet_id, EntityRef::sheet_port(port_id),
                                         std::vector{EntityRef::net(port.net())});
        }
    }

    for (const auto field_id : sheet.symbol_fields()) {
        const auto &field = schematic.symbol_field(field_id);
        if (!point_inside_sheet(sheet, field.position())) {
            add_outside_sheet_diagnostic(
                report, sheet_id, EntityRef::symbol_field(field_id),
                std::vector{EntityRef::symbol_instance(field.symbol_instance())});
        }
    }
}

inline void validate_same_net_crossings(const Schematic &schematic, SheetId sheet_id,
                                        const Sheet &sheet, DiagnosticReport &report) {
    const auto &wires = sheet.wire_runs();
    for (std::size_t first_index = 0; first_index < wires.size(); ++first_index) {
        const auto first_id = wires[first_index];
        const auto &first = schematic.wire_run(first_id);
        for (std::size_t second_index = first_index + 1U; second_index < wires.size();
             ++second_index) {
            const auto second_id = wires[second_index];
            const auto &second = schematic.wire_run(second_id);
            if (first.net() != second.net()) {
                continue;
            }
            for (std::size_t first_point = 1; first_point < first.points().size(); ++first_point) {
                const auto first_segment =
                    SchematicSegment{first.points()[first_point - 1U], first.points()[first_point]};
                for (std::size_t second_point = 1; second_point < second.points().size();
                     ++second_point) {
                    const auto second_segment = SchematicSegment{second.points()[second_point - 1U],
                                                                 second.points()[second_point]};
                    if (classify_segment_relationship(first_segment, second_segment) !=
                        SchematicSegmentRelationship::Crossing) {
                        continue;
                    }
                    if (sheet_has_junction_on_segments(schematic, sheet, first_segment,
                                                       second_segment, first.net())) {
                        continue;
                    }
                    report.add(Diagnostic{
                        Severity::Warning,
                        DiagnosticCode{"SCHEMATIC_WIRE_CROSSING_WITHOUT_JUNCTION"},
                        "Same-net schematic wires cross without an explicit junction",
                        std::vector{EntityRef::sheet(sheet_id), EntityRef::wire_run(first_id),
                                    EntityRef::wire_run(second_id), EntityRef::net(first.net())},
                    });
                }
            }
        }
    }
}

[[nodiscard]] inline bool schematic_has_no_connect_marker_for_pin(const Schematic &schematic,
                                                                  PinId pin) {
    for (std::size_t sheet_index = 0; sheet_index < schematic.sheet_count(); ++sheet_index) {
        const auto &sheet = schematic.sheet(SheetId{sheet_index});
        for (const auto marker_id : sheet.no_connect_markers()) {
            if (schematic.no_connect_marker(marker_id).pin() == pin) {
                return true;
            }
        }
    }

    return false;
}

inline void validate_no_connect_markers(const Schematic &schematic, SheetId sheet_id,
                                        const Sheet &sheet, DiagnosticReport &report) {
    const auto &circuit = schematic.circuit();
    for (const auto marker_id : sheet.no_connect_markers()) {
        const auto &marker = schematic.no_connect_marker(marker_id);
        const auto &pin = circuit.pin(marker.pin());
        const auto net = circuit.net_of(marker.pin());
        if (net.has_value()) {
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"SCHEMATIC_NO_CONNECT_MARKER_ON_CONNECTED_PIN"},
                "Schematic no-connect marker is attached to a connected pin",
                std::vector{EntityRef::no_connect_marker(marker_id), EntityRef::pin(marker.pin()),
                            EntityRef::component(pin.component()), EntityRef::net(net.value())},
            });
            continue;
        }
        if (!schematic_readiness_exempts_pin(circuit, marker.pin(), pin.definition())) {
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"SCHEMATIC_NO_CONNECT_MARKER_WITHOUT_INTENT"},
                "Schematic no-connect marker has no matching kernel no-connect intent",
                std::vector{EntityRef::no_connect_marker(marker_id), EntityRef::pin(marker.pin()),
                            EntityRef::component(pin.component())},
            });
        }
    }

    static_cast<void>(sheet_id);
}

inline void validate_missing_no_connect_markers(const Schematic &schematic,
                                                DiagnosticReport &report) {
    const auto &circuit = schematic.circuit();
    for (const auto pin_id : circuit.intentional_no_connect_pins()) {
        if (schematic_has_no_connect_marker_for_pin(schematic, pin_id)) {
            continue;
        }
        const auto &pin = circuit.pin(pin_id);
        report.add(Diagnostic{
            Severity::Warning,
            DiagnosticCode{"SCHEMATIC_NO_CONNECT_INTENT_NOT_MARKED"},
            "Kernel no-connect intent is not shown by a schematic no-connect marker",
            std::vector{EntityRef::pin(pin_id), EntityRef::component(pin.component()),
                        EntityRef::pin_def(pin.definition())},
        });
    }
}

[[nodiscard]] inline std::vector<ReadabilityObject>
readability_objects_for_sheet(const Schematic &schematic, const Sheet &sheet) {
    auto objects = std::vector<ReadabilityObject>{};
    for (const auto instance_id : sheet.symbol_instances()) {
        const auto &instance = schematic.symbol_instance(instance_id);
        objects.push_back(ReadabilityObject{EntityRef::symbol_instance(instance_id),
                                            std::vector{EntityRef::component(instance.component())},
                                            symbol_instance_bounds(schematic, instance_id),
                                            instance.authored_region()});
    }
    for (const auto wire_id : sheet.wire_runs()) {
        const auto &wire = schematic.wire_run(wire_id);
        objects.push_back(ReadabilityObject{EntityRef::wire_run(wire_id),
                                            std::vector{EntityRef::net(wire.net())},
                                            wire_run_bounds(wire), wire.authored_region()});
    }
    for (const auto label_id : sheet.net_labels()) {
        const auto &label = schematic.net_label(label_id);
        const auto &net = schematic.circuit().net(label.net());
        objects.push_back(ReadabilityObject{
            EntityRef::net_label(label_id), std::vector{EntityRef::net(label.net())},
            text_bounds(label.position(), label.orientation(), net.name().value(), false),
            label.authored_region()});
    }
    for (const auto junction_id : sheet.junctions()) {
        const auto &junction = schematic.junction(junction_id);
        objects.push_back(ReadabilityObject{
            EntityRef::junction(junction_id),
            std::vector{EntityRef::net(junction.net())},
            padded_bounds(bounds_from_point(junction.position()), 1.8),
            junction.authored_region(),
        });
    }
    for (const auto port_id : sheet.power_ports()) {
        const auto &port = schematic.power_port(port_id);
        objects.push_back(ReadabilityObject{EntityRef::power_port(port_id),
                                            std::vector{EntityRef::net(port.net())},
                                            power_port_bounds(port), port.authored_region()});
    }
    for (const auto marker_id : sheet.no_connect_markers()) {
        const auto &marker = schematic.no_connect_marker(marker_id);
        objects.push_back(ReadabilityObject{
            EntityRef::no_connect_marker(marker_id), std::vector{EntityRef::pin(marker.pin())},
            no_connect_marker_bounds(marker), marker.authored_region()});
    }
    for (const auto port_id : sheet.sheet_ports()) {
        const auto &port = schematic.sheet_port(port_id);
        objects.push_back(ReadabilityObject{EntityRef::sheet_port(port_id),
                                            std::vector{EntityRef::net(port.net())},
                                            sheet_port_bounds(port), port.authored_region()});
    }
    for (const auto field_id : sheet.symbol_fields()) {
        const auto &field = schematic.symbol_field(field_id);
        objects.push_back(ReadabilityObject{
            EntityRef::symbol_field(field_id),
            std::vector{EntityRef::symbol_instance(field.symbol_instance())},
            text_bounds(field.position(), field.orientation(), field.value(), true),
            field.authored_region()});
    }
    return objects;
}

inline void validate_readability_bounds(const Schematic &schematic, SheetId sheet_id,
                                        const Sheet &sheet, DiagnosticReport &report) {
    const auto area = drawing_area_bounds(sheet.metadata());
    const auto title = title_block_bounds(sheet.metadata());
    for (const auto &object : readability_objects_for_sheet(schematic, sheet)) {
        if (!contains_bounds(area, object.bounds)) {
            add_readability_diagnostic(
                report, Severity::Error, "SCHEMATIC_OBJECT_OUTSIDE_USABLE_AREA",
                "Schematic object is outside the usable drawing area or reserved border strips",
                sheet_id, object.entity, object.context);
        }
        if (intersects_bounds(title, object.bounds)) {
            add_readability_diagnostic(report, Severity::Warning,
                                       "SCHEMATIC_OBJECT_OVERLAPS_TITLE_BLOCK",
                                       "Schematic object overlaps the reserved title block",
                                       sheet_id, object.entity, object.context);
        }
        if (object.authored_region.has_value()) {
            const auto &region = sheet.region(object.authored_region.value());
            if (!contains_bounds(region_bounds(region), object.bounds)) {
                add_readability_diagnostic(report, Severity::Error,
                                           "SCHEMATIC_OBJECT_OUTSIDE_AUTHORED_REGION",
                                           "Schematic object authored through region '" +
                                               region.name() + "' extends outside that region",
                                           sheet_id, object.entity, object.context);
            }
        }
    }
}

[[nodiscard]] inline bool display_label_is_overlong_or_scoped(const std::string &label) {
    return label.size() > overlong_label_character_threshold ||
           label.find('/') != std::string::npos || label.find("::") != std::string::npos;
}

inline void validate_label_readability(const Schematic &schematic, SheetId sheet_id,
                                       const Sheet &sheet, DiagnosticReport &report) {
    for (const auto label_id : sheet.net_labels()) {
        const auto &label = schematic.net_label(label_id);
        const auto &net = schematic.circuit().net(label.net());
        if (label.orientation() != SchematicOrientation::Right) {
            add_readability_diagnostic(
                report, Severity::Warning, "SCHEMATIC_TEXT_NOT_HORIZONTAL",
                "Schematic net label is rotated where horizontal text is expected", sheet_id,
                EntityRef::net_label(label_id), std::vector{EntityRef::net(label.net())});
        }
        if (display_label_is_overlong_or_scoped(net.name().value())) {
            add_readability_diagnostic(
                report, Severity::Warning, "SCHEMATIC_OVERLONG_DISPLAY_LABEL",
                "Schematic display label is long or exposes an internal scoped name", sheet_id,
                EntityRef::net_label(label_id), std::vector{EntityRef::net(label.net())});
        }
    }
    for (const auto field_id : sheet.symbol_fields()) {
        const auto &field = schematic.symbol_field(field_id);
        if (field.orientation() != SchematicOrientation::Right) {
            add_readability_diagnostic(
                report, Severity::Warning, "SCHEMATIC_TEXT_NOT_HORIZONTAL",
                "Schematic symbol field is rotated where horizontal text is expected", sheet_id,
                EntityRef::symbol_field(field_id),
                std::vector{EntityRef::symbol_instance(field.symbol_instance())});
        }
        if (display_label_is_overlong_or_scoped(field.value())) {
            add_readability_diagnostic(
                report, Severity::Warning, "SCHEMATIC_OVERLONG_DISPLAY_LABEL",
                "Schematic symbol field is long or exposes an internal scoped name", sheet_id,
                EntityRef::symbol_field(field_id),
                std::vector{EntityRef::symbol_instance(field.symbol_instance())});
        }
    }
    for (const auto port_id : sheet.sheet_ports()) {
        const auto &port = schematic.sheet_port(port_id);
        if (display_label_is_overlong_or_scoped(port.name())) {
            add_readability_diagnostic(
                report, Severity::Warning, "SCHEMATIC_OVERLONG_DISPLAY_LABEL",
                "Schematic sheet port label is long or exposes an internal scoped name", sheet_id,
                EntityRef::sheet_port(port_id), std::vector{EntityRef::net(port.net())});
        }
    }
}

inline void validate_duplicate_junctions(const Schematic &schematic, SheetId sheet_id,
                                         const Sheet &sheet, DiagnosticReport &report) {
    const auto &junctions = sheet.junctions();
    for (std::size_t first_index = 0; first_index < junctions.size(); ++first_index) {
        const auto first_id = junctions[first_index];
        const auto &first = schematic.junction(first_id);
        for (std::size_t second_index = first_index + 1U; second_index < junctions.size();
             ++second_index) {
            const auto second_id = junctions[second_index];
            const auto &second = schematic.junction(second_id);
            if (first.net() == second.net() &&
                same_schematic_point(first.position(), second.position())) {
                report.add(Diagnostic{
                    Severity::Warning,
                    DiagnosticCode{"SCHEMATIC_DUPLICATE_JUNCTION_MARKERS"},
                    "Schematic contains duplicate junction markers at the same point for one net",
                    std::vector{EntityRef::sheet(sheet_id), EntityRef::net(first.net()),
                                EntityRef::junction(first_id), EntityRef::junction(second_id)},
                });
            }
        }
    }
}

[[nodiscard]] inline bool component_definition_is_passive(const Circuit &circuit,
                                                          ComponentDefId definition_id) {
    const auto &definition = circuit.component_definition(definition_id);
    if (definition.pins().empty()) {
        return false;
    }
    return std::all_of(definition.pins().begin(), definition.pins().end(),
                       [&circuit](PinDefId pin_definition) {
                           return circuit.pin_definition(pin_definition).role() == PinRole::Passive;
                       });
}

[[nodiscard]] inline bool component_has_known_value(const ComponentInstance &component) {
    if (component.properties().contains(PropertyKey{"value"}) ||
        component.properties().contains(PropertyKey{"Value"})) {
        return true;
    }
    static const auto known_values = std::vector<ElectricalAttributeName>{
        ElectricalAttributeName{"resistance"}, ElectricalAttributeName{"capacitance"},
        ElectricalAttributeName{"inductance"}, ElectricalAttributeName{"voltage"},
        ElectricalAttributeName{"current"},    ElectricalAttributeName{"power"}};
    const auto &attributes = component.electrical_attributes();
    return std::any_of(
        known_values.begin(), known_values.end(),
        [&attributes](const ElectricalAttributeName &name) { return attributes.contains(name); });
}

[[nodiscard]] inline bool symbol_instance_has_value_field(const Schematic &schematic,
                                                          const Sheet &sheet,
                                                          SymbolInstanceId instance) {
    for (const auto field_id : sheet.symbol_fields()) {
        const auto &field = schematic.symbol_field(field_id);
        if (field.symbol_instance() == instance &&
            (field.name() == "value" || field.name() == "Value")) {
            return true;
        }
    }
    return false;
}

inline void validate_missing_passive_value_fields(const Schematic &schematic, SheetId sheet_id,
                                                  const Sheet &sheet, DiagnosticReport &report) {
    const auto &circuit = schematic.circuit();
    for (const auto instance_id : sheet.symbol_instances()) {
        const auto &instance = schematic.symbol_instance(instance_id);
        const auto &component = circuit.component(instance.component());
        if (!component_definition_is_passive(circuit, component.definition()) ||
            !component_has_known_value(component) ||
            symbol_instance_has_value_field(schematic, sheet, instance_id)) {
            continue;
        }
        report.add(Diagnostic{
            Severity::Warning,
            DiagnosticCode{"SCHEMATIC_PASSIVE_VALUE_FIELD_MISSING"},
            "Passive component has a known value but no visible schematic value field",
            std::vector{EntityRef::sheet(sheet_id), EntityRef::component(instance.component()),
                        EntityRef::symbol_instance(instance_id)},
        });
    }
}

[[nodiscard]] inline double squared_distance(Point lhs, Point rhs) noexcept {
    const auto dx = lhs.x() - rhs.x();
    const auto dy = lhs.y() - rhs.y();
    return (dx * dx) + (dy * dy);
}

inline void validate_dense_no_connect_clusters(const Schematic &schematic, SheetId sheet_id,
                                               const Sheet &sheet, DiagnosticReport &report) {
    const auto radius_squared = dense_no_connect_cluster_radius * dense_no_connect_cluster_radius;
    for (const auto instance_id : sheet.symbol_instances()) {
        const auto &instance = schematic.symbol_instance(instance_id);
        auto markers = std::vector<NoConnectMarkerId>{};
        for (const auto marker_id : sheet.no_connect_markers()) {
            const auto &marker = schematic.no_connect_marker(marker_id);
            const auto &pin = schematic.circuit().pin(marker.pin());
            if (pin.component() == instance.component() &&
                squared_distance(marker.position(), instance.position()) <= radius_squared) {
                markers.push_back(marker_id);
            }
        }
        if (markers.size() < dense_no_connect_marker_threshold) {
            continue;
        }
        auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id),
                                           EntityRef::symbol_instance(instance_id)};
        for (const auto marker_id : markers) {
            refs.push_back(EntityRef::no_connect_marker(marker_id));
        }
        report.add(Diagnostic{
            Severity::Warning,
            DiagnosticCode{"SCHEMATIC_DENSE_NO_CONNECT_MARKERS"},
            "Dense no-connect marker cluster may make the symbol hard to read",
            std::move(refs),
        });
    }
}

inline void validate_text_collisions(const Schematic &schematic, SheetId sheet_id,
                                     const Sheet &sheet, DiagnosticReport &report) {
    auto texts = std::vector<ReadabilityTextObject>{};
    for (const auto label_id : sheet.net_labels()) {
        const auto &label = schematic.net_label(label_id);
        const auto &net = schematic.circuit().net(label.net());
        texts.push_back(ReadabilityTextObject{
            EntityRef::net_label(label_id), std::vector{EntityRef::net(label.net())},
            text_bounds(label.position(), label.orientation(), net.name().value(), false)});
    }
    for (const auto field_id : sheet.symbol_fields()) {
        const auto &field = schematic.symbol_field(field_id);
        texts.push_back(ReadabilityTextObject{
            EntityRef::symbol_field(field_id),
            std::vector{EntityRef::symbol_instance(field.symbol_instance())},
            text_bounds(field.position(), field.orientation(), field.value(), true)});
    }

    for (std::size_t first = 0; first < texts.size(); ++first) {
        for (std::size_t second = first + 1U; second < texts.size(); ++second) {
            if (!intersects_bounds(texts[first].bounds, texts[second].bounds)) {
                continue;
            }
            auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id), texts[first].entity,
                                               texts[second].entity};
            refs.insert(refs.end(), texts[first].context.begin(), texts[first].context.end());
            refs.insert(refs.end(), texts[second].context.begin(), texts[second].context.end());
            report.add(Diagnostic{
                Severity::Warning,
                DiagnosticCode{"SCHEMATIC_TEXT_COLLISION"},
                "Conservative schematic text bounds overlap; review label placement",
                std::move(refs),
            });
        }
    }
}

} // namespace detail

/** Validate that placed connected schematic pins have visible net geometry on their sheet. */
[[nodiscard]] inline DiagnosticReport validate_schematic_readiness(const Schematic &schematic) {
    auto report = DiagnosticReport{};

    detail::validate_component_placements(schematic, report);

    for (std::size_t sheet_index = 0; sheet_index < schematic.sheet_count(); ++sheet_index) {
        const auto sheet_id = SheetId{sheet_index};
        const auto &sheet = schematic.sheet(sheet_id);
        for (const auto instance_id : sheet.symbol_instances()) {
            detail::validate_component_placement_coverage(schematic, sheet, sheet_id, instance_id,
                                                          report);
        }
        detail::validate_no_connect_markers(schematic, sheet_id, sheet, report);
        detail::validate_repeated_labels(schematic, sheet_id, sheet, report);
        detail::validate_fragmented_pin_labels(schematic, sheet_id, sheet, report);
        detail::validate_outside_sheet_objects(schematic, sheet_id, sheet, report);
        detail::validate_same_net_crossings(schematic, sheet_id, sheet, report);
    }
    detail::validate_missing_no_connect_markers(schematic, report);

    return report;
}

/**
 * Validate schematic document readability and presentation quality.
 *
 * This layer is separate from logical netlist correctness and schematic readiness. Text
 * collision diagnostics use deterministic conservative bounding boxes rather than renderer
 * font measurement, so they are suitable for tests and agent feedback but may over-report.
 */
[[nodiscard]] inline DiagnosticReport validate_schematic_readability(const Schematic &schematic) {
    auto report = DiagnosticReport{};

    for (std::size_t sheet_index = 0; sheet_index < schematic.sheet_count(); ++sheet_index) {
        const auto sheet_id = SheetId{sheet_index};
        const auto &sheet = schematic.sheet(sheet_id);
        detail::validate_readability_bounds(schematic, sheet_id, sheet, report);
        detail::validate_duplicate_junctions(schematic, sheet_id, sheet, report);
        detail::validate_label_readability(schematic, sheet_id, sheet, report);
        detail::validate_missing_passive_value_fields(schematic, sheet_id, sheet, report);
        detail::validate_dense_no_connect_clusters(schematic, sheet_id, sheet, report);
        detail::validate_text_collisions(schematic, sheet_id, sheet, report);
    }

    return report;
}

} // namespace volt
