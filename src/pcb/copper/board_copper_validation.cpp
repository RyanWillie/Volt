#include <volt/pcb/board.hpp>
#include <volt/pcb/routing/board_spatial_index.hpp>

#include <volt/circuit/validation/validation.hpp>

#include "../validation/board_capability_validation.hpp"
#include "../validation/board_footprint_drc.hpp"
#include "board_copper_detail.hpp"

#include "board_room_rules.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/circuit/constraints/net_class_resolution.hpp>
#include <volt/core/rule_set.hpp>

namespace volt::detail {

void validate_track_widths(const Board &board, DiagnosticReport &report) {
    const auto &rules = board.design_rules();
    const auto rooms = BoardRoomRuleResolver{board};
    for (std::size_t index = 0; index < board.all<volt::BoardTrackId>().size(); ++index) {
        const auto track_id = BoardTrackId{index};
        const auto &track = board.get(track_id);
        if (track.width_mm() + board_drc_epsilon < rules.minimum_track_width_mm()) {
            report.add(drc_diagnostic(
                drc_diagnostic_codes::TrackWidthBelowMinimum,
                "Track width is below the board minimum",
                std::vector{EntityRef::board_track(track_id), EntityRef::net(track.net()),
                            EntityRef::board_layer(track.layer())},
                track_overlays(track, track_id),
                DiagnosticMeasurement{track.width_mm(), rules.minimum_track_width_mm()}));
        }

        const auto net_rules = resolve_net_class_rules(board.circuit(), track.net());
        auto width_requirement = net_rules.track_width_mm;
        auto room_requirement = std::optional<BoardRoomId>{};
        const auto room_override = rooms.track_width_override(track);
        if (room_override.has_value()) {
            width_requirement = room_override->value_mm;
            room_requirement = room_override->room;
        }
        if (width_requirement.has_value() &&
            track.width_mm() + board_drc_epsilon < width_requirement.value()) {
            auto entities =
                std::vector{EntityRef::board_track(track_id), EntityRef::net(track.net()),
                            EntityRef::board_layer(track.layer())};
            if (room_requirement.has_value()) {
                entities.push_back(EntityRef::board_room(room_requirement.value()));
            }
            report.add(
                drc_diagnostic(drc_diagnostic_codes::NetClassTrackWidthViolation,
                               "Track width is below the resolved net class width",
                               std::move(entities), track_overlays(track, track_id),
                               DiagnosticMeasurement{track.width_mm(), width_requirement.value()}));
        }
    }
}

void validate_via_rules(const Board &board, DiagnosticReport &report) {
    const auto &rules = board.design_rules();
    for (std::size_t index = 0; index < board.all<volt::BoardViaId>().size(); ++index) {
        const auto via_id = BoardViaId{index};
        const auto &via = board.get(via_id);
        if (via.drill_diameter_mm() + board_drc_epsilon < rules.minimum_via_drill_diameter_mm()) {
            report.add(
                drc_diagnostic(drc_diagnostic_codes::ViaDrillBelowMinimum,
                               "Via drill diameter is below the board minimum",
                               std::vector{EntityRef::board_via(via_id), EntityRef::net(via.net())},
                               std::vector{via_overlay(board, via, via_id)},
                               DiagnosticMeasurement{via.drill_diameter_mm(),
                                                     rules.minimum_via_drill_diameter_mm()}));
        }
        if (via.annular_diameter_mm() + board_drc_epsilon <
            rules.minimum_via_annular_diameter_mm()) {
            report.add(
                drc_diagnostic(drc_diagnostic_codes::ViaAnnularBelowMinimum,
                               "Via annular copper diameter is below the board minimum",
                               std::vector{EntityRef::board_via(via_id), EntityRef::net(via.net())},
                               std::vector{via_overlay(board, via, via_id)},
                               DiagnosticMeasurement{via.annular_diameter_mm(),
                                                     rules.minimum_via_annular_diameter_mm()}));
        }

        const auto net_rules = resolve_net_class_rules(board.circuit(), via.net());
        if (net_rules.via_drill_mm.has_value() &&
            via.drill_diameter_mm() + board_drc_epsilon < net_rules.via_drill_mm.value()) {
            report.add(drc_diagnostic(
                drc_diagnostic_codes::NetClassViaDrillViolation,
                "Via drill diameter is below the resolved net class drill",
                std::vector{EntityRef::board_via(via_id), EntityRef::net(via.net())},
                std::vector{via_overlay(board, via, via_id)},
                DiagnosticMeasurement{via.drill_diameter_mm(), net_rules.via_drill_mm.value()}));
        }
        if (net_rules.via_diameter_mm.has_value() &&
            via.annular_diameter_mm() + board_drc_epsilon < net_rules.via_diameter_mm.value()) {
            report.add(
                drc_diagnostic(drc_diagnostic_codes::NetClassViaDiameterViolation,
                               "Via copper diameter is below the resolved net class diameter",
                               std::vector{EntityRef::board_via(via_id), EntityRef::net(via.net())},
                               std::vector{via_overlay(board, via, via_id)},
                               DiagnosticMeasurement{via.annular_diameter_mm(),
                                                     net_rules.via_diameter_mm.value()}));
        }
    }
}

[[nodiscard]] bool layer_scope_allows(NetClassLayerScope scope, BoardLayerSide side) {
    switch (scope) {
    case NetClassLayerScope::AnyCopper:
        return true;
    case NetClassLayerScope::OuterOnly:
        return side == BoardLayerSide::Top || side == BoardLayerSide::Bottom;
    case NetClassLayerScope::InnerOnly:
        return side == BoardLayerSide::Inner;
    case NetClassLayerScope::TopOnly:
        return side == BoardLayerSide::Top;
    case NetClassLayerScope::BottomOnly:
        return side == BoardLayerSide::Bottom;
    }
    return true;
}

void validate_net_class_layers(const Board &board, DiagnosticReport &report) {
    const auto layer_allowed = [&board](const ResolvedNetClassRules &net_rules,
                                        BoardLayerId layer) {
        if (!net_rules.allowed_layer_names.empty()) {
            return std::find(net_rules.allowed_layer_names.begin(),
                             net_rules.allowed_layer_names.end(),
                             board.get(layer).name()) != net_rules.allowed_layer_names.end();
        }
        return layer_scope_allows(net_rules.layer_scope, board.get(layer).side());
    };

    for (std::size_t index = 0; index < board.all<volt::BoardTrackId>().size(); ++index) {
        const auto track_id = BoardTrackId{index};
        const auto &track = board.get(track_id);
        const auto net_rules = resolve_net_class_rules(board.circuit(), track.net());
        if (layer_allowed(net_rules, track.layer())) {
            continue;
        }
        report.add(drc_diagnostic(drc_diagnostic_codes::NetClassDisallowedLayer,
                                  "Track is on a layer the resolved net class does not allow",
                                  std::vector{EntityRef::board_track(track_id),
                                              EntityRef::net(track.net()),
                                              EntityRef::board_layer(track.layer())},
                                  track_overlays(track, track_id)));
    }

    for (std::size_t index = 0; index < board.all<volt::BoardZoneId>().size(); ++index) {
        const auto zone_id = BoardZoneId{index};
        const auto &zone = board.get(zone_id);
        if (!zone.net().has_value()) {
            continue;
        }
        const auto net_rules = resolve_net_class_rules(board.circuit(), zone.net().value());
        for (const auto layer : zone.layers()) {
            if (layer_allowed(net_rules, layer)) {
                continue;
            }
            auto vertices = std::vector<DiagnosticPoint>{};
            vertices.reserve(zone.outline().size());
            for (const auto &point : zone.outline()) {
                vertices.push_back(to_diagnostic_point(point));
            }
            report.add(drc_diagnostic(
                drc_diagnostic_codes::NetClassDisallowedLayer,
                "Zone is on a layer the resolved net class does not allow",
                std::vector{EntityRef::board_zone(zone_id), EntityRef::net(zone.net().value()),
                            EntityRef::board_layer(layer)},
                std::vector{DiagnosticOverlay::polygon(std::move(vertices),
                                                       std::vector{EntityRef::board_zone(zone_id)},
                                                       std::vector{layer})}));
        }
    }
}

void validate_outline_clearance(const Board &board, const std::vector<BoardCopperShape> &shapes,
                                DiagnosticReport &report) {
    if (!board.outline().has_value()) {
        return;
    }

    const auto &outline = board.outline().value();
    for (const auto &shape : shapes) {
        const auto outline_clearance = board.design_rules().clearance_mm(
            shape_clearance_kind(shape), BoardClearanceKind::BoardEdge);
        if (shape_satisfies_outline(shape, outline, outline_clearance)) {
            continue;
        }
        auto layer = shape.layers.empty() ? std::optional<BoardLayerId>{} : shape.layers.front();
        if (!layer.has_value()) {
            continue;
        }
        report.add(drc_diagnostic(drc_diagnostic_codes::CopperOutsideOutline,
                                  "Copper does not satisfy the board outline clearance",
                                  copper_shape_entities(shape, shape.net, layer.value()),
                                  std::vector{shape_overlay(shape, layer.value())}));
    }
}

void validate_netless_zone_outline_clearance(const Board &board, DiagnosticReport &report) {
    if (!board.outline().has_value()) {
        return;
    }

    const auto &outline = board.outline().value();
    const auto outline_clearance =
        board.design_rules().clearance_mm(BoardClearanceKind::Zone, BoardClearanceKind::BoardEdge);
    for (std::size_t zone_index = 0; zone_index < board.all<volt::BoardZoneId>().size();
         ++zone_index) {
        const auto zone_id = BoardZoneId{zone_index};
        const auto &zone = board.get(zone_id);
        if (zone.net().has_value() ||
            outline_contains_polygon(outline, zone.outline(), outline_clearance)) {
            continue;
        }
        auto vertices = std::vector<DiagnosticPoint>{};
        vertices.reserve(zone.outline().size());
        for (const auto &point : zone.outline()) {
            vertices.push_back(to_diagnostic_point(point));
        }
        const auto zone_layer = zone.layers().front();
        report.add(drc_diagnostic(
            drc_diagnostic_codes::CopperOutsideOutline,
            "Copper does not satisfy the board outline clearance",
            std::vector{EntityRef::board_zone(zone_id), EntityRef::board_layer(zone_layer)},
            std::vector{DiagnosticOverlay::polygon(std::move(vertices),
                                                   std::vector{EntityRef::board_zone(zone_id)},
                                                   std::vector{zone_layer})}));
    }
}

void validate_copper_clearance(const Board &board, const std::vector<BoardCopperShape> &shapes,
                               DiagnosticReport &report) {
    const auto index = BoardSpatialIndex{board, shapes};
    for (const auto pair : index.copper_clearance_candidates()) {
        const auto &lhs = shapes[pair.lhs_index];
        const auto &rhs = shapes[pair.rhs_index];
        const auto check = check_copper_clearance(board, lhs, rhs);
        if (!check.violates) {
            continue;
        }

        auto entities = lhs.primary_entities;
        entities.insert(entities.end(), rhs.primary_entities.begin(), rhs.primary_entities.end());
        entities.push_back(EntityRef::net(lhs.net));
        entities.push_back(EntityRef::net(rhs.net));
        entities.push_back(EntityRef::board_layer(check.layer.value()));
        if (check.room.has_value()) {
            entities.push_back(EntityRef::board_room(check.room.value()));
        }
        const auto layer = check.layer.value();
        auto overlays = std::vector{shape_overlay(lhs, layer), shape_overlay(rhs, layer)};
        report.add(drc_diagnostic(
            drc_diagnostic_codes::CopperClearanceViolation,
            clearance_pair_message(shape_clearance_kind(lhs), shape_clearance_kind(rhs)),
            std::move(entities), std::move(overlays),
            DiagnosticMeasurement{check.actual_clearance_mm, check.required_clearance_mm}));
    }
}

[[nodiscard]] bool keepout_restricts(const BoardKeepout &keepout,
                                     BoardKeepoutRestriction restriction) {
    return std::find(keepout.restrictions().begin(), keepout.restrictions().end(),
                     BoardKeepoutRestriction::All) != keepout.restrictions().end() ||
           std::find(keepout.restrictions().begin(), keepout.restrictions().end(), restriction) !=
               keepout.restrictions().end();
}

[[nodiscard]] std::optional<BoardLayerId>
first_common_keepout_layer(const BoardKeepout &keepout, const std::vector<BoardLayerId> &layers) {
    for (const auto keepout_layer : keepout.layers()) {
        if (std::find(layers.begin(), layers.end(), keepout_layer) != layers.end()) {
            return keepout_layer;
        }
    }
    return std::nullopt;
}

void validate_keepout_copper_shapes(const Board &board, const std::vector<BoardCopperShape> &shapes,
                                    DiagnosticReport &report) {
    for (std::size_t keepout_index = 0; keepout_index < board.all<volt::BoardKeepoutId>().size();
         ++keepout_index) {
        const auto keepout_id = BoardKeepoutId{keepout_index};
        const auto &keepout = board.get(keepout_id);
        if (!keepout_restricts(keepout, BoardKeepoutRestriction::Copper)) {
            continue;
        }
        for (const auto &shape : shapes) {
            if (shape_has_entity_kind(shape, EntityKind::BoardVia) ||
                shape_has_entity_kind(shape, EntityKind::BoardZone)) {
                continue;
            }
            const auto layer = first_common_keepout_layer(keepout, shape.layers);
            if (!layer.has_value() || !shape_violates_keepout(shape, keepout)) {
                continue;
            }
            report.add(drc_diagnostic(drc_diagnostic_codes::KeepoutCopperViolation,
                                      "Copper violates a board keepout",
                                      keepout_copper_entities(keepout_id, shape, layer.value())));
        }
    }
}

void validate_keepout_zones(const Board &board, DiagnosticReport &report) {
    for (std::size_t keepout_index = 0; keepout_index < board.all<volt::BoardKeepoutId>().size();
         ++keepout_index) {
        const auto keepout_id = BoardKeepoutId{keepout_index};
        const auto &keepout = board.get(keepout_id);
        if (!keepout_restricts(keepout, BoardKeepoutRestriction::Copper)) {
            continue;
        }
        for (std::size_t zone_index = 0; zone_index < board.all<volt::BoardZoneId>().size();
             ++zone_index) {
            const auto zone_id = BoardZoneId{zone_index};
            const auto &zone = board.get(zone_id);
            const auto layer = first_common_keepout_layer(keepout, zone.layers());
            if (!layer.has_value() ||
                polygon_polygon_distance(zone.outline(), keepout.outline()) > board_drc_epsilon) {
                continue;
            }
            auto entities =
                std::vector{EntityRef::board_keepout(keepout_id), EntityRef::board_zone(zone_id)};
            if (zone.net().has_value()) {
                entities.push_back(EntityRef::net(zone.net().value()));
            }
            entities.push_back(EntityRef::board_layer(layer.value()));
            report.add(drc_diagnostic(drc_diagnostic_codes::KeepoutCopperViolation,
                                      "Copper zone violates a board keepout", std::move(entities)));
        }
    }
}

void validate_keepout_vias(const Board &board, DiagnosticReport &report) {
    for (std::size_t keepout_index = 0; keepout_index < board.all<volt::BoardKeepoutId>().size();
         ++keepout_index) {
        const auto keepout_id = BoardKeepoutId{keepout_index};
        const auto &keepout = board.get(keepout_id);
        if (!keepout_restricts(keepout, BoardKeepoutRestriction::Via)) {
            continue;
        }
        for (std::size_t via_index = 0; via_index < board.all<volt::BoardViaId>().size();
             ++via_index) {
            const auto via_id = BoardViaId{via_index};
            const auto &via = board.get(via_id);
            const auto layers = via_copper_layers(board, via);
            const auto layer = first_common_keepout_layer(keepout, layers);
            if (!layer.has_value() || point_polygon_distance(via.position(), keepout.outline()) >
                                          (via.annular_diameter_mm() / 2.0) + board_drc_epsilon) {
                continue;
            }
            report.add(drc_diagnostic(
                drc_diagnostic_codes::KeepoutViaViolation, "Via violates a board keepout",
                std::vector{EntityRef::board_keepout(keepout_id), EntityRef::board_via(via_id),
                            EntityRef::net(via.net()), EntityRef::board_layer(layer.value())}));
        }
    }
}

void validate_keepout_placements(const Board &board, DiagnosticReport &report) {
    for (std::size_t keepout_index = 0; keepout_index < board.all<volt::BoardKeepoutId>().size();
         ++keepout_index) {
        const auto keepout_id = BoardKeepoutId{keepout_index};
        const auto &keepout = board.get(keepout_id);
        if (!keepout_restricts(keepout, BoardKeepoutRestriction::Placement)) {
            continue;
        }
        for (std::size_t placement_index = 0;
             placement_index < board.all<volt::ComponentPlacementId>().size(); ++placement_index) {
            const auto placement_id = ComponentPlacementId{placement_index};
            const auto &placement = board.get(placement_id);
            if (point_polygon_distance(placement.position(), keepout.outline()) >
                board_drc_epsilon) {
                continue;
            }
            report.add(drc_diagnostic(drc_diagnostic_codes::KeepoutPlacementViolation,
                                      "Component placement violates a board keepout",
                                      std::vector{EntityRef::board_keepout(keepout_id),
                                                  EntityRef::component_placement(placement_id),
                                                  EntityRef::component(placement.component())}));
        }
    }
}

[[nodiscard]] std::size_t connectivity_root(std::vector<std::size_t> &parents, std::size_t index) {
    while (parents[index] != index) {
        parents[index] = parents[parents[index]];
        index = parents[index];
    }
    return index;
}

void validate_unrouted_nets(const Board &board, const std::vector<PadResolution> &resolutions,
                            const std::vector<BoardCopperShape> &shapes, DiagnosticReport &report) {
    if (shapes.empty()) {
        return;
    }

    const auto continuity = NetContinuityView{board.circuit()};
    auto parents = std::vector<std::size_t>(shapes.size());
    std::iota(parents.begin(), parents.end(), 0U);
    for (std::size_t lhs_index = 0; lhs_index < shapes.size(); ++lhs_index) {
        for (std::size_t rhs_index = lhs_index + 1U; rhs_index < shapes.size(); ++rhs_index) {
            const auto &lhs = shapes[lhs_index];
            const auto &rhs = shapes[rhs_index];
            if (!continuity.same_group(lhs.net, rhs.net) || !layers_overlap(lhs, rhs)) {
                continue;
            }
            if (shape_distance(lhs, rhs) > lhs.radius_mm + rhs.radius_mm + board_drc_epsilon) {
                continue;
            }
            const auto lhs_root = connectivity_root(parents, lhs_index);
            const auto rhs_root = connectivity_root(parents, rhs_index);
            if (lhs_root != rhs_root) {
                parents[rhs_root] = lhs_root;
            }
        }
    }

    for (const auto &edge : derive_ratsnest_edges(board.circuit(), resolutions)) {
        const auto from_index =
            shape_index_for_pad(shapes, edge.from().placement(), edge.from().pad());
        const auto to_index = shape_index_for_pad(shapes, edge.to().placement(), edge.to().pad());
        if (!from_index.has_value() || !to_index.has_value()) {
            continue;
        }
        if (connectivity_root(parents, from_index.value()) ==
            connectivity_root(parents, to_index.value())) {
            continue;
        }

        report.add(drc_warning(drc_diagnostic_codes::NetUnrouted,
                               "Logical net still has unrouted placed pads",
                               std::vector{EntityRef::net(edge.net()),
                                           EntityRef::component_placement(edge.from().placement()),
                                           EntityRef::footprint_pad(edge.from().pad()),
                                           EntityRef::component_placement(edge.to().placement()),
                                           EntityRef::footprint_pad(edge.to().pad())}));
    }
}

void validate_board_drc(const Board &board, const FootprintLibrary &footprints,
                        const std::vector<PadResolution> &pad_resolutions,
                        DiagnosticReport &report) {
    const auto shapes = collect_copper_shapes(board, footprints, pad_resolutions);
    auto rules = RuleSet<Board>{};
    rules
        .add([](const Board &rule_board, DiagnosticReport &rule_report) {
            validate_track_widths(rule_board, rule_report);
        })
        .add([](const Board &rule_board, DiagnosticReport &rule_report) {
            validate_via_rules(rule_board, rule_report);
        })
        .add([](const Board &rule_board, DiagnosticReport &rule_report) {
            validate_net_class_layers(rule_board, rule_report);
        })
        .add([&footprints](const Board &rule_board, DiagnosticReport &rule_report) {
            validate_capability_profile_rules(rule_board, footprints, rule_report);
        })
        .add([&shapes](const Board &rule_board, DiagnosticReport &rule_report) {
            validate_outline_clearance(rule_board, shapes, rule_report);
        })
        .add([](const Board &rule_board, DiagnosticReport &rule_report) {
            validate_netless_zone_outline_clearance(rule_board, rule_report);
        })
        .add([&shapes](const Board &rule_board, DiagnosticReport &rule_report) {
            validate_copper_clearance(rule_board, shapes, rule_report);
        })
        .add([&footprints](const Board &rule_board, DiagnosticReport &rule_report) {
            validate_footprint_geometry_drc(rule_board, footprints, rule_report);
        })
        .add([&shapes](const Board &rule_board, DiagnosticReport &rule_report) {
            validate_keepout_copper_shapes(rule_board, shapes, rule_report);
        })
        .add([](const Board &rule_board, DiagnosticReport &rule_report) {
            validate_keepout_zones(rule_board, rule_report);
        })
        .add([](const Board &rule_board, DiagnosticReport &rule_report) {
            validate_keepout_vias(rule_board, rule_report);
        })
        .add([](const Board &rule_board, DiagnosticReport &rule_report) {
            validate_keepout_placements(rule_board, rule_report);
        })
        .add([&pad_resolutions, &shapes](const Board &rule_board, DiagnosticReport &rule_report) {
            validate_unrouted_nets(rule_board, pad_resolutions, shapes, rule_report);
        });
    rules.run(board, report);
}

} // namespace volt::detail
