#include <volt/io/schematic/schematic_svg_writer.hpp>

#include <volt/core/errors.hpp>

namespace volt::io::detail {

[[nodiscard]] SvgBounds symbol_primitive_bounds(const SymbolPrimitive &primitive,
                                                const SymbolInstance &instance) {
    const auto stroke_padding = schematic_svg_visual_scale.symbol_stroke_width / 2.0;
    return ::volt::detail::symbol_primitive_bounds(
        primitive, instance, stroke_padding, stroke_padding,
        schematic_svg_visual_scale.symbol_text_font_size);
}

[[nodiscard]] SvgBounds symbol_instance_bounds(const Schematic &schematic, SymbolInstanceId id) {
    const auto stroke_padding = schematic_svg_visual_scale.symbol_stroke_width / 2.0;
    return ::volt::detail::symbol_instance_bounds(schematic, id, stroke_padding, stroke_padding,
                                                  schematic_svg_visual_scale.symbol_text_font_size);
}

[[nodiscard]] SvgBounds symbol_debug_overlay_bounds(const Schematic &schematic,
                                                    SymbolInstanceId id) {
    const auto &instance = schematic.symbol_instance(id);
    const auto &symbol = schematic.symbol_definition(instance.symbol_definition());
    auto bounds = bounds_from_point(instance.position());
    const auto pin_anchor_padding = schematic_svg_visual_scale.pin_anchor_radius +
                                    (schematic_svg_visual_scale.pin_overlay_stroke_width / 2.0);
    for (const auto &pin : symbol.pins()) {
        const auto anchor =
            transform_schematic_point(pin.anchor(), instance.position(), instance.orientation());
        include_bounds(bounds, padded_bounds(bounds_from_point(anchor), pin_anchor_padding));
        const auto label_anchor = transform_schematic_point(
            Point{pin.anchor().x(), pin.anchor().y() + debug_pin_label_offset}, instance.position(),
            instance.orientation());
        include_bounds(bounds, text_bounds(label_anchor, instance.orientation(), pin.name(),
                                           SchematicTextStyle{},
                                           schematic_svg_visual_scale.pin_label_font_size));
    }
    return bounds;
}

[[nodiscard]] SvgBounds wire_run_bounds(const WireRun &wire) {
    return ::volt::detail::wire_run_bounds(wire,
                                           schematic_svg_visual_scale.wire_stroke_width / 2.0);
}

[[nodiscard]] SchematicOrientation power_port_bounds_orientation(PowerPortKind kind,
                                                                 SchematicOrientation orientation) {
    switch (kind) {
    case PowerPortKind::Power:
        return orientation_from_quarter_turns(orientation_quarter_turns(orientation) + 1);
    case PowerPortKind::Ground:
        return orientation_from_quarter_turns(orientation_quarter_turns(orientation) - 1);
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled power port kind"};
}

[[nodiscard]] Point transformed_power_port_anchor(const PowerPort &port, Point local_anchor) {
    return transform_schematic_point(
        local_anchor, port.position(),
        power_port_bounds_orientation(port.kind(), port.orientation()));
}

[[nodiscard]] SvgBounds power_port_label_bounds(const PowerPort &port, std::string_view label) {
    return ::volt::detail::power_port_label_bounds(
        port, label, schematic_svg_visual_scale.tag_port_label_font_size);
}

[[nodiscard]] SvgBounds power_port_bounds(const PowerPort &port, std::string_view label) {
    const auto stroke_padding = schematic_svg_visual_scale.tag_port_stroke_width / 2.0;
    return ::volt::detail::power_port_bounds(port, label, stroke_padding,
                                             schematic_svg_visual_scale.tag_port_label_font_size);
}

[[nodiscard]] SvgBounds no_connect_marker_bounds(const NoConnectMarker &marker) {
    return ::volt::detail::no_connect_marker_bounds(
        marker, 3.0 + (schematic_svg_visual_scale.no_connect_stroke_width / 2.0));
}

[[nodiscard]] Point transformed_sheet_port_anchor(const SheetPort &port, Point local_anchor) {
    return transform_schematic_point(local_anchor, port.position(), port.orientation());
}

[[nodiscard]] SvgBounds sheet_port_label_bounds(const SheetPort &port) {
    return ::volt::detail::sheet_port_label_bounds(
        port, schematic_svg_visual_scale.tag_port_label_font_size);
}

[[nodiscard]] SvgBounds sheet_port_bounds(const SheetPort &port) {
    return ::volt::detail::sheet_port_bounds(port,
                                             schematic_svg_visual_scale.tag_port_stroke_width / 2.0,
                                             schematic_svg_visual_scale.tag_port_label_font_size);
}

[[nodiscard]] std::optional<SvgBounds> sheet_content_bounds(const Schematic &schematic,
                                                            SheetId sheet_id,
                                                            SchematicSvgBodyOptions options) {
    const auto &sheet = schematic.sheet(sheet_id);
    auto bounds = std::optional<SvgBounds>{};
    const auto include = [&bounds](SvgBounds next) {
        if (bounds.has_value()) {
            include_bounds(bounds.value(), next);
        } else {
            bounds = next;
        }
    };

    if (options.include_regions) {
        for (std::size_t index = 0; index < sheet.regions().size(); ++index) {
            const auto region = sheet.region(index).bounds();
            include(rect_bounds(region.x(), region.y(), region.width(), region.height()));
        }
    }
    for (const auto instance : sheet.symbol_instances()) {
        include(::volt::io::detail::symbol_instance_bounds(schematic, instance));
    }
    for (const auto wire : sheet.wire_runs()) {
        include(::volt::io::detail::wire_run_bounds(schematic.wire_run(wire)));
    }
    for (const auto junction : sheet.junctions()) {
        include(padded_bounds(bounds_from_point(schematic.junction(junction).position()),
                              schematic_svg_visual_scale.junction_radius));
    }
    for (const auto port_id : sheet.power_ports()) {
        const auto &port = schematic.power_port(port_id);
        const auto &net = schematic.circuit().net(port.net());
        include(
            ::volt::io::detail::power_port_bounds(port, port.label().value_or(net.name().value())));
    }
    for (const auto marker : sheet.no_connect_markers()) {
        include(::volt::io::detail::no_connect_marker_bounds(schematic.no_connect_marker(marker)));
    }
    for (const auto port : sheet.sheet_ports()) {
        include(::volt::io::detail::sheet_port_bounds(schematic.sheet_port(port)));
    }
    for (const auto label_id : sheet.net_labels()) {
        const auto &label = schematic.net_label(label_id);
        const auto &net = schematic.circuit().net(label.net());
        include(text_bounds(label.text_position(), label.orientation(),
                            label.label().value_or(net.name().value()), label.style(),
                            schematic_svg_visual_scale.net_label_font_size));
    }
    for (const auto field_id : sheet.symbol_fields()) {
        const auto &field = schematic.symbol_field(field_id);
        include(text_bounds(field.position(), field.orientation(), field.value(), field.style(),
                            schematic_svg_visual_scale.symbol_field_font_size));
    }
    if (options.svg.debug_overlays) {
        for (const auto instance : sheet.symbol_instances()) {
            include(::volt::io::detail::symbol_debug_overlay_bounds(schematic, instance));
        }
    }
    return bounds;
}

[[nodiscard]] SvgBounds expanded_body_bounds(const Schematic &schematic, SheetId sheet_id,
                                             SchematicSvgBodyOptions options) {
    if (!std::isfinite(options.margin) || options.margin < 0.0) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Schematic SVG body margin must be finite and non-negative"};
    }
    auto bounds =
        sheet_content_bounds(schematic, sheet_id, options).value_or(SvgBounds{0.0, 0.0, 1.0, 1.0});
    return padded_bounds(bounds, options.margin);
}

void write_svg_style(std::ostream &out, SchematicSvgOptions options) {
    const auto &scale = schematic_svg_visual_scale;

    out << "  <style>"
           ".document-background,.sheet{fill:#fff;stroke:none}"
           ".sheet-border{fill:none;stroke:#111;";
    write_css_stroke_width(out, scale.sheet_border_stroke_width);
    out << "}.drawing-frame{fill:none;stroke:#111;";
    write_css_stroke_width(out, scale.drawing_frame_stroke_width);
    out << "}.sheet-grid{stroke:none}"
           ".sheet-grid-dot{fill:#d7d7d7;stroke:none}"
           ".coordinate-zone-label{";
    write_css_font(out, scale.coordinate_zone_font_size);
    out << ";fill:#444;text-anchor:middle;dominant-baseline:middle}"
           ".sheet-title{font-weight:600}"
           ".title-block-outline,.title-block-rule{fill:none;stroke:#111;";
    write_css_stroke_width(out, scale.title_block_stroke_width);
    out << "}.title-block-label{";
    write_css_font(out, scale.title_block_font_size);
    out << ";fill:#444;text-anchor:start}"
           ".title-block-value{";
    write_css_font(out, scale.title_block_font_size);
    out << ";fill:#111;text-anchor:start}"
           ".sheet-region-frame{fill:#fff;fill-opacity:0;stroke:#78716c;";
    write_css_stroke_width(out, scale.region_frame_stroke_width);
    out << "}.sheet-region-frame.dashed{stroke-dasharray:3 2}"
           ".sheet-region-title{";
    write_css_font(out, scale.region_title_font_size);
    out << ";fill:#57534e;text-anchor:start;font-weight:600}"
           ".wire-run{fill:none;stroke:#111;";
    write_css_stroke_width(out, scale.wire_stroke_width);
    out << ";stroke-linecap:round;stroke-linejoin:round}.net-label{";
    write_css_font(out, scale.net_label_font_size);
    out << ";fill:#111}"
           ".junction{fill:#111;stroke:none}"
           ".power-port-line,.ground-bar{stroke:#111;";
    write_css_stroke_width(out, scale.tag_port_stroke_width);
    out << ";stroke-linecap:round;stroke-linejoin:round}.no-connect-line{stroke:#111;";
    write_css_stroke_width(out, scale.no_connect_stroke_width);
    out << ";stroke-linecap:round}"
           ".power-port-shape,.sheet-port-shape{fill:#fff;stroke:#111;";
    write_css_stroke_width(out, scale.tag_port_stroke_width);
    out << ";stroke-linejoin:round}";
    out << ".power-port-label,.sheet-port-label{";
    write_css_font(out, scale.tag_port_label_font_size);
    out << ";fill:#111;text-anchor:middle}"
           ".symbol-field{";
    write_css_font(out, scale.symbol_field_font_size);
    out << ";fill:#111}"
           ".symbol-line,.symbol-rectangle,.symbol-circle,.symbol-arc{fill:none;stroke:#111;";
    write_css_stroke_width(out, scale.symbol_stroke_width);
    out << ";stroke-linecap:round;stroke-linejoin:round}.symbol-text{";
    write_css_font(out, scale.symbol_text_font_size);
    out << ";fill:#111}";
    if (options.debug_overlays) {
        out << ".pin-anchor{fill:#fff;stroke:#c2410c;";
        write_css_stroke_width(out, scale.pin_overlay_stroke_width);
        out << "}.pin-label{";
        write_css_font(out, scale.pin_label_font_size);
        out << ";fill:#c2410c;text-anchor:middle}";
    }
    out << "</style>\n";
}

void write_sheet_svg(std::ostream &out, const Schematic &schematic, SheetId sheet_id,
                     double y_offset, SchematicSvgOptions options) {
    const auto &sheet = schematic.sheet(sheet_id);

    out << "  <g class=\"schematic-sheet\" data-sheet=\"" << svg_escape(svg_sheet_id(sheet_id))
        << "\" transform=\"translate(0 ";
    write_svg_number(out, y_offset);
    out << ")\">\n";
    write_sheet_defs_svg(out, sheet_id, sheet);
    write_sheet_page_chrome_svg(out, sheet_id, sheet);
    write_schematic_object_layers_svg(
        out, schematic, sheet_id,
        SchematicSvgLayerOptions{.include_regions = true,
                                 .debug_overlays = options.debug_overlays});
    out << "  </g>\n";
}

} // namespace volt::io::detail

