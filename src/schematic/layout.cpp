#include <volt/schematic/layout.hpp>

namespace volt::detail {

[[nodiscard]] std::vector<Point> schematic_text_candidate_positions(Point anchor) {
    auto candidates = std::vector<Point>{};
    candidates.reserve(65U);
    candidates.push_back(anchor);

    constexpr auto offsets = std::array<double, 8U>{4.0, 6.0, 8.0, 12.0, 16.0, 22.0, 28.0, 36.0};
    constexpr auto directions = std::array<std::pair<double, double>, 8U>{{{1.0, 0.0},
                                                                           {0.0, -1.0},
                                                                           {0.0, 1.0},
                                                                           {1.0, -1.0},
                                                                           {1.0, 1.0},
                                                                           {-1.0, 0.0},
                                                                           {-1.0, -1.0},
                                                                           {-1.0, 1.0}}};
    for (const auto distance : offsets) {
        for (const auto &[dx, dy] : directions) {
            candidates.emplace_back(anchor.x() + (dx * distance), anchor.y() + (dy * distance));
        }
    }
    return candidates;
}

[[nodiscard]] bool schematic_text_bounds_clear_of_wires(const Schematic &schematic,
                                                        const Sheet &sheet,
                                                        SchematicBounds bounds) {
    for (const auto wire_id : sheet.wire_runs()) {
        const auto &wire = schematic.get(wire_id);
        for (std::size_t point_index = 1; point_index < wire.points().size(); ++point_index) {
            const auto segment =
                SchematicSegment{wire.points()[point_index - 1U], wire.points()[point_index]};
            if (segment_intersects_bounds(segment, bounds)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool schematic_text_bounds_clear_of_symbols(const Schematic &schematic,
                                                          const Sheet &sheet,
                                                          SchematicBounds bounds) {
    for (const auto instance_id : sheet.symbol_instances()) {
        if (intersects_bounds(bounds, symbol_instance_bounds(schematic, instance_id))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool schematic_text_bounds_clear_of_fixed_objects(const Schematic &schematic,
                                                                const Sheet &sheet,
                                                                SchematicBounds bounds) {
    for (const auto junction_id : sheet.junctions()) {
        const auto &junction = schematic.get(junction_id);
        if (intersects_bounds(bounds, padded_bounds(bounds_from_point(junction.position()), 1.8))) {
            return false;
        }
    }
    for (const auto port_id : sheet.power_ports()) {
        if (intersects_bounds(bounds, power_port_glyph_bounds(schematic.get(port_id)))) {
            return false;
        }
    }
    for (const auto marker_id : sheet.no_connect_markers()) {
        if (intersects_bounds(bounds, no_connect_marker_bounds(schematic.get(marker_id)))) {
            return false;
        }
    }
    for (const auto port_id : sheet.sheet_ports()) {
        if (intersects_bounds(bounds, sheet_port_bounds(schematic.get(port_id)))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool
schematic_text_bounds_clear_of_placed_text(SchematicBounds bounds,
                                           const std::vector<SchematicBounds> &placed_text_bounds) {
    for (const auto placed : placed_text_bounds) {
        if (intersects_bounds(bounds, placed)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool
schematic_text_candidate_is_clear(const Schematic &schematic, const Sheet &sheet,
                                  SchematicBounds bounds,
                                  const std::vector<SchematicBounds> &placed_text_bounds,
                                  double clearance, std::optional<std::size_t> authored_region) {
    const auto keepout = padded_bounds(bounds, clearance);
    if (authored_region.has_value() &&
        !contains_bounds(region_bounds(sheet.region(authored_region.value())), keepout)) {
        return false;
    }
    return schematic_text_bounds_clear_of_wires(schematic, sheet, keepout) &&
           schematic_text_bounds_clear_of_symbols(schematic, sheet, keepout) &&
           schematic_text_bounds_clear_of_fixed_objects(schematic, sheet, keepout) &&
           schematic_text_bounds_clear_of_placed_text(keepout, placed_text_bounds);
}

[[nodiscard]] Point
choose_net_label_position(const Schematic &schematic, const Sheet &sheet, const NetLabel &label,
                          const std::vector<SchematicBounds> &placed_text_bounds,
                          double clearance) {
    const auto &net = schematic.circuit().get(label.net());
    const auto text = label.label().value_or(net.name().value());
    for (const auto candidate : schematic_text_candidate_positions(label.text_position())) {
        const auto bounds = text_bounds(candidate, label.orientation(), text, label.style(),
                                        net_label_rendered_font_size);
        if (schematic_text_candidate_is_clear(schematic, sheet, bounds, placed_text_bounds,
                                              clearance, label.authored_region())) {
            return candidate;
        }
    }
    return label.text_position();
}

[[nodiscard]] Point choose_symbol_field_position(
    const Schematic &schematic, const Sheet &sheet, const SymbolField &field,
    const std::vector<SchematicBounds> &placed_text_bounds, double clearance) {
    const auto owner_bounds = symbol_instance_bounds(schematic, field.symbol_instance());
    auto best_position = field.position();
    auto best_anchor_distance = std::numeric_limits<double>::max();
    auto best_owner_gap = std::numeric_limits<double>::max();
    auto found = false;

    for (const auto candidate : schematic_text_candidate_positions(field.position())) {
        const auto bounds = text_bounds(candidate, field.orientation(), field.value(),
                                        field.style(), symbol_field_rendered_font_size);
        if (!schematic_text_candidate_is_clear(schematic, sheet, bounds, placed_text_bounds,
                                               clearance, field.authored_region())) {
            continue;
        }
        const auto dx = candidate.x() - field.position().x();
        const auto dy = candidate.y() - field.position().y();
        const auto anchor_distance = (dx * dx) + (dy * dy);
        const auto owner_gap = bounds_gap(owner_bounds, bounds);
        if (!found || anchor_distance < best_anchor_distance ||
            (std::abs(anchor_distance - best_anchor_distance) <= schematic_geometry_tolerance &&
             owner_gap < best_owner_gap)) {
            best_position = candidate;
            best_anchor_distance = anchor_distance;
            best_owner_gap = owner_gap;
            found = true;
        }
    }
    return found ? best_position : field.position();
}

[[nodiscard]] Point default_power_port_label_position(const PowerPort &port) {
    const auto label_y =
        port.kind() == PowerPortKind::Ground ? ground_port_label_offset : -power_port_label_offset;
    return transformed_port_anchor(port, Point{0.0, label_y});
}

[[nodiscard]] Point choose_power_port_label_position(
    const Schematic &schematic, const Sheet &sheet, const PowerPort &port,
    const std::vector<SchematicBounds> &placed_text_bounds, double clearance) {
    const auto &net = schematic.circuit().get(port.net());
    const auto label = port.label().value_or(net.name().value());
    const auto anchor =
        port.explicit_label_position().value_or(default_power_port_label_position(port));
    for (const auto candidate : schematic_text_candidate_positions(anchor)) {
        const auto bounds = text_bounds(candidate, SchematicOrientation::Right, label,
                                        sheet_port_rendered_label_font_size, true);
        if (schematic_text_candidate_is_clear(schematic, sheet, bounds, placed_text_bounds,
                                              clearance, port.authored_region())) {
            return candidate;
        }
    }
    return anchor;
}

void collect_fixed_text_bounds(const Schematic &schematic, const Sheet &sheet,
                               std::vector<SchematicBounds> &placed_text_bounds) {
    for (const auto port_id : sheet.sheet_ports()) {
        placed_text_bounds.push_back(sheet_port_label_bounds(schematic.get(port_id)));
    }
}

} // namespace volt::detail

namespace volt {

void layout_schematic_text(Schematic &schematic, SchematicTextLayoutOptions options) {
    for (std::size_t sheet_index = 0; sheet_index < schematic.all<volt::SheetId>().size();
         ++sheet_index) {
        const auto sheet_id = SheetId{sheet_index};
        const auto &sheet = schematic.get(sheet_id);
        auto placed_text_bounds = std::vector<detail::SchematicBounds>{};
        detail::collect_fixed_text_bounds(schematic, sheet, placed_text_bounds);

        for (const auto port_id : sheet.power_ports()) {
            const auto position = detail::choose_power_port_label_position(
                schematic, sheet, schematic.get(port_id), placed_text_bounds, options.clearance);
            const auto &current_port = schematic.get(port_id);
            if (!same_schematic_point(
                    position, current_port.explicit_label_position().value_or(
                                  detail::default_power_port_label_position(current_port)))) {
                schematic.move(MovePowerPortLabel{port_id, position});
            }
            const auto &port = schematic.get(port_id);
            const auto &net = schematic.circuit().get(port.net());
            placed_text_bounds.push_back(
                detail::power_port_label_bounds(port, port.label().value_or(net.name().value())));
        }

        for (const auto label_id : sheet.net_labels()) {
            const auto position = detail::choose_net_label_position(
                schematic, sheet, schematic.get(label_id), placed_text_bounds, options.clearance);
            if (!same_schematic_point(position, schematic.get(label_id).text_position())) {
                schematic.move(MoveNetLabelText{label_id, position});
            }
            const auto &label = schematic.get(label_id);
            const auto &net = schematic.circuit().get(label.net());
            placed_text_bounds.push_back(
                detail::text_bounds(label.text_position(), label.orientation(),
                                    label.label().value_or(net.name().value()), label.style(),
                                    detail::net_label_rendered_font_size));
        }

        for (const auto field_id : sheet.symbol_fields()) {
            const auto position = detail::choose_symbol_field_position(
                schematic, sheet, schematic.get(field_id), placed_text_bounds, options.clearance);
            schematic.move(MoveSymbolField{field_id, position});
            const auto &field = schematic.get(field_id);
            placed_text_bounds.push_back(
                detail::text_bounds(field.position(), field.orientation(), field.value(),
                                    field.style(), detail::symbol_field_rendered_font_size));
        }
    }
}

} // namespace volt
