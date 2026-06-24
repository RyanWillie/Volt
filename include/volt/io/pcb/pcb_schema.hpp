#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include <volt/core/diagnostics.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints/footprints.hpp>

namespace volt::io {

/** Return the canonical PCB projection format name. */
[[nodiscard]] inline constexpr std::string_view pcb_format_name() noexcept { return "volt.pcb"; }

/** Return the canonical PCB projection format version written by this library. */
[[nodiscard]] inline constexpr int pcb_format_version() noexcept { return 3; }

namespace detail {

[[nodiscard]] std::string pcb_pad_projection_id(ComponentPlacementId placement, FootprintPadId pad);

[[nodiscard]] std::string pcb_ratsnest_edge_id(NetId net, std::size_t edge);

[[nodiscard]] std::string board_units_name(BoardUnits units);

[[nodiscard]] BoardUnits board_units_from_name(const std::string &value);

[[nodiscard]] std::string board_side_name(BoardSide side);

[[nodiscard]] BoardSide board_side_from_name(const std::string &value);

[[nodiscard]] std::string board_layer_role_name(BoardLayerRole role);

[[nodiscard]] BoardLayerRole board_layer_role_from_name(const std::string &value);

[[nodiscard]] std::string board_layer_side_name(BoardLayerSide side);

[[nodiscard]] BoardLayerSide board_layer_side_from_name(const std::string &value);

[[nodiscard]] std::string board_feature_kind_name(BoardFeatureKind kind);

[[nodiscard]] BoardFeatureKind board_feature_kind_from_name(const std::string &value);

[[nodiscard]] std::string board_zone_fill_name(BoardZoneFill fill);

[[nodiscard]] BoardZoneFill board_zone_fill_from_name(const std::string &value);

[[nodiscard]] std::string board_keepout_restriction_name(BoardKeepoutRestriction restriction);

[[nodiscard]] BoardKeepoutRestriction board_keepout_restriction_from_name(const std::string &value);

[[nodiscard]] std::string footprint_layer_name(FootprintLayer layer);

[[nodiscard]] FootprintLayer footprint_layer_from_name(const std::string &value);

[[nodiscard]] std::string footprint_pad_kind_name(FootprintPadKind kind);

[[nodiscard]] FootprintPadKind footprint_pad_kind_from_name(const std::string &value);

[[nodiscard]] std::string footprint_pad_shape_name(FootprintPadShape shape);

[[nodiscard]] FootprintPadShape footprint_pad_shape_from_name(const std::string &value);

[[nodiscard]] std::string footprint_pad_plating_name(FootprintPadPlating plating);

[[nodiscard]] FootprintPadPlating footprint_pad_plating_from_name(const std::string &value);

[[nodiscard]] std::string footprint_pad_mechanical_role_name(FootprintPadMechanicalRole role);

[[nodiscard]] FootprintPadMechanicalRole
footprint_pad_mechanical_role_from_name(const std::string &value);

[[nodiscard]] std::string footprint_marking_kind_name(FootprintMarkingKind kind);

[[nodiscard]] FootprintMarkingKind footprint_marking_kind_from_name(const std::string &value);

[[nodiscard]] std::string pad_resolution_status_name(PadResolutionStatus status);

[[nodiscard]] PadResolutionStatus pad_resolution_status_from_name(const std::string &value);

} // namespace detail

} // namespace volt::io
