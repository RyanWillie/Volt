#include <volt/pcb/board.hpp>

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

#include <volt/circuit/net_class_resolution.hpp>
#include <volt/core/rule_set.hpp>

namespace volt {

BoardZone::BoardZone(std::vector<BoardPoint> outline, std::vector<BoardLayerId> layers,
                     std::optional<NetId> net, BoardZoneFill fill, int priority)
    : outline_{std::move(outline)}, layers_{std::move(layers)}, net_{net}, fill_{fill},
      priority_{priority} {
    validate_layers();
}

[[nodiscard]] const std::vector<BoardPoint> &BoardZone::outline() const noexcept {
    return outline_.vertices();
}

void BoardZone::validate_layers() const {
    if (layers_.empty()) {
        throw std::invalid_argument{"Board zone layers must not be empty"};
    }
    auto sorted = layers_;
    std::sort(sorted.begin(), sorted.end(),
              [](BoardLayerId lhs, BoardLayerId rhs) { return lhs.index() < rhs.index(); });
    const auto duplicate = std::adjacent_find(sorted.begin(), sorted.end());
    if (duplicate != sorted.end()) {
        throw std::invalid_argument{"Board zone layers must not contain duplicates"};
    }
}

BoardKeepout::BoardKeepout(std::vector<BoardPoint> outline, std::vector<BoardLayerId> layers,
                           std::vector<BoardKeepoutRestriction> restrictions)
    : outline_{std::move(outline)}, layers_{std::move(layers)},
      restrictions_{std::move(restrictions)} {
    validate_layers();
    validate_restrictions();
}

[[nodiscard]] const std::vector<BoardPoint> &BoardKeepout::outline() const noexcept {
    return outline_.vertices();
}

[[nodiscard]] const std::vector<BoardKeepoutRestriction> &
BoardKeepout::restrictions() const noexcept {
    return restrictions_;
}

void BoardKeepout::validate_layers() const {
    if (layers_.empty()) {
        throw std::invalid_argument{"Board keepout layers must not be empty"};
    }
    auto sorted = layers_;
    std::sort(sorted.begin(), sorted.end(),
              [](BoardLayerId lhs, BoardLayerId rhs) { return lhs.index() < rhs.index(); });
    const auto duplicate = std::adjacent_find(sorted.begin(), sorted.end());
    if (duplicate != sorted.end()) {
        throw std::invalid_argument{"Board keepout layers must not contain duplicates"};
    }
}

void BoardKeepout::validate_restrictions() const {
    if (restrictions_.empty()) {
        throw std::invalid_argument{"Board keepout restrictions must not be empty"};
    }
    auto sorted = restrictions_;
    std::sort(sorted.begin(), sorted.end());
    const auto duplicate = std::adjacent_find(sorted.begin(), sorted.end());
    if (duplicate != sorted.end()) {
        throw std::invalid_argument{"Board keepout restrictions must not contain duplicates"};
    }
}

BoardText::BoardText(std::string text, BoardPoint position, BoardRotation rotation,
                     BoardLayerId layer, double size_mm, bool locked)
    : text_{std::move(text)}, position_{position}, rotation_{rotation}, layer_{layer},
      size_mm_{size_mm}, locked_{locked} {
    if (text_.empty()) {
        throw std::invalid_argument{"Board text must not be empty"};
    }
    if (!std::isfinite(size_mm_)) {
        throw std::invalid_argument{"Board text size must be finite"};
    }
    if (size_mm_ <= 0.0) {
        throw std::invalid_argument{"Board text size must be positive"};
    }
}

BoardTrack::BoardTrack(NetId net, BoardLayerId layer, std::vector<BoardPoint> points,
                       double width_mm)
    : net_{net}, layer_{layer}, points_{std::move(points)}, width_mm_{width_mm} {
    if (points_.size() < 2U) {
        throw std::invalid_argument{"Board track must contain at least two points"};
    }
    if (!std::isfinite(width_mm_)) {
        throw std::invalid_argument{"Board track width must be finite"};
    }
    if (width_mm_ <= 0.0) {
        throw std::invalid_argument{"Board track width must be positive"};
    }
    for (std::size_t index = 1; index < points_.size(); ++index) {
        if (points_[index - 1U] == points_[index]) {
            throw std::invalid_argument{"Board track points must not repeat adjacent vertices"};
        }
    }
}

BoardVia::BoardVia(NetId net, BoardPoint position, BoardLayerId start_layer, BoardLayerId end_layer,
                   double drill_diameter_mm, double annular_diameter_mm)
    : net_{net}, position_{position}, start_layer_{start_layer}, end_layer_{end_layer},
      drill_diameter_mm_{drill_diameter_mm}, annular_diameter_mm_{annular_diameter_mm} {
    if (start_layer_ == end_layer_) {
        throw std::invalid_argument{"Board via layer span must reference distinct layers"};
    }
    if (!std::isfinite(drill_diameter_mm_) || !std::isfinite(annular_diameter_mm_)) {
        throw std::invalid_argument{"Board via diameters must be finite"};
    }
    if (drill_diameter_mm_ <= 0.0 || annular_diameter_mm_ <= 0.0) {
        throw std::invalid_argument{"Board via diameters must be positive"};
    }
    if (annular_diameter_mm_ <= drill_diameter_mm_) {
        throw std::invalid_argument{
            "Board via annular diameter must be greater than drill diameter"};
    }
}

BoardDesignRules::BoardDesignRules(double copper_clearance_mm, double minimum_track_width_mm,
                                   double minimum_via_drill_diameter_mm,
                                   double minimum_via_annular_diameter_mm,
                                   double board_outline_clearance_mm)
    : copper_clearance_mm_{copper_clearance_mm}, minimum_track_width_mm_{minimum_track_width_mm},
      minimum_via_drill_diameter_mm_{minimum_via_drill_diameter_mm},
      minimum_via_annular_diameter_mm_{minimum_via_annular_diameter_mm},
      board_outline_clearance_mm_{board_outline_clearance_mm} {
    if (!std::isfinite(copper_clearance_mm_) || !std::isfinite(board_outline_clearance_mm_)) {
        throw std::invalid_argument{"Board design rule clearances must be finite"};
    }
    if (copper_clearance_mm_ < 0.0 || board_outline_clearance_mm_ < 0.0) {
        throw std::invalid_argument{"Board design rule clearances must not be negative"};
    }
    if (!std::isfinite(minimum_track_width_mm_) || !std::isfinite(minimum_via_drill_diameter_mm_) ||
        !std::isfinite(minimum_via_annular_diameter_mm_)) {
        throw std::invalid_argument{"Board design rule minimum dimensions must be finite"};
    }
    if (minimum_track_width_mm_ <= 0.0 || minimum_via_drill_diameter_mm_ <= 0.0 ||
        minimum_via_annular_diameter_mm_ <= 0.0) {
        throw std::invalid_argument{"Board design rule minimum dimensions must be positive"};
    }
    if (minimum_via_annular_diameter_mm_ <= minimum_via_drill_diameter_mm_) {
        throw std::invalid_argument{
            "Board design rule via annular diameter must be greater than drill diameter"};
    }
}

[[nodiscard]] double BoardDesignRules::minimum_via_drill_diameter_mm() const noexcept {
    return minimum_via_drill_diameter_mm_;
}

[[nodiscard]] double BoardDesignRules::minimum_via_annular_diameter_mm() const noexcept {
    return minimum_via_annular_diameter_mm_;
}

[[nodiscard]] double BoardDesignRules::board_outline_clearance_mm() const noexcept {
    return board_outline_clearance_mm_;
}

namespace {

[[nodiscard]] std::pair<BoardClearanceKind, BoardClearanceKind>
canonical_clearance_pair(BoardClearanceKind first, BoardClearanceKind second) {
    if (static_cast<int>(first) > static_cast<int>(second)) {
        return {second, first};
    }
    return {first, second};
}

} // namespace

void BoardDesignRules::set_clearance_mm(BoardClearanceKind first, BoardClearanceKind second,
                                        double clearance_mm) {
    if (first == BoardClearanceKind::BoardEdge && second == BoardClearanceKind::BoardEdge) {
        throw std::invalid_argument{"Clearance matrix cannot pair the board edge with itself"};
    }
    if (!std::isfinite(clearance_mm) || clearance_mm < 0.0) {
        throw std::invalid_argument{"Clearance matrix values must be finite and non-negative"};
    }

    const auto pair = canonical_clearance_pair(first, second);
    const auto low = pair.first;
    const auto high = pair.second;
    const auto position = std::find_if(
        clearance_matrix_.begin(), clearance_matrix_.end(),
        [low, high](const auto &entry) { return entry.first == low && entry.second == high; });
    if (position != clearance_matrix_.end()) {
        position->clearance_mm = clearance_mm;
        return;
    }

    const auto insert_before = std::find_if(
        clearance_matrix_.begin(), clearance_matrix_.end(), [low, high](const auto &entry) {
            return static_cast<int>(entry.first) > static_cast<int>(low) ||
                   (entry.first == low && static_cast<int>(entry.second) > static_cast<int>(high));
        });
    clearance_matrix_.insert(insert_before, BoardClearancePair{low, high, clearance_mm});
}

[[nodiscard]] double BoardDesignRules::clearance_mm(BoardClearanceKind first,
                                                    BoardClearanceKind second) const noexcept {
    const auto [low, high] = canonical_clearance_pair(first, second);
    for (const auto &entry : clearance_matrix_) {
        if (entry.first == low && entry.second == high) {
            return entry.clearance_mm;
        }
    }
    if (low == BoardClearanceKind::BoardEdge || high == BoardClearanceKind::BoardEdge) {
        return board_outline_clearance_mm_;
    }
    return copper_clearance_mm_;
}

} // namespace volt

