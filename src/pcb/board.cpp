#include <volt/pcb/board.hpp>

#include <volt/core/errors.hpp>
#include <volt/pcb/queries/board_queries.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/connectivity/queries.hpp>

namespace volt {

Board::Board(const Circuit &circuit, BoardName name) : circuit_{&circuit}, name_{std::move(name)} {}

[[nodiscard]] BoardLayerId Board::add_layer(BoardLayer layer) {
    for (const auto &existing : all<BoardLayerId>()) {
        if (existing.name() == layer.name()) {
            throw KernelLogicError{ErrorCode::DuplicateName, "Board layer name already exists"};
        }
    }
    const auto id = structure_.layers.insert(std::move(layer));
    ++geometry_generation_;
    return id;
}

void Board::set_layer_stack(LayerStack stack) {
    auto copper_count = std::size_t{0};
    for (const auto layer : stack.layers()) {
        require_layer(layer);
        if (get(layer).role() == BoardLayerRole::Copper) {
            ++copper_count;
        }
    }
    if (!stack.dielectrics().empty() && stack.dielectrics().size() + 1U != copper_count) {
        throw KernelArgumentError{
            ErrorCode::InvalidArgument,
            "Layer stack dielectrics must sit between adjacent copper layers"};
    }
    structure_.layer_stack = std::move(stack);
    ++geometry_generation_;
}

void Board::set_outline(BoardOutline outline) {
    structure_.outline = std::move(outline);
    ++geometry_generation_;
}

void Board::set_design_rules(BoardDesignRules rules) { structure_.design_rules = std::move(rules); }

void Board::set_capability_profile(BoardCapabilityProfile profile) {
    structure_.capability_profile = std::move(profile);
}

[[nodiscard]] BoardFeatureId Board::add_feature(BoardFeature feature) {
    const auto id = structure_.features.insert(std::move(feature));
    ++geometry_generation_;
    return id;
}

[[nodiscard]] FootprintDefId Board::cache_footprint_definition(FootprintDefinition footprint) {
    for (std::size_t index = 0; index < all<FootprintDefId>().size(); ++index) {
        const auto id = FootprintDefId{index};
        const auto &existing = get(id);
        if (existing.ref() != footprint.ref()) {
            continue;
        }
        if (existing == footprint) {
            return id;
        }
        throw KernelLogicError{ErrorCode::DuplicateName,
                               "Board footprint definition conflicts with existing definition"};
    }
    const auto id = footprint_cache_.definitions.insert(std::move(footprint));
    ++geometry_generation_;
    return id;
}

[[nodiscard]] ComponentPlacementId Board::place_component(ComponentPlacement placement) {
    static_cast<void>(circuit().get(placement.component()));
    for (const auto &existing : all<ComponentPlacementId>()) {
        if (existing.component() == placement.component()) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Component already has a board placement"};
        }
    }
    const auto id = placements_.placements.insert(placement);
    ++geometry_generation_;
    return id;
}

[[nodiscard]] BoardTrackId Board::add_track(BoardTrack track) {
    require_net(track.net());
    require_copper_layer(track.layer());
    const auto id = copper_.tracks.insert(std::move(track));
    ++geometry_generation_;
    return id;
}

[[nodiscard]] BoardViaId Board::add_via(BoardVia via) {
    require_net(via.net());
    require_copper_layer(via.start_layer());
    require_copper_layer(via.end_layer());
    const auto id = copper_.vias.insert(via);
    ++geometry_generation_;
    return id;
}

[[nodiscard]] BoardZoneId Board::add_zone(BoardZone zone) {
    if (zone.net().has_value()) {
        require_net(zone.net().value());
    }
    for (const auto layer_id : zone.layers()) {
        require_layer(layer_id);
        if (get(layer_id).role() != BoardLayerRole::Copper) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Board copper zones require copper layers",
                                   EntityRef::board_layer(layer_id)};
        }
    }
    const auto id = copper_.zones.insert(std::move(zone));
    ++geometry_generation_;
    return id;
}

