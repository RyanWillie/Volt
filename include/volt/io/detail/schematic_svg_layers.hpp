#pragma once

#include <ostream>

#include <volt/core/ids.hpp>
#include <volt/io/detail/schematic_svg_elements.hpp>
#include <volt/io/detail/schematic_svg_page_chrome.hpp>
#include <volt/schematic/schematic.hpp>

namespace volt::io::detail {

/** Options controlling which schematic projection layers are emitted into an SVG group. */
struct SchematicSvgLayerOptions {
    /** Whether authored regions are included with the object layers. */
    bool include_regions = true;
    /** Whether debug-only visual overlays are included. */
    bool debug_overlays = false;
};

void write_schematic_object_layers_svg(std::ostream &out, const Schematic &schematic,
                                       SheetId sheet_id, SchematicSvgLayerOptions options = {});

} // namespace volt::io::detail
