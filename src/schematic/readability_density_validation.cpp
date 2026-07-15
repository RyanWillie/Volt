#include <volt/schematic/readability_density_validation.hpp>

#include <volt/circuit/connectivity/queries.hpp>

namespace volt::detail {

void validate_port_tag_scale(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                             DiagnosticReport &report) {
    for (const auto port_id : sheet.power_ports()) {
        const auto &port = schematic.get(port_id);
        const auto &net = schematic.circuit().get(port.net());
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
        const auto &port = schematic.get(port_id);
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

[[nodiscard]] bool label_or_tag_crowds_symbols(ReadabilityObjectKind kind) noexcept {
    return kind == ReadabilityObjectKind::NetLabel || kind == ReadabilityObjectKind::PowerPort ||
           kind == ReadabilityObjectKind::SheetPort;
}

void validate_symbol_crowding(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                              DiagnosticReport &report) {
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

[[nodiscard]] double tag_stack_primary_position(const ReadabilityTagObject &tag) noexcept {
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

[[nodiscard]] double tag_stack_cross_position(const ReadabilityTagObject &tag) noexcept {
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

void add_crowded_tag_stack_diagnostic(DiagnosticReport &report, SheetId sheet_id,
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

void validate_crowded_tag_stacks(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                                 DiagnosticReport &report) {
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

void validate_dense_region_port_tags(const Schematic &schematic, SheetId sheet_id,
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

[[nodiscard]] bool component_definition_is_passive(const Circuit &circuit,
                                                   ComponentDefId definition_id) {
    const auto &definition = circuit.get(definition_id);
    if (definition.pins().empty()) {
        return false;
    }
    return std::all_of(definition.pins().begin(), definition.pins().end(),
                       [&circuit](PinDefId pin_definition) {
                           const auto &pin = circuit.get(pin_definition);
                           return pin.terminal_kind() == ElectricalTerminalKind::Passive &&
                                  pin.direction() == ElectricalDirection::Passive;
                       });
}

[[nodiscard]] bool component_has_known_value(const Circuit &circuit, ComponentId component_id) {
    const auto &component = circuit.get(component_id);
    if (component.properties().contains(PropertyKey{"value"}) ||
        component.properties().contains(PropertyKey{"Value"})) {
        return true;
    }
    static const auto known_values = std::vector<ElectricalAttributeName>{
        ElectricalAttributeName{"resistance"}, ElectricalAttributeName{"capacitance"},
        ElectricalAttributeName{"inductance"}, ElectricalAttributeName{"voltage"},
        ElectricalAttributeName{"current"},    ElectricalAttributeName{"power"}};
    const auto &attributes = volt::queries::component_electrical_attributes(circuit, component_id);
    return std::any_of(
        known_values.begin(), known_values.end(),
        [&attributes](const ElectricalAttributeName &name) { return attributes.contains(name); });
}

[[nodiscard]] bool symbol_instance_has_value_field(const Schematic &schematic, const Sheet &sheet,
                                                   SymbolInstanceId instance) {
    for (const auto field_id : sheet.symbol_fields()) {
        const auto &field = schematic.get(field_id);
        if (field.symbol_instance() == instance &&
            (field.name() == "value" || field.name() == "Value")) {
            return true;
        }
    }
    return false;
}

void validate_missing_passive_value_fields(const Schematic &schematic, SheetId sheet_id,
                                           const Sheet &sheet, DiagnosticReport &report) {
    const auto &circuit = schematic.circuit();
    for (const auto instance_id : sheet.symbol_instances()) {
        const auto &instance = schematic.get(instance_id);
        const auto &component = circuit.get(instance.component());
        if (!component_definition_is_passive(circuit, component.definition()) ||
            !component_has_known_value(circuit, instance.component()) ||
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

[[nodiscard]] double squared_distance(Point lhs, Point rhs) noexcept {
    const auto dx = lhs.x() - rhs.x();
    const auto dy = lhs.y() - rhs.y();
    return (dx * dx) + (dy * dy);
}

void validate_terminal_marker_net_kind_mismatch(const Schematic &schematic, SheetId sheet_id,
                                                const Sheet &sheet, DiagnosticReport &report) {
    const auto &circuit = schematic.circuit();
    for (const auto port_id : sheet.power_ports()) {
        const auto &port = schematic.get(port_id);
        const auto &net = circuit.get(port.net());

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

void validate_dense_no_connect_clusters(const Schematic &schematic, SheetId sheet_id,
                                        const Sheet &sheet, DiagnosticReport &report) {
    const auto radius_squared = dense_no_connect_cluster_radius * dense_no_connect_cluster_radius;
    for (const auto instance_id : sheet.symbol_instances()) {
        const auto &instance = schematic.get(instance_id);
        auto markers = std::vector<NoConnectMarkerId>{};
        for (const auto marker_id : sheet.no_connect_markers()) {
            const auto &marker = schematic.get(marker_id);
            const auto &pin = schematic.circuit().get(marker.pin());
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

} // namespace volt::detail
