#pragma once

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <volt/core/ids.hpp>
#include <volt/io/detail/schematic_svg_common.hpp>
#include <volt/io/detail/typed_id.hpp>
#include <volt/schematic/presentation_geometry.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/symbols.hpp>

namespace volt::io {

/** Options that control deterministic schematic SVG rendering. */
struct SchematicSvgOptions {
    /** Include development overlays such as symbol pin anchors and labels. */
    bool debug_overlays = false;
};

/** Options that control content-tight schematic body SVG rendering. */
struct SchematicSvgBodyOptions {
    /** Shared SVG rendering options for schematic drawing content. */
    SchematicSvgOptions svg;
    /** Sheet-space margin added around the computed content bounds. */
    double margin = 4.0;
    /** Include authored sheet region frames in the body output and bounds. */
    bool include_regions = false;
};

/** One production-oriented SVG page exported from a schematic sheet. */
struct SchematicSvgPage {
    /** Source schematic sheet ID. */
    SheetId sheet;
    /** Human-readable sheet name used for filenames and labels. */
    std::string name;
    /** Complete SVG document for the sheet. */
    std::string svg;
};

namespace detail {

[[nodiscard]] SvgBounds symbol_primitive_bounds(const SymbolPrimitive &primitive,
                                                const SymbolInstance &instance);

[[nodiscard]] SvgBounds symbol_instance_bounds(const Schematic &schematic, SymbolInstanceId id);

[[nodiscard]] SvgBounds symbol_debug_overlay_bounds(const Schematic &schematic,
                                                    SymbolInstanceId id);

[[nodiscard]] SvgBounds wire_run_bounds(const WireRun &wire);

[[nodiscard]] SchematicOrientation power_port_bounds_orientation(PowerPortKind kind,
                                                                 SchematicOrientation orientation);

[[nodiscard]] Point transformed_power_port_anchor(const PowerPort &port, Point local_anchor);

[[nodiscard]] SvgBounds power_port_label_bounds(const PowerPort &port, std::string_view label);

[[nodiscard]] SvgBounds power_port_bounds(const PowerPort &port, std::string_view label);

[[nodiscard]] SvgBounds no_connect_marker_bounds(const NoConnectMarker &marker);

[[nodiscard]] Point transformed_sheet_port_anchor(const SheetPort &port, Point local_anchor);

[[nodiscard]] SvgBounds sheet_port_label_bounds(const SheetPort &port);

[[nodiscard]] SvgBounds sheet_port_bounds(const SheetPort &port);

[[nodiscard]] std::optional<SvgBounds>
sheet_content_bounds(const Schematic &schematic, SheetId sheet_id, SchematicSvgBodyOptions options);

[[nodiscard]] SvgBounds expanded_body_bounds(const Schematic &schematic, SheetId sheet_id,
                                             SchematicSvgBodyOptions options);

void write_svg_style(std::ostream &out, SchematicSvgOptions options);

} // namespace detail
} // namespace volt::io

// Include the layer implementation after the helpers above are declared.
#include <volt/io/detail/schematic_svg_layers.hpp>

namespace volt::io {
namespace detail {

void write_sheet_svg(std::ostream &out, const Schematic &schematic, SheetId sheet_id,
                     double y_offset, SchematicSvgOptions options);

} // namespace detail

/** Write one content-tight SVG body for a single schematic sheet. */
void write_schematic_body_svg(std::ostream &out, const Schematic &schematic, SheetId sheet_id,
                              SchematicSvgBodyOptions options = {});

/** Return one content-tight SVG body for a single schematic sheet. */
[[nodiscard]] std::string write_schematic_body_svg(const Schematic &schematic, SheetId sheet_id,
                                                   SchematicSvgBodyOptions options = {});

/** Write a deterministic SVG rendering of a schematic projection. */
void write_schematic_svg(std::ostream &out, const Schematic &schematic,
                         SchematicSvgOptions options = {});

/** Return a deterministic SVG rendering of a schematic projection. */
[[nodiscard]] std::string write_schematic_svg(const Schematic &schematic,
                                              SchematicSvgOptions options = {});

/** Write one deterministic SVG page for a single schematic sheet. */
void write_schematic_sheet_svg(std::ostream &out, const Schematic &schematic, SheetId sheet_id,
                               SchematicSvgOptions options = {});

/** Return one deterministic SVG page for a single schematic sheet. */
[[nodiscard]] std::string write_schematic_sheet_svg(const Schematic &schematic, SheetId sheet_id,
                                                    SchematicSvgOptions options = {});

/** Return separate SVG pages for production-oriented multi-sheet export. */
[[nodiscard]] std::vector<SchematicSvgPage>
write_schematic_svg_pages(const Schematic &schematic, SchematicSvgOptions options = {});

} // namespace volt::io
