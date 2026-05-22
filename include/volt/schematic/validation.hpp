#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <volt/circuit/definitions.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/schematic/readability_geometry.hpp>
#include <volt/schematic/schematic.hpp>

namespace volt {

namespace detail {

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
        if (sheet_has_coincident_same_net_symbol_pin(schematic, sheet, net.value(), pin_id,
                                                     pin_point)) {
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
        objects.push_back(ReadabilityObject{ReadabilityObjectKind::SymbolInstance,
                                            EntityRef::symbol_instance(instance_id),
                                            std::vector{EntityRef::component(instance.component())},
                                            symbol_instance_bounds(schematic, instance_id),
                                            instance.authored_region(), instance_id});
    }
    for (const auto wire_id : sheet.wire_runs()) {
        const auto &wire = schematic.wire_run(wire_id);
        objects.push_back(ReadabilityObject{ReadabilityObjectKind::WireRun,
                                            EntityRef::wire_run(wire_id),
                                            std::vector{EntityRef::net(wire.net())},
                                            wire_run_bounds(wire), wire.authored_region()});
    }
    for (const auto label_id : sheet.net_labels()) {
        const auto &label = schematic.net_label(label_id);
        const auto &net = schematic.circuit().net(label.net());
        objects.push_back(
            ReadabilityObject{ReadabilityObjectKind::NetLabel, EntityRef::net_label(label_id),
                              std::vector{EntityRef::net(label.net())},
                              text_bounds(label.text_position(), label.orientation(),
                                          label.label().value_or(net.name().value()), label.style(),
                                          net_label_rendered_font_size),
                              label.authored_region()});
    }
    for (const auto junction_id : sheet.junctions()) {
        const auto &junction = schematic.junction(junction_id);
        objects.push_back(ReadabilityObject{
            ReadabilityObjectKind::Junction,
            EntityRef::junction(junction_id),
            std::vector{EntityRef::net(junction.net())},
            padded_bounds(bounds_from_point(junction.position()), 1.8),
            junction.authored_region(),
        });
    }
    for (const auto port_id : sheet.power_ports()) {
        const auto &port = schematic.power_port(port_id);
        const auto &net = schematic.circuit().net(port.net());
        const auto label = port.label().value_or(net.name().value());
        objects.push_back(ReadabilityObject{
            ReadabilityObjectKind::PowerPort, EntityRef::power_port(port_id),
            std::vector{EntityRef::net(port.net())}, power_port_bounds(port, label),
            port.authored_region(), std::nullopt, port_id});
    }
    for (const auto marker_id : sheet.no_connect_markers()) {
        const auto &marker = schematic.no_connect_marker(marker_id);
        objects.push_back(ReadabilityObject{
            ReadabilityObjectKind::NoConnectMarker, EntityRef::no_connect_marker(marker_id),
            std::vector{EntityRef::pin(marker.pin())}, no_connect_marker_bounds(marker),
            marker.authored_region()});
    }
    for (const auto port_id : sheet.sheet_ports()) {
        const auto &port = schematic.sheet_port(port_id);
        objects.push_back(ReadabilityObject{ReadabilityObjectKind::SheetPort,
                                            EntityRef::sheet_port(port_id),
                                            std::vector{EntityRef::net(port.net())},
                                            sheet_port_bounds(port), port.authored_region()});
    }
    for (const auto field_id : sheet.symbol_fields()) {
        const auto &field = schematic.symbol_field(field_id);
        objects.push_back(
            ReadabilityObject{ReadabilityObjectKind::SymbolField, EntityRef::symbol_field(field_id),
                              std::vector{EntityRef::symbol_instance(field.symbol_instance())},
                              text_bounds(field.position(), field.orientation(), field.value(),
                                          field.style(), symbol_field_rendered_font_size),
                              field.authored_region()});
    }
    return objects;
}

[[nodiscard]] inline std::vector<ReadabilityTagObject>
readability_tags_for_sheet(const Schematic &schematic, const Sheet &sheet) {
    auto tags = std::vector<ReadabilityTagObject>{};
    for (const auto port_id : sheet.power_ports()) {
        const auto &port = schematic.power_port(port_id);
        const auto &net = schematic.circuit().net(port.net());
        const auto label = port.label().value_or(net.name().value());
        tags.push_back(ReadabilityTagObject{
            EntityRef::power_port(port_id),
            std::vector{EntityRef::net(port.net())},
            power_port_bounds(port, label),
            port.position(),
            port.orientation(),
            port.authored_region(),
        });
    }
    for (const auto port_id : sheet.sheet_ports()) {
        const auto &port = schematic.sheet_port(port_id);
        tags.push_back(ReadabilityTagObject{
            EntityRef::sheet_port(port_id),
            std::vector{EntityRef::net(port.net())},
            sheet_port_bounds(port),
            port.position(),
            port.orientation(),
            port.authored_region(),
        });
    }
    return tags;
}

[[nodiscard]] inline std::vector<ReadabilityTextObject>
readability_texts_for_sheet(const Schematic &schematic, const Sheet &sheet) {
    auto texts = std::vector<ReadabilityTextObject>{};
    for (const auto label_id : sheet.net_labels()) {
        const auto &label = schematic.net_label(label_id);
        const auto &net = schematic.circuit().net(label.net());
        texts.push_back(ReadabilityTextObject{
            ReadabilityTextKind::NetLabel,
            EntityRef::net_label(label_id),
            std::vector{EntityRef::net(label.net())},
            text_bounds(label.text_position(), label.orientation(),
                        label.label().value_or(net.name().value()), label.style(),
                        net_label_rendered_font_size),
            label.position(),
            label.net(),
            std::nullopt,
        });
    }
    for (const auto field_id : sheet.symbol_fields()) {
        const auto &field = schematic.symbol_field(field_id);
        texts.push_back(ReadabilityTextObject{
            ReadabilityTextKind::SymbolField,
            EntityRef::symbol_field(field_id),
            std::vector{EntityRef::symbol_instance(field.symbol_instance())},
            text_bounds(field.position(), field.orientation(), field.value(), field.style(),
                        symbol_field_rendered_font_size),
            field.position(),
            std::nullopt,
            std::nullopt,
        });
    }
    for (const auto instance_id : sheet.symbol_instances()) {
        const auto &instance = schematic.symbol_instance(instance_id);
        const auto &symbol = schematic.symbol_definition(instance.symbol_definition());
        for (const auto &primitive : symbol.primitives()) {
            if (!std::holds_alternative<SymbolText>(primitive)) {
                continue;
            }
            const auto &text = std::get<SymbolText>(primitive);
            const auto anchor = transform_schematic_point(text.anchor(), instance.position(),
                                                          instance.orientation());
            texts.push_back(ReadabilityTextObject{
                ReadabilityTextKind::SymbolText,
                EntityRef::symbol_instance(instance_id),
                std::vector{EntityRef::component(instance.component())},
                text_bounds(anchor,
                            combined_text_orientation(instance.orientation(), text.orientation()),
                            text.text(), text.style(), symbol_text_rendered_font_size),
                anchor,
                std::nullopt,
                instance_id,
            });
        }
    }
    for (const auto port_id : sheet.power_ports()) {
        const auto &port = schematic.power_port(port_id);
        const auto &net = schematic.circuit().net(port.net());
        const auto label = port.label().value_or(net.name().value());
        const auto bounds = power_port_label_bounds(port, label);
        texts.push_back(ReadabilityTextObject{
            ReadabilityTextKind::PowerPortLabel,
            EntityRef::power_port(port_id),
            std::vector{EntityRef::net(port.net())},
            bounds,
            bounds_center(bounds),
            port.net(),
            std::nullopt,
        });
    }
    for (const auto port_id : sheet.sheet_ports()) {
        const auto &port = schematic.sheet_port(port_id);
        const auto bounds = sheet_port_label_bounds(port);
        texts.push_back(ReadabilityTextObject{
            ReadabilityTextKind::SheetPortLabel,
            EntityRef::sheet_port(port_id),
            std::vector{EntityRef::net(port.net())},
            bounds,
            bounds_center(bounds),
            port.net(),
            std::nullopt,
        });
    }
    return texts;
}

[[nodiscard]] inline std::vector<ReadabilityCollisionObject>
readability_collision_objects_for_sheet(const Schematic &schematic, const Sheet &sheet) {
    auto objects = std::vector<ReadabilityCollisionObject>{};

    for (const auto instance_id : sheet.symbol_instances()) {
        const auto bounds = symbol_instance_body_bounds(schematic, instance_id);
        if (!bounds.has_value()) {
            continue;
        }
        const auto &instance = schematic.symbol_instance(instance_id);
        objects.push_back(ReadabilityCollisionObject{
            ReadabilityCollisionKind::SymbolBody,
            EntityRef::symbol_instance(instance_id),
            std::vector{EntityRef::component(instance.component())},
            bounds.value(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            instance.component(),
            instance_id,
        });
    }

    for (const auto wire_id : sheet.wire_runs()) {
        const auto &wire = schematic.wire_run(wire_id);
        for (std::size_t point_index = 1; point_index < wire.points().size(); ++point_index) {
            const auto segment =
                SchematicSegment{wire.points()[point_index - 1U], wire.points()[point_index]};
            objects.push_back(ReadabilityCollisionObject{
                ReadabilityCollisionKind::WireSegment,
                EntityRef::wire_run(wire_id),
                std::vector{EntityRef::net(wire.net())},
                segment_bounds(segment),
                segment,
                std::nullopt,
                wire.net(),
                std::nullopt,
            });
        }
    }

    for (const auto &text : readability_texts_for_sheet(schematic, sheet)) {
        objects.push_back(ReadabilityCollisionObject{
            ReadabilityCollisionKind::Text,
            text.entity,
            text.context,
            text.bounds,
            std::nullopt,
            text.anchor,
            text.net,
            std::nullopt,
        });
    }

    for (const auto port_id : sheet.power_ports()) {
        const auto &port = schematic.power_port(port_id);
        objects.push_back(ReadabilityCollisionObject{
            ReadabilityCollisionKind::TerminalGlyph,
            EntityRef::power_port(port_id),
            std::vector{EntityRef::net(port.net())},
            power_port_glyph_bounds(port),
            std::nullopt,
            port.position(),
            port.net(),
            std::nullopt,
        });
    }

    for (const auto junction_id : sheet.junctions()) {
        const auto &junction = schematic.junction(junction_id);
        objects.push_back(ReadabilityCollisionObject{
            ReadabilityCollisionKind::Junction,
            EntityRef::junction(junction_id),
            std::vector{EntityRef::net(junction.net())},
            padded_bounds(bounds_from_point(junction.position()), 1.8),
            std::nullopt,
            junction.position(),
            junction.net(),
            std::nullopt,
        });
    }

    for (const auto marker_id : sheet.no_connect_markers()) {
        const auto &marker = schematic.no_connect_marker(marker_id);
        const auto &pin = schematic.circuit().pin(marker.pin());
        objects.push_back(ReadabilityCollisionObject{
            ReadabilityCollisionKind::NoConnectMarker,
            EntityRef::no_connect_marker(marker_id),
            std::vector{EntityRef::pin(marker.pin()), EntityRef::component(pin.component())},
            no_connect_marker_collision_bounds(marker),
            std::nullopt,
            marker.position(),
            std::nullopt,
            pin.component(),
            std::nullopt,
            marker.pin(),
        });
    }

    for (const auto port_id : sheet.sheet_ports()) {
        const auto &port = schematic.sheet_port(port_id);
        objects.push_back(ReadabilityCollisionObject{
            ReadabilityCollisionKind::SheetPort,
            EntityRef::sheet_port(port_id),
            std::vector{EntityRef::net(port.net())},
            sheet_port_bounds(port),
            std::nullopt,
            port.position(),
            port.net(),
            std::nullopt,
        });
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

inline void add_region_content_object_refs(std::vector<EntityRef> &refs,
                                           const std::vector<const ReadabilityObject *> &objects,
                                           SchematicBounds comparison_bounds) {
    auto added = false;
    for (const auto *object : objects) {
        if (!overlaps_bounds_area(object->bounds, comparison_bounds)) {
            continue;
        }
        refs.push_back(object->entity);
        refs.insert(refs.end(), object->context.begin(), object->context.end());
        added = true;
    }
    if (!added && !objects.empty()) {
        refs.push_back(objects.front()->entity);
        refs.insert(refs.end(), objects.front()->context.begin(), objects.front()->context.end());
    }
}

inline void validate_authored_region_content_overlaps(const Schematic &schematic, SheetId sheet_id,
                                                      const Sheet &sheet,
                                                      DiagnosticReport &report) {
    const auto objects = readability_objects_for_sheet(schematic, sheet);
    auto bounds_by_region = std::vector<std::optional<SchematicBounds>>(sheet.regions().size());
    auto objects_by_region =
        std::vector<std::vector<const ReadabilityObject *>>(sheet.regions().size());

    for (const auto &object : objects) {
        if (!object.authored_region.has_value()) {
            continue;
        }
        const auto region_index = object.authored_region.value();
        objects_by_region[region_index].push_back(&object);
        if (bounds_by_region[region_index].has_value()) {
            include_bounds(bounds_by_region[region_index].value(), object.bounds);
        } else {
            bounds_by_region[region_index] = object.bounds;
        }
    }

    for (std::size_t first = 0; first < bounds_by_region.size(); ++first) {
        if (!bounds_by_region[first].has_value()) {
            continue;
        }
        for (std::size_t second = first + 1U; second < bounds_by_region.size(); ++second) {
            if (!bounds_by_region[second].has_value() ||
                !overlaps_bounds_area(bounds_by_region[first].value(),
                                      bounds_by_region[second].value())) {
                continue;
            }

            auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id)};
            add_region_content_object_refs(refs, objects_by_region[first],
                                           bounds_by_region[second].value());
            add_region_content_object_refs(refs, objects_by_region[second],
                                           bounds_by_region[first].value());
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"SCHEMATIC_AUTHORED_REGION_CONTENT_OVERLAP"},
                "Authored schematic regions '" + sheet.region(first).name() + "' and '" +
                    sheet.region(second).name() +
                    "' have overlapping occupied content bounds; move one region or tighten "
                    "the placement",
                std::move(refs),
            });
        }
    }
}

