#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <volt/io/detail/typed_id.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/io/pcb/pcb_schema.hpp>
#include <volt/pcb/resolution/board_resolution.hpp>

namespace volt::io {

namespace detail {

[[nodiscard]] std::string severity_name(Severity severity);

[[nodiscard]] std::string entity_ref_id(EntityRef entity);

[[nodiscard]] std::optional<FootprintDefId>
find_footprint_definition(const std::vector<FootprintDefinition> &definitions,
                          const FootprintRef &ref);

[[nodiscard]] std::vector<FootprintDefinition>
collect_footprint_definitions(const ResolvedBoardView &resolved);

[[nodiscard]] FootprintLibrary
footprint_library_from_definitions(const std::vector<FootprintDefinition> &definitions);

void write_number(std::ostream &out, double value);

void write_board_point(std::ostream &out, BoardPoint point);

void write_board_points(std::ostream &out, const std::vector<BoardPoint> &points);

void write_board_layers(std::ostream &out, const std::vector<BoardLayerId> &layers);

void write_footprint_point(std::ostream &out, FootprintPoint point);

void write_footprint_polygon(std::ostream &out, const FootprintPolygon &polygon);

void write_footprint_size(std::ostream &out, FootprintSize size);

void write_footprint_ref(std::ostream &out, const FootprintRef &ref);

void write_footprint_layers(std::ostream &out, const FootprintLayerSet &layers);

void write_drill(std::ostream &out, const std::optional<FootprintDrill> &drill);

void write_mechanical_role(std::ostream &out,
                           const std::optional<FootprintPadMechanicalRole> &mechanical_role);

void write_pad_geometry_fields(std::ostream &out, const FootprintPad &pad);

void write_layers(std::ostream &out, const Board &board);

void write_layer_stack(std::ostream &out, const Board &board);

void write_outline(std::ostream &out, const Board &board);

void write_board_geometry(std::ostream &out, const Board &board);

void write_rules(std::ostream &out, const Board &board);

void write_features(std::ostream &out, const Board &board);

void write_footprint_definitions(std::ostream &out,
                                 const std::vector<FootprintDefinition> &definitions);

void write_placements(std::ostream &out, const Board &board, bool trailing_comma = false);

void write_tracks(std::ostream &out, const Board &board, bool trailing_comma = false);

void write_vias(std::ostream &out, const Board &board, bool trailing_comma = false);

void write_board_zones(std::ostream &out, const Board &board, bool trailing_comma = false);

void write_board_keepouts(std::ostream &out, const Board &board, bool trailing_comma = false);

void write_board_rooms(std::ostream &out, const Board &board, bool trailing_comma = false);

void write_board_texts(std::ostream &out, const Board &board, bool trailing_comma = false);

} // namespace detail

/** Write deterministic canonical PCB projection JSON without derived viewer state. */
void write_pcb_board(std::ostream &out, const ResolvedBoardView &resolved);

/** Return deterministic canonical PCB projection JSON without derived viewer state. */
[[nodiscard]] std::string write_pcb_board(const ResolvedBoardView &resolved);

} // namespace volt::io