namespace volt::detail {

[[nodiscard]] double shape_distance(const BoardCopperShape &lhs, const BoardCopperShape &rhs) {
    if (lhs.kind == BoardCopperShapeKind::Disc && rhs.kind == BoardCopperShapeKind::Disc) {
        return board_distance(lhs.points[0], rhs.points[0]);
    }
    if (lhs.kind == BoardCopperShapeKind::Segment && rhs.kind == BoardCopperShapeKind::Segment) {
        return segment_segment_distance(lhs.points[0], lhs.points[1], rhs.points[0], rhs.points[1]);
    }
    if (lhs.kind == BoardCopperShapeKind::Polygon && rhs.kind == BoardCopperShapeKind::Polygon) {
        return polygon_polygon_distance(lhs.points, rhs.points);
    }
    if (lhs.kind == BoardCopperShapeKind::Segment && rhs.kind == BoardCopperShapeKind::Disc) {
        return point_segment_distance(rhs.points[0], lhs.points[0], lhs.points[1]);
    }
    if (lhs.kind == BoardCopperShapeKind::Disc && rhs.kind == BoardCopperShapeKind::Segment) {
        return shape_distance(rhs, lhs);
    }
    if (lhs.kind == BoardCopperShapeKind::Polygon && rhs.kind == BoardCopperShapeKind::Disc) {
        return point_polygon_distance(rhs.points[0], lhs.points);
    }
    if (lhs.kind == BoardCopperShapeKind::Disc && rhs.kind == BoardCopperShapeKind::Polygon) {
        return shape_distance(rhs, lhs);
    }
    if (lhs.kind == BoardCopperShapeKind::Polygon && rhs.kind == BoardCopperShapeKind::Segment) {
        return segment_polygon_distance(rhs.points[0], rhs.points[1], lhs.points);
    }
    return shape_distance(rhs, lhs);
}

[[nodiscard]] std::optional<BoardLayerId> first_common_layer(const BoardCopperShape &lhs,
                                                             const BoardCopperShape &rhs) {
    for (const auto lhs_layer : lhs.layers) {
        if (std::find(rhs.layers.begin(), rhs.layers.end(), lhs_layer) != rhs.layers.end()) {
            return lhs_layer;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool layers_overlap(const BoardCopperShape &lhs, const BoardCopperShape &rhs) {
    return first_common_layer(lhs, rhs).has_value();
}

void append_unique_layer(std::vector<BoardLayerId> &layers, BoardLayerId layer) {
    if (std::find(layers.begin(), layers.end(), layer) == layers.end()) {
        layers.push_back(layer);
    }
}

[[nodiscard]] Diagnostic drc_diagnostic(std::string_view code, std::string message,
                                        std::vector<EntityRef> entities = {}) {
    return Diagnostic{Severity::Error, DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::Drc}, std::move(message),
                      std::move(entities)};
}

[[nodiscard]] Diagnostic drc_warning(std::string_view code, std::string message,
                                     std::vector<EntityRef> entities = {}) {
    return Diagnostic{Severity::Warning, DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::Drc}, std::move(message),
                      std::move(entities)};
}

[[nodiscard]] std::vector<BoardLayerId> via_copper_layers(const Board &board, const BoardVia &via) {
    auto result = std::vector<BoardLayerId>{};
    if (board.layer_stack().has_value()) {
        auto start = std::optional<std::size_t>{};
        auto end = std::optional<std::size_t>{};
        const auto &layers = board.layer_stack()->layers();
        for (std::size_t index = 0; index < layers.size(); ++index) {
            if (layers[index] == via.start_layer()) {
                start = index;
            }
            if (layers[index] == via.end_layer()) {
                end = index;
            }
        }
        if (start.has_value() && end.has_value()) {
            const auto first = std::min(start.value(), end.value());
            const auto last = std::max(start.value(), end.value());
            for (std::size_t index = first; index <= last; ++index) {
                if (board.layer(layers[index]).role() == BoardLayerRole::Copper) {
                    append_unique_layer(result, layers[index]);
                }
            }
            return result;
        }
    }

    append_unique_layer(result, via.start_layer());
    append_unique_layer(result, via.end_layer());
    return result;
}

[[nodiscard]] std::vector<BoardLayerId>
pad_copper_layers(const Board &board, const FootprintPad &pad, BoardSide placement_side) {
    auto result = std::vector<BoardLayerId>{};
    if (pad.layers().is_through_hole()) {
        for (std::size_t index = 0; index < board.layer_count(); ++index) {
            const auto layer_id = BoardLayerId{index};
            if (board.layer(layer_id).role() == BoardLayerRole::Copper) {
                result.push_back(layer_id);
            }
        }
        return result;
    }

    const auto front_copper = pad.layers().contains(FootprintLayer::FrontCopper);
    const auto back_copper = pad.layers().contains(FootprintLayer::BackCopper);
    const auto maps_to_top = placement_side == BoardSide::Top ? front_copper : back_copper;
    const auto maps_to_bottom = placement_side == BoardSide::Top ? back_copper : front_copper;
    for (std::size_t index = 0; index < board.layer_count(); ++index) {
        const auto layer_id = BoardLayerId{index};
        const auto &layer = board.layer(layer_id);
        if (layer.role() != BoardLayerRole::Copper) {
            continue;
        }
        if ((layer.side() == BoardLayerSide::Top && maps_to_top) ||
            (layer.side() == BoardLayerSide::Bottom && maps_to_bottom)) {
            result.push_back(layer_id);
        }
    }
    return result;
}

[[nodiscard]] const PadResolution *
find_board_pad_resolution(const std::vector<PadResolution> &resolutions,
                          ComponentPlacementId placement, FootprintPadId pad) {
    const auto match = std::find_if(
        resolutions.begin(), resolutions.end(), [placement, pad](const PadResolution &candidate) {
            return candidate.placement() == placement && candidate.pad() == pad;
        });
    if (match == resolutions.end()) {
        return nullptr;
    }
    return &*match;
}

void append_track_shapes(const Board &board, std::vector<BoardCopperShape> &shapes) {
    for (std::size_t track_index = 0; track_index < board.track_count(); ++track_index) {
        const auto track_id = BoardTrackId{track_index};
        const auto &track = board.track(track_id);
        for (std::size_t point_index = 1; point_index < track.points().size(); ++point_index) {
            shapes.push_back(BoardCopperShape{
                BoardCopperShapeKind::Segment,
                track.net(),
                std::vector{track.layer()},
                std::vector{EntityRef::board_track(track_id)},
                std::vector{track.points()[point_index - 1U], track.points()[point_index]},
                track.width_mm() / 2.0,
                std::nullopt,
            });
        }
    }
}

void append_via_shapes(const Board &board, std::vector<BoardCopperShape> &shapes) {
    for (std::size_t via_index = 0; via_index < board.via_count(); ++via_index) {
        const auto via_id = BoardViaId{via_index};
        const auto &via = board.via(via_id);
        shapes.push_back(BoardCopperShape{
            BoardCopperShapeKind::Disc,
            via.net(),
            via_copper_layers(board, via),
            std::vector{EntityRef::board_via(via_id)},
            std::vector{via.position()},
            via.annular_diameter_mm() / 2.0,
            std::nullopt,
        });
    }
}

void append_zone_shapes(const Board &board, std::vector<BoardCopperShape> &shapes) {
    for (std::size_t zone_index = 0; zone_index < board.zone_count(); ++zone_index) {
        const auto zone_id = BoardZoneId{zone_index};
        const auto &zone = board.zone(zone_id);
        if (!zone.net().has_value()) {
            continue;
        }
        shapes.push_back(BoardCopperShape{
            BoardCopperShapeKind::Polygon,
            zone.net().value(),
            zone.layers(),
            std::vector{EntityRef::board_zone(zone_id)},
            zone.outline(),
            0.0,
            std::nullopt,
        });
    }
}

void append_pad_shapes(const Board &board, const FootprintLibrary &footprints,
                       const std::vector<PadResolution> &resolutions,
                       std::vector<BoardCopperShape> &shapes) {
    for (std::size_t placement_index = 0; placement_index < board.placement_count();
         ++placement_index) {
        const auto placement_id = ComponentPlacementId{placement_index};
        const auto &placement = board.placement(placement_id);
        const auto &selected_part = board.circuit().selected_physical_part(placement.component());
        if (!selected_part.has_value()) {
            continue;
        }
        const auto footprint_resolution = resolve_footprint(selected_part.value(), footprints);
        const auto *definition = footprint_resolution.definition();
        if (definition == nullptr) {
            continue;
        }

        for (std::size_t pad_index = 0; pad_index < definition->pad_count(); ++pad_index) {
            const auto pad_id = FootprintPadId{pad_index};
            const auto *resolution = find_board_pad_resolution(resolutions, placement_id, pad_id);
            if (resolution == nullptr || resolution->status() != PadResolutionStatus::Connected ||
                !resolution->net().has_value()) {
                continue;
            }
            const auto net = resolution->net().value();

            const auto &pad = definition->pad(pad_id);
            auto layers = pad_copper_layers(board, pad, placement.side());
            if (layers.empty()) {
                continue;
            }

            auto shape_points = std::vector<BoardPoint>{};
            auto radius = 0.0;
            auto kind = BoardCopperShapeKind::Polygon;
            if (pad.shape() == FootprintPadShape::Circle ||
                pad.shape() == FootprintPadShape::Oval) {
                kind = BoardCopperShapeKind::Disc;
                shape_points.push_back(resolution->position());
                radius = std::max(pad.size().width_mm(), pad.size().height_mm()) / 2.0;
            } else {
                shape_points = transformed_pad_body_corners(placement, pad);
            }

            shapes.push_back(BoardCopperShape{
                kind,
                net,
                std::move(layers),
                std::vector{EntityRef::component_placement(placement_id),
                            EntityRef::footprint_pad(pad_id)},
                std::move(shape_points),
                radius,
                BoardPadShapeKey{placement_id, pad_id},
            });
        }
    }
}

[[nodiscard]] std::vector<BoardCopperShape>
collect_copper_shapes(const Board &board, const FootprintLibrary &footprints,
                      const std::vector<PadResolution> &resolutions) {
    auto shapes = std::vector<BoardCopperShape>{};
    append_track_shapes(board, shapes);
    append_via_shapes(board, shapes);
    append_zone_shapes(board, shapes);
    append_pad_shapes(board, footprints, resolutions, shapes);
    return shapes;
}

[[nodiscard]] bool shape_satisfies_outline(const BoardCopperShape &shape,
                                           const BoardOutline &outline, double clearance_mm) {
    if (shape.kind == BoardCopperShapeKind::Disc) {
        return outline_contains_disc(outline, shape.points[0], shape.radius_mm, clearance_mm);
    }
    if (shape.kind == BoardCopperShapeKind::Segment) {
        return outline_contains_segment(outline, shape.points[0], shape.points[1], shape.radius_mm,
                                        clearance_mm);
    }

    return outline_contains_polygon(outline, shape.points, clearance_mm);
}

[[nodiscard]] std::vector<EntityRef> copper_shape_entities(const BoardCopperShape &shape, NetId net,
                                                           BoardLayerId layer) {
    auto entities = shape.primary_entities;
    entities.push_back(EntityRef::net(net));
    entities.push_back(EntityRef::board_layer(layer));
    return entities;
}

void validate_track_widths(const Board &board, DiagnosticReport &report) {
    const auto &rules = board.design_rules();
    for (std::size_t index = 0; index < board.track_count(); ++index) {
        const auto track_id = BoardTrackId{index};
        const auto &track = board.track(track_id);
        if (track.width_mm() + board_drc_epsilon < rules.minimum_track_width_mm()) {
            report.add(drc_diagnostic(drc_diagnostic_codes::TrackWidthBelowMinimum,
                                      "Track width is below the board minimum",
                                      std::vector{EntityRef::board_track(track_id),
                                                  EntityRef::net(track.net()),
                                                  EntityRef::board_layer(track.layer())}));
        }

        const auto net_rules = resolve_net_class_rules(board.circuit(), track.net());
        if (net_rules.track_width_mm.has_value() &&
            track.width_mm() + board_drc_epsilon < net_rules.track_width_mm.value()) {
            report.add(drc_diagnostic(drc_diagnostic_codes::NetClassTrackWidthViolation,
                                      "Track width is below the resolved net class width",
                                      std::vector{EntityRef::board_track(track_id),
                                                  EntityRef::net(track.net()),
                                                  EntityRef::board_layer(track.layer())}));
        }
    }
}

void validate_via_rules(const Board &board, DiagnosticReport &report) {
    const auto &rules = board.design_rules();
    for (std::size_t index = 0; index < board.via_count(); ++index) {
        const auto via_id = BoardViaId{index};
        const auto &via = board.via(via_id);
        if (via.drill_diameter_mm() + board_drc_epsilon < rules.minimum_via_drill_diameter_mm()) {
            report.add(drc_diagnostic(
                drc_diagnostic_codes::ViaDrillBelowMinimum,
                "Via drill diameter is below the board minimum",
                std::vector{EntityRef::board_via(via_id), EntityRef::net(via.net())}));
        }
        if (via.annular_diameter_mm() + board_drc_epsilon <
            rules.minimum_via_annular_diameter_mm()) {
            report.add(drc_diagnostic(
                drc_diagnostic_codes::ViaAnnularBelowMinimum,
                "Via annular copper diameter is below the board minimum",
                std::vector{EntityRef::board_via(via_id), EntityRef::net(via.net())}));
        }

        const auto net_rules = resolve_net_class_rules(board.circuit(), via.net());
        if (net_rules.via_drill_mm.has_value() &&
            via.drill_diameter_mm() + board_drc_epsilon < net_rules.via_drill_mm.value()) {
            report.add(drc_diagnostic(
                drc_diagnostic_codes::NetClassViaDrillViolation,
                "Via drill diameter is below the resolved net class drill",
                std::vector{EntityRef::board_via(via_id), EntityRef::net(via.net())}));
        }
        if (net_rules.via_diameter_mm.has_value() &&
            via.annular_diameter_mm() + board_drc_epsilon < net_rules.via_diameter_mm.value()) {
            report.add(drc_diagnostic(
                drc_diagnostic_codes::NetClassViaDiameterViolation,
                "Via copper diameter is below the resolved net class diameter",
                std::vector{EntityRef::board_via(via_id), EntityRef::net(via.net())}));
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
                             board.layer(layer).name()) != net_rules.allowed_layer_names.end();
        }
        return layer_scope_allows(net_rules.layer_scope, board.layer(layer).side());
    };

    for (std::size_t index = 0; index < board.track_count(); ++index) {
        const auto track_id = BoardTrackId{index};
        const auto &track = board.track(track_id);
        const auto net_rules = resolve_net_class_rules(board.circuit(), track.net());
        if (layer_allowed(net_rules, track.layer())) {
            continue;
        }
        report.add(drc_diagnostic(drc_diagnostic_codes::NetClassDisallowedLayer,
                                  "Track is on a layer the resolved net class does not allow",
                                  std::vector{EntityRef::board_track(track_id),
                                              EntityRef::net(track.net()),
                                              EntityRef::board_layer(track.layer())}));
    }

    for (std::size_t index = 0; index < board.zone_count(); ++index) {
        const auto zone_id = BoardZoneId{index};
        const auto &zone = board.zone(zone_id);
        if (!zone.net().has_value()) {
            continue;
        }
        const auto net_rules = resolve_net_class_rules(board.circuit(), zone.net().value());
        for (const auto layer : zone.layers()) {
            if (layer_allowed(net_rules, layer)) {
                continue;
            }
            report.add(drc_diagnostic(drc_diagnostic_codes::NetClassDisallowedLayer,
                                      "Zone is on a layer the resolved net class does not allow",
                                      std::vector{EntityRef::board_zone(zone_id),
                                                  EntityRef::net(zone.net().value()),
                                                  EntityRef::board_layer(layer)}));
        }
    }
}

[[nodiscard]] BoardClearanceKind shape_clearance_kind(const BoardCopperShape &shape) {
    if (shape.pad.has_value() || shape_has_entity_kind(shape, EntityKind::FootprintPad)) {
        return BoardClearanceKind::Pad;
    }
    if (shape_has_entity_kind(shape, EntityKind::BoardVia)) {
        return BoardClearanceKind::Via;
    }
    if (shape_has_entity_kind(shape, EntityKind::BoardZone)) {
        return BoardClearanceKind::Zone;
    }
    return BoardClearanceKind::Track;
}

[[nodiscard]] std::string_view clearance_kind_name(BoardClearanceKind kind) {
    switch (kind) {
    case BoardClearanceKind::Track:
        return "track";
    case BoardClearanceKind::Pad:
        return "pad";
    case BoardClearanceKind::Via:
        return "via";
    case BoardClearanceKind::Zone:
        return "zone";
    case BoardClearanceKind::BoardEdge:
        return "board-edge";
    }
    return "track";
}

[[nodiscard]] std::string clearance_pair_message(BoardClearanceKind lhs, BoardClearanceKind rhs) {
    auto low = lhs;
    auto high = rhs;
    if (static_cast<int>(low) > static_cast<int>(high)) {
        std::swap(low, high);
    }
    return std::string{"Copper on different nets violates required "} +
           std::string{clearance_kind_name(low)} + "-to-" + std::string{clearance_kind_name(high)} +
           " clearance";
}

[[nodiscard]] double required_copper_clearance(const Board &board, const BoardCopperShape &lhs,
                                               const BoardCopperShape &rhs) {
    const auto pair_floor =
        board.design_rules().clearance_mm(shape_clearance_kind(lhs), shape_clearance_kind(rhs));
    return resolve_copper_clearance_mm(board.circuit(), lhs.net, rhs.net, pair_floor);
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
                                  copper_shape_entities(shape, shape.net, layer.value())));
    }
}