inline void add_title_block_overflow_diagnostic(DiagnosticReport &report, SheetId sheet_id,
                                                std::string_view column,
                                                std::string_view row_label) {
    report.add(Diagnostic{
        Severity::Warning,
        DiagnosticCode{"SCHEMATIC_TITLE_BLOCK_TEXT_OVERFLOW"},
        "Schematic title-block " + std::string{column} + " text for '" + std::string{row_label} +
            "' exceeds the rendered column width",
        std::vector{EntityRef::sheet(sheet_id)},
    });
}

inline void validate_title_block_text_overflow(SheetId sheet_id, const Sheet &sheet,
                                               DiagnosticReport &report) {
    const auto &metadata = sheet.metadata();
    const auto title_bounds = title_block_bounds(metadata);
    const auto label_available_width =
        std::max(0.0, std::min(title_block_label_width, bounds_width(title_bounds)) -
                          title_block_label_x - 1.0);
    const auto value_available_width =
        std::max(0.0, bounds_width(title_bounds) - title_block_value_x - title_block_right_padding);

    const auto check_cell = [&](std::string_view column, std::string_view row_label,
                                std::string_view text, double available_width) {
        if (title_block_rendered_text_width(text, title_block_rendered_font_size) <=
            available_width + schematic_geometry_tolerance) {
            return;
        }
        add_title_block_overflow_diagnostic(report, sheet_id, column, row_label);
    };

    check_cell("label", "Title", "Title", label_available_width);
    check_cell("value", "Title", metadata.title(), value_available_width);
    for (const auto &field : metadata.title_block()) {
        check_cell("label", field.key(), field.key(), label_available_width);
        check_cell("value", field.key(), field.value(), value_available_width);
    }
}

