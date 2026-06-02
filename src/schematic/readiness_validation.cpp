#include <volt/schematic/readiness_validation.hpp>

#include <volt/circuit/queries.hpp>

namespace volt::detail {

void add_outside_sheet_diagnostic(DiagnosticReport &report, SheetId sheet_id, EntityRef object,
                                  std::vector<EntityRef> entities) {
    auto refs = std::vector<EntityRef>{EntityRef::sheet(sheet_id), object};
    refs.insert(refs.end(), entities.begin(), entities.end());
    report.add(Diagnostic{
        Severity::Warning,
        DiagnosticCode{"SCHEMATIC_OBJECT_OUTSIDE_SHEET_BOUNDS"},
        "Schematic object is outside the sheet drawing bounds",
        std::move(refs),
    });
}
void validate_component_placement_coverage(const Schematic &schematic, const Sheet &sheet,
                                           SheetId sheet_id, SymbolInstanceId instance_id,
                                           DiagnosticReport &report) {
    const auto &circuit = schematic.circuit();
    const auto &instance = schematic.symbol_instance(instance_id);
    const auto &component = circuit.component(instance.component());
    const auto &symbol = schematic.symbol_definition(instance.symbol_definition());

    for (const auto &symbol_pin : symbol.pins()) {
        const auto pin = queries::pin_by_number(circuit, instance.component(), symbol_pin.number());
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

    for (const auto pin_id : queries::pins_for(circuit, instance.component())) {
        const auto &pin_instance = circuit.pin(pin_id);
        const auto pin_def_id = pin_instance.definition();
        const auto net = queries::net_of(circuit, pin_id);
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
void validate_component_placements(const Schematic &schematic, DiagnosticReport &report) {
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
void validate_repeated_labels(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                              DiagnosticReport &report) {
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
void validate_fragmented_pin_labels_for_net(const Schematic &schematic, SheetId sheet_id,
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
            const auto pin =
                queries::pin_by_number(circuit, instance.component(), symbol_pin.number());
            if (!pin.has_value()) {
                continue;
            }
            const auto pin_net = queries::net_of(circuit, pin.value());
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
void validate_fragmented_pin_labels(const Schematic &schematic, SheetId sheet_id,
                                    const Sheet &sheet, DiagnosticReport &report) {
    const auto &circuit = schematic.circuit();
    for (std::size_t net_index = 0; net_index < circuit.net_count(); ++net_index) {
        validate_fragmented_pin_labels_for_net(schematic, sheet_id, sheet, NetId{net_index},
                                               report);
    }
}
void validate_outside_sheet_objects(const Schematic &schematic, SheetId sheet_id,
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
void validate_same_net_crossings(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                                 DiagnosticReport &report) {
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
            const auto topology = classify_wire_pair_topology(
                first.points(), second.points(), SchematicWireNetRelationship::SameNet,
                [&schematic, &sheet, net = first.net()](SchematicSegment first_segment,
                                                        SchematicSegment second_segment) {
                    return sheet_has_junction_on_segments(schematic, sheet, first_segment,
                                                          second_segment, net)
                               ? SchematicJunction::Present
                               : SchematicJunction::Absent;
                });
            if (!topology.has_crossing_without_junction()) {
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
[[nodiscard]] bool schematic_has_no_connect_marker_for_pin(const Schematic &schematic, PinId pin) {
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
void validate_no_connect_markers(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                                 DiagnosticReport &report) {
    const auto &circuit = schematic.circuit();
    for (const auto marker_id : sheet.no_connect_markers()) {
        const auto &marker = schematic.no_connect_marker(marker_id);
        const auto &pin = circuit.pin(marker.pin());
        const auto net = queries::net_of(circuit, marker.pin());
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
void validate_missing_no_connect_markers(const Schematic &schematic, DiagnosticReport &report) {
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

} // namespace volt::detail

namespace volt {

[[nodiscard]] DiagnosticReport validate_schematic_readiness(const Schematic &schematic) {
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

} // namespace volt