void validate_netless_zone_outline_clearance(const Board &board, DiagnosticReport &report) {
    if (!board.outline().has_value()) {
        return;
    }

    const auto &outline = board.outline().value();
    const auto outline_clearance =
        board.design_rules().clearance_mm(BoardClearanceKind::Zone, BoardClearanceKind::BoardEdge);
    for (std::size_t zone_index = 0; zone_index < board.zone_count(); ++zone_index) {
        const auto zone_id = BoardZoneId{zone_index};
        const auto &zone = board.zone(zone_id);
        if (zone.net().has_value() ||
            outline_contains_polygon(outline, zone.outline(), outline_clearance)) {
            continue;
        }
        report.add(drc_diagnostic(drc_diagnostic_codes::CopperOutsideOutline,
                                  "Copper does not satisfy the board outline clearance",
                                  std::vector{EntityRef::board_zone(zone_id),
                                              EntityRef::board_layer(zone.layers().front())}));
    }
}

void validate_copper_clearance(const Board &board, const std::vector<BoardCopperShape> &shapes,
                               DiagnosticReport &report) {
    for (std::size_t lhs_index = 0; lhs_index < shapes.size(); ++lhs_index) {
        for (std::size_t rhs_index = lhs_index + 1U; rhs_index < shapes.size(); ++rhs_index) {
            const auto &lhs = shapes[lhs_index];
            const auto &rhs = shapes[rhs_index];
            if (lhs.net == rhs.net) {
                continue;
            }
            const auto layer = first_common_layer(lhs, rhs);
            if (!layer.has_value()) {
                continue;
            }
            const auto clearance = shape_distance(lhs, rhs) - lhs.radius_mm - rhs.radius_mm;
            const auto required = required_copper_clearance(board, lhs, rhs);
            if (clearance + board_drc_epsilon >= required) {
                continue;
            }

            auto entities = lhs.primary_entities;
            entities.insert(entities.end(), rhs.primary_entities.begin(),
                            rhs.primary_entities.end());
            entities.push_back(EntityRef::net(lhs.net));
            entities.push_back(EntityRef::net(rhs.net));
            entities.push_back(EntityRef::board_layer(layer.value()));
            report.add(drc_diagnostic(
                drc_diagnostic_codes::CopperClearanceViolation,
                clearance_pair_message(shape_clearance_kind(lhs), shape_clearance_kind(rhs)),
                std::move(entities)));
        }
    }
}