namespace volt::io {

void write_schematic_body_svg(std::ostream &out, const Schematic &schematic, SheetId sheet_id,
                              SchematicSvgBodyOptions options) {
    const auto bounds = detail::expanded_body_bounds(schematic, sheet_id, options);
    const auto width = detail::bounds_width(bounds);
    const auto height = detail::bounds_height(bounds);

    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"";
    detail::write_svg_number(out, bounds.min_x);
    out << ' ';
    detail::write_svg_number(out, bounds.min_y);
    out << ' ';
    detail::write_svg_number(out, width);
    out << ' ';
    detail::write_svg_number(out, height);
    out << "\" width=\"";
    detail::write_svg_number(out, width);
    out << "\" height=\"";
    detail::write_svg_number(out, height);
    out << "\">\n";
    detail::write_svg_style(out, options.svg);
    const auto &sheet = schematic.sheet(sheet_id);
    if (options.include_regions && !sheet.regions().empty()) {
        out << "  <defs>\n";
        detail::write_region_title_clip_defs_svg(out, sheet_id, sheet);
        out << "  </defs>\n";
    }
    out << "  <g class=\"schematic-body\" data-sheet=\""
        << detail::svg_escape(detail::svg_sheet_id(sheet_id)) << "\">\n";
    const auto layer_options = detail::SchematicSvgLayerOptions{
        .include_regions = options.include_regions,
        .debug_overlays = options.svg.debug_overlays,
    };
    detail::write_schematic_object_layers_svg(out, schematic, sheet_id, layer_options);
    out << "  </g>\n";
    out << "</svg>\n";
}

[[nodiscard]] std::string write_schematic_body_svg(const Schematic &schematic, SheetId sheet_id,
                                                   SchematicSvgBodyOptions options) {
    auto out = std::ostringstream{};
    write_schematic_body_svg(out, schematic, sheet_id, options);
    return out.str();
}

void write_schematic_svg(std::ostream &out, const Schematic &schematic,
                         SchematicSvgOptions options) {
    const auto sheet_count = schematic.sheet_count();
    auto width = detail::svg_sheet_width;
    auto height = sheet_count == 0 ? detail::svg_sheet_height : 0.0;
    for (std::size_t sheet_index = 0; sheet_index < sheet_count; ++sheet_index) {
        const auto &metadata = schematic.sheet(SheetId{sheet_index}).metadata();
        width = std::max(width, metadata.size().width());
        if (sheet_index != 0) {
            height += detail::svg_sheet_gap;
        }
        height += metadata.size().height();
    }

    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 ";
    detail::write_svg_number(out, width);
    out << ' ';
    detail::write_svg_number(out, height);
    out << "\" width=\"";
    detail::write_svg_number(out, width);
    out << "\" height=\"";
    detail::write_svg_number(out, height);
    out << "\">\n";
    detail::write_svg_style(out, options);
    out << "  <rect class=\"document-background\" x=\"0\" y=\"0\" width=\"";
    detail::write_svg_number(out, width);
    out << "\" height=\"";
    detail::write_svg_number(out, height);
    out << "\"/>\n";

    auto y_offset = 0.0;
    for (std::size_t sheet_index = 0; sheet_index < sheet_count; ++sheet_index) {
        const auto sheet_id = SheetId{sheet_index};
        detail::write_sheet_svg(out, schematic, sheet_id, y_offset, options);
        y_offset += schematic.sheet(sheet_id).metadata().size().height() + detail::svg_sheet_gap;
    }

    out << "</svg>\n";
}

[[nodiscard]] std::string write_schematic_svg(const Schematic &schematic,
                                              SchematicSvgOptions options) {
    auto out = std::ostringstream{};
    write_schematic_svg(out, schematic, options);
    return out.str();
}

void write_schematic_sheet_svg(std::ostream &out, const Schematic &schematic, SheetId sheet_id,
                               SchematicSvgOptions options) {
    const auto &metadata = schematic.sheet(sheet_id).metadata();
    const auto width = metadata.size().width();
    const auto height = metadata.size().height();

    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 ";
    detail::write_svg_number(out, width);
    out << ' ';
    detail::write_svg_number(out, height);
    out << "\" width=\"";
    detail::write_svg_number(out, width);
    out << "\" height=\"";
    detail::write_svg_number(out, height);
    out << "\">\n";
    detail::write_svg_style(out, options);
    out << "  <rect class=\"document-background\" x=\"0\" y=\"0\" width=\"";
    detail::write_svg_number(out, width);
    out << "\" height=\"";
    detail::write_svg_number(out, height);
    out << "\"/>\n";
    detail::write_sheet_svg(out, schematic, sheet_id, 0.0, options);
    out << "</svg>\n";
}

[[nodiscard]] std::string write_schematic_sheet_svg(const Schematic &schematic, SheetId sheet_id,
                                                    SchematicSvgOptions options) {
    auto out = std::ostringstream{};
    write_schematic_sheet_svg(out, schematic, sheet_id, options);
    return out.str();
}

[[nodiscard]] std::vector<SchematicSvgPage> write_schematic_svg_pages(const Schematic &schematic,
                                                                      SchematicSvgOptions options) {
    auto pages = std::vector<SchematicSvgPage>{};
    pages.reserve(schematic.sheet_count());
    for (std::size_t sheet_index = 0; sheet_index < schematic.sheet_count(); ++sheet_index) {
        const auto sheet_id = SheetId{sheet_index};
        const auto &sheet = schematic.sheet(sheet_id);
        pages.push_back(SchematicSvgPage{
            sheet_id,
            sheet.name(),
            write_schematic_sheet_svg(schematic, sheet_id, options),
        });
    }
    return pages;
}

} // namespace volt::io
