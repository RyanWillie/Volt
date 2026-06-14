#include <volt/pcb/board.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/queries.hpp>

namespace volt {

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

    const auto resolution_footprints = detail::board_resolution_footprints(board, footprints);
    for (std::size_t index = 0; index < board.placement_count(); ++index) {
        const auto placement_id = ComponentPlacementId{index};
        const auto &component_placement = board.placement(placement_id);
        const auto &selected_part =
            board.circuit().selected_physical_part(component_placement.component());
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

[[nodiscard]] bool
optional_library_polygon_conflicts(const std::optional<FootprintPolygon> &board_polygon,
                                   const std::optional<FootprintPolygon> &library_polygon) {
    if (!library_polygon.has_value()) {
        return false;
    }
    if (!board_polygon.has_value()) {
        return true;
    }
    return board_polygon.value() != library_polygon.value();
}

} // namespace

Board::Board(const Circuit &circuit, BoardName name) : circuit_{&circuit}, name_{std::move(name)} {}

[[nodiscard]] BoardLayerId Board::add_layer(BoardLayer layer) {
    const auto id = structure_.add_layer(std::move(layer));
    ++geometry_mutation_count_;
    return id;
}

void Board::set_layer_stack(LayerStack stack) {
    structure_.set_layer_stack(std::move(stack));
    ++geometry_mutation_count_;
}

void Board::set_outline(BoardOutline outline) {
    structure_.set_outline(std::move(outline));
    ++geometry_mutation_count_;
}

void Board::set_design_rules(BoardDesignRules rules) { structure_.set_design_rules(rules); }

void Board::set_capability_profile(BoardCapabilityProfile profile) {
    structure_.set_capability_profile(std::move(profile));
}

[[nodiscard]] BoardFeatureId Board::add_feature(BoardFeature feature) {
    const auto id = structure_.add_feature(std::move(feature));
    ++geometry_mutation_count_;
    return id;
}

[[nodiscard]] FootprintDefId Board::cache_footprint_definition(FootprintDefinition footprint) {
    const auto count_before = footprint_cache_.footprint_definition_count();
    const auto id = footprint_cache_.cache_footprint_definition(std::move(footprint));
    if (footprint_cache_.footprint_definition_count() != count_before) {
        ++geometry_mutation_count_;
    }
    return id;
}

[[nodiscard]] ComponentPlacementId Board::place_component(ComponentPlacement placement) {
    static_cast<void>(circuit().component(placement.component()));
    const auto id = placements_.place_component(placement);
    ++geometry_mutation_count_;
    return id;
}

[[nodiscard]] BoardTrackId Board::add_track(BoardTrack track) {
    require_net(track.net());
    require_copper_layer(track.layer());
    const auto id = copper_.add_track(std::move(track));
    ++geometry_mutation_count_;
    return id;
}

[[nodiscard]] BoardViaId Board::add_via(BoardVia via) {
    require_net(via.net());
    require_copper_layer(via.start_layer());
    require_copper_layer(via.end_layer());
    const auto id = copper_.add_via(via);
    ++geometry_mutation_count_;
    return id;
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
    const auto id = copper_.add_zone(std::move(zone));
    ++geometry_mutation_count_;
    return id;
}

[[nodiscard]] BoardKeepoutId Board::add_keepout(BoardKeepout keepout) {
    for (const auto layer : keepout.layers()) {
        require_layer(layer);
    }
    const auto id = copper_.add_keepout(std::move(keepout));
    ++geometry_mutation_count_;
    return id;
}

[[nodiscard]] BoardRoomId Board::add_room(BoardRoom room) {
    for (const auto layer : room.layers()) {
        require_layer(layer);
    }
    const auto id = copper_.add_room(std::move(room));
    ++geometry_mutation_count_;
    return id;
}

[[nodiscard]] BoardTextId Board::add_text(BoardText text) {
    require_layer(text.layer());
    const auto id = copper_.add_text(std::move(text));
    ++geometry_mutation_count_;
    return id;
}

[[nodiscard]] const std::optional<LayerStack> &Board::layer_stack() const noexcept {
    return structure_.layer_stack();
}

[[nodiscard]] const FootprintDefinition &Board::footprint_definition(FootprintDefId id) const {
    return footprint_cache_.footprint_definition(id);
}

[[nodiscard]] std::size_t Board::footprint_definition_count() const noexcept {
    return footprint_cache_.footprint_definition_count();
}

[[nodiscard]] std::optional<FootprintDefId>
Board::footprint_definition_id(const FootprintRef &ref) const noexcept {
    return footprint_cache_.footprint_definition_id(ref);
}

[[nodiscard]] const ComponentPlacement &Board::placement(ComponentPlacementId id) const {
    return placements_.placement(id);
}

[[nodiscard]] const BoardKeepout &Board::keepout(BoardKeepoutId id) const {
    return copper_.keepout(id);
}

[[nodiscard]] std::size_t Board::keepout_count() const noexcept { return copper_.keepout_count(); }

[[nodiscard]] const BoardRoom &Board::room(BoardRoomId id) const { return copper_.room(id); }

[[nodiscard]] std::size_t Board::room_count() const noexcept { return copper_.room_count(); }

[[nodiscard]] const BoardText &Board::text(BoardTextId id) const { return copper_.text(id); }

[[nodiscard]] std::size_t Board::text_count() const noexcept { return copper_.text_count(); }

[[nodiscard]] std::optional<ComponentPlacementId>
Board::placement_for_component(ComponentId component) const noexcept {
    return placements_.placement_for_component(component);
}

[[nodiscard]] std::vector<PadResolution>
Board::resolve_pads(const FootprintLibrary &footprints) const {
    auto resolutions = std::vector<PadResolution>{};
    for (const auto &resolved : resolved_placement_footprints(*this, footprints)) {
        append_pad_resolutions(resolved.placement_id, resolved.placement, resolved.definition,
                               resolved.pad_bindings, resolutions);
    }

    return resolutions;
}

[[nodiscard]] std::vector<ProjectedFootprintGeometry>
Board::project_footprint_geometries(const FootprintLibrary &footprints) const {
    auto geometries = std::vector<ProjectedFootprintGeometry>{};
    for (const auto &resolved : resolved_placement_footprints(*this, footprints)) {
        auto courtyard = std::optional<std::vector<BoardPoint>>{};
        if (resolved.definition.courtyard().has_value()) {
            courtyard = detail::transformed_footprint_polygon(
                resolved.placement, resolved.definition.courtyard().value());
        }
        auto body = std::optional<std::vector<BoardPoint>>{};
        if (resolved.definition.body().has_value()) {
            body = detail::transformed_footprint_polygon(resolved.placement,
                                                         resolved.definition.body().value());
        }

        geometries.emplace_back(resolved.placement_id, resolved.placement.component(),
                                std::move(courtyard), std::move(body));
    }

    return geometries;
}

[[nodiscard]] std::vector<RatsnestEdge>
Board::ratsnest_edges(const FootprintLibrary &footprints) const {
    return derive_ratsnest_edges(resolve_pads(footprints));
}

void Board::require_layer(BoardLayerId layer) const { structure_.require_layer(layer); }

void Board::require_net(NetId net) const { static_cast<void>(circuit().net(net)); }

void Board::require_copper_layer(BoardLayerId layer_id) const {
    require_layer(layer_id);
    if (layer(layer_id).role() != BoardLayerRole::Copper) {
        throw std::logic_error{"Board copper primitives require copper layers"};
    }
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
            queries::pin_by_definition(circuit(), component_placement.component(), binding->pin());
        if (!pin.has_value()) {
            resolutions.emplace_back(placement_id, component_placement.component(), pad_id,
                                     pad.label(), position, std::nullopt, std::nullopt,
                                     PadResolutionStatus::Invalid);
            continue;
        }

        const auto net = queries::net_of(circuit(), pin.value());
        const auto status =
            net.has_value() ? PadResolutionStatus::Connected : PadResolutionStatus::Unconnected;
        resolutions.emplace_back(placement_id, component_placement.component(), pad_id, pad.label(),
                                 position, pin, net, status);
    }
}