[[nodiscard]] bool keepout_restricts(const BoardKeepout &keepout,
                                     BoardKeepoutRestriction restriction) {
    return std::find(keepout.restrictions().begin(), keepout.restrictions().end(),
                     BoardKeepoutRestriction::All) != keepout.restrictions().end() ||
           std::find(keepout.restrictions().begin(), keepout.restrictions().end(), restriction) !=
               keepout.restrictions().end();
}

[[nodiscard]] bool shape_has_entity_kind(const BoardCopperShape &shape, EntityKind kind) {
    return std::any_of(shape.primary_entities.begin(), shape.primary_entities.end(),
                       [kind](EntityRef entity) { return entity.kind() == kind; });
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

[[nodiscard]] bool shape_violates_keepout(const BoardCopperShape &shape,
                                          const BoardKeepout &keepout) {
    if (shape.kind == BoardCopperShapeKind::Disc) {
        return point_polygon_distance(shape.points[0], keepout.outline()) <=
               shape.radius_mm + board_drc_epsilon;
    }
    if (shape.kind == BoardCopperShapeKind::Segment) {
        return segment_polygon_distance(shape.points[0], shape.points[1], keepout.outline()) <=
               shape.radius_mm + board_drc_epsilon;
    }
    return polygon_polygon_distance(shape.points, keepout.outline()) <= board_drc_epsilon;
}

[[nodiscard]] std::vector<EntityRef>
keepout_copper_entities(BoardKeepoutId keepout, const BoardCopperShape &shape, BoardLayerId layer) {
    auto entities = std::vector{EntityRef::board_keepout(keepout)};
    entities.insert(entities.end(), shape.primary_entities.begin(), shape.primary_entities.end());
    entities.push_back(EntityRef::net(shape.net));
    entities.push_back(EntityRef::board_layer(layer));
    return entities;
}

void validate_keepout_copper_shapes(const Board &board, const std::vector<BoardCopperShape> &shapes,
                                    DiagnosticReport &report) {
    for (std::size_t keepout_index = 0; keepout_index < board.keepout_count(); ++keepout_index) {
        const auto keepout_id = BoardKeepoutId{keepout_index};
        const auto &keepout = board.keepout(keepout_id);
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
    for (std::size_t keepout_index = 0; keepout_index < board.keepout_count(); ++keepout_index) {
        const auto keepout_id = BoardKeepoutId{keepout_index};
        const auto &keepout = board.keepout(keepout_id);
        if (!keepout_restricts(keepout, BoardKeepoutRestriction::Copper)) {
            continue;
        }
        for (std::size_t zone_index = 0; zone_index < board.zone_count(); ++zone_index) {
            const auto zone_id = BoardZoneId{zone_index};
            const auto &zone = board.zone(zone_id);
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
    for (std::size_t keepout_index = 0; keepout_index < board.keepout_count(); ++keepout_index) {
        const auto keepout_id = BoardKeepoutId{keepout_index};
        const auto &keepout = board.keepout(keepout_id);
        if (!keepout_restricts(keepout, BoardKeepoutRestriction::Via)) {
            continue;
        }
        for (std::size_t via_index = 0; via_index < board.via_count(); ++via_index) {
            const auto via_id = BoardViaId{via_index};
            const auto &via = board.via(via_id);
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
    for (std::size_t keepout_index = 0; keepout_index < board.keepout_count(); ++keepout_index) {
        const auto keepout_id = BoardKeepoutId{keepout_index};
        const auto &keepout = board.keepout(keepout_id);
        if (!keepout_restricts(keepout, BoardKeepoutRestriction::Placement)) {
            continue;
        }
        for (std::size_t placement_index = 0; placement_index < board.placement_count();
             ++placement_index) {
            const auto placement_id = ComponentPlacementId{placement_index};
            const auto &placement = board.placement(placement_id);
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

[[nodiscard]] std::optional<std::size_t>
shape_index_for_pad(const std::vector<BoardCopperShape> &shapes, ComponentPlacementId placement,
                    FootprintPadId pad) {
    for (std::size_t index = 0; index < shapes.size(); ++index) {
        if (!shapes[index].pad.has_value()) {
            continue;
        }
        const auto shape_pad = shapes[index].pad.value();
        if (shape_pad.placement == placement && shape_pad.pad == pad) {
            return index;
        }
    }
    return std::nullopt;
}

void validate_unrouted_nets(const std::vector<PadResolution> &resolutions,
                            const std::vector<BoardCopperShape> &shapes, DiagnosticReport &report) {
    if (shapes.empty()) {
        return;
    }

    auto parents = std::vector<std::size_t>(shapes.size());
    std::iota(parents.begin(), parents.end(), 0U);
    for (std::size_t lhs_index = 0; lhs_index < shapes.size(); ++lhs_index) {
        for (std::size_t rhs_index = lhs_index + 1U; rhs_index < shapes.size(); ++rhs_index) {
            const auto &lhs = shapes[lhs_index];
            const auto &rhs = shapes[rhs_index];
            if (lhs.net != rhs.net || !layers_overlap(lhs, rhs)) {
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

    for (const auto &edge : derive_ratsnest_edges(resolutions)) {
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
        .add([&shapes](const Board &rule_board, DiagnosticReport &rule_report) {
            validate_outline_clearance(rule_board, shapes, rule_report);
        })
        .add([](const Board &rule_board, DiagnosticReport &rule_report) {
            validate_netless_zone_outline_clearance(rule_board, rule_report);
        })
        .add([&shapes](const Board &rule_board, DiagnosticReport &rule_report) {
            validate_copper_clearance(rule_board, shapes, rule_report);
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
        .add([&pad_resolutions, &shapes](const Board &, DiagnosticReport &rule_report) {
            validate_unrouted_nets(pad_resolutions, shapes, rule_report);
        });
    rules.run(board, report);
}

} // namespace volt::detail
