#include <volt/pcb/board.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace volt {

Board::Board(const Circuit &circuit, BoardName name)
    : Board{CircuitView{circuit}, std::move(name)} {}
Board::Board(CircuitView circuit, BoardName name) : circuit_{circuit}, name_{std::move(name)} {}
[[nodiscard]] BoardLayerId Board::add_layer(BoardLayer layer) {
    if (layer_by_name(layer.name()).has_value()) {
        throw std::logic_error{"Board layer name already exists"};
    }

    return layers_.insert(std::move(layer));
}
void Board::set_layer_stack(LayerStack stack) {
    for (const auto layer : stack.layers()) {
        require_layer(layer);
    }
    layer_stack_ = std::move(stack);
}
void Board::set_outline(BoardOutline outline) { outline_ = std::move(outline); }
void Board::set_design_rules(BoardDesignRules rules) { design_rules_ = rules; }
[[nodiscard]] BoardFeatureId Board::add_feature(BoardFeature feature) {
    return features_.insert(std::move(feature));
}
[[nodiscard]] FootprintDefId Board::cache_footprint_definition(FootprintDefinition footprint) {
    const auto existing = footprint_definition_id(footprint.ref());
    if (existing.has_value()) {
        if (footprint_definition(existing.value()) == footprint) {
            return existing.value();
        }
        throw std::logic_error{"Board footprint definition conflicts with existing definition"};
    }

    return footprint_definitions_.insert(std::move(footprint));
}
[[nodiscard]] ComponentPlacementId Board::place_component(ComponentPlacement placement) {
    static_cast<void>(circuit().component(placement.component()));
    if (placement_for_component(placement.component()).has_value()) {
        throw std::logic_error{"Component already has a board placement"};
    }

    return placements_.insert(placement);
}
[[nodiscard]] BoardTrackId Board::add_track(BoardTrack track) {
    require_net(track.net());
    require_copper_layer(track.layer());
    return tracks_.insert(std::move(track));
}
[[nodiscard]] BoardViaId Board::add_via(BoardVia via) {
    require_net(via.net());
    require_copper_layer(via.start_layer());
    require_copper_layer(via.end_layer());
    return vias_.insert(via);
}
[[nodiscard]] BoardZoneId Board::add_zone(BoardZone zone) {
    if (zone.net().has_value()) {
        require_net(zone.net().value());
    }
    for (const auto layer_id : zone.layers()) {
        require_layer(layer_id);
        if (layer(layer_id).role() != BoardLayerRole::Copper) {
            throw std::logic_error{"Board copper zones require copper layers"};
        }
    }
    return zones_.insert(std::move(zone));
}
[[nodiscard]] BoardKeepoutId Board::add_keepout(BoardKeepout keepout) {
    for (const auto layer : keepout.layers()) {
        require_layer(layer);
    }
    return keepouts_.insert(std::move(keepout));
}
[[nodiscard]] BoardTextId Board::add_text(BoardText text) {
    require_layer(text.layer());
    return texts_.insert(std::move(text));
}
[[nodiscard]] const std::optional<LayerStack> &Board::layer_stack() const noexcept {
    return layer_stack_;
}
[[nodiscard]] const FootprintDefinition &Board::footprint_definition(FootprintDefId id) const {
    return footprint_definitions_.get(id);
}
[[nodiscard]] std::size_t Board::footprint_definition_count() const noexcept {
    return footprint_definitions_.size();
}
[[nodiscard]] std::optional<FootprintDefId>
Board::footprint_definition_id(const FootprintRef &ref) const noexcept {
    for (std::size_t index = 0; index < footprint_definitions_.size(); ++index) {
        const auto id = FootprintDefId{index};
        if (footprint_definitions_.get(id).ref() == ref) {
            return id;
        }
    }

    return std::nullopt;
}
[[nodiscard]] const ComponentPlacement &Board::placement(ComponentPlacementId id) const {
    return placements_.get(id);
}
[[nodiscard]] std::optional<ComponentPlacementId>
Board::placement_for_component(ComponentId component) const noexcept {
    for (std::size_t index = 0; index < placements_.size(); ++index) {
        const auto id = ComponentPlacementId{index};
        if (placements_.get(id).component() == component) {
            return id;
        }
    }

    return std::nullopt;
}
[[nodiscard]] std::vector<PadResolution>
Board::resolve_pads(const FootprintLibrary &footprints) const {
    auto resolutions = std::vector<PadResolution>{};
    const auto resolution_footprints = detail::board_resolution_footprints(*this, footprints);
    for (std::size_t index = 0; index < placements_.size(); ++index) {
        const auto placement_id = ComponentPlacementId{index};
        const auto &component_placement = placement(placement_id);
        const auto &selected_part =
            circuit().selected_physical_part(component_placement.component());
        if (!selected_part.has_value()) {
            continue;
        }

        const auto footprint_resolution =
            resolve_footprint(selected_part.value(), resolution_footprints);
        const auto *definition = footprint_resolution.definition();
        if (definition == nullptr) {
            continue;
        }

        append_pad_resolutions(placement_id, component_placement, *definition,
                               footprint_resolution.pad_bindings(), resolutions);
    }

    return resolutions;
}
[[nodiscard]] std::vector<RatsnestEdge>
Board::ratsnest_edges(const FootprintLibrary &footprints) const {
    return derive_ratsnest_edges(resolve_pads(footprints));
}
void Board::require_layer(BoardLayerId layer) const {
    if (!layers_.contains(layer)) {
        throw std::out_of_range{"Board layer ID does not belong to this board"};
    }
}
void Board::require_net(NetId net) const { static_cast<void>(circuit().net(net)); }
void Board::require_copper_layer(BoardLayerId layer_id) const {
    require_layer(layer_id);
    if (layer(layer_id).role() != BoardLayerRole::Copper) {
        throw std::logic_error{"Board copper primitives require copper layers"};
    }
}
[[nodiscard]] std::optional<BoardLayerId> Board::layer_by_name(const std::string &name) const {
    for (std::size_t index = 0; index < layers_.size(); ++index) {
        const auto id = BoardLayerId{index};
        if (layers_.get(id).name() == name) {
            return id;
        }
    }

    return std::nullopt;
}
void Board::append_pad_resolutions(ComponentPlacementId placement_id,
                                   const ComponentPlacement &component_placement,
                                   const FootprintDefinition &definition,
                                   const std::vector<FootprintPadBinding> &bindings,
                                   std::vector<PadResolution> &resolutions) const {
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
            circuit().pin_by_definition(component_placement.component(), binding->pin());
        if (!pin.has_value()) {
            resolutions.emplace_back(placement_id, component_placement.component(), pad_id,
                                     pad.label(), position, std::nullopt, std::nullopt,
                                     PadResolutionStatus::Invalid);
            continue;
        }

        const auto net = circuit().net_of(pin.value());
        const auto status =
            net.has_value() ? PadResolutionStatus::Connected : PadResolutionStatus::Unconnected;
        resolutions.emplace_back(placement_id, component_placement.component(), pad_id, pad.label(),
                                 position, pin, net, status);
    }
}
[[nodiscard]] DiagnosticReport validate_board(const Board &board,
                                              const FootprintLibrary &footprints) {
    auto report = DiagnosticReport{};
    const auto resolution_footprints = detail::board_resolution_footprints(board, footprints);
    const auto pad_resolutions = board.resolve_pads(resolution_footprints);

    if (!board.outline().has_value()) {
        report.add(detail::board_diagnostic(DiagnosticCode{"PCB_BOARD_OUTLINE_MISSING"},
                                            "Board has no outline"));
    }

    for (std::size_t index = 0; index < board.circuit().component_count(); ++index) {
        const auto component = ComponentId{index};
        if (!board.placement_for_component(component).has_value()) {
            report.add(
                detail::board_component_diagnostic(DiagnosticCode{"PCB_COMPONENT_NOT_PLACED"},
                                                   "Component has no board placement", component));
        }

        if (!board.circuit().selected_physical_part(component).has_value()) {
            report.add(detail::board_component_diagnostic(
                DiagnosticCode{"PCB_COMPONENT_MISSING_SELECTED_PART"},
                "Component requires a selected physical part for board placement", component));
        }
    }

    for (std::size_t index = 0; index < board.placement_count(); ++index) {
        const auto placement_id = ComponentPlacementId{index};
        const auto &placement = board.placement(placement_id);
        const auto &selected_part = board.circuit().selected_physical_part(placement.component());
        if (!selected_part.has_value()) {
            continue;
        }

        const auto footprint_resolution =
            resolve_footprint(selected_part.value(), resolution_footprints);
        for (const auto &diagnostic : footprint_resolution.diagnostics().diagnostics()) {
            report.add(Diagnostic{diagnostic.severity(), diagnostic.code(), diagnostic.message(),
                                  std::vector{EntityRef::component(placement.component()),
                                              EntityRef::component_placement(placement_id)}});
        }

        const auto *definition = footprint_resolution.definition();
        if (definition == nullptr) {
            continue;
        }

        for (const auto &binding : footprint_resolution.pad_bindings()) {
            if (board.circuit()
                    .pin_by_definition(placement.component(), binding.pin())
                    .has_value()) {
                continue;
            }

            report.add(detail::board_component_diagnostic(
                DiagnosticCode{"PCB_PIN_PAD_MISMATCH"},
                "Selected part pin-pad mapping does not resolve to a concrete component pin",
                placement.component()));
        }

        if (!board.outline().has_value()) {
            continue;
        }

        for (std::size_t pad_index = 0; pad_index < definition->pad_count(); ++pad_index) {
            const auto &pad = definition->pad(FootprintPadId{pad_index});
            const auto pad_corners = detail::transformed_pad_body_corners(placement, pad);
            if (detail::pad_body_exits_outline(board.outline().value(), pad_corners)) {
                report.add(detail::board_placement_diagnostic(
                    DiagnosticCode{"PCB_PLACEMENT_OUTSIDE_OUTLINE"},
                    "Placement pad '" + pad.label() + "' is outside the board outline",
                    placement.component(), placement_id));
            }
        }
    }

    for (std::size_t index = 0; index < board.circuit().net_count(); ++index) {
        const auto net_id = NetId{index};
        const auto &net = board.circuit().net(net_id);
        if (net.pins().size() < 2U) {
            continue;
        }

        const auto placed_pad_count =
            std::count_if(pad_resolutions.begin(), pad_resolutions.end(),
                          [net_id](const PadResolution &resolution) {
                              return resolution.status() == PadResolutionStatus::Connected &&
                                     resolution.net().has_value() &&
                                     resolution.net().value() == net_id;
                          });
        if (placed_pad_count != 0) {
            continue;
        }

        report.add(
            detail::board_warning(DiagnosticCode{"PCB_NET_WITHOUT_PLACED_PADS"},
                                  "Net has logical connectivity but no placed pads on the board",
                                  std::vector{EntityRef::net(net_id)}));
    }

    detail::validate_board_drc(board, resolution_footprints, pad_resolutions, report);

    return report;
}

} // namespace volt

namespace volt::detail {

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
        if (!(*existing == definition)) {
            throw std::logic_error{
                "Board footprint definition conflicts with footprint library definition"};
        }
    }
    return library;
}
[[nodiscard]] Diagnostic board_diagnostic(DiagnosticCode code, std::string message,
                                          std::vector<EntityRef> entities) {
    return Diagnostic{Severity::Error, std::move(code), std::move(message), std::move(entities)};
}
[[nodiscard]] Diagnostic board_warning(DiagnosticCode code, std::string message,
                                       std::vector<EntityRef> entities) {
    return Diagnostic{Severity::Warning, std::move(code), std::move(message), std::move(entities)};
}
[[nodiscard]] Diagnostic board_component_diagnostic(DiagnosticCode code, std::string message,
                                                    ComponentId component) {
    return board_diagnostic(std::move(code), std::move(message),
                            std::vector{EntityRef::component(component)});
}
[[nodiscard]] Diagnostic board_placement_diagnostic(DiagnosticCode code, std::string message,
                                                    ComponentId component,
                                                    ComponentPlacementId placement) {
    return board_diagnostic(
        std::move(code), std::move(message),
        std::vector{EntityRef::component(component), EntityRef::component_placement(placement)});
}

} // namespace volt::detail