inline void validate_port_tag_scale(const Schematic &schematic, SheetId sheet_id,
                                    const Sheet &sheet, DiagnosticReport &report) {
    for (const auto port_id : sheet.power_ports()) {
        const auto &port = schematic.power_port(port_id);
        const auto &net = schematic.circuit().net(port.net());
        const auto label = port.label().value_or(net.name().value());
        if (rendered_text_width(label, sheet_port_rendered_label_font_size) <=
            oversized_port_tag_rendered_length) {
            continue;
        }
        add_readability_diagnostic(
            report, Severity::Warning, "SCHEMATIC_OVERSIZED_PORT_TAG",
            "Schematic power port label is visually oversized for the rendered tag scale", sheet_id,
            EntityRef::power_port(port_id), std::vector{EntityRef::net(port.net())});
    }
    for (const auto port_id : sheet.sheet_ports()) {
        const auto &port = schematic.sheet_port(port_id);
        const auto rendered_length =
            sheet_port_rendered_body_length(port.name()) + sheet_port_rendered_tip_length;
        if (rendered_length <= oversized_port_tag_rendered_length) {
            continue;
        }
        add_readability_diagnostic(
            report, Severity::Warning, "SCHEMATIC_OVERSIZED_PORT_TAG",
            "Schematic sheet/off-page port is visually oversized for the rendered tag scale",
            sheet_id, EntityRef::sheet_port(port_id), std::vector{EntityRef::net(port.net())});
    }
}

[[nodiscard]] inline bool label_or_tag_crowds_symbols(ReadabilityObjectKind kind) noexcept {
    return kind == ReadabilityObjectKind::NetLabel || kind == ReadabilityObjectKind::PowerPort ||
           kind == ReadabilityObjectKind::SheetPort;
}

[[nodiscard]] inline bool power_port_attaches_to_symbol_pin(const Schematic &schematic,
                                                            const PowerPort &port,
                                                            SymbolInstanceId symbol_id) {
    const auto &instance = schematic.symbol_instance(symbol_id);
    const auto &definition = schematic.symbol_definition(instance.symbol_definition());
    const auto &circuit = schematic.circuit();
    for (const auto &symbol_pin : definition.pins()) {
        const auto pin = circuit.pin_by_number(instance.component(), symbol_pin.number());
        if (!pin.has_value()) {
            continue;
        }
        const auto pin_net = circuit.net_of(pin.value());
        if (!pin_net.has_value() || pin_net.value() != port.net()) {
            continue;
        }
        const auto pin_point = transform_schematic_point(symbol_pin.anchor(), instance.position(),
                                                         instance.orientation());
        if (same_schematic_point(pin_point, port.position())) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool terminal_marker_attaches_to_symbol_pin(const Schematic &schematic,
                                                                 const ReadabilityObject &symbol,
                                                                 const ReadabilityObject &object) {
    if (object.kind != ReadabilityObjectKind::PowerPort) {
        return false;
    }
    if (!object.power_port.has_value() || !symbol.symbol_instance.has_value()) {
        return false;
    }

    const auto &port = schematic.power_port(object.power_port.value());
    return power_port_attaches_to_symbol_pin(schematic, port, symbol.symbol_instance.value());
}

inline void validate_symbol_crowding(const Schematic &schematic, SheetId sheet_id,
                                     const Sheet &sheet, DiagnosticReport &report) {
    const auto objects = readability_objects_for_sheet(schematic, sheet);
    for (const auto &symbol : objects) {
        if (symbol.kind != ReadabilityObjectKind::SymbolInstance) {
            continue;
        }
        const auto symbol_clearance = padded_bounds(symbol.bounds, label_symbol_clearance);
        auto crowded = std::vector<const ReadabilityObject *>{};
        for (const auto &object : objects) {
            if (!label_or_tag_crowds_symbols(object.kind) ||
                !intersects_bounds(symbol_clearance, object.bounds)) {
                continue;
            }
            if (terminal_marker_attaches_to_symbol_pin(schematic, symbol, object)) {
                continue;
            }
            crowded.push_back(&object);
        }
        if (crowded.empty()) {
            continue;
        }
        auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id), symbol.entity};
        for (const auto *object : crowded) {
            refs.push_back(object->entity);
        }
        refs.insert(refs.end(), symbol.context.begin(), symbol.context.end());
        for (const auto *object : crowded) {
            refs.insert(refs.end(), object->context.begin(), object->context.end());
        }
        report.add(Diagnostic{
            Severity::Warning,
            DiagnosticCode{"SCHEMATIC_LABEL_CROWDS_SYMBOL"},
            "Schematic tags or net labels crowd a symbol; add spacing or use compact stubs",
            std::move(refs),
        });
    }
}

[[nodiscard]] inline double tag_stack_primary_position(const ReadabilityTagObject &tag) noexcept {
    switch (tag.orientation) {
    case SchematicOrientation::Right:
    case SchematicOrientation::Left:
        return tag.position.y();
    case SchematicOrientation::Up:
    case SchematicOrientation::Down:
        return tag.position.x();
    }
    return tag.position.y();
}

[[nodiscard]] inline double tag_stack_cross_position(const ReadabilityTagObject &tag) noexcept {
    switch (tag.orientation) {
    case SchematicOrientation::Right:
    case SchematicOrientation::Left:
        return tag.position.x();
    case SchematicOrientation::Up:
    case SchematicOrientation::Down:
        return tag.position.y();
    }
    return tag.position.x();
}

inline void add_crowded_tag_stack_diagnostic(DiagnosticReport &report, SheetId sheet_id,
                                             const std::vector<ReadabilityTagObject> &tags,
                                             const std::vector<std::size_t> &cluster) {
    auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id)};
    for (const auto tag_index : cluster) {
        refs.push_back(tags[tag_index].entity);
    }
    for (const auto tag_index : cluster) {
        refs.insert(refs.end(), tags[tag_index].context.begin(), tags[tag_index].context.end());
    }
    report.add(Diagnostic{
        Severity::Warning,
        DiagnosticCode{"SCHEMATIC_CROWDED_TAG_STACK"},
        "Repeated schematic tags are stacked too tightly for the rendered tag scale",
        std::move(refs),
    });
}

inline void validate_crowded_tag_stacks(const Schematic &schematic, SheetId sheet_id,
                                        const Sheet &sheet, DiagnosticReport &report) {
    const auto tags = readability_tags_for_sheet(schematic, sheet);
    auto order = std::vector<std::size_t>{};
    order.reserve(tags.size());
    for (std::size_t index = 0; index < tags.size(); ++index) {
        order.push_back(index);
    }
    std::sort(order.begin(), order.end(), [&tags](std::size_t lhs, std::size_t rhs) {
        if (tags[lhs].orientation != tags[rhs].orientation) {
            return tags[lhs].orientation < tags[rhs].orientation;
        }
        const auto lhs_cross = tag_stack_cross_position(tags[lhs]);
        const auto rhs_cross = tag_stack_cross_position(tags[rhs]);
        if (lhs_cross != rhs_cross) {
            return lhs_cross < rhs_cross;
        }
        const auto lhs_primary = tag_stack_primary_position(tags[lhs]);
        const auto rhs_primary = tag_stack_primary_position(tags[rhs]);
        if (lhs_primary != rhs_primary) {
            return lhs_primary < rhs_primary;
        }
        return lhs < rhs;
    });

    auto cluster = std::vector<std::size_t>{};
    for (const auto tag_index : order) {
        if (cluster.empty()) {
            cluster.push_back(tag_index);
            continue;
        }
        const auto previous_index = cluster.back();
        const auto same_stack =
            tags[tag_index].orientation == tags[previous_index].orientation &&
            std::abs(tag_stack_cross_position(tags[tag_index]) -
                     tag_stack_cross_position(tags[previous_index])) <=
                tag_stack_alignment_tolerance &&
            std::abs(tag_stack_primary_position(tags[tag_index]) -
                     tag_stack_primary_position(tags[previous_index])) <= tag_stack_min_spacing;
        if (same_stack) {
            cluster.push_back(tag_index);
            continue;
        }
        if (cluster.size() > 1U) {
            add_crowded_tag_stack_diagnostic(report, sheet_id, tags, cluster);
        }
        cluster = std::vector<std::size_t>{tag_index};
    }
    if (cluster.size() > 1U) {
        add_crowded_tag_stack_diagnostic(report, sheet_id, tags, cluster);
    }
}

