#pragma once

#include <optional>
#include <vector>

#include <volt/core/ids.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/copper/board_copper.hpp>
#include <volt/pcb/features/board_features.hpp>
#include <volt/pcb/footprints/footprints.hpp>

namespace volt::queries {

/** Return a board-cached footprint definition ID for an exact footprint reference. */
[[nodiscard]] std::optional<FootprintDefId>
footprint_definition_id(const Board &board, const FootprintRef &ref) noexcept;

/** Return the placement of an exact logical component, if it is placed on this board. */
[[nodiscard]] std::optional<ComponentPlacementId>
placement_for_component(const Board &board, ComponentId component) noexcept;

/** Compare complete footprint definitions that claim the same library-qualified identity. */
[[nodiscard]] bool footprint_definition_conflicts(const FootprintDefinition &board_definition,
                                                  const FootprintDefinition &library_definition);

/**
 * Compose board-cached definitions ahead of an explicit caller-supplied footprint library.
 *
 * This preserves the current authoring resolution contract until B3 replaces late library
 * resolution. The result owns its definitions and does not mutate Board or selected-part truth.
 */
[[nodiscard]] FootprintLibrary board_resolution_footprints(const Board &board,
                                                           const FootprintLibrary &footprints);

/** Resolve placed footprint pads against existing Circuit pins and connectivity. */
[[nodiscard]] std::vector<PadResolution> resolve_pads(const Board &board,
                                                      const FootprintLibrary &footprints);

/** Project resolved footprint package geometry into board coordinates. */
[[nodiscard]] std::vector<ProjectedFootprintGeometry>
project_footprint_geometries(const Board &board, const FootprintLibrary &footprints);

/** Derive deterministic ratsnest inputs from resolved placed pads and Circuit continuity. */
[[nodiscard]] std::vector<RatsnestEdge> ratsnest_edges(const Board &board,
                                                       const FootprintLibrary &footprints);

/** Resolve endpoint-aware track authoring intent to one existing logical net. */
[[nodiscard]] NetId resolve_board_route_net(const Board &board,
                                            const BoardTrackRouteRequest &request,
                                            const FootprintLibrary &footprints);

} // namespace volt::queries
