#include <volt/schematic/readability_collision_validation.hpp>

#include <volt/circuit/queries.hpp>

namespace volt::detail {

[[nodiscard]] bool text_anchor_intentionally_attaches_to_wire(const ReadabilityTextObject &text,
                                                              const WireRun &wire) {
    return text.kind == ReadabilityTextKind::NetLabel && text.net.has_value() &&
           text.net.value() == wire.net() && wire_covers_point(wire, text.anchor);
}

void validate_text_wire_collisions(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                                   DiagnosticReport &report) {
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

void validate_text_symbol_collisions(const Schematic &schematic, SheetId sheet_id,
                                     const Sheet &sheet, DiagnosticReport &report) {
    const auto texts = readability_texts_for_sheet(schematic, sheet);
    for (const auto &text : texts) {
        if (text.kind == ReadabilityTextKind::SymbolText) {
            continue;
        }
        for (const auto instance_id : sheet.symbol_instances()) {
            if (!intersects_bounds(text.bounds, symbol_instance_bounds(schematic, instance_id))) {
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

void validate_symbol_overlaps(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                              DiagnosticReport &report) {
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

[[nodiscard]] bool segment_visually_attaches_to_symbol_pin(const Schematic &schematic,
                                                           const WireRun &wire,
                                                           SchematicSegment segment,
                                                           SymbolInstanceId instance_id) {
    const auto &instance = schematic.symbol_instance(instance_id);
    const auto &symbol = schematic.symbol_definition(instance.symbol_definition());
    const auto &circuit = schematic.circuit();
    for (const auto &symbol_pin : symbol.pins()) {
        const auto pin = queries::pin_by_number(circuit, instance.component(), symbol_pin.number());
        if (!pin.has_value()) {
            continue;
        }
        const auto pin_net = queries::net_of(circuit, pin.value());
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

void validate_wire_symbol_collisions(const Schematic &schematic, SheetId sheet_id,
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

void validate_terminal_wire_collisions(const Schematic &schematic, SheetId sheet_id,
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

void validate_terminal_symbol_collisions(const Schematic &schematic, SheetId sheet_id,
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

[[nodiscard]] bool readability_collision_kind_pair(ReadabilityCollisionKind first,
                                                   ReadabilityCollisionKind second,
                                                   ReadabilityCollisionKind lhs,
                                                   ReadabilityCollisionKind rhs) noexcept {
    return (first == lhs && second == rhs) || (first == rhs && second == lhs);
}

[[nodiscard]] bool
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

[[nodiscard]] bool
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

[[nodiscard]] bool readability_collision_is_intentional_owning_symbol_contact(
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
        const auto pin = queries::pin_by_number(circuit, instance.component(), symbol_pin.number());
        if (!pin.has_value() || pin.value() != object->pin.value()) {
            continue;
        }
        const auto pin_point = transform_schematic_point(symbol_pin.anchor(), instance.position(),
                                                         instance.orientation());
        return same_schematic_point(pin_point, object->anchor.value());
    }
    return false;
}

[[nodiscard]] bool
readability_collision_is_intentional_junction_contact(const ReadabilityCollisionObject &first,
                                                      const ReadabilityCollisionObject &second) {
    return (first.kind == ReadabilityCollisionKind::Junction ||
            second.kind == ReadabilityCollisionKind::Junction) &&
           first.net.has_value() && second.net.has_value() &&
           first.net.value() == second.net.value();
}

[[nodiscard]] bool readability_collision_is_intentional_junction_symbol_pin_contact(
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
        const auto pin = queries::pin_by_number(circuit, instance.component(), symbol_pin.number());
        if (!pin.has_value()) {
            continue;
        }
        const auto net = queries::net_of(circuit, pin.value());
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

[[nodiscard]] bool
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

[[nodiscard]] bool
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

[[nodiscard]] bool readability_collision_pair_reported(
    const std::vector<std::pair<EntityRef, EntityRef>> &reported_pairs, EntityRef first,
    EntityRef second) {
    return std::any_of(reported_pairs.begin(), reported_pairs.end(),
                       [first, second](const auto &pair) {
                           return (pair.first == first && pair.second == second) ||
                                  (pair.first == second && pair.second == first);
                       });
}

void validate_visual_element_collisions(const Schematic &schematic, SheetId sheet_id,
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

} // namespace volt::detail