inline void validate_dense_region_port_tags(const Schematic &schematic, SheetId sheet_id,
                                            const Sheet &sheet, DiagnosticReport &report) {
    const auto tags = readability_tags_for_sheet(schematic, sheet);
    auto tags_by_region = std::vector<std::vector<std::size_t>>(sheet.regions().size());
    for (std::size_t index = 0; index < tags.size(); ++index) {
        if (tags[index].authored_region.has_value()) {
            tags_by_region[tags[index].authored_region.value()].push_back(index);
        }
    }
    for (std::size_t region_index = 0; region_index < tags_by_region.size(); ++region_index) {
        const auto &region_tags = tags_by_region[region_index];
        if (region_tags.size() < dense_region_port_tag_threshold) {
            continue;
        }
        auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id)};
        for (const auto tag_index : region_tags) {
            refs.push_back(tags[tag_index].entity);
        }
        report.add(Diagnostic{
            Severity::Warning,
            DiagnosticCode{"SCHEMATIC_DENSE_PORT_TAGS"},
            "Schematic region '" + sheet.region(region_index).name() +
                "' contains dense sheet or power tags; prefer compact local labels where possible",
            std::move(refs),
        });
    }
}

[[nodiscard]] inline bool display_label_is_overlong_or_scoped(const std::string &label) {
    return label.size() > overlong_label_character_threshold ||
           label.find('/') != std::string::npos || label.find("::") != std::string::npos;
}

[[nodiscard]] inline bool ascii_upper_alpha(char character) noexcept {
    return character >= 'A' && character <= 'Z';
}

[[nodiscard]] inline bool ascii_digit(char character) noexcept {
    return character >= '0' && character <= '9';
}

// Returns true if the label looks like a conventional EDA reference designator: 1–4 uppercase
// letters followed by one or more decimal digits (e.g. R1, C12, U3, CONN4, TP10).  Labels
// containing scope separators ('/', "::"), underscores, mixed-case suffixes, or all-letter
// tokens are treated as unconventional.  Prefixes longer than four characters are also flagged
// to keep designators compact and readable; use a shorter type prefix instead (e.g. "FB" for
// ferrite bead rather than "FBEAD").
[[nodiscard]] inline bool
visible_reference_label_looks_conventional(std::string_view label) noexcept {
    auto prefix_length = std::size_t{0};
    while (prefix_length < label.size() && ascii_upper_alpha(label[prefix_length])) {
        ++prefix_length;
    }
    if (prefix_length == 0U || prefix_length > 4U || prefix_length == label.size()) {
        return false;
    }
    for (std::size_t index = prefix_length; index < label.size(); ++index) {
        if (!ascii_digit(label[index])) {
            return false;
        }
    }
    return true;
}

inline void validate_visible_reference_labels(const Schematic &schematic, SheetId sheet_id,
                                              const Sheet &sheet, DiagnosticReport &report) {
    struct VisibleReference {
        SymbolInstanceId instance;
        ComponentId component;
        std::string_view label;
    };

    auto references = std::vector<VisibleReference>{};
    references.reserve(sheet.symbol_fields().size());
    const auto record_visible_reference = [&](SymbolInstanceId instance_id, ComponentId component,
                                              std::string_view label) {
        references.push_back(VisibleReference{instance_id, component, label});
        if (!visible_reference_label_looks_conventional(references.back().label)) {
            report.add(Diagnostic{
                Severity::Warning,
                DiagnosticCode{"SCHEMATIC_UNCONVENTIONAL_REFERENCE_LABEL"},
                "Visible schematic reference label '" + std::string{references.back().label} +
                    "' does not look like a conventional EDA reference designator",
                std::vector{EntityRef::sheet(sheet_id), EntityRef::symbol_instance(instance_id),
                            EntityRef::component(component)},
            });
        }
    };

    for (const auto field_id : sheet.symbol_fields()) {
        const auto &field = schematic.symbol_field(field_id);
        if (field.name() != "reference") {
            continue;
        }
        const auto &instance = schematic.symbol_instance(field.symbol_instance());
        record_visible_reference(field.symbol_instance(), instance.component(), field.value());
    }

    auto reported = std::vector<bool>(references.size(), false);
    for (std::size_t first = 0; first < references.size(); ++first) {
        if (reported[first]) {
            continue;
        }
        auto duplicate_indices = std::vector<std::size_t>{first};
        for (std::size_t second = first + 1U; second < references.size(); ++second) {
            if (!reported[second] && references[second].label == references[first].label) {
                duplicate_indices.push_back(second);
            }
        }
        if (duplicate_indices.size() < 2U) {
            continue;
        }
        auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id)};
        for (const auto index : duplicate_indices) {
            refs.push_back(EntityRef::symbol_instance(references[index].instance));
            refs.push_back(EntityRef::component(references[index].component));
            reported[index] = true;
        }
        report.add(Diagnostic{
            Severity::Warning,
            DiagnosticCode{"SCHEMATIC_DUPLICATE_REFERENCE_LABEL"},
            "Schematic sheet has duplicate visible reference label '" +
                std::string{references[first].label} + "'",
            std::move(refs),
        });
    }
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
        if (display_label_is_overlong_or_scoped(label.label().value_or(net.name().value()))) {
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

inline void validate_symbol_field_ownership_distance(const Schematic &schematic, SheetId sheet_id,
                                                     const Sheet &sheet, DiagnosticReport &report) {
    for (const auto field_id : sheet.symbol_fields()) {
        const auto &field = schematic.symbol_field(field_id);
        const auto field_bounds = text_bounds(field.position(), field.orientation(), field.value(),
                                              field.style(), symbol_field_rendered_font_size);
        const auto owner_bounds = symbol_instance_bounds(schematic, field.symbol_instance());
        if (bounds_gap(owner_bounds, field_bounds) <= symbol_field_owner_max_gap) {
            continue;
        }
        report.add(Diagnostic{
            Severity::Warning,
            DiagnosticCode{"SCHEMATIC_SYMBOL_FIELD_FAR_FROM_SYMBOL"},
            "Schematic symbol field is far from its owning symbol",
            std::vector{EntityRef::sheet(sheet_id), EntityRef::symbol_field(field_id),
                        EntityRef::symbol_instance(field.symbol_instance())},
        });
    }
}

[[nodiscard]] inline bool
text_anchor_intentionally_attaches_to_wire(const ReadabilityTextObject &text, const WireRun &wire) {
    return text.kind == ReadabilityTextKind::NetLabel && text.net.has_value() &&
           text.net.value() == wire.net() && wire_covers_point(wire, text.anchor);
}

inline void validate_text_wire_collisions(const Schematic &schematic, SheetId sheet_id,
                                          const Sheet &sheet, DiagnosticReport &report) {
    const auto texts = readability_texts_for_sheet(schematic, sheet);
    for (const auto &text : texts) {
        for (const auto wire_id : sheet.wire_runs()) {
            const auto &wire = schematic.wire_run(wire_id);
            if (text_anchor_intentionally_attaches_to_wire(text, wire)) {
                continue;
            }
            const auto checked_bounds =
                text.net.has_value() && text.net.value() != wire.net()
                    ? padded_bounds(text.bounds, unrelated_text_wire_clearance)
                    : text.bounds;
            auto touches_wire = false;
            for (std::size_t point_index = 1; point_index < wire.points().size(); ++point_index) {
                const auto segment =
                    SchematicSegment{wire.points()[point_index - 1U], wire.points()[point_index]};
                if (segment_intersects_bounds(segment, checked_bounds)) {
                    touches_wire = true;
                    break;
                }
            }
            if (!touches_wire) {
                continue;
            }
            auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id), text.entity,
                                               EntityRef::wire_run(wire_id)};
            refs.insert(refs.end(), text.context.begin(), text.context.end());
            refs.push_back(EntityRef::net(wire.net()));
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"SCHEMATIC_TEXT_TOUCHES_WIRE"},
                "Schematic text touches or crosses a wire; move the label or reroute the wire",
                std::move(refs),
            });
        }
    }
}

inline void validate_text_symbol_collisions(const Schematic &schematic, SheetId sheet_id,
                                            const Sheet &sheet, DiagnosticReport &report) {
    const auto texts = readability_texts_for_sheet(schematic, sheet);
    for (const auto &text : texts) {
        if (text.kind == ReadabilityTextKind::SymbolText) {
            continue;
        }
        for (const auto instance_id : sheet.symbol_instances()) {
            if (!intersects_bounds(text.bounds,
                                   symbol_instance_geometry_bounds(schematic, instance_id))) {
                continue;
            }
            auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id), text.entity,
                                               EntityRef::symbol_instance(instance_id)};
            refs.insert(refs.end(), text.context.begin(), text.context.end());
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"SCHEMATIC_TEXT_TOUCHES_SYMBOL"},
                "Schematic text touches or crosses a symbol outline; add spacing around the text",
                std::move(refs),
            });
        }
    }
}

