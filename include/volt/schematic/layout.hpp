#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <volt/schematic/presentation_geometry.hpp>
#include <volt/schematic/schematic.hpp>

namespace volt {

/** Options for engine-owned schematic text placement. */
struct SchematicTextLayoutOptions {
    /** Clearance kept between movable text bounds and existing schematic geometry. */
    double clearance = 1.0;
};

namespace detail {

[[nodiscard]] std::vector<Point> schematic_text_candidate_positions(Point anchor);

[[nodiscard]] bool schematic_text_bounds_clear_of_wires(const Schematic &schematic,
                                                        const Sheet &sheet, SchematicBounds bounds);

[[nodiscard]] bool schematic_text_bounds_clear_of_symbols(const Schematic &schematic,
                                                          const Sheet &sheet,
                                                          SchematicBounds bounds);

[[nodiscard]] bool schematic_text_bounds_clear_of_fixed_objects(const Schematic &schematic,
                                                                const Sheet &sheet,
                                                                SchematicBounds bounds);

[[nodiscard]] bool
schematic_text_bounds_clear_of_placed_text(SchematicBounds bounds,
                                           const std::vector<SchematicBounds> &placed_text_bounds);

[[nodiscard]] bool
schematic_text_candidate_is_clear(const Schematic &schematic, const Sheet &sheet,
                                  SchematicBounds bounds,
                                  const std::vector<SchematicBounds> &placed_text_bounds,
                                  double clearance, std::optional<std::size_t> authored_region);

[[nodiscard]] Point
choose_net_label_position(const Schematic &schematic, const Sheet &sheet, const NetLabel &label,
                          const std::vector<SchematicBounds> &placed_text_bounds, double clearance);

[[nodiscard]] Point choose_symbol_field_position(
    const Schematic &schematic, const Sheet &sheet, const SymbolField &field,
    const std::vector<SchematicBounds> &placed_text_bounds, double clearance);

[[nodiscard]] Point default_power_port_label_position(const PowerPort &port);

[[nodiscard]] Point choose_power_port_label_position(
    const Schematic &schematic, const Sheet &sheet, const PowerPort &port,
    const std::vector<SchematicBounds> &placed_text_bounds, double clearance);

void collect_fixed_text_bounds(const Schematic &schematic, const Sheet &sheet,
                               std::vector<SchematicBounds> &placed_text_bounds);

} // namespace detail

/**
 * Place movable schematic text around authored geometry without changing wire paths,
 * symbols, or logical connectivity.
 */
void layout_schematic_text(Schematic &schematic, SchematicTextLayoutOptions options = {});

} // namespace volt
