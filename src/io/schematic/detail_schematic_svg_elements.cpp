#include <volt/io/schematic/detail/schematic_svg_elements.hpp>

namespace volt::io::detail {

void write_symbol_arc_svg(std::ostream &out, const SymbolArc &arc) {
    const auto sweep_magnitude = std::abs(arc.sweep_degrees());
    const auto segment_count =
        std::max<std::size_t>(1U, static_cast<std::size_t>(std::ceil(sweep_magnitude / 180.0)));
    const auto segment_sweep = arc.sweep_degrees() / static_cast<double>(segment_count);

    out << "      <path class=\"symbol-arc\" d=\"M ";
    const auto start = point_on_arc(arc, arc.start_degrees());
    write_svg_number(out, start.x());
    out << ' ';
    write_svg_number(out, start.y());

    for (std::size_t segment = 0; segment < segment_count; ++segment) {
        const auto end_degrees =
            arc.start_degrees() + (segment_sweep * static_cast<double>(segment + 1U));
        const auto end = point_on_arc(arc, end_degrees);
        const auto large_arc = std::abs(segment_sweep) > 180.0 ? 1 : 0;
        const auto sweep = segment_sweep >= 0.0 ? 1 : 0;

        out << " A ";
        write_svg_number(out, arc.radius());
        out << ' ';
        write_svg_number(out, arc.radius());
        out << " 0 " << large_arc << ' ' << sweep << ' ';
        write_svg_number(out, end.x());
        out << ' ';
        write_svg_number(out, end.y());
    }

    out << "\"/>\n";
}

[[nodiscard]] bool is_symbol_text(const SymbolPrimitive &primitive) {
    return std::holds_alternative<SymbolText>(primitive);
}

void write_symbol_primitive_svg(std::ostream &out, const SymbolPrimitive &primitive) {
    if (std::holds_alternative<SymbolLine>(primitive)) {
        const auto &line = std::get<SymbolLine>(primitive);
        out << "      <line class=\"symbol-line\"";
        write_xy_attributes(out, line.start(), "x1", "y1");
        write_xy_attributes(out, line.end(), "x2", "y2");
        out << "/>\n";
        return;
    }
    if (std::holds_alternative<SymbolRectangle>(primitive)) {
        const auto &rectangle = std::get<SymbolRectangle>(primitive);
        const auto min_x = std::min(rectangle.first_corner().x(), rectangle.second_corner().x());
        const auto min_y = std::min(rectangle.first_corner().y(), rectangle.second_corner().y());
        const auto width = std::abs(rectangle.second_corner().x() - rectangle.first_corner().x());
        const auto height = std::abs(rectangle.second_corner().y() - rectangle.first_corner().y());
        out << "      <rect class=\"symbol-rectangle\" x=\"";
        write_svg_number(out, min_x);
        out << "\" y=\"";
        write_svg_number(out, min_y);
        out << "\" width=\"";
        write_svg_number(out, width);
        out << "\" height=\"";
        write_svg_number(out, height);
        out << "\"/>\n";
        return;
    }
    if (std::holds_alternative<SymbolCircle>(primitive)) {
        const auto &circle = std::get<SymbolCircle>(primitive);
        out << "      <circle class=\"symbol-circle\" cx=\"";
        write_svg_number(out, circle.center().x());
        out << "\" cy=\"";
        write_svg_number(out, circle.center().y());
        out << "\" r=\"";
        write_svg_number(out, circle.radius());
        out << "\"/>\n";
        return;
    }
    if (std::holds_alternative<SymbolArc>(primitive)) {
        write_symbol_arc_svg(out, std::get<SymbolArc>(primitive));
        return;
    }

    const auto &text = std::get<SymbolText>(primitive);
    out << "      <text class=\"symbol-text\" x=\"";
    write_svg_number(out, text.anchor().x());
    out << "\" y=\"";
    write_svg_number(out, text.anchor().y());
    out << '"';
    write_text_presentation_attributes(out, text.style());
    out << " transform=\"rotate(";
    write_svg_number(out, orientation_degrees(text.orientation()));
    out << ' ';
    write_svg_number(out, text.anchor().x());
    out << ' ';
    write_svg_number(out, text.anchor().y());
    out << ")\">" << svg_escape(text.text()) << "</text>\n";
}

void write_symbol_pin_svg(std::ostream &out, const SymbolPin &pin) {
    out << "      <circle class=\"pin-anchor\" cx=\"";
    write_svg_number(out, pin.anchor().x());
    out << "\" cy=\"";
    write_svg_number(out, pin.anchor().y());
    out << "\" r=\"";
    write_svg_number(out, schematic_svg_visual_scale.pin_anchor_radius);
    out << "\"/>\n";
    out << "      <text class=\"pin-label\" x=\"";
    write_svg_number(out, pin.anchor().x());
    out << "\" y=\"";
    write_svg_number(out, pin.anchor().y() + debug_pin_label_offset);
    out << "\">" << svg_escape(pin.name()) << "</text>\n";
}

void write_symbol_instance_svg(std::ostream &out, const Schematic &schematic, SymbolInstanceId id) {
    const auto &instance = schematic.symbol_instance(id);
    const auto &symbol = schematic.symbol_definition(instance.symbol_definition());

    out << "    <g class=\"symbol-instance\" data-component=\""
        << svg_escape(svg_component_id(instance.component())) << "\" data-symbol-definition=\""
        << svg_escape(svg_symbol_def_id(instance.symbol_definition()))
        << "\" transform=\"translate(";
    write_svg_number(out, instance.position().x());
    out << ' ';
    write_svg_number(out, instance.position().y());
    out << ") rotate(";
    write_svg_number(out, orientation_degrees(instance.orientation()));
    out << ")\">\n";
    for (const auto &primitive : symbol.primitives()) {
        if (is_symbol_text(primitive)) {
            continue;
        }
        write_symbol_primitive_svg(out, primitive);
    }
    out << "    </g>\n";
}

void write_symbol_text_instance_svg(std::ostream &out, const Schematic &schematic,
                                    SymbolInstanceId id) {
    const auto &instance = schematic.symbol_instance(id);
    const auto &symbol = schematic.symbol_definition(instance.symbol_definition());
    const auto has_text =
        std::any_of(symbol.primitives().begin(), symbol.primitives().end(),
                    [](const auto &primitive) { return is_symbol_text(primitive); });
    if (!has_text) {
        return;
    }

    out << "    <g class=\"symbol-text-instance\" data-component=\""
        << svg_escape(svg_component_id(instance.component())) << "\" data-symbol-definition=\""
        << svg_escape(svg_symbol_def_id(instance.symbol_definition()))
        << "\" transform=\"translate(";
    write_svg_number(out, instance.position().x());
    out << ' ';
    write_svg_number(out, instance.position().y());
    out << ") rotate(";
    write_svg_number(out, orientation_degrees(instance.orientation()));
    out << ")\">\n";
    for (const auto &primitive : symbol.primitives()) {
        if (!is_symbol_text(primitive)) {
            continue;
        }
        write_symbol_primitive_svg(out, primitive);
    }
    out << "    </g>\n";
}

void write_symbol_debug_overlay_svg(std::ostream &out, const Schematic &schematic,
                                    SymbolInstanceId id) {
    const auto &instance = schematic.symbol_instance(id);
    const auto &symbol = schematic.symbol_definition(instance.symbol_definition());

    out << "      <g class=\"symbol-debug-overlay\" data-component=\""
        << svg_escape(svg_component_id(instance.component())) << "\" data-symbol-definition=\""
        << svg_escape(svg_symbol_def_id(instance.symbol_definition()))
        << "\" transform=\"translate(";
    write_svg_number(out, instance.position().x());
    out << ' ';
    write_svg_number(out, instance.position().y());
    out << ") rotate(";
    write_svg_number(out, orientation_degrees(instance.orientation()));
    out << ")\">\n";
    for (const auto &pin : symbol.pins()) {
        write_symbol_pin_svg(out, pin);
    }
    out << "      </g>\n";
}

void write_wire_run_svg(std::ostream &out, const Schematic &schematic, WireRunId id) {
    const auto &wire = schematic.wire_run(id);

    out << "    <polyline class=\"wire-run\" data-net=\"" << svg_escape(svg_net_id(wire.net()))
        << "\" points=\"";
    for (std::size_t index = 0; index < wire.points().size(); ++index) {
        if (index != 0) {
            out << ' ';
        }
        write_svg_number(out, wire.points()[index].x());
        out << ',';
        write_svg_number(out, wire.points()[index].y());
    }
    out << "\"/>\n";
}

void write_net_label_svg(std::ostream &out, const Schematic &schematic, NetLabelId id) {
    const auto &label = schematic.net_label(id);
    const auto &net = schematic.circuit().get(label.net());
    const auto &text = label.label().value_or(net.name().value());
    const auto text_position = label.text_position();

    out << "    <text class=\"net-label\" data-net=\"" << svg_escape(svg_net_id(label.net()))
        << "\" x=\"";
    write_svg_number(out, text_position.x());
    out << "\" y=\"";
    write_svg_number(out, text_position.y());
    out << '"';
    write_text_presentation_attributes(out, label.style());
    out << " transform=\"rotate(";
    write_svg_number(out, orientation_degrees(label.orientation()));
    out << ' ';
    write_svg_number(out, text_position.x());
    out << ' ';
    write_svg_number(out, text_position.y());
    out << ")\">" << svg_escape(text) << "</text>\n";
}

void write_junction_svg(std::ostream &out, const Schematic &schematic, JunctionId id) {
    const auto &junction = schematic.junction(id);

    out << "    <circle class=\"junction\" data-net=\"" << svg_escape(svg_net_id(junction.net()))
        << "\" cx=\"";
    write_svg_number(out, junction.position().x());
    out << "\" cy=\"";
    write_svg_number(out, junction.position().y());
    out << "\" r=\"";
    write_svg_number(out, schematic_svg_visual_scale.junction_radius);
    out << "\"/>\n";
}

void write_power_port_svg(std::ostream &out, const Schematic &schematic, PowerPortId id) {
    const auto &port = schematic.power_port(id);
    const auto &net = schematic.circuit().get(port.net());
    const auto kind_class = power_port_class(port.kind());

    out << "    <g class=\"power-port " << kind_class << "\" data-net=\""
        << svg_escape(svg_net_id(port.net())) << "\" transform=\"translate(";
    write_svg_number(out, port.position().x());
    out << ' ';
    write_svg_number(out, port.position().y());
    out << ")";
    const auto glyph_degrees = power_port_glyph_degrees(port.kind(), port.orientation());
    if (std::abs(glyph_degrees) >= 1e-12) {
        out << " rotate(";
        write_svg_number(out, glyph_degrees);
        out << ")";
    }
    out << "\">\n";
    if (port.kind() == PowerPortKind::Ground) {
        out << "      <line class=\"power-port-line\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"";
        write_svg_number(out, ground_port_stem_length);
        out << "\"/>\n";
        out << "      <line class=\"ground-bar\" x1=\"-3.6\" y1=\"";
        write_svg_number(out, ground_port_stem_length);
        out << "\" x2=\"3.6\" y2=\"";
        write_svg_number(out, ground_port_stem_length);
        out << "\"/>\n";
        out << "      <line class=\"ground-bar\" x1=\"-2.2\" y1=\"4.6\" x2=\"2.2\" y2=\"4.6\"/>\n";
        out << "      <line class=\"ground-bar\" x1=\"-0.9\" y1=\"6\" x2=\"0.9\" y2=\"6\"/>\n";
    } else {
        out << "      <line class=\"power-port-line\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"-";
        write_svg_number(out, power_port_stem_length);
        out << "\"/>\n";
        out << "      <path class=\"power-port-shape\" d=\"M -";
        write_svg_number(out, power_port_half_width);
        out << " -";
        write_svg_number(out, power_port_stem_length);
        out << " L 0 -";
        write_svg_number(out, power_port_tip_offset);
        out << " L ";
        write_svg_number(out, power_port_half_width);
        out << " -";
        write_svg_number(out, power_port_stem_length);
        out << " Z\"/>\n";
    }
    const auto port_label = port.label().value_or(net.name().value());
    if (!port.explicit_label_position()) {
        const auto label_y = port.kind() == PowerPortKind::Ground ? ground_port_label_offset
                                                                  : -power_port_label_offset;
        out << "      <text class=\"power-port-label\" x=\"0\" y=\"";
        write_svg_number(out, label_y);
        out << "\"";
        write_upright_text_transform_degrees(out, glyph_degrees, Point{0.0, label_y});
        out << ">" << svg_escape(port_label) << "</text>\n";
    }
    out << "    </g>\n";
    if (port.explicit_label_position()) {
        const auto label_position = *port.explicit_label_position();
        out << "    <text class=\"power-port-label\" data-net=\""
            << svg_escape(svg_net_id(port.net())) << "\" x=\"";
        write_svg_number(out, label_position.x());
        out << "\" y=\"";
        write_svg_number(out, label_position.y());
        out << "\">" << svg_escape(port_label) << "</text>\n";
    }
}

void write_no_connect_marker_svg(std::ostream &out, const Schematic &schematic,
                                 NoConnectMarkerId id) {
    const auto &marker = schematic.no_connect_marker(id);

    out << "    <g class=\"no-connect-marker\" data-pin=\"" << svg_escape(svg_pin_id(marker.pin()))
        << "\" transform=\"translate(";
    write_svg_number(out, marker.position().x());
    out << ' ';
    write_svg_number(out, marker.position().y());
    out << ") rotate(";
    write_svg_number(out, orientation_degrees(marker.orientation()));
    out << ")\">\n";
    out << "      <line class=\"no-connect-line\" x1=\"-3\" y1=\"-3\" x2=\"3\" y2=\"3\"/>\n";
    out << "      <line class=\"no-connect-line\" x1=\"-3\" y1=\"3\" x2=\"3\" y2=\"-3\"/>\n";
    out << "    </g>\n";
}

void write_sheet_port_svg(std::ostream &out, const Schematic &schematic, SheetPortId id) {
    const auto &port = schematic.sheet_port(id);
    const auto kind_class = sheet_port_class(port.kind());

    out << "    <g class=\"sheet-port " << kind_class << "\" data-net=\""
        << svg_escape(svg_net_id(port.net())) << "\" transform=\"translate(";
    write_svg_number(out, port.position().x());
    out << ' ';
    write_svg_number(out, port.position().y());
    out << ") rotate(";
    write_svg_number(out, orientation_degrees(port.orientation()));
    out << ")\">\n";
    const auto body_length = sheet_port_body_length(port.name());
    const auto tip_x = body_length + sheet_port_tip_length;
    out << "      <path class=\"sheet-port-shape\" d=\"M 0 -";
    write_svg_number(out, sheet_port_half_height);
    out << " L ";
    write_svg_number(out, body_length);
    out << " -";
    write_svg_number(out, sheet_port_half_height);
    out << " L ";
    write_svg_number(out, tip_x);
    out << " 0 L ";
    write_svg_number(out, body_length);
    out << ' ';
    write_svg_number(out, sheet_port_half_height);
    out << " L 0 ";
    write_svg_number(out, sheet_port_half_height);
    out << " Z\"/>\n";
    const auto label_x = body_length * 0.5;
    const auto label_anchor = Point{label_x, sheet_port_label_baseline};
    out << "      <text class=\"sheet-port-label\" x=\"";
    write_svg_number(out, label_x);
    out << "\" y=\"";
    write_svg_number(out, sheet_port_label_baseline);
    out << "\"";
    write_upright_text_transform(out, port.orientation(), label_anchor);
    out << ">" << svg_escape(port.name()) << "</text>\n";
    out << "    </g>\n";
}

void write_symbol_field_svg(std::ostream &out, const Schematic &schematic, SymbolFieldId id) {
    const auto &field = schematic.symbol_field(id);

    out << "    <text class=\"symbol-field\" data-symbol-instance=\""
        << svg_escape(svg_symbol_instance_id(field.symbol_instance())) << "\" data-field=\""
        << svg_escape(field.name()) << "\" x=\"";
    write_svg_number(out, field.position().x());
    out << "\" y=\"";
    write_svg_number(out, field.position().y());
    out << '"';
    write_text_presentation_attributes(out, field.style());
    out << " transform=\"rotate(";
    write_svg_number(out, orientation_degrees(field.orientation()));
    out << ' ';
    write_svg_number(out, field.position().x());
    out << ' ';
    write_svg_number(out, field.position().y());
    out << ")\">" << svg_escape(field.value()) << "</text>\n";
}

} // namespace volt::io::detail