inline void validate_symbol_overlaps(const Schematic &schematic, SheetId sheet_id,
                                     const Sheet &sheet, DiagnosticReport &report) {
    for (std::size_t first = 0; first < sheet.symbol_instances().size(); ++first) {
        const auto first_instance = sheet.symbol_instances()[first];
        const auto first_bounds = symbol_instance_body_bounds(schematic, first_instance);
        if (!first_bounds.has_value()) {
            continue;
        }
        for (std::size_t second = first + 1U; second < sheet.symbol_instances().size(); ++second) {
            const auto second_instance = sheet.symbol_instances()[second];
            const auto second_bounds = symbol_instance_body_bounds(schematic, second_instance);
            if (!second_bounds.has_value() ||
                !overlaps_bounds_area(first_bounds.value(), second_bounds.value())) {
                continue;
            }
            if (symbol_overlap_is_shared_pin_contact(schematic, first_instance,
                                                     first_bounds.value(), second_instance,
                                                     second_bounds.value())) {
                continue;
            }
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"SCHEMATIC_SYMBOL_OVERLAP"},
                "Schematic symbols overlap; separate the component placements",
                std::vector{
                    EntityRef::sheet(sheet_id), EntityRef::symbol_instance(first_instance),
                    EntityRef::symbol_instance(second_instance),
                    EntityRef::component(schematic.symbol_instance(first_instance).component()),
                    EntityRef::component(schematic.symbol_instance(second_instance).component())},
            });
        }
    }
}

[[nodiscard]] inline bool segment_visually_attaches_to_symbol_pin(const Schematic &schematic,
                                                                  const WireRun &wire,
                                                                  SchematicSegment segment,
                                                                  SymbolInstanceId instance_id) {
    const auto &instance = schematic.symbol_instance(instance_id);
    const auto &symbol = schematic.symbol_definition(instance.symbol_definition());
    const auto &circuit = schematic.circuit();
    for (const auto &symbol_pin : symbol.pins()) {
        const auto pin = circuit.pin_by_number(instance.component(), symbol_pin.number());
        if (!pin.has_value()) {
            continue;
        }
        const auto pin_net = circuit.net_of(pin.value());
        if (!pin_net.has_value() || pin_net.value() != wire.net()) {
            continue;
        }
        const auto pin_point = transform_schematic_point(symbol_pin.anchor(), instance.position(),
                                                         instance.orientation());
        if (point_on_schematic_segment(pin_point, segment)) {
            return true;
        }
    }
    return false;
}

inline void validate_wire_symbol_collisions(const Schematic &schematic, SheetId sheet_id,
                                            const Sheet &sheet, DiagnosticReport &report) {
    for (const auto wire_id : sheet.wire_runs()) {
        const auto &wire = schematic.wire_run(wire_id);
        for (const auto instance_id : sheet.symbol_instances()) {
            const auto body_bounds = symbol_instance_body_bounds(schematic, instance_id);
            if (!body_bounds.has_value()) {
                continue;
            }
            auto crosses_symbol = false;
            for (std::size_t point_index = 1; point_index < wire.points().size(); ++point_index) {
                const auto segment =
                    SchematicSegment{wire.points()[point_index - 1U], wire.points()[point_index]};
                if (segment_intersects_bounds(segment, body_bounds.value()) &&
                    !segment_visually_attaches_to_symbol_pin(schematic, wire, segment,
                                                             instance_id)) {
                    crosses_symbol = true;
                    break;
                }
            }
            if (!crosses_symbol) {
                continue;
            }
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"SCHEMATIC_WIRE_CROSSES_SYMBOL"},
                "Schematic wire crosses a symbol body; reroute the wire or move the symbol",
                std::vector{
                    EntityRef::sheet(sheet_id), EntityRef::wire_run(wire_id),
                    EntityRef::symbol_instance(instance_id), EntityRef::net(wire.net()),
                    EntityRef::component(schematic.symbol_instance(instance_id).component())},
            });
        }
    }
}

inline void validate_terminal_wire_collisions(const Schematic &schematic, SheetId sheet_id,
                                              const Sheet &sheet, DiagnosticReport &report) {
    for (const auto port_id : sheet.power_ports()) {
        const auto &port = schematic.power_port(port_id);
        const auto port_bounds = power_port_glyph_bounds(port);
        for (const auto wire_id : sheet.wire_runs()) {
            const auto &wire = schematic.wire_run(wire_id);
            if (wire.net() == port.net()) {
                continue;
            }
            auto touches_wire = false;
            for (std::size_t point_index = 1; point_index < wire.points().size(); ++point_index) {
                const auto segment =
                    SchematicSegment{wire.points()[point_index - 1U], wire.points()[point_index]};
                if (segment_intersects_bounds(segment, port_bounds)) {
                    touches_wire = true;
                    break;
                }
            }
            if (!touches_wire) {
                continue;
            }
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"SCHEMATIC_TERMINAL_TOUCHES_UNRELATED_WIRE"},
                "Schematic terminal marker touches an unrelated wire; move the marker or reroute "
                "the wire",
                std::vector{EntityRef::sheet(sheet_id), EntityRef::power_port(port_id),
                            EntityRef::wire_run(wire_id), EntityRef::net(port.net()),
                            EntityRef::net(wire.net())},
            });
        }
    }
}

inline void validate_terminal_symbol_collisions(const Schematic &schematic, SheetId sheet_id,
                                                const Sheet &sheet, DiagnosticReport &report) {
    for (const auto port_id : sheet.power_ports()) {
        const auto &port = schematic.power_port(port_id);
        const auto port_bounds = power_port_glyph_bounds(port);
        for (const auto instance_id : sheet.symbol_instances()) {
            const auto body_bounds = symbol_instance_body_bounds(schematic, instance_id);
            if (!body_bounds.has_value() || !intersects_bounds(port_bounds, body_bounds.value())) {
                continue;
            }
            if (power_port_attaches_to_symbol_pin(schematic, port, instance_id)) {
                continue;
            }
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"SCHEMATIC_TERMINAL_TOUCHES_SYMBOL"},
                "Schematic terminal marker touches or crosses a symbol body; move the marker away "
                "from the component body",
                std::vector{
                    EntityRef::sheet(sheet_id), EntityRef::power_port(port_id),
                    EntityRef::symbol_instance(instance_id), EntityRef::net(port.net()),
                    EntityRef::component(schematic.symbol_instance(instance_id).component())},
            });
        }
    }
}

[[nodiscard]] inline bool readability_collision_kind_pair(ReadabilityCollisionKind first,
                                                          ReadabilityCollisionKind second,
                                                          ReadabilityCollisionKind lhs,
                                                          ReadabilityCollisionKind rhs) noexcept {
    return (first == lhs && second == rhs) || (first == rhs && second == lhs);
}

[[nodiscard]] inline bool
readability_collision_is_handled_by_specific_validator(const ReadabilityCollisionObject &first,
                                                       const ReadabilityCollisionObject &second) {
    if (first.entity == second.entity) {
        return true;
    }

    if (readability_collision_kind_pair(first.kind, second.kind,
                                        ReadabilityCollisionKind::WireSegment,
                                        ReadabilityCollisionKind::WireSegment)) {
        return true;
    }
    if (readability_collision_kind_pair(first.kind, second.kind, ReadabilityCollisionKind::Text,
                                        ReadabilityCollisionKind::WireSegment)) {
        return true;
    }
    if (readability_collision_kind_pair(first.kind, second.kind, ReadabilityCollisionKind::Text,
                                        ReadabilityCollisionKind::SymbolBody)) {
        return true;
    }
    if (readability_collision_kind_pair(first.kind, second.kind, ReadabilityCollisionKind::Text,
                                        ReadabilityCollisionKind::Text)) {
        return true;
    }
    if (readability_collision_kind_pair(first.kind, second.kind,
                                        ReadabilityCollisionKind::SymbolBody,
                                        ReadabilityCollisionKind::SymbolBody)) {
        return true;
    }
    if (readability_collision_kind_pair(first.kind, second.kind,
                                        ReadabilityCollisionKind::WireSegment,
                                        ReadabilityCollisionKind::SymbolBody)) {
        return true;
    }
    if (readability_collision_kind_pair(first.kind, second.kind,
                                        ReadabilityCollisionKind::TerminalGlyph,
                                        ReadabilityCollisionKind::WireSegment)) {
        return true;
    }
    return readability_collision_kind_pair(first.kind, second.kind,
                                           ReadabilityCollisionKind::TerminalGlyph,
                                           ReadabilityCollisionKind::SymbolBody);
}

