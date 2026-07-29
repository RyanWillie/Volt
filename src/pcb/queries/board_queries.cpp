#include <volt/pcb/queries/board_queries.hpp>

#include <volt/core/errors.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include <volt/circuit/connectivity/queries.hpp>

namespace volt::queries {

namespace {

struct ResolvedPlacementFootprint {
    ComponentPlacementId placement_id;
    ComponentPlacement placement;
    FootprintDefinition definition;
    std::vector<FootprintPadBinding> pad_bindings;
};

[[nodiscard]] std::vector<ResolvedPlacementFootprint>
resolved_placement_footprints(const ResolvedBoardView &resolved) {
    const auto &board = resolved.board();
    auto placements = std::vector<ResolvedPlacementFootprint>{};
    placements.reserve(board.all<volt::ComponentPlacementId>().size());

    for (std::size_t index = 0; index < board.all<volt::ComponentPlacementId>().size(); ++index) {
        const auto placement_id = ComponentPlacementId{index};
        const auto &component_placement = board.get(placement_id);
        const auto *selected_part = resolved.part(component_placement.component());
        if (selected_part == nullptr) {
            continue;
        }

        const auto footprint_resolution =
            resolve_footprint(selected_part->physical_part(), resolved.footprints());
        const auto *definition = footprint_resolution.definition();
        if (definition == nullptr) {
            continue;
        }

        placements.push_back(ResolvedPlacementFootprint{
            placement_id, component_placement, *definition, footprint_resolution.pad_bindings()});
    }
    return placements;
}

void append_pad_resolutions(const Board &board, ComponentPlacementId placement_id,
                            const ComponentPlacement &component_placement,
                            const FootprintDefinition &definition,
                            const std::vector<FootprintPadBinding> &bindings,
                            std::vector<PadResolution> &resolutions) {
    for (std::size_t pad_index = 0; pad_index < definition.pad_count(); ++pad_index) {
        const auto pad_id = FootprintPadId{pad_index};
        const auto &pad = definition.pad(pad_id);
        const auto position =
            detail::transform_footprint_point(component_placement, pad.position());
        if (!pad.requires_pin_mapping()) {
            resolutions.emplace_back(placement_id, component_placement.component(), pad_id,
                                     pad.label(), position, std::nullopt, std::nullopt,
                                     PadResolutionStatus::NonElectrical);
            continue;
        }

        const auto binding = std::find_if(
            bindings.begin(), bindings.end(),
            [pad_id](const FootprintPadBinding &candidate) { return candidate.pad() == pad_id; });
        if (binding == bindings.end()) {
            resolutions.emplace_back(placement_id, component_placement.component(), pad_id,
                                     pad.label(), position, std::nullopt, std::nullopt,
                                     PadResolutionStatus::Invalid);
            continue;
        }

        const auto pin =
            pin_by_definition(board.circuit(), component_placement.component(), binding->pin());
        if (!pin.has_value()) {
            resolutions.emplace_back(placement_id, component_placement.component(), pad_id,
                                     pad.label(), position, std::nullopt, std::nullopt,
                                     PadResolutionStatus::Invalid);
            continue;
        }

        const auto net = net_of(board.circuit(), pin.value());
        const auto status =
            net.has_value() ? PadResolutionStatus::Connected : PadResolutionStatus::Unconnected;
        resolutions.emplace_back(placement_id, component_placement.component(), pad_id, pad.label(),
                                 position, pin, net, status);
    }
}

[[nodiscard]] std::optional<std::vector<BoardPoint>>
project_optional_polygon(const ComponentPlacement &placement,
                         const std::optional<FootprintPolygon> &polygon) {
    if (!polygon.has_value()) {
        return std::nullopt;
    }
    return detail::transformed_footprint_polygon(placement, polygon.value());
}

[[nodiscard]] std::vector<ProjectedFootprintMarking>
project_markings(const ComponentPlacement &placement,
                 const std::vector<FootprintMarking> &markings) {
    auto projected = std::vector<ProjectedFootprintMarking>{};
    projected.reserve(markings.size());
    for (const auto &marking : markings) {
        projected.emplace_back(marking.kind(),
                               detail::transformed_footprint_polygon(placement, marking.polygon()));
    }
    return projected;
}

[[nodiscard]] std::optional<NetId> route_endpoint_net(const ResolvedBoardView &resolved,
                                                      const BoardRouteEndpoint &endpoint) {
    const auto &board = resolved.board();
    if (!endpoint.placement.has_value() && !endpoint.pad.has_value()) {
        return std::nullopt;
    }
    if (!endpoint.placement.has_value() || !endpoint.pad.has_value()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Board route pad endpoints require placement and pad IDs"};
    }

    const auto &component_placement = board.get(endpoint.placement.value());
    const auto *selected_part = resolved.part(component_placement.component());
    if (selected_part == nullptr) {
        throw KernelArgumentError{ErrorCode::InvalidState,
                                  "Board route endpoint component has no selected physical part",
                                  EntityRef::component(component_placement.component())};
    }

    const auto footprint_resolution =
        resolve_footprint(selected_part->physical_part(), resolved.footprints());
    const auto *definition = footprint_resolution.definition();
    if (definition == nullptr) {
        throw KernelArgumentError{ErrorCode::InvalidState,
                                  "Board route endpoint footprint cannot be resolved",
                                  EntityRef::component(component_placement.component())};
    }

    static_cast<void>(definition->pad(endpoint.pad.value()));
    const auto pad_resolutions = resolve_pads(resolved);
    const auto *resolution = detail::find_board_pad_resolution(
        pad_resolutions, endpoint.placement.value(), endpoint.pad.value());
    if (resolution == nullptr || resolution->status() != PadResolutionStatus::Connected ||
        !resolution->net().has_value()) {
        throw KernelArgumentError{ErrorCode::InvalidState,
                                  "Board route endpoint pad is not connected to a logical net",
                                  EntityRef::footprint_pad(endpoint.pad.value())};
    }
    return resolution->net().value();
}

} // namespace

[[nodiscard]] std::optional<FootprintDefId>
footprint_definition_id(const Board &board, const FootprintRef &ref) noexcept {
    for (std::size_t index = 0; index < board.all<volt::FootprintDefId>().size(); ++index) {
        const auto id = FootprintDefId{index};
        if (board.get(id).ref() == ref) {
            return id;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ComponentPlacementId>
placement_for_component(const Board &board, ComponentId component) noexcept {
    for (std::size_t index = 0; index < board.all<volt::ComponentPlacementId>().size(); ++index) {
        const auto id = ComponentPlacementId{index};
        if (board.get(id).component() == component) {
            return id;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool footprint_definition_conflicts(const FootprintDefinition &board_definition,
                                                  const FootprintDefinition &library_definition) {
    return board_definition.ref() == library_definition.ref() &&
           board_definition != library_definition;
}

[[nodiscard]] std::vector<PadResolution> resolve_pads(const ResolvedBoardView &resolved) {
    const auto &board = resolved.board();
    auto resolutions = std::vector<PadResolution>{};
    for (const auto &placement : resolved_placement_footprints(resolved)) {
        append_pad_resolutions(board, placement.placement_id, placement.placement,
                               placement.definition, placement.pad_bindings, resolutions);
    }
    return resolutions;
}

[[nodiscard]] std::vector<ProjectedFootprintGeometry>
project_footprint_geometries(const ResolvedBoardView &resolved) {
    auto geometries = std::vector<ProjectedFootprintGeometry>{};
    for (const auto &placement : resolved_placement_footprints(resolved)) {
        geometries.emplace_back(
            placement.placement_id, placement.placement.component(), placement.placement.side(),
            project_optional_polygon(placement.placement, placement.definition.courtyard()),
            project_optional_polygon(placement.placement, placement.definition.body()),
            project_optional_polygon(placement.placement,
                                     placement.definition.fabrication_outline()),
            project_optional_polygon(placement.placement, placement.definition.assembly_outline()),
            project_markings(placement.placement, placement.definition.markings()));
    }
    return geometries;
}

[[nodiscard]] std::vector<RatsnestEdge> ratsnest_edges(const ResolvedBoardView &resolved) {
    return derive_ratsnest_edges(resolved.board().circuit(), resolve_pads(resolved));
}

[[nodiscard]] NetId resolve_board_route_net(const ResolvedBoardView &resolved,
                                            const BoardTrackRouteRequest &request) {
    const auto &board = resolved.board();
    auto resolved_net = std::optional<NetId>{};
    if (request.net.has_value()) {
        static_cast<void>(board.circuit().get(request.net.value()));
        resolved_net = request.net.value();
    }

    for (const auto &endpoint : request.endpoints) {
        const auto endpoint_net = route_endpoint_net(resolved, endpoint);
        if (!endpoint_net.has_value()) {
            continue;
        }
        if (!resolved_net.has_value()) {
            resolved_net = endpoint_net.value();
            continue;
        }
        if (resolved_net.value() != endpoint_net.value()) {
            if (request.net.has_value()) {
                throw KernelLogicError{
                    ErrorCode::InvalidState,
                    "Board route endpoint net does not match explicit route net"};
            }
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Board route endpoints resolve to different nets"};
        }
    }

    if (!resolved_net.has_value()) {
        throw KernelArgumentError{
            ErrorCode::InvalidArgument,
            "Board routed track requires an explicit net or a pad endpoint with a net"};
    }
    return resolved_net.value();
}

} // namespace volt::queries
