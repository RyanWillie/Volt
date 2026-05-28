#pragma once

#include <ostream>

#include <volt/core/ids.hpp>
#include <volt/io/detail/schematic_svg_page_chrome.hpp>
#include <volt/schematic/schematic.hpp>

// Implementation fragment for schematic_svg_writer.hpp. This header depends on
// SVG helpers declared earlier there and is not intended for standalone include.
namespace volt::io::detail {

/** Options controlling which schematic projection layers are emitted into an SVG group. */
struct SchematicSvgLayerOptions {
    /** Whether authored regions are included with the object layers. */
    bool include_regions = true;
    /** Whether debug-only visual overlays are included. */
    bool debug_overlays = false;
};

inline void write_schematic_object_layers_svg(std::ostream &out, const Schematic &schematic,
                                              SheetId sheet_id,
                                              SchematicSvgLayerOptions options = {}) {
    const auto &sheet = schematic.sheet(sheet_id);

    if (options.include_regions) {
        out << "    <g class=\"layer layer-regions\">\n";
        write_regions_svg(out, sheet_id, sheet);
        out << "    </g>\n";
    }
    out << "    <g class=\"layer layer-symbols\">\n";
    for (const auto instance : sheet.symbol_instances()) {
        write_symbol_instance_svg(out, schematic, instance);
    }
    out << "    </g>\n";
    out << "    <g class=\"layer layer-wires\">\n";
    for (const auto wire : sheet.wire_runs()) {
        write_wire_run_svg(out, schematic, wire);
    }
    out << "    </g>\n";
    out << "    <g class=\"layer layer-junctions\">\n";
    for (const auto junction : sheet.junctions()) {
        write_junction_svg(out, schematic, junction);
    }
    out << "    </g>\n";
    out << "    <g class=\"layer layer-ports\">\n";
    for (const auto port : sheet.power_ports()) {
        write_power_port_svg(out, schematic, port);
    }
    for (const auto marker : sheet.no_connect_markers()) {
        write_no_connect_marker_svg(out, schematic, marker);
    }
    for (const auto port : sheet.sheet_ports()) {
        write_sheet_port_svg(out, schematic, port);
    }
    out << "    </g>\n";
    out << "    <g class=\"layer layer-labels\">\n";
    for (const auto label : sheet.net_labels()) {
        write_net_label_svg(out, schematic, label);
    }
    out << "    </g>\n";
    out << "    <g class=\"layer layer-symbol-text\">\n";
    for (const auto instance : sheet.symbol_instances()) {
        write_symbol_text_instance_svg(out, schematic, instance);
    }
    out << "    </g>\n";
    out << "    <g class=\"layer layer-fields\">\n";
    for (const auto field : sheet.symbol_fields()) {
        write_symbol_field_svg(out, schematic, field);
    }
    out << "    </g>\n";
    if (options.debug_overlays) {
        out << "    <g class=\"layer layer-debug\">\n";
        for (const auto instance : sheet.symbol_instances()) {
            write_symbol_debug_overlay_svg(out, schematic, instance);
        }
        out << "    </g>\n";
    }
}

} // namespace volt::io::detail
