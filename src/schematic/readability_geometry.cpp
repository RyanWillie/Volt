#include <volt/schematic/readability_geometry.hpp>

#include <volt/circuit/queries.hpp>

namespace volt::detail {

[[nodiscard]] bool wire_covers_point(const WireRun &wire, Point point) noexcept {
    for (std::size_t index = 1; index < wire.points().size(); ++index) {
        if (point_on_schematic_segment(
                point, SchematicSegment{wire.points()[index - 1U], wire.points()[index]})) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool sheet_has_net_label_at_point(const Schematic &schematic, const Sheet &sheet,
                                                NetId net, Point point) {
    for (const auto label_id : sheet.net_labels()) {
        const auto &label = schematic.net_label(label_id);
        if (label.net() == net && same_schematic_point(label.position(), point)) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool sheet_visually_covers_net_at_pin(const Schematic &schematic, const Sheet &sheet,
                                                    NetId net, Point point) {
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

[[nodiscard]] bool sheet_has_coincident_same_net_symbol_pin(const Schematic &schematic,
                                                            const Sheet &sheet, NetId net,
                                                            PinId pin_id, Point point) {
    const auto &circuit = schematic.circuit();

    for (const auto other_instance_id : sheet.symbol_instances()) {
        const auto &other_instance = schematic.symbol_instance(other_instance_id);
        const auto &other_symbol = schematic.symbol_definition(other_instance.symbol_definition());
        for (const auto &other_symbol_pin : other_symbol.pins()) {
            const auto other_pin = queries::pin_by_number(circuit, other_instance.component(),
                                                          other_symbol_pin.number());
            if (!other_pin.has_value() || other_pin.value() == pin_id) {
                continue;
            }
            const auto other_net = queries::net_of(circuit, other_pin.value());
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

[[nodiscard]] bool schematic_readiness_exempts_pin(const Circuit &circuit, PinId pin_id,
                                                   PinDefId pin_def_id) {
    const auto &definition = circuit.pin_definition(pin_def_id);
    return definition.connection_requirement() == ConnectionRequirement::MustNotConnect ||
           definition.terminal_kind() == ElectricalTerminalKind::NoConnect ||
           circuit.is_intentional_no_connect_pin(pin_id);
}

[[nodiscard]] std::optional<SymbolPin> symbol_pin_by_number(const SymbolDefinition &symbol,
                                                            const std::string &number) {
    for (const auto &symbol_pin : symbol.pins()) {
        if (symbol_pin.number() == number) {
            return symbol_pin;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::vector<SymbolInstanceId>
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

[[nodiscard]] bool component_is_schematic_relevant(const Circuit &circuit, ComponentId component) {
    const auto &component_instance = circuit.component(component);
    const auto &definition = circuit.component_definition(component_instance.definition());
    const auto category = PropertyKey{"category"};
    if (definition.properties().contains(category)) {
        const auto &value = definition.properties().get(category);
        if (value.kind() == PropertyValueKind::String && value.as_string() == "mechanical") {
            return false;
        }
    }

    for (const auto pin_id : queries::pins_for(circuit, component)) {
        if (queries::net_of(circuit, pin_id).has_value() ||
            circuit.is_intentional_no_connect_pin(pin_id)) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool sheet_has_junction_on_segments(const Schematic &schematic, const Sheet &sheet,
                                                  SchematicSegment first, SchematicSegment second,
                                                  NetId net) {
    for (const auto junction_id : sheet.junctions()) {
        const auto &junction = schematic.junction(junction_id);
        if (junction.net() == net && point_on_schematic_segment(junction.position(), first) &&
            point_on_schematic_segment(junction.position(), second)) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool point_inside_sheet(const Sheet &sheet, Point point) noexcept {
    const auto size = sheet.metadata().size();
    return point.x() >= 0.0 && point.y() >= 0.0 && point.x() <= size.width() &&
           point.y() <= size.height();
}

[[nodiscard]] SchematicBounds no_connect_marker_collision_bounds(const NoConnectMarker &marker) {
    return padded_bounds(no_connect_marker_bounds(marker), no_connect_marker_clearance);
}

} // namespace volt::detail
