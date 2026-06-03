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

void write_pcb_svg_number(std::ostream &out, double value);

[[nodiscard]] PcbSvgBounds bounds_from_outline(const Board &board);

[[nodiscard]] double preview_width(const PcbSvgBounds &bounds);

[[nodiscard]] double preview_height(const PcbSvgBounds &bounds, const DiagnosticReport &diagnostics,
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

void include_footprint_bounds(PcbSvgBounds &bounds, const ComponentPlacement &placement,
                              const FootprintDefinition &definition);

void include_feature_bounds(PcbSvgBounds &bounds, const BoardFeature &feature);

[[nodiscard]] PcbSvgBounds bounds_from_board(const Board &board,
                                             const FootprintLibrary &footprints);

[[nodiscard]] std::string pad_shape_class(FootprintPadShape shape);

void write_style(std::ostream &out, bool include_copper, bool include_zones, bool include_keepouts,
                 bool include_texts);

void write_pcb_svg_outline(std::ostream &out, const Board &board);

void write_pcb_svg_features(std::ostream &out, const Board &board);

void write_pcb_point_list(std::ostream &out, const std::vector<BoardPoint> &points);

[[nodiscard]] std::string board_layer_list_attr(const std::vector<BoardLayerId> &layers);

[[nodiscard]] std::string
keepout_restriction_list_attr(const std::vector<BoardKeepoutRestriction> &restrictions);

void write_zones(std::ostream &out, const Board &board);

void write_keepouts(std::ostream &out, const Board &board);

void write_texts(std::ostream &out, const Board &board);

void write_copper(std::ostream &out, const Board &board);

void write_pad(std::ostream &out, const FootprintPad &pad, FootprintPadId pad_id,
               const PadResolution *resolution);

void write_placements(std::ostream &out, const Board &board, const FootprintLibrary &footprints,
                      const std::vector<PadResolution> &resolutions);

void write_pad_overlays(std::ostream &out, const Board &board,
                        const std::vector<PadResolution> &resolutions,
                        PcbPlacementSvgOptions options);

void write_ratsnest(std::ostream &out, const std::vector<RatsnestEdge> &edges);

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
