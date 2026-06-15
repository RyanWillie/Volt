#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <volt/io/detail/typed_id.hpp>
#include <volt/io/pcb_schema.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints.hpp>

namespace volt::io {

/** Options that control deterministic PCB placement preview SVG rendering. */
struct PcbPlacementSvgOptions {
    /** Include derived pad-to-net resolution markers and net labels. */
    bool pad_net_overlays = true;
    /** Include board validation diagnostics in the preview. */
    bool diagnostic_overlays = true;
    /** Include derived ratsnest edges between placed pads. */
    bool ratsnest_edges = true;
    /** When set, render only content that belongs to or usefully annotates this board layer. */
    std::optional<BoardLayerId> layer_filter = std::nullopt;
};

namespace detail {

inline constexpr double pcb_svg_margin_mm = 4.0;
inline constexpr double pcb_svg_default_width_mm = 80.0;
inline constexpr double pcb_svg_default_height_mm = 50.0;

/** Board-space bounds used to size and offset the PCB SVG preview. */
struct PcbSvgBounds {
    /** Minimum board-space X coordinate. */
    double min_x;
    /** Minimum board-space Y coordinate. */
    double min_y;
    /** Maximum board-space X coordinate. */
    double max_x;
    /** Maximum board-space Y coordinate. */
    double max_y;
};

[[nodiscard]] std::string pcb_svg_escape(std::string_view value);

[[nodiscard]] std::string pcb_svg_layer_filename_token(std::string_view layer_name);

[[nodiscard]] std::string pcb_svg_layer_token(const Board &board, BoardLayerId layer_id);

void write_pcb_svg_number(std::ostream &out, double value);

[[nodiscard]] PcbSvgBounds bounds_from_outline(const Board &board);

[[nodiscard]] double preview_width(const PcbSvgBounds &bounds);

[[nodiscard]] double preview_height(const PcbSvgBounds &bounds, const Board &board,
                                    const DiagnosticReport &diagnostics,
                                    PcbPlacementSvgOptions options);

[[nodiscard]] std::string entity_ref_svg_id(EntityRef entity);

[[nodiscard]] std::string entity_ref_list(const Diagnostic &diagnostic);

[[nodiscard]] std::string severity_class(Severity severity);

[[nodiscard]] std::string footprint_ref_token(const FootprintRef &ref);

[[nodiscard]] FootprintLibrary preview_footprint_library(const Board &board,
                                                         const FootprintLibrary &footprints);

[[nodiscard]] const FootprintDefinition *
resolve_definition_for_placement(const Board &board, const ComponentPlacement &placement,
                                 const FootprintLibrary &footprints);

[[nodiscard]] bool contains_footprint_ref(const std::vector<FootprintRef> &refs,
                                          const FootprintRef &ref);

[[nodiscard]] std::optional<FootprintDefId> projection_footprint_definition_id_for_placement(
    const Board &board, ComponentPlacementId placement_id, const FootprintLibrary &footprints);

[[nodiscard]] const PadResolution *
find_pad_resolution(const std::vector<PadResolution> &resolutions, ComponentPlacementId placement,
                    FootprintPadId pad);

[[nodiscard]] PcbSvgBounds footprint_local_bounds(const FootprintDefinition &definition);

void include_board_point(PcbSvgBounds &bounds, BoardPoint point);

[[nodiscard]] const ProjectedFootprintGeometry *projected_footprint_geometry_for_placement(
    const std::vector<ProjectedFootprintGeometry> &geometries, ComponentPlacementId placement);

void include_footprint_bounds(PcbSvgBounds &bounds, const ComponentPlacement &placement,
                              const FootprintDefinition &definition,
                              const ProjectedFootprintGeometry *projected_geometry);

void include_feature_bounds(PcbSvgBounds &bounds, const BoardFeature &feature);

[[nodiscard]] PcbSvgBounds
bounds_from_board(const Board &board, const FootprintLibrary &footprints,
                  const std::vector<ProjectedFootprintGeometry> &footprint_geometries);

[[nodiscard]] std::string pad_shape_class(FootprintPadShape shape);

void write_style(std::ostream &out, bool include_copper, bool include_zones, bool include_keepouts,
                 bool include_texts);

void write_pcb_svg_outline(std::ostream &out, const Board &board);

void write_pcb_svg_features(std::ostream &out, const Board &board);

void write_pcb_point_list(std::ostream &out, const std::vector<BoardPoint> &points);

[[nodiscard]] std::string board_layer_list_attr(const std::vector<BoardLayerId> &layers);

[[nodiscard]] std::string
keepout_restriction_list_attr(const std::vector<BoardKeepoutRestriction> &restrictions);

[[nodiscard]] bool layer_list_contains(const std::vector<BoardLayerId> &layers, BoardLayerId layer);

[[nodiscard]] bool layer_selected(PcbPlacementSvgOptions options, BoardLayerId layer);

[[nodiscard]] bool placement_selected(const Board &board, const ComponentPlacement &placement,
                                      PcbPlacementSvgOptions options);

[[nodiscard]] bool pad_selected_for_layer(const Board &board, const FootprintPad &pad,
                                          BoardSide placement_side, BoardLayerId layer_id);

[[nodiscard]] bool placement_pad_selected_for_layer(const Board &board,
                                                    const FootprintLibrary &footprints,
                                                    ComponentPlacementId placement_id,
                                                    FootprintPadId pad_id, BoardLayerId layer_id);

[[nodiscard]] bool pad_resolution_selected(const Board &board, const PadResolution &resolution,
                                           const FootprintLibrary &footprints,
                                           PcbPlacementSvgOptions options);

[[nodiscard]] bool via_intersects_layer(const Board &board, const BoardVia &via,
                                        BoardLayerId layer);

[[nodiscard]] bool diagnostic_selected(const Board &board, const Diagnostic &diagnostic,
                                       PcbPlacementSvgOptions options);

void write_board_layer_group_open(std::ostream &out, const Board &board, BoardLayerId layer_id);

void write_zones(std::ostream &out, const Board &board, BoardLayerId layer);

void write_keepouts(std::ostream &out, const Board &board, BoardLayerId layer);

void write_texts(std::ostream &out, const Board &board, BoardLayerId layer);

void write_copper(std::ostream &out, const Board &board, BoardLayerId layer);

void write_pad(std::ostream &out, const FootprintPad &pad, FootprintPadId pad_id,
               const PadResolution *resolution);

void write_placements(std::ostream &out, const Board &board, const FootprintLibrary &footprints,
                      const std::vector<PadResolution> &resolutions,
                      const std::vector<ProjectedFootprintGeometry> &footprint_geometries,
                      PcbPlacementSvgOptions options);

void write_pad_overlays(std::ostream &out, const Board &board,
                        const std::vector<PadResolution> &resolutions,
                        const FootprintLibrary &footprints, PcbPlacementSvgOptions options);

void write_ratsnest(std::ostream &out, const Board &board, const std::vector<RatsnestEdge> &edges,
                    const FootprintLibrary &footprints, PcbPlacementSvgOptions options);

void write_diagnostics(std::ostream &out, const Board &board, const DiagnosticReport &diagnostics,
                       const PcbSvgBounds &bounds, PcbPlacementSvgOptions options);

} // namespace detail

/** Write a deterministic SVG preview for a placement-only PCB projection. */
void write_pcb_placement_svg(std::ostream &out, const Board &board,
                             const FootprintLibrary &footprints,
                             PcbPlacementSvgOptions options = {});

/** Return a deterministic SVG preview for a placement-only PCB projection. */
[[nodiscard]] std::string write_pcb_placement_svg(const Board &board,
                                                  const FootprintLibrary &footprints,
                                                  PcbPlacementSvgOptions options = {});

} // namespace volt::io