[[nodiscard]] inline bool
readability_collision_is_intentional_wire_contact(const ReadabilityCollisionObject &first,
                                                  const ReadabilityCollisionObject &second) {
    const auto *wire = first.kind == ReadabilityCollisionKind::WireSegment ? &first : nullptr;
    const auto *object = first.kind == ReadabilityCollisionKind::WireSegment ? &second : &first;
    if (wire == nullptr && second.kind == ReadabilityCollisionKind::WireSegment) {
        wire = &second;
        object = &first;
    }
    if (wire == nullptr || !wire->segment.has_value() || !wire->net.has_value() ||
        !object->anchor.has_value() || !object->net.has_value() ||
        wire->net.value() != object->net.value()) {
        return false;
    }
    return point_on_schematic_segment(object->anchor.value(), wire->segment.value());
}

[[nodiscard]] inline bool readability_collision_is_intentional_owning_symbol_contact(
    const Schematic &schematic, const ReadabilityCollisionObject &first,
    const ReadabilityCollisionObject &second) {
    const auto *symbol = first.kind == ReadabilityCollisionKind::SymbolBody ? &first : nullptr;
    const auto *object = first.kind == ReadabilityCollisionKind::SymbolBody ? &second : &first;
    if (symbol == nullptr && second.kind == ReadabilityCollisionKind::SymbolBody) {
        symbol = &second;
        object = &first;
    }
    if (symbol == nullptr || object->kind != ReadabilityCollisionKind::NoConnectMarker ||
        !object->anchor.has_value() || !object->pin.has_value() ||
        !symbol->symbol_instance.has_value()) {
        return false;
    }

    const auto &instance = schematic.symbol_instance(symbol->symbol_instance.value());
    const auto &definition = schematic.symbol_definition(instance.symbol_definition());
    const auto &circuit = schematic.circuit();
    for (const auto &symbol_pin : definition.pins()) {
        const auto pin = circuit.pin_by_number(instance.component(), symbol_pin.number());
        if (!pin.has_value() || pin.value() != object->pin.value()) {
            continue;
        }
        const auto pin_point = transform_schematic_point(symbol_pin.anchor(), instance.position(),
                                                         instance.orientation());
        return same_schematic_point(pin_point, object->anchor.value());
    }
    return false;
}

[[nodiscard]] inline bool
readability_collision_is_intentional_junction_contact(const ReadabilityCollisionObject &first,
                                                      const ReadabilityCollisionObject &second) {
    return (first.kind == ReadabilityCollisionKind::Junction ||
            second.kind == ReadabilityCollisionKind::Junction) &&
           first.net.has_value() && second.net.has_value() &&
           first.net.value() == second.net.value();
}

[[nodiscard]] inline bool readability_collision_is_intentional_junction_symbol_pin_contact(
    const Schematic &schematic, const ReadabilityCollisionObject &first,
    const ReadabilityCollisionObject &second) {
    const auto *symbol = first.kind == ReadabilityCollisionKind::SymbolBody ? &first : nullptr;
    const auto *junction = first.kind == ReadabilityCollisionKind::SymbolBody ? &second : &first;
    if (symbol == nullptr && second.kind == ReadabilityCollisionKind::SymbolBody) {
        symbol = &second;
        junction = &first;
    }
    if (symbol == nullptr || junction->kind != ReadabilityCollisionKind::Junction ||
        !junction->anchor.has_value() || !junction->net.has_value() ||
        !symbol->symbol_instance.has_value()) {
        return false;
    }

    const auto &instance = schematic.symbol_instance(symbol->symbol_instance.value());
    const auto &definition = schematic.symbol_definition(instance.symbol_definition());
    const auto &circuit = schematic.circuit();
    for (const auto &symbol_pin : definition.pins()) {
        const auto pin = circuit.pin_by_number(instance.component(), symbol_pin.number());
        if (!pin.has_value()) {
            continue;
        }
        const auto net = circuit.net_of(pin.value());
        if (!net.has_value() || net.value() != junction->net.value()) {
            continue;
        }
        const auto pin_point = transform_schematic_point(symbol_pin.anchor(), instance.position(),
                                                         instance.orientation());
        if (same_schematic_point(pin_point, junction->anchor.value())) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool
readability_collision_is_intentional_contact(const Schematic &schematic,
                                             const ReadabilityCollisionObject &first,
                                             const ReadabilityCollisionObject &second) {
    return readability_collision_is_handled_by_specific_validator(first, second) ||
           readability_collision_is_intentional_wire_contact(first, second) ||
           readability_collision_is_intentional_owning_symbol_contact(schematic, first, second) ||
           readability_collision_is_intentional_junction_contact(first, second) ||
           readability_collision_is_intentional_junction_symbol_pin_contact(schematic, first,
                                                                            second);
}

[[nodiscard]] inline bool
readability_collision_shapes_intersect(const ReadabilityCollisionObject &first,
                                       const ReadabilityCollisionObject &second) {
    if (!intersects_bounds(first.bounds, second.bounds)) {
        return false;
    }
    if (first.segment.has_value() && second.segment.has_value()) {
        return classify_segment_relationship(first.segment.value(), second.segment.value()) !=
               SchematicSegmentRelationship::Disjoint;
    }
    if (first.segment.has_value()) {
        return segment_intersects_bounds(first.segment.value(), second.bounds);
    }
    if (second.segment.has_value()) {
        return segment_intersects_bounds(second.segment.value(), first.bounds);
    }
    return true;
}

[[nodiscard]] inline bool readability_collision_pair_reported(
    const std::vector<std::pair<EntityRef, EntityRef>> &reported_pairs, EntityRef first,
    EntityRef second) {
    return std::any_of(reported_pairs.begin(), reported_pairs.end(),
                       [first, second](const auto &pair) {
                           return (pair.first == first && pair.second == second) ||
                                  (pair.first == second && pair.second == first);
                       });
}

inline void validate_visual_element_collisions(const Schematic &schematic, SheetId sheet_id,
                                               const Sheet &sheet, DiagnosticReport &report) {
    const auto objects = readability_collision_objects_for_sheet(schematic, sheet);
    auto reported_pairs = std::vector<std::pair<EntityRef, EntityRef>>{};
    for (std::size_t first = 0; first < objects.size(); ++first) {
        for (std::size_t second = first + 1U; second < objects.size(); ++second) {
            if (!readability_collision_shapes_intersect(objects[first], objects[second]) ||
                readability_collision_is_intentional_contact(schematic, objects[first],
                                                             objects[second])) {
                continue;
            }
            if (readability_collision_pair_reported(reported_pairs, objects[first].entity,
                                                    objects[second].entity)) {
                continue;
            }
            reported_pairs.emplace_back(objects[first].entity, objects[second].entity);
            auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id), objects[first].entity,
                                               objects[second].entity};
            refs.insert(refs.end(), objects[first].context.begin(), objects[first].context.end());
            refs.insert(refs.end(), objects[second].context.begin(), objects[second].context.end());
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"SCHEMATIC_VISUAL_COLLISION"},
                "Schematic visual elements overlap; separate the placements or use an explicit "
                "connection idiom",
                std::move(refs),
            });
        }
    }
}

[[nodiscard]] inline double wire_run_length(const WireRun &wire) noexcept {
    auto length = 0.0;
    for (std::size_t point_index = 1; point_index < wire.points().size(); ++point_index) {
        length += point_distance(wire.points()[point_index - 1U], wire.points()[point_index]);
    }
    return length;
}

