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
resolved_placement_footprints(const Board &board, const FootprintLibrary &footprints) {
    auto resolved = std::vector<ResolvedPlacementFootprint>{};
    resolved.reserve(board.placement_count());

    const auto resolution_footprints = board_resolution_footprints(board, footprints);
    for (std::size_t index = 0; index < board.placement_count(); ++index) {
        const auto placement_id = ComponentPlacementId{index};
        const auto &component_placement = board.placement(placement_id);
        const auto &selected_part =
            selected_physical_part(board.circuit(), component_placement.component());
        if (!selected_part.has_value()) {
            continue;
        }

        const auto footprint_resolution =
            resolve_footprint(selected_part.value(), resolution_footprints);
        const auto *definition = footprint_resolution.definition();
        if (definition == nullptr) {
            continue;
        }

        resolved.push_back(ResolvedPlacementFootprint{
            placement_id, component_placement, *definition, footprint_resolution.pad_bindings()});
    }
    return resolved;
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

[[nodiscard]] std::optional<NetId> route_endpoint_net(const Board &board,
                                                      const BoardRouteEndpoint &endpoint,
                                                      const FootprintLibrary &footprints) {
    if (!endpoint.placement.has_value() && !endpoint.pad.has_value()) {
        return std::nullopt;
    }
    if (!endpoint.placement.has_value() || !endpoint.pad.has_value()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Board route pad endpoints require placement and pad IDs"};
    }

    const auto &component_placement = board.placement(endpoint.placement.value());
    const auto &selected_part =
        selected_physical_part(board.circuit(), component_placement.component());
    if (!selected_part.has_value()) {
        throw KernelArgumentError{ErrorCode::InvalidState,
                                  "Board route endpoint component has no selected physical part",
                                  EntityRef::component(component_placement.component())};
    }

    const auto resolution_footprints = board_resolution_footprints(board, footprints);
    const auto footprint_resolution =
        resolve_footprint(selected_part.value(), resolution_footprints);
    const auto *definition = footprint_resolution.definition();
    if (definition == nullptr) {
        throw KernelArgumentError{ErrorCode::InvalidState,
                                  "Board route endpoint footprint cannot be resolved",
                                  EntityRef::component(component_placement.component())};
    }

    static_cast<void>(definition->pad(endpoint.pad.value()));
    const auto pad_resolutions = resolve_pads(board, resolution_footprints);
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
    for (std::size_t index = 0; index < board.footprint_definition_count(); ++index) {
        const auto id = FootprintDefId{index};
        if (board.footprint_definition(id).ref() == ref) {
            return id;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ComponentPlacementId>
placement_for_component(const Board &board, ComponentId component) noexcept {
    for (std::size_t index = 0; index < board.placement_count(); ++index) {
        const auto id = ComponentPlacementId{index};
        if (board.placement(id).component() == component) {
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

[[nodiscard]] FootprintLibrary board_resolution_footprints(const Board &board,
                                                           const FootprintLibrary &footprints) {
    auto library = FootprintLibrary{};
    for (std::size_t index = 0; index < board.footprint_definition_count(); ++index) {
        library.add(board.footprint_definition(FootprintDefId{index}));
    }
    for (const auto &definition : footprints.definitions()) {
        const auto *existing = library.find(definition.ref());
        if (existing == nullptr) {
            library.add(definition);
            continue;
        }
        if (footprint_definition_conflicts(*existing, definition)) {
            throw KernelLogicError{
                ErrorCode::DuplicateName,
                "Board footprint definition conflicts with footprint library definition"};
        }
    }
    return library;
}

[[nodiscard]] std::vector<PadResolution> resolve_pads(const Board &board,
                                                      const FootprintLibrary &footprints) {
    auto resolutions = std::vector<PadResolution>{};
    for (const auto &resolved : resolved_placement_footprints(board, footprints)) {
        append_pad_resolutions(board, resolved.placement_id, resolved.placement,
                               resolved.definition, resolved.pad_bindings, resolutions);
    }
    return resolutions;
}

[[nodiscard]] std::vector<ProjectedFootprintGeometry>
project_footprint_geometries(const Board &board, const FootprintLibrary &footprints) {
    auto geometries = std::vector<ProjectedFootprintGeometry>{};
    for (const auto &resolved : resolved_placement_footprints(board, footprints)) {
        geometries.emplace_back(
            resolved.placement_id, resolved.placement.component(), resolved.placement.side(),
            project_optional_polygon(resolved.placement, resolved.definition.courtyard()),
            project_optional_polygon(resolved.placement, resolved.definition.body()),
            project_optional_polygon(resolved.placement, resolved.definition.fabrication_outline()),
            project_optional_polygon(resolved.placement, resolved.definition.assembly_outline()),
            project_markings(resolved.placement, resolved.definition.markings()));
    }
    return geometries;
}

[[nodiscard]] std::vector<RatsnestEdge> ratsnest_edges(const Board &board,
                                                       const FootprintLibrary &footprints) {
    return derive_ratsnest_edges(board.circuit(), resolve_pads(board, footprints));
}

[[nodiscard]] NetId resolve_board_route_net(const Board &board,
                                            const BoardTrackRouteRequest &request,
                                            const FootprintLibrary &footprints) {
    auto resolved_net = std::optional<NetId>{};
    if (request.net.has_value()) {
        static_cast<void>(board.circuit().get(request.net.value()));
        resolved_net = request.net.value();
    }

    for (const auto &endpoint : request.endpoints) {
        const auto endpoint_net = route_endpoint_net(board, endpoint, footprints);
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
