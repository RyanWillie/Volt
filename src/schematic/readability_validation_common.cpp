#include <volt/schematic/readability_validation_common.hpp>

#include <volt/circuit/connectivity/queries.hpp>

namespace volt::detail {

void add_readability_diagnostic(DiagnosticReport &report, Severity severity,
                                const std::string &code, std::string message, SheetId sheet_id,
                                EntityRef object, const std::vector<EntityRef> &context) {
    auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id), object};
    refs.insert(refs.end(), context.begin(), context.end());
    report.add(Diagnostic{severity, DiagnosticCode{code}, std::move(message), std::move(refs)});
}

[[nodiscard]] std::vector<ReadabilityObject>
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
        const auto &net = schematic.circuit().get(label.net());
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
        const auto &net = schematic.circuit().get(port.net());
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

[[nodiscard]] std::vector<ReadabilityTagObject>
readability_tags_for_sheet(const Schematic &schematic, const Sheet &sheet) {
    auto tags = std::vector<ReadabilityTagObject>{};
    for (const auto port_id : sheet.power_ports()) {
        const auto &port = schematic.power_port(port_id);
        const auto &net = schematic.circuit().get(port.net());
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

[[nodiscard]] std::vector<ReadabilityTextObject>
readability_texts_for_sheet(const Schematic &schematic, const Sheet &sheet) {
    auto texts = std::vector<ReadabilityTextObject>{};
    for (const auto label_id : sheet.net_labels()) {
        const auto &label = schematic.net_label(label_id);
        const auto &net = schematic.circuit().get(label.net());
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
        const auto &net = schematic.circuit().get(port.net());
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

[[nodiscard]] std::vector<ReadabilityCollisionObject>
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
        const auto &pin = schematic.circuit().get(marker.pin());
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

[[nodiscard]] bool power_port_attaches_to_symbol_pin(const Schematic &schematic,
                                                     const PowerPort &port,
                                                     SymbolInstanceId symbol_id) {
    const auto &instance = schematic.symbol_instance(symbol_id);
    const auto &definition = schematic.symbol_definition(instance.symbol_definition());
    const auto &circuit = schematic.circuit();
    for (const auto &symbol_pin : definition.pins()) {
        const auto pin = queries::pin_by_number(circuit, instance.component(), symbol_pin.number());
        if (!pin.has_value()) {
            continue;
        }
        const auto pin_net = queries::net_of(circuit, pin.value());
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

[[nodiscard]] bool terminal_marker_attaches_to_symbol_pin(const Schematic &schematic,
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

} // namespace volt::detail