inline void validate_long_local_doglegs(const Schematic &schematic, SheetId sheet_id,
                                        const Sheet &sheet, DiagnosticReport &report) {
    for (const auto wire_id : sheet.wire_runs()) {
        const auto &wire = schematic.wire_run(wire_id);
        if (wire.route_intent() != RouteIntent::Orthogonal || wire.points().size() < 4U) {
            continue;
        }
        const auto endpoint_distance = point_distance(wire.points().front(), wire.points().back());
        const auto path_length = wire_run_length(wire);
        if (endpoint_distance > local_dogleg_endpoint_max_distance ||
            path_length < local_dogleg_min_length ||
            path_length < endpoint_distance * local_dogleg_min_detour_ratio) {
            continue;
        }
        report.add(Diagnostic{
            Severity::Warning,
            DiagnosticCode{"SCHEMATIC_LONG_LOCAL_DOGLEG"},
            "Local schematic route takes a long dogleg between nearby endpoints; use a short local "
            "stub or label where clearer",
            std::vector{EntityRef::sheet(sheet_id), EntityRef::wire_run(wire_id),
                        EntityRef::net(wire.net())},
        });
    }
}

[[nodiscard]] inline bool points_align_as_stack(const std::vector<Point> &points) noexcept {
    auto min_x = points.front().x();
    auto max_x = points.front().x();
    auto min_y = points.front().y();
    auto max_y = points.front().y();
    for (const auto point : points) {
        min_x = std::min(min_x, point.x());
        max_x = std::max(max_x, point.x());
        min_y = std::min(min_y, point.y());
        max_y = std::max(max_y, point.y());
    }
    return (max_x - min_x) <= local_label_alignment_tolerance ||
           (max_y - min_y) <= local_label_alignment_tolerance;
}

inline void validate_misaligned_local_labels(const Schematic &schematic, SheetId sheet_id,
                                             const Sheet &sheet, DiagnosticReport &report) {
    const auto &circuit = schematic.circuit();
    for (std::size_t net_index = 0; net_index < circuit.net_count(); ++net_index) {
        const auto net_id = NetId{net_index};
        auto labels = std::vector<NetLabelId>{};
        auto points = std::vector<Point>{};
        for (const auto label_id : sheet.net_labels()) {
            const auto &label = schematic.net_label(label_id);
            if (label.net() == net_id) {
                labels.push_back(label_id);
                points.push_back(label.position());
            }
        }
        if (labels.size() < local_label_cluster_threshold) {
            continue;
        }
        auto reported = std::vector<bool>(labels.size(), false);
        for (std::size_t seed = 0; seed < labels.size(); ++seed) {
            if (reported[seed]) {
                continue;
            }
            auto cluster = std::vector<std::size_t>{};
            auto cluster_points = std::vector<Point>{};
            auto bounds = bounds_from_point(points[seed]);
            for (std::size_t candidate = 0; candidate < labels.size(); ++candidate) {
                if (std::abs(points[candidate].x() - points[seed].x()) >
                        local_label_cluster_max_span ||
                    std::abs(points[candidate].y() - points[seed].y()) >
                        local_label_cluster_max_span) {
                    continue;
                }
                cluster.push_back(candidate);
                cluster_points.push_back(points[candidate]);
                include_point(bounds, points[candidate]);
            }
            if (cluster.size() < local_label_cluster_threshold ||
                bounds_width(bounds) > local_label_cluster_max_span ||
                bounds_height(bounds) > local_label_cluster_max_span ||
                points_align_as_stack(cluster_points)) {
                continue;
            }
            auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id), EntityRef::net(net_id)};
            for (const auto label_index : cluster) {
                refs.push_back(EntityRef::net_label(labels[label_index]));
                reported[label_index] = true;
            }
            report.add(Diagnostic{
                Severity::Warning,
                DiagnosticCode{"SCHEMATIC_MISALIGNED_LOCAL_LABELS"},
                "Repeated same-net labels in one local area are not aligned as an intentional "
                "stack",
                std::move(refs),
            });
        }
    }
}

inline void validate_ambiguous_same_net_crossings(const Schematic &schematic, SheetId sheet_id,
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
                        DiagnosticCode{"SCHEMATIC_AMBIGUOUS_SAME_NET_CROSSING"},
                        "Same-net schematic wires cross without a clear junction; make the local "
                        "signal path explicit",
                        std::vector{EntityRef::sheet(sheet_id), EntityRef::wire_run(first_id),
                                    EntityRef::wire_run(second_id), EntityRef::net(first.net())},
                    });
                }
            }
        }
    }
}

inline void validate_different_net_wire_crossings(const Schematic &schematic, SheetId sheet_id,
                                                  const Sheet &sheet, DiagnosticReport &report) {
    const auto &wires = sheet.wire_runs();
    for (std::size_t first_index = 0; first_index < wires.size(); ++first_index) {
        const auto first_id = wires[first_index];
        const auto &first = schematic.wire_run(first_id);
        for (std::size_t second_index = first_index + 1U; second_index < wires.size();
             ++second_index) {
            const auto second_id = wires[second_index];
            const auto &second = schematic.wire_run(second_id);
            if (first.net() == second.net()) {
                continue;
            }
            auto crossing_reported = false;
            for (std::size_t first_point = 1; first_point < first.points().size(); ++first_point) {
                if (crossing_reported) {
                    break;
                }
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
                    report.add(Diagnostic{
                        Severity::Error,
                        DiagnosticCode{"SCHEMATIC_DIFFERENT_NET_WIRE_CROSSING"},
                        "Different-net schematic wires cross visually; reroute one wire to keep "
                        "the drawing readable",
                        std::vector{EntityRef::sheet(sheet_id), EntityRef::wire_run(first_id),
                                    EntityRef::wire_run(second_id), EntityRef::net(first.net()),
                                    EntityRef::net(second.net())},
                    });
                    crossing_reported = true;
                    break;
                }
            }
        }
    }
}