[[nodiscard]] BoardKeepoutId Board::add_keepout(BoardKeepout keepout) {
    for (const auto layer : keepout.layers()) {
        require_layer(layer);
    }
    const auto id = copper_.keepouts.insert(std::move(keepout));
    ++geometry_generation_;
    return id;
}

[[nodiscard]] BoardRoomId Board::add_room(BoardRoom room) {
    for (const auto layer : room.layers()) {
        require_layer(layer);
    }
    for (const auto &existing : all<BoardRoomId>()) {
        if (existing.name() == room.name()) {
            throw KernelLogicError{ErrorCode::DuplicateName, "Board room name already exists"};
        }
    }
    const auto id = copper_.rooms.insert(std::move(room));
    ++geometry_generation_;
    return id;
}

[[nodiscard]] BoardTextId Board::add_text(BoardText text) {
    require_layer(text.layer());
    const auto id = copper_.texts.insert(std::move(text));
    ++geometry_generation_;
    return id;
}

void Board::move(BoardPlacementMove change) {
    const auto &current = get(change.placement);
    auto replacement = ComponentPlacement{current.component(), change.position, change.rotation,
                                          change.side, current.locked()};
    placements_.placements.get(change.placement) = replacement;
    ++geometry_generation_;
}

void Board::require_layer(BoardLayerId layer) const {
    if (layer.index() >= all<BoardLayerId>().size()) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Board layer ID does not belong to this board",
                               EntityRef::board_layer(layer)};
    }
}

void Board::require_net(NetId net) const { static_cast<void>(circuit().get(net)); }

void Board::require_copper_layer(BoardLayerId layer_id) const {
    require_layer(layer_id);
    if (get(layer_id).role() != BoardLayerRole::Copper) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Board copper primitives require copper layers",
                               EntityRef::board_layer(layer_id)};
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
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled board feature kind"};
}

[[nodiscard]] std::vector<BoardLayerId> layer_stack_side_order_conflicts(const Board &board) {
    if (!board.layer_stack().has_value()) {
        return {};
    }

    auto conflicts = std::vector<BoardLayerId>{};
    auto first_bottom = std::optional<BoardLayerId>{};
    for (const auto layer_id : board.layer_stack()->layers()) {
        const auto side = board.get(layer_id).side();
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
    const auto resolution_footprints = queries::board_resolution_footprints(board, footprints);
    const auto pad_resolutions = queries::resolve_pads(board, resolution_footprints);

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

    for (std::size_t index = 0; index < board.all<volt::BoardFeatureId>().size(); ++index) {
        const auto feature_id = BoardFeatureId{index};
        const auto &feature = board.get(feature_id);
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

    for (std::size_t index = 0; index < board.circuit().all<volt::ComponentId>().size(); ++index) {
        const auto component = ComponentId{index};
        if (!queries::placement_for_component(board, component).has_value()) {
            report.add(
                detail::board_component_diagnostic(DiagnosticCode{"PCB_COMPONENT_NOT_PLACED"},
                                                   "Component has no board placement", component));
        }

        const auto has_legacy_selection =
            volt::queries::selected_physical_part(board.circuit(), component).has_value();
        const auto has_exact_selection =
            volt::queries::selected_library_part_ref(board.circuit(), component).has_value();
        if (!has_legacy_selection && !has_exact_selection) {
            report.add(detail::board_component_diagnostic(
                DiagnosticCode{"PCB_COMPONENT_MISSING_SELECTED_PART"},
                "Component requires a selected physical part for board placement", component));
        } else if (!has_legacy_selection) {
            report.add(detail::board_component_diagnostic(
                DiagnosticCode{"PCB_FOOTPRINT_UNRESOLVED"},
                "Exact selected part requires library resolution for board geometry", component));
        }
    }

    for (std::size_t index = 0; index < board.all<volt::ComponentPlacementId>().size(); ++index) {
        const auto placement_id = ComponentPlacementId{index};
        const auto &placement = board.get(placement_id);
        const auto &selected_part =
            volt::queries::selected_physical_part(board.circuit(), placement.component());
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

    for (std::size_t index = 0; index < board.circuit().all<volt::NetId>().size(); ++index) {
        const auto net_id = NetId{index};
        const auto &net = board.circuit().get(net_id);
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
