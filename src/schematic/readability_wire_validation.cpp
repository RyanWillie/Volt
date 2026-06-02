#include <volt/schematic/readability_wire_validation.hpp>

#include <volt/circuit/queries.hpp>

namespace volt::detail {

[[nodiscard]] double wire_run_length(const WireRun &wire) noexcept {
    auto length = 0.0;
    for (std::size_t point_index = 1; point_index < wire.points().size(); ++point_index) {
        length += point_distance(wire.points()[point_index - 1U], wire.points()[point_index]);
    }
    return length;
}

void validate_long_local_doglegs(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                                 DiagnosticReport &report) {
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

[[nodiscard]] bool points_align_as_stack(const std::vector<Point> &points) noexcept {
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

void validate_misaligned_local_labels(const Schematic &schematic, SheetId sheet_id,
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

void validate_ambiguous_same_net_crossings(const Schematic &schematic, SheetId sheet_id,
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
                DiagnosticCode{"SCHEMATIC_AMBIGUOUS_SAME_NET_CROSSING"},
                "Same-net schematic wires cross without a clear junction; make the local "
                "signal path explicit",
                std::vector{EntityRef::sheet(sheet_id), EntityRef::wire_run(first_id),
                            EntityRef::wire_run(second_id), EntityRef::net(first.net())},
            });
        }
    }
}

void validate_different_net_wire_crossings(const Schematic &schematic, SheetId sheet_id,
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
            const auto topology = classify_wire_pair_topology(
                first.points(), second.points(), SchematicWireNetRelationship::DifferentNet,
                [&schematic, &sheet, first_net = first.net(), second_net = second.net()](
                    SchematicSegment first_segment, SchematicSegment second_segment) {
                    return (sheet_has_junction_on_segments(schematic, sheet, first_segment,
                                                           second_segment, first_net) ||
                            sheet_has_junction_on_segments(schematic, sheet, first_segment,
                                                           second_segment, second_net))
                               ? SchematicJunction::Present
                               : SchematicJunction::Absent;
                });
            if (!topology.has_crossing_without_junction()) {
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
        }
    }
}

[[nodiscard]] bool sheet_has_same_net_tag_at_point(const Schematic &schematic, const Sheet &sheet,
                                                   NetId net, Point point) {
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

[[nodiscard]] bool sheet_has_symbol_pin_for_net_at_point(const Schematic &schematic,
                                                         const Sheet &sheet, NetId net,
                                                         Point point) {
    const auto &circuit = schematic.circuit();
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

[[nodiscard]] bool sheet_has_terminal_or_sheet_port_for_net_at_point(const Schematic &schematic,
                                                                     const Sheet &sheet, NetId net,
                                                                     Point point) {
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

[[nodiscard]] bool sheet_has_junction_for_net_at_point(const Schematic &schematic,
                                                       const Sheet &sheet, NetId net, Point point) {
    for (const auto junction_id : sheet.junctions()) {
        const auto &junction = schematic.junction(junction_id);
        if (junction.net() == net && same_schematic_point(junction.position(), point)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool wire_has_endpoint_at_point(const WireRun &wire, Point point) noexcept {
    return same_schematic_point(wire.points().front(), point) ||
           same_schematic_point(wire.points().back(), point);
}

[[nodiscard]] bool sheet_has_other_same_net_wire_endpoint_at_point(const Schematic &schematic,
                                                                   const Sheet &sheet, NetId net,
                                                                   Point point,
                                                                   WireRunId excluded_wire) {
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

[[nodiscard]] bool wire_endpoint_has_readable_anchor(const Schematic &schematic, const Sheet &sheet,
                                                     NetId net, Point point, WireRunId wire_id) {
    return sheet_has_symbol_pin_for_net_at_point(schematic, sheet, net, point) ||
           sheet_has_terminal_or_sheet_port_for_net_at_point(schematic, sheet, net, point) ||
           sheet_has_junction_for_net_at_point(schematic, sheet, net, point) ||
           sheet_has_other_same_net_wire_endpoint_at_point(schematic, sheet, net, point, wire_id);
}

void validate_dangling_wire_endpoints(const Schematic &schematic, SheetId sheet_id,
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

[[nodiscard]] bool sheet_has_other_same_net_wire_at_point(const Schematic &schematic,
                                                          const Sheet &sheet, NetId net,
                                                          Point point, WireRunId excluded_wire) {
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

void validate_floating_stub_clusters(const Schematic &schematic, SheetId sheet_id,
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

void validate_duplicate_junctions(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                                  DiagnosticReport &report) {
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

} // namespace volt::detail