[[nodiscard]] inline bool sheet_has_same_net_tag_at_point(const Schematic &schematic,
                                                          const Sheet &sheet, NetId net,
                                                          Point point) {
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

[[nodiscard]] inline bool sheet_has_symbol_pin_for_net_at_point(const Schematic &schematic,
                                                                const Sheet &sheet, NetId net,
                                                                Point point) {
    const auto &circuit = schematic.circuit();
    for (const auto instance_id : sheet.symbol_instances()) {
        const auto &instance = schematic.symbol_instance(instance_id);
        const auto &symbol = schematic.symbol_definition(instance.symbol_definition());
        for (const auto &symbol_pin : symbol.pins()) {
            const auto pin = circuit.pin_by_number(instance.component(), symbol_pin.number());
            if (!pin.has_value()) {
                continue;
            }
            const auto pin_net = circuit.net_of(pin.value());
            if (!pin_net.has_value() || pin_net.value() != net) {
                continue;
            }
            const auto pin_point = transform_schematic_point(
                symbol_pin.anchor(), instance.position(), instance.orientation());
            if (same_schematic_point(pin_point, point)) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] inline bool
sheet_has_terminal_or_sheet_port_for_net_at_point(const Schematic &schematic, const Sheet &sheet,
                                                  NetId net, Point point) {
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

[[nodiscard]] inline bool sheet_has_junction_for_net_at_point(const Schematic &schematic,
                                                              const Sheet &sheet, NetId net,
                                                              Point point) {
    for (const auto junction_id : sheet.junctions()) {
        const auto &junction = schematic.junction(junction_id);
        if (junction.net() == net && same_schematic_point(junction.position(), point)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool wire_has_endpoint_at_point(const WireRun &wire, Point point) noexcept {
    return same_schematic_point(wire.points().front(), point) ||
           same_schematic_point(wire.points().back(), point);
}

[[nodiscard]] inline bool
sheet_has_other_same_net_wire_endpoint_at_point(const Schematic &schematic, const Sheet &sheet,
                                                NetId net, Point point, WireRunId excluded_wire) {
    for (const auto wire_id : sheet.wire_runs()) {
        if (wire_id == excluded_wire) {
            continue;
        }
        const auto &wire = schematic.wire_run(wire_id);
        if (wire.net() == net && wire_has_endpoint_at_point(wire, point)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool wire_endpoint_has_readable_anchor(const Schematic &schematic,
                                                            const Sheet &sheet, NetId net,
                                                            Point point, WireRunId wire_id) {
    return sheet_has_symbol_pin_for_net_at_point(schematic, sheet, net, point) ||
           sheet_has_terminal_or_sheet_port_for_net_at_point(schematic, sheet, net, point) ||
           sheet_has_junction_for_net_at_point(schematic, sheet, net, point) ||
           sheet_has_other_same_net_wire_endpoint_at_point(schematic, sheet, net, point, wire_id);
}

inline void validate_dangling_wire_endpoints(const Schematic &schematic, SheetId sheet_id,
                                             const Sheet &sheet, DiagnosticReport &report) {
    for (const auto wire_id : sheet.wire_runs()) {
        const auto &wire = schematic.wire_run(wire_id);
        for (const auto point : {wire.points().front(), wire.points().back()}) {
            if (wire_endpoint_has_readable_anchor(schematic, sheet, wire.net(), point, wire_id)) {
                continue;
            }
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"SCHEMATIC_DANGLING_WIRE_ENDPOINT"},
                "Schematic wire endpoint does not land on an explicit visual connection anchor",
                std::vector{EntityRef::sheet(sheet_id), EntityRef::wire_run(wire_id),
                            EntityRef::net(wire.net())},
            });
        }
    }
}

[[nodiscard]] inline bool sheet_has_other_same_net_wire_at_point(const Schematic &schematic,
                                                                 const Sheet &sheet, NetId net,
                                                                 Point point,
                                                                 WireRunId excluded_wire) {
    for (const auto wire_id : sheet.wire_runs()) {
        if (wire_id == excluded_wire) {
            continue;
        }
        const auto &wire = schematic.wire_run(wire_id);
        if (wire.net() == net && wire_covers_point(wire, point)) {
            return true;
        }
    }
    return false;
}

/** Short tagged wire run that may read as a floating local stub. */
struct FloatingStubCandidate {
    /** Wire run that forms the short stub. */
    WireRunId wire;
    /** Sheet-space midpoint used to cluster nearby stubs deterministically. */
    Point center;
};

inline void validate_floating_stub_clusters(const Schematic &schematic, SheetId sheet_id,
                                            const Sheet &sheet, DiagnosticReport &report) {
    const auto &circuit = schematic.circuit();
    for (std::size_t net_index = 0; net_index < circuit.net_count(); ++net_index) {
        const auto net_id = NetId{net_index};
        auto stubs = std::vector<FloatingStubCandidate>{};
        for (const auto wire_id : sheet.wire_runs()) {
            const auto &wire = schematic.wire_run(wire_id);
            if (wire.net() != net_id || wire.points().size() != 2U ||
                wire_run_length(wire) > floating_stub_max_length) {
                continue;
            }
            const auto start = wire.points().front();
            const auto end = wire.points().back();
            if (!sheet_has_same_net_tag_at_point(schematic, sheet, net_id, start) &&
                !sheet_has_same_net_tag_at_point(schematic, sheet, net_id, end)) {
                continue;
            }
            if (sheet_has_symbol_pin_for_net_at_point(schematic, sheet, net_id, start) ||
                sheet_has_symbol_pin_for_net_at_point(schematic, sheet, net_id, end)) {
                continue;
            }
            if (sheet_has_other_same_net_wire_at_point(schematic, sheet, net_id, start, wire_id) ||
                sheet_has_other_same_net_wire_at_point(schematic, sheet, net_id, end, wire_id)) {
                continue;
            }
            stubs.push_back(FloatingStubCandidate{
                wire_id,
                Point{(start.x() + end.x()) / 2.0, (start.y() + end.y()) / 2.0},
            });
        }
        if (stubs.size() < floating_stub_cluster_threshold) {
            continue;
        }
        auto reported = std::vector<bool>(stubs.size(), false);
        for (std::size_t seed = 0; seed < stubs.size(); ++seed) {
            if (reported[seed]) {
                continue;
            }
            auto cluster = std::vector<std::size_t>{};
            auto cluster_bounds = bounds_from_point(stubs[seed].center);
            for (std::size_t candidate = 0; candidate < stubs.size(); ++candidate) {
                if (std::abs(stubs[candidate].center.x() - stubs[seed].center.x()) >
                        floating_stub_cluster_max_span ||
                    std::abs(stubs[candidate].center.y() - stubs[seed].center.y()) >
                        floating_stub_cluster_max_span) {
                    continue;
                }
                cluster.push_back(candidate);
                include_point(cluster_bounds, stubs[candidate].center);
            }
            if (cluster.size() < floating_stub_cluster_threshold ||
                bounds_width(cluster_bounds) > floating_stub_cluster_max_span ||
                bounds_height(cluster_bounds) > floating_stub_cluster_max_span) {
                continue;
            }
            auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id), EntityRef::net(net_id)};
            for (const auto stub_index : cluster) {
                refs.push_back(EntityRef::wire_run(stubs[stub_index].wire));
                reported[stub_index] = true;
            }
            report.add(Diagnostic{
                Severity::Warning,
                DiagnosticCode{"SCHEMATIC_FLOATING_STUB_CLUSTER"},
                "Local cluster of short tagged wire stubs can read as floating segments",
                std::move(refs),
            });
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

inline void validate_terminal_marker_net_kind_mismatch(const Schematic &schematic, SheetId sheet_id,
                                                       const Sheet &sheet,
                                                       DiagnosticReport &report) {
    const auto &circuit = schematic.circuit();
    for (const auto port_id : sheet.power_ports()) {
        const auto &port = schematic.power_port(port_id);
        const auto &net = circuit.net(port.net());

        if (port.kind() == PowerPortKind::Power && net.kind() == NetKind::Ground) {
            report.add(Diagnostic{
                Severity::Warning,
                DiagnosticCode{"SCHEMATIC_POWER_MARKER_ON_GROUND_NET"},
                "Power marker placed on ground net",
                std::vector{EntityRef::sheet(sheet_id), EntityRef::net(port.net()),
                            EntityRef::power_port(port_id)},
            });
        } else if (port.kind() == PowerPortKind::Ground && net.kind() == NetKind::Power) {
            report.add(Diagnostic{
                Severity::Warning,
                DiagnosticCode{"SCHEMATIC_GROUND_MARKER_ON_POWER_NET"},
                "Ground marker placed on power net",
                std::vector{EntityRef::sheet(sheet_id), EntityRef::net(port.net()),
                            EntityRef::power_port(port_id)},
            });
        }
    }
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
    const auto texts = readability_texts_for_sheet(schematic, sheet);

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
                Severity::Error,
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
        detail::validate_authored_region_content_overlaps(schematic, sheet_id, sheet, report);
        detail::validate_title_block_text_overflow(sheet_id, sheet, report);
        detail::validate_duplicate_junctions(schematic, sheet_id, sheet, report);
        detail::validate_visible_reference_labels(schematic, sheet_id, sheet, report);
        detail::validate_label_readability(schematic, sheet_id, sheet, report);
        detail::validate_symbol_field_ownership_distance(schematic, sheet_id, sheet, report);
        detail::validate_port_tag_scale(schematic, sheet_id, sheet, report);
        detail::validate_text_wire_collisions(schematic, sheet_id, sheet, report);
        detail::validate_text_symbol_collisions(schematic, sheet_id, sheet, report);
        detail::validate_symbol_overlaps(schematic, sheet_id, sheet, report);
        detail::validate_wire_symbol_collisions(schematic, sheet_id, sheet, report);
        detail::validate_terminal_wire_collisions(schematic, sheet_id, sheet, report);
        detail::validate_terminal_symbol_collisions(schematic, sheet_id, sheet, report);
        detail::validate_visual_element_collisions(schematic, sheet_id, sheet, report);
        detail::validate_long_local_doglegs(schematic, sheet_id, sheet, report);
        detail::validate_misaligned_local_labels(schematic, sheet_id, sheet, report);
        detail::validate_ambiguous_same_net_crossings(schematic, sheet_id, sheet, report);
        detail::validate_different_net_wire_crossings(schematic, sheet_id, sheet, report);
        detail::validate_dangling_wire_endpoints(schematic, sheet_id, sheet, report);
        detail::validate_floating_stub_clusters(schematic, sheet_id, sheet, report);
        detail::validate_symbol_crowding(schematic, sheet_id, sheet, report);
        detail::validate_crowded_tag_stacks(schematic, sheet_id, sheet, report);
        detail::validate_dense_region_port_tags(schematic, sheet_id, sheet, report);
        detail::validate_missing_passive_value_fields(schematic, sheet_id, sheet, report);
        detail::validate_terminal_marker_net_kind_mismatch(schematic, sheet_id, sheet, report);
        detail::validate_dense_no_connect_clusters(schematic, sheet_id, sheet, report);
        detail::validate_text_collisions(schematic, sheet_id, sheet, report);
    }

    return report;
}

} // namespace volt