namespace {

[[nodiscard]] bool feature_role_expected(BoardFeatureKind kind) noexcept {
    return kind == BoardFeatureKind::Hole || kind == BoardFeatureKind::Slot ||
           kind == BoardFeatureKind::Cutout || kind == BoardFeatureKind::Circle;
}

[[nodiscard]] bool board_feature_fits_outline(const BoardOutline &outline,
                                              const BoardFeature &feature) {
    switch (feature.kind()) {
    case BoardFeatureKind::Hole:
        return detail::outline_contains_disc(outline, feature.hole().center(),
                                             feature.hole().drill_diameter_mm() / 2.0, 0.0);
    case BoardFeatureKind::Slot:
        return detail::outline_contains_segment(outline, feature.slot().start(),
                                                feature.slot().end(),
                                                feature.slot().width_mm() / 2.0, 0.0);
    case BoardFeatureKind::Cutout:
        return detail::outline_contains_polygon(outline, feature.cutout().outline(), 0.0);
    case BoardFeatureKind::Circle:
        return detail::outline_contains_disc(outline, feature.circle().center(),
                                             feature.circle().diameter_mm() / 2.0, 0.0);
    }
    throw std::logic_error{"Unhandled board feature kind"};
}

[[nodiscard]] std::vector<BoardLayerId> layer_stack_side_order_conflicts(const Board &board) {
    if (!board.layer_stack().has_value()) {
        return {};
    }

    auto conflicts = std::vector<BoardLayerId>{};
    auto first_bottom = std::optional<BoardLayerId>{};
    for (const auto layer_id : board.layer_stack()->layers()) {
        const auto side = board.layer(layer_id).side();
        if (side == BoardLayerSide::Bottom && !first_bottom.has_value()) {
            first_bottom = layer_id;
            continue;
        }
        if (side == BoardLayerSide::Top && first_bottom.has_value()) {
            if (conflicts.empty()) {
                conflicts.push_back(first_bottom.value());
            }
            conflicts.push_back(layer_id);
        }
    }
    return conflicts;
}

[[nodiscard]] std::vector<EntityRef> layer_entities(const std::vector<BoardLayerId> &layers) {
    auto entities = std::vector<EntityRef>{};
    entities.reserve(layers.size());
    for (const auto layer : layers) {
        entities.push_back(EntityRef::board_layer(layer));
    }
    return entities;
}

} // namespace

[[nodiscard]] DiagnosticReport validate_board(const Board &board,
                                              const FootprintLibrary &footprints) {
    auto report = DiagnosticReport{};
    const auto resolution_footprints = detail::board_resolution_footprints(board, footprints);
    const auto pad_resolutions = board.resolve_pads(resolution_footprints);

    if (!board.outline().has_value()) {
        report.add(detail::board_diagnostic(DiagnosticCode{"PCB_BOARD_OUTLINE_MISSING"},
                                            "Board has no outline"));
    }

    if (const auto conflicts = layer_stack_side_order_conflicts(board); !conflicts.empty()) {
        report.add(detail::board_diagnostic(
            DiagnosticCode{"PCB_LAYER_STACK_SIDE_ORDER_CONFLICT"},
            "Layer stack side metadata is inconsistent with top-to-bottom order",
            layer_entities(conflicts)));
    }

    for (std::size_t index = 0; index < board.feature_count(); ++index) {
        const auto feature_id = BoardFeatureId{index};
        const auto &feature = board.feature(feature_id);
        if (feature_role_expected(feature.kind()) && feature.role().empty()) {
            report.add(detail::board_warning(DiagnosticCode{"PCB_BOARD_FEATURE_ROLE_MISSING"},
                                             "Board feature is missing mechanical role metadata",
                                             std::vector{EntityRef::board_feature(feature_id)}));
        }
        if (board.outline().has_value() &&
            !board_feature_fits_outline(board.outline().value(), feature)) {
            report.add(
                detail::board_diagnostic(DiagnosticCode{"PCB_BOARD_FEATURE_OUTSIDE_OUTLINE"},
                                         "Board feature geometry is outside the board outline",
                                         std::vector{EntityRef::board_feature(feature_id)}));
        }
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
            report.add(Diagnostic{diagnostic.severity(), diagnostic.code(),
                                  DiagnosticCategory{diagnostic_categories::PcbBoard},
                                  diagnostic.message(),
                                  std::vector{EntityRef::component(placement.component()),
                                              EntityRef::component_placement(placement_id)}});
        }

        const auto *definition = footprint_resolution.definition();
        if (definition == nullptr) {
            continue;
        }

        for (const auto &binding : footprint_resolution.pad_bindings()) {
            if (queries::pin_by_definition(board.circuit(), placement.component(), binding.pin())
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

    detail::validate_board_visual(board, resolution_footprints, report);
    detail::validate_board_drc(board, resolution_footprints, pad_resolutions, report);

    return report;
}

} // namespace volt

namespace volt::detail {

[[nodiscard]] bool
footprint_library_definition_conflicts(const FootprintDefinition &board_definition,
                                       const FootprintDefinition &library_definition) {
    if (board_definition.ref() != library_definition.ref()) {
        return false;
    }
    if (board_definition.pads() != library_definition.pads()) {
        return true;
    }
    return optional_library_polygon_conflicts(board_definition.courtyard(),
                                              library_definition.courtyard()) ||
           optional_library_polygon_conflicts(board_definition.body(), library_definition.body());
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
        if (footprint_library_definition_conflicts(*existing, definition)) {
            throw std::logic_error{
                "Board footprint definition conflicts with footprint library definition"};
        }
    }
    return library;
}

[[nodiscard]] Diagnostic board_diagnostic(DiagnosticCode code, std::string message,
                                          std::vector<EntityRef> entities) {
    return Diagnostic{Severity::Error, std::move(code),
                      DiagnosticCategory{diagnostic_categories::PcbBoard}, std::move(message),
                      std::move(entities)};
}

[[nodiscard]] Diagnostic board_warning(DiagnosticCode code, std::string message,
                                       std::vector<EntityRef> entities) {
    return Diagnostic{Severity::Warning, std::move(code),
                      DiagnosticCategory{diagnostic_categories::PcbBoard}, std::move(message),
                      std::move(entities)};
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
