#include <volt/io/pcb/fabrication_writer.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <locale>
#include <map>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace volt::io {
namespace {

constexpr auto kCoordScale = 1000000.0;
constexpr auto kEpsilon = 1.0e-9;

enum class FabLayer {
    FrontCopper,
    BackCopper,
    FrontMask,
    BackMask,
    FrontPaste,
    BackPaste,
    FrontSilk,
    BackSilk,
    EdgeCuts,
};

enum class ApertureKind {
    Circle,
    Rectangle,
    Oval,
};

struct Aperture {
    ApertureKind kind;
    double width_mm;
    double height_mm;

    [[nodiscard]] friend bool operator<(const Aperture &lhs, const Aperture &rhs) noexcept {
        return std::tie(lhs.kind, lhs.width_mm, lhs.height_mm) <
               std::tie(rhs.kind, rhs.width_mm, rhs.height_mm);
    }
};

enum class GerberCommandKind {
    Flash,
    Polyline,
    Region,
};

struct GerberCommand {
    GerberCommandKind kind;
    std::optional<Aperture> aperture;
    std::vector<BoardPoint> points;
};

struct DrillHit {
    BoardPoint position;
    double diameter_mm;
};

struct FabricationLayerPlan {
    std::map<FabLayer, BoardLayerId> owners;
    std::map<FabLayer, bool> ambiguous;

    [[nodiscard]] bool is_ambiguous(FabLayer layer) const {
        const auto match = ambiguous.find(layer);
        return match != ambiguous.end() && match->second;
    }

    [[nodiscard]] std::optional<BoardLayerId> owner(FabLayer layer) const {
        const auto match = owners.find(layer);
        if (match == owners.end() || is_ambiguous(layer)) {
            return std::nullopt;
        }
        return match->second;
    }

    [[nodiscard]] bool has(FabLayer layer) const { return owner(layer).has_value(); }
};

struct GerberDocument {
    std::vector<GerberCommand> commands;
};

struct FabAccumulator {
    FabricationLayerPlan layer_plan;
    std::map<FabLayer, GerberDocument> gerbers;
    std::vector<DrillHit> plated_drills;
    std::vector<DrillHit> nonplated_drills;
    DiagnosticReport diagnostics;
};

[[nodiscard]] std::string decimal(double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument{"Fabrication numeric values must be finite"};
    }
    if (std::abs(value) < kEpsilon) {
        value = 0.0;
    }
    auto out = std::ostringstream{};
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(6) << value;
    return out.str();
}

[[nodiscard]] long long scaled_key(double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument{"Fabrication numeric values must be finite"};
    }
    return static_cast<long long>(std::llround(value * kCoordScale));
}

[[nodiscard]] std::string decimal_from_scaled_key(long long key) {
    return decimal(static_cast<double>(key) / kCoordScale);
}

[[nodiscard]] std::string coordinate(double value) { return std::to_string(scaled_key(value)); }

void write_coordinate(std::ostream &out, BoardPoint point) {
    out << 'X' << coordinate(point.x_mm()) << 'Y' << coordinate(point.y_mm());
}

[[nodiscard]] Diagnostic fabrication_diagnostic(Severity severity, std::string_view code,
                                                std::string message, std::string rule,
                                                std::vector<EntityRef> entities) {
    if (entities.empty()) {
        entities.push_back(EntityRef::board());
    }
    return Diagnostic{
        severity,
        DiagnosticCode{std::string{code}},
        DiagnosticCategory{diagnostic_categories::PcbFabrication},
        std::move(message),
        std::move(entities),
        {},
        std::nullopt,
        std::move(rule),
    };
}

[[nodiscard]] std::vector<EntityRef> board_entity() { return std::vector{EntityRef::board()}; }

void add_missing_geometry(FabAccumulator &fab, std::string rule, std::string message,
                          std::vector<EntityRef> entities = board_entity()) {
    fab.diagnostics.add(fabrication_diagnostic(
        Severity::Error, pcb_fabrication_diagnostic_codes::NativeFabMissingGeometry,
        std::move(message), std::move(rule), std::move(entities)));
}

void add_unsupported_layer(FabAccumulator &fab, std::string rule, std::string message,
                           std::vector<EntityRef> entities = board_entity()) {
    fab.diagnostics.add(fabrication_diagnostic(
        Severity::Error, pcb_fabrication_diagnostic_codes::NativeFabUnsupportedLayer,
        std::move(message), std::move(rule), std::move(entities)));
}

void add_unsupported_geometry(FabAccumulator &fab, std::string rule, std::string message,
                              std::vector<EntityRef> entities = board_entity()) {
    fab.diagnostics.add(fabrication_diagnostic(
        Severity::Error, pcb_fabrication_diagnostic_codes::NativeFabUnsupportedGeometry,
        std::move(message), std::move(rule), std::move(entities)));
}

void add_lossy_geometry(FabAccumulator &fab, std::string rule, std::string message,
                        std::vector<EntityRef> entities = board_entity()) {
    fab.diagnostics.add(fabrication_diagnostic(
        Severity::Warning, pcb_fabrication_diagnostic_codes::NativeFabLossyGeometry,
        std::move(message), std::move(rule), std::move(entities)));
}

[[nodiscard]] std::optional<FabLayer> supported_fab_layer(const BoardLayer &layer) {
    if (!layer.enabled()) {
        return std::nullopt;
    }

    switch (layer.role()) {
    case BoardLayerRole::Copper:
        switch (layer.side()) {
        case BoardLayerSide::Top:
            return FabLayer::FrontCopper;
        case BoardLayerSide::Bottom:
            return FabLayer::BackCopper;
        case BoardLayerSide::Inner:
        case BoardLayerSide::Both:
        case BoardLayerSide::None:
            return std::nullopt;
        }
        break;
    case BoardLayerRole::SolderMask:
        switch (layer.side()) {
        case BoardLayerSide::Top:
            return FabLayer::FrontMask;
        case BoardLayerSide::Bottom:
            return FabLayer::BackMask;
        case BoardLayerSide::Inner:
        case BoardLayerSide::Both:
        case BoardLayerSide::None:
            return std::nullopt;
        }
        break;
    case BoardLayerRole::Paste:
        switch (layer.side()) {
        case BoardLayerSide::Top:
            return FabLayer::FrontPaste;
        case BoardLayerSide::Bottom:
            return FabLayer::BackPaste;
        case BoardLayerSide::Inner:
        case BoardLayerSide::Both:
        case BoardLayerSide::None:
            return std::nullopt;
        }
        break;
    case BoardLayerRole::Silkscreen:
        switch (layer.side()) {
        case BoardLayerSide::Top:
            return FabLayer::FrontSilk;
        case BoardLayerSide::Bottom:
            return FabLayer::BackSilk;
        case BoardLayerSide::Inner:
        case BoardLayerSide::Both:
        case BoardLayerSide::None:
            return std::nullopt;
        }
        break;
    case BoardLayerRole::EdgeCuts:
        return FabLayer::EdgeCuts;
    case BoardLayerRole::Fabrication:
    case BoardLayerRole::Drill:
    case BoardLayerRole::Mechanical:
    case BoardLayerRole::Courtyard:
    case BoardLayerRole::Keepout:
        return std::nullopt;
    }
    throw std::logic_error{"Unhandled board layer side"};
}

[[nodiscard]] bool is_fabrication_output_layer(const BoardLayer &layer) {
    if (!layer.enabled()) {
        return false;
    }
    switch (layer.role()) {
    case BoardLayerRole::Copper:
    case BoardLayerRole::SolderMask:
    case BoardLayerRole::Paste:
    case BoardLayerRole::Silkscreen:
    case BoardLayerRole::EdgeCuts:
        return true;
    case BoardLayerRole::Fabrication:
    case BoardLayerRole::Drill:
    case BoardLayerRole::Mechanical:
    case BoardLayerRole::Courtyard:
    case BoardLayerRole::Keepout:
        return false;
    }
    throw std::logic_error{"Unhandled board layer role"};
}

[[nodiscard]] bool is_planned_layer(const FabricationLayerPlan &plan, BoardLayerId layer) {
    return std::any_of(plan.owners.begin(), plan.owners.end(),
                       [layer](const auto &entry) { return entry.second == layer; });
}

void add_planned_layer(FabAccumulator &fab, FabLayer fab_layer, BoardLayerId board_layer) {
    const auto existing = fab.layer_plan.owners.find(fab_layer);
    if (existing == fab.layer_plan.owners.end()) {
        fab.layer_plan.owners.emplace(fab_layer, board_layer);
        return;
    }

    fab.layer_plan.ambiguous[fab_layer] = true;
    add_unsupported_layer(
        fab, "board.layer.fabrication_role_collision",
        "Multiple enabled board layers map to the same native fabrication output layer",
        std::vector{EntityRef::board_layer(existing->second), EntityRef::board_layer(board_layer)});
}

[[nodiscard]] std::vector<EntityRef> layer_entities(const std::vector<BoardLayerId> &layers) {
    auto entities = std::vector<EntityRef>{};
    entities.reserve(layers.size());
    for (const auto layer : layers) {
        entities.push_back(EntityRef::board_layer(layer));
    }
    return entities;
}

void build_copper_layer_plan(FabAccumulator &fab, const Board &board) {
    if (!board.layer_stack().has_value()) {
        add_missing_geometry(fab, "board.layer_stack",
                             "Native fabrication export requires a board layer stack for copper "
                             "output ordering");
        return;
    }

    auto copper_layers = std::vector<BoardLayerId>{};
    for (const auto layer_id : board.layer_stack()->layers()) {
        const auto &layer = board.layer(layer_id);
        if (layer.enabled() && layer.role() == BoardLayerRole::Copper) {
            copper_layers.push_back(layer_id);
        }
    }
    if (copper_layers.size() != 2U) {
        add_unsupported_layer(fab, "board.layer_stack.copper_count",
                              "Native fabrication writer v1 exports exactly two stackup copper "
                              "layers",
                              layer_entities(copper_layers));
        return;
    }

    const auto front = copper_layers.front();
    const auto back = copper_layers.back();
    if (board.layer(front).side() != BoardLayerSide::Top ||
        board.layer(back).side() != BoardLayerSide::Bottom) {
        add_unsupported_layer(
            fab, "board.layer_stack.outer_sides",
            "Native fabrication writer v1 requires stackup outer copper layers "
            "to be top then bottom",
            std::vector{EntityRef::board_layer(front), EntityRef::board_layer(back)});
        return;
    }

    add_planned_layer(fab, FabLayer::FrontCopper, front);
    add_planned_layer(fab, FabLayer::BackCopper, back);
}

void build_layer_plan(FabAccumulator &fab, const Board &board) {
    build_copper_layer_plan(fab, board);
    const auto has_copper_plan =
        fab.layer_plan.has(FabLayer::FrontCopper) && fab.layer_plan.has(FabLayer::BackCopper);

    for (std::size_t index = 0; index < board.layer_count(); ++index) {
        const auto layer_id = BoardLayerId{index};
        const auto &layer = board.layer(layer_id);
        if (layer.role() == BoardLayerRole::Copper) {
            if (has_copper_plan && layer.enabled() && !is_planned_layer(fab.layer_plan, layer_id)) {
                add_unsupported_layer(fab, "board.layer.copper_stack_membership",
                                      "Enabled copper layers must be owned by the two-layer board "
                                      "stack to participate in native fabrication output",
                                      std::vector{EntityRef::board_layer(layer_id)});
            }
            continue;
        }
        const auto fab_layer = supported_fab_layer(layer);
        if (fab_layer.has_value()) {
            add_planned_layer(fab, fab_layer.value(), layer_id);
        } else if (is_fabrication_output_layer(layer)) {
            add_unsupported_layer(fab, "board.layer.fabrication_role",
                                  "Enabled board fabrication layer role and side cannot be emitted "
                                  "by the native v1 writer",
                                  std::vector{EntityRef::board_layer(layer_id)});
        }
    }
}

[[nodiscard]] bool has_output_layer(const FabAccumulator &fab, FabLayer layer) {
    return fab.layer_plan.has(layer);
}

[[nodiscard]] std::optional<FabLayer> owned_fab_layer(const FabAccumulator &fab, const Board &board,
                                                      BoardLayerId layer_id) {
    const auto fab_layer = supported_fab_layer(board.layer(layer_id));
    if (!fab_layer.has_value()) {
        return std::nullopt;
    }
    const auto owner = fab.layer_plan.owner(fab_layer.value());
    if (!owner.has_value() || owner.value() != layer_id) {
        return std::nullopt;
    }
    return fab_layer;
}

[[nodiscard]] bool ensure_output_layer(FabAccumulator &fab, FabLayer layer, std::string rule,
                                       std::string message, std::vector<EntityRef> entities) {
    if (has_output_layer(fab, layer)) {
        return true;
    }
    add_unsupported_layer(fab, std::move(rule), std::move(message), std::move(entities));
    return false;
}

[[nodiscard]] BoardPoint transform_point(const ComponentPlacement &placement,
                                         FootprintPoint point) {
    const auto radians = placement.rotation().degrees() * std::acos(-1.0) / 180.0;
    const auto cosine = std::cos(radians);
    const auto sine = std::sin(radians);
    const auto x = (point.x_mm() * cosine) - (point.y_mm() * sine);
    const auto y = (point.x_mm() * sine) + (point.y_mm() * cosine);
    return BoardPoint{placement.position().x_mm() + x, placement.position().y_mm() + y};
}

[[nodiscard]] bool is_quarter_turn(double degrees) {
    auto normalized = std::fmod(degrees, 180.0);
    if (normalized < 0.0) {
        normalized += 180.0;
    }
    return std::abs(normalized - 90.0) < kEpsilon;
}

[[nodiscard]] bool is_orthogonal(double degrees) {
    auto normalized = std::fmod(degrees, 90.0);
    if (normalized < 0.0) {
        normalized += 90.0;
    }
    return normalized < kEpsilon || std::abs(normalized - 90.0) < kEpsilon;
}

[[nodiscard]] std::vector<EntityRef> pad_entities(ComponentPlacementId placement,
                                                  FootprintPadId pad) {
    return std::vector{EntityRef::component_placement(placement), EntityRef::footprint_pad(pad)};
}

[[nodiscard]] std::optional<Aperture> pad_aperture(const FootprintPad &pad,
                                                   const ComponentPlacement &placement,
                                                   ComponentPlacementId placement_id,
                                                   FootprintPadId pad_id, FabAccumulator &fab) {
    auto width = pad.size().width_mm();
    auto height = pad.size().height_mm();
    if (is_quarter_turn(placement.rotation().degrees())) {
        std::swap(width, height);
    } else if (!is_orthogonal(placement.rotation().degrees()) &&
               pad.shape() != FootprintPadShape::Circle) {
        add_unsupported_geometry(fab, "footprint.pad.rotation",
                                 "Native fabrication writer v1 does not export non-orthogonally "
                                 "rotated non-circular pads",
                                 pad_entities(placement_id, pad_id));
        return std::nullopt;
    }

    switch (pad.shape()) {
    case FootprintPadShape::Circle:
        return Aperture{ApertureKind::Circle, width, width};
    case FootprintPadShape::Rectangle:
        return Aperture{ApertureKind::Rectangle, width, height};
    case FootprintPadShape::RoundedRectangle:
        add_unsupported_geometry(fab, "footprint.pad.rounded_rectangle",
                                 "Native fabrication writer v1 does not export rounded rectangle "
                                 "pads without approximation",
                                 pad_entities(placement_id, pad_id));
        return std::nullopt;
    case FootprintPadShape::Oval:
        return Aperture{ApertureKind::Oval, width, height};
    }
    throw std::logic_error{"Unhandled footprint pad shape"};
}

void add_flash(FabAccumulator &fab, FabLayer layer, Aperture aperture, BoardPoint point) {
    fab.gerbers[layer].commands.push_back(
        GerberCommand{GerberCommandKind::Flash, aperture, std::vector{point}});
}

void add_polyline(FabAccumulator &fab, FabLayer layer, Aperture aperture,
                  std::vector<BoardPoint> points) {
    if (points.size() < 2U) {
        return;
    }
    fab.gerbers[layer].commands.push_back(
        GerberCommand{GerberCommandKind::Polyline, aperture, std::move(points)});
}

void add_region(FabAccumulator &fab, FabLayer layer, std::vector<BoardPoint> points) {
    if (points.size() < 3U) {
        return;
    }
    if (points.back() != points.front()) {
        points.push_back(points.front());
    }
    fab.gerbers[layer].commands.push_back(
        GerberCommand{GerberCommandKind::Region, std::nullopt, std::move(points)});
}

void add_pad_to_layers(FabAccumulator &fab, const FootprintPad &pad,
                       const ComponentPlacement &placement, ComponentPlacementId placement_id,
                       FootprintPadId pad_id, BoardPoint position) {
    const auto aperture = pad_aperture(pad, placement, placement_id, pad_id, fab);
    if (!aperture.has_value()) {
        return;
    }
    const auto entities = pad_entities(placement_id, pad_id);
    if (pad.layers().contains(FootprintLayer::FrontCopper)) {
        if (ensure_output_layer(fab, FabLayer::FrontCopper, "board.layer.front_copper",
                                "Footprint pad references front copper but no enabled front copper "
                                "board layer is available",
                                entities)) {
            add_flash(fab, FabLayer::FrontCopper, aperture.value(), position);
        }
    }
    if (pad.layers().contains(FootprintLayer::BackCopper)) {
        if (ensure_output_layer(fab, FabLayer::BackCopper, "board.layer.back_copper",
                                "Footprint pad references back copper but no enabled back copper "
                                "board layer is available",
                                entities)) {
            add_flash(fab, FabLayer::BackCopper, aperture.value(), position);
        }
    }
    if (pad.layers().contains(FootprintLayer::FrontSolderMask)) {
        if (ensure_output_layer(fab, FabLayer::FrontMask, "board.layer.front_mask",
                                "Footprint pad references front solder mask but no enabled front "
                                "mask board layer is available",
                                entities)) {
            add_flash(fab, FabLayer::FrontMask, aperture.value(), position);
        }
    }
    if (pad.layers().contains(FootprintLayer::BackSolderMask)) {
        if (ensure_output_layer(fab, FabLayer::BackMask, "board.layer.back_mask",
                                "Footprint pad references back solder mask but no enabled back "
                                "mask board layer is available",
                                entities)) {
            add_flash(fab, FabLayer::BackMask, aperture.value(), position);
        }
    }
    if (pad.layers().contains(FootprintLayer::FrontPaste)) {
        if (ensure_output_layer(fab, FabLayer::FrontPaste, "board.layer.front_paste",
                                "Footprint pad references front paste but no enabled front paste "
                                "board layer is available",
                                entities)) {
            add_flash(fab, FabLayer::FrontPaste, aperture.value(), position);
        }
    }
    if (pad.layers().contains(FootprintLayer::BackPaste)) {
        if (ensure_output_layer(fab, FabLayer::BackPaste, "board.layer.back_paste",
                                "Footprint pad references back paste but no enabled back paste "
                                "board layer is available",
                                entities)) {
            add_flash(fab, FabLayer::BackPaste, aperture.value(), position);
        }
    }
}

[[nodiscard]] const FootprintDefinition *
definition_for_placement(const Board &board, const ComponentPlacement &placement,
                         const FootprintLibrary &footprints) {
    const auto &part = board.circuit().selected_physical_part(placement.component());
    if (!part.has_value()) {
        return nullptr;
    }
    const auto cached = board.footprint_definition_id(part->footprint());
    if (cached.has_value()) {
        return &board.footprint_definition(cached.value());
    }
    return footprints.find(part->footprint());
}

void append_body_diagnostic(FabAccumulator &fab, const FootprintDefinition &definition,
                            ComponentPlacementId placement_id) {
    if (!definition.body().has_value()) {
        return;
    }
    add_unsupported_geometry(fab, "footprint.body",
                             "Native fabrication writer v1 does not treat ambiguous footprint "
                             "body polygons as silkscreen artwork",
                             std::vector{EntityRef::component_placement(placement_id)});
}

void append_placements(FabAccumulator &fab, const Board &board,
                       const FootprintLibrary &footprints) {
    for (std::size_t placement_index = 0; placement_index < board.placement_count();
         ++placement_index) {
        const auto placement_id = ComponentPlacementId{placement_index};
        const auto &placement = board.placement(placement_id);
        if (placement.side() != BoardSide::Top) {
            add_unsupported_geometry(
                fab, "component_placement.side",
                "Native fabrication writer v1 exports top-side component placements",
                std::vector{EntityRef::component_placement(placement_id)});
            continue;
        }

        const auto *definition = definition_for_placement(board, placement, footprints);
        if (definition == nullptr) {
            add_missing_geometry(fab, "footprint",
                                 "Component placement has no resolved footprint definition for "
                                 "native fabrication export",
                                 std::vector{EntityRef::component_placement(placement_id)});
            continue;
        }

        for (std::size_t pad_index = 0; pad_index < definition->pad_count(); ++pad_index) {
            const auto pad_id = FootprintPadId{pad_index};
            const auto &pad = definition->pad(pad_id);
            const auto position = transform_point(placement, pad.position());
            add_pad_to_layers(fab, pad, placement, placement_id, pad_id, position);
            if (pad.drill().has_value()) {
                auto &drills = pad.drill()->plating() == FootprintPadPlating::Plated
                                   ? fab.plated_drills
                                   : fab.nonplated_drills;
                drills.push_back(DrillHit{position, pad.drill()->diameter_mm()});
            }
        }
        append_body_diagnostic(fab, *definition, placement_id);
    }
}

void append_tracks(FabAccumulator &fab, const Board &board) {
    for (std::size_t index = 0; index < board.track_count(); ++index) {
        const auto track_id = BoardTrackId{index};
        const auto &track = board.track(track_id);
        const auto layer = owned_fab_layer(fab, board, track.layer());
        if (!layer.has_value()) {
            add_unsupported_layer(
                fab, "board.track.layer",
                "Native fabrication writer v1 exports tracks only on declared top or bottom copper",
                std::vector{EntityRef::board_track(track_id),
                            EntityRef::board_layer(track.layer())});
            continue;
        }
        add_polyline(fab, layer.value(),
                     Aperture{ApertureKind::Circle, track.width_mm(), track.width_mm()},
                     track.points());
    }
}

void append_vias(FabAccumulator &fab, const Board &board) {
    for (std::size_t index = 0; index < board.via_count(); ++index) {
        const auto via_id = BoardViaId{index};
        const auto &via = board.via(via_id);
        const auto start_layer = owned_fab_layer(fab, board, via.start_layer());
        const auto end_layer = owned_fab_layer(fab, board, via.end_layer());
        if (!start_layer.has_value() || !end_layer.has_value()) {
            add_unsupported_layer(fab, "board.via.layer_span",
                                  "Native fabrication writer v1 exports vias only between declared "
                                  "top and bottom copper",
                                  std::vector{EntityRef::board_via(via_id),
                                              EntityRef::board_layer(via.start_layer()),
                                              EntityRef::board_layer(via.end_layer())});
            continue;
        }
        const auto aperture =
            Aperture{ApertureKind::Circle, via.annular_diameter_mm(), via.annular_diameter_mm()};
        add_flash(fab, start_layer.value(), aperture, via.position());
        add_flash(fab, end_layer.value(), aperture, via.position());
        fab.plated_drills.push_back(DrillHit{via.position(), via.drill_diameter_mm()});
    }
}

void append_zones(FabAccumulator &fab, const Board &board) {
    for (std::size_t index = 0; index < board.zone_count(); ++index) {
        const auto zone_id = BoardZoneId{index};
        const auto &zone = board.zone(zone_id);
        for (const auto layer_id : zone.layers()) {
            const auto layer = owned_fab_layer(fab, board, layer_id);
            if (!layer.has_value()) {
                add_unsupported_layer(
                    fab, "board.zone.layer",
                    "Native fabrication writer v1 exports zones only on declared top or bottom "
                    "copper",
                    std::vector{EntityRef::board_zone(zone_id), EntityRef::board_layer(layer_id)});
                continue;
            }
            add_region(fab, layer.value(), zone.outline());
        }
    }
}

void append_features(FabAccumulator &fab, const Board &board) {
    for (std::size_t index = 0; index < board.feature_count(); ++index) {
        const auto feature_id = BoardFeatureId{index};
        const auto &feature = board.feature(feature_id);
        switch (feature.kind()) {
        case BoardFeatureKind::Hole: {
            const auto &hole = feature.hole();
            if (hole.finished_diameter_mm().has_value() &&
                scaled_key(hole.finished_diameter_mm().value()) !=
                    scaled_key(hole.drill_diameter_mm())) {
                add_lossy_geometry(
                    fab, "board.feature.hole.finished_diameter",
                    "Native Excellon drill output emits drill diameter and cannot encode a "
                    "distinct finished board-hole diameter",
                    std::vector{EntityRef::board_feature(feature_id)});
            }
            auto &drills = hole.plated() ? fab.plated_drills : fab.nonplated_drills;
            drills.push_back(DrillHit{hole.center(), hole.drill_diameter_mm()});
            break;
        }
        case BoardFeatureKind::Slot:
            add_unsupported_geometry(fab, "board.feature.slot",
                                     "Native fabrication writer v1 does not export slotted holes",
                                     std::vector{EntityRef::board_feature(feature_id)});
            break;
        case BoardFeatureKind::Cutout:
            add_unsupported_geometry(fab, "board.feature.cutout",
                                     "Native fabrication writer v1 does not export cutout features",
                                     std::vector{EntityRef::board_feature(feature_id)});
            break;
        case BoardFeatureKind::Circle:
            add_unsupported_geometry(fab, "board.feature.circle",
                                     "Native fabrication writer v1 does not export surface circles",
                                     std::vector{EntityRef::board_feature(feature_id)});
            break;
        }
    }
}

void append_text_diagnostics(FabAccumulator &fab, const Board &board) {
    for (std::size_t index = 0; index < board.text_count(); ++index) {
        const auto text_id = BoardTextId{index};
        const auto &text = board.text(text_id);
        add_unsupported_geometry(
            fab, "board.text", "Native fabrication writer v1 does not render arbitrary board text",
            std::vector{EntityRef::board_text(text_id), EntityRef::board_layer(text.layer())});
    }
}

void append_outline(FabAccumulator &fab, const Board &board) {
    if (!board.outline().has_value()) {
        add_missing_geometry(fab, "board.outline",
                             "Native fabrication export requires a board outline/profile");
        return;
    }
    if (!ensure_output_layer(
            fab, FabLayer::EdgeCuts, "board.layer.edge_cuts",
            "Board outline exists but no enabled edge-cuts board layer is available",
            std::vector{EntityRef::board()})) {
        return;
    }
    auto points = board.outline()->vertices();
    points.push_back(points.front());
    add_polyline(fab, FabLayer::EdgeCuts, Aperture{ApertureKind::Circle, 0.1, 0.1},
                 std::move(points));
}

[[nodiscard]] std::string gerber_filename(FabLayer layer) {
    switch (layer) {
    case FabLayer::FrontCopper:
        return "F_Cu.gbr";
    case FabLayer::BackCopper:
        return "B_Cu.gbr";
    case FabLayer::FrontMask:
        return "F_Mask.gbr";
    case FabLayer::BackMask:
        return "B_Mask.gbr";
    case FabLayer::FrontPaste:
        return "F_Paste.gbr";
    case FabLayer::BackPaste:
        return "B_Paste.gbr";
    case FabLayer::FrontSilk:
        return "F_SilkS.gbr";
    case FabLayer::BackSilk:
        return "B_SilkS.gbr";
    case FabLayer::EdgeCuts:
        return "Edge_Cuts.gbr";
    }
    throw std::logic_error{"Unhandled fabrication layer"};
}

[[nodiscard]] std::string file_function(FabLayer layer) {
    switch (layer) {
    case FabLayer::FrontCopper:
        return "Copper,L1,Top";
    case FabLayer::BackCopper:
        return "Copper,L2,Bot";
    case FabLayer::FrontMask:
        return "Soldermask,Top";
    case FabLayer::BackMask:
        return "Soldermask,Bot";
    case FabLayer::FrontPaste:
        return "Paste,Top";
    case FabLayer::BackPaste:
        return "Paste,Bot";
    case FabLayer::FrontSilk:
        return "Legend,Top";
    case FabLayer::BackSilk:
        return "Legend,Bot";
    case FabLayer::EdgeCuts:
        return "Profile,NP";
    }
    throw std::logic_error{"Unhandled fabrication layer"};
}

[[nodiscard]] std::string aperture_definition(const Aperture &aperture, int code) {
    auto out = std::ostringstream{};
    out << "%ADD" << code;
    switch (aperture.kind) {
    case ApertureKind::Circle:
        out << 'C' << ',' << decimal(aperture.width_mm);
        break;
    case ApertureKind::Rectangle:
        out << 'R' << ',' << decimal(aperture.width_mm) << 'X' << decimal(aperture.height_mm);
        break;
    case ApertureKind::Oval:
        out << 'O' << ',' << decimal(aperture.width_mm) << 'X' << decimal(aperture.height_mm);
        break;
    }
    out << "*%\n";
    return out.str();
}

[[nodiscard]] std::vector<Aperture> ordered_apertures(const GerberDocument &document) {
    auto apertures = std::vector<Aperture>{};
    auto seen = std::map<Aperture, bool>{};
    for (const auto &command : document.commands) {
        if (!command.aperture.has_value() || seen[command.aperture.value()]) {
            continue;
        }
        seen[command.aperture.value()] = true;
        apertures.push_back(command.aperture.value());
    }
    return apertures;
}

[[nodiscard]] std::map<Aperture, int> aperture_codes(const std::vector<Aperture> &apertures) {
    auto codes = std::map<Aperture, int>{};
    for (std::size_t index = 0; index < apertures.size(); ++index) {
        codes.emplace(apertures[index], static_cast<int>(10U + index));
    }
    return codes;
}

void select_aperture(std::ostream &out, int code, std::optional<int> &selected_code) {
    if (selected_code.has_value() && selected_code.value() == code) {
        return;
    }
    selected_code = code;
    out << 'D' << code << "*\n";
}

void write_flash(std::ostream &out, const GerberCommand &command,
                 const std::map<Aperture, int> &codes, std::optional<int> &selected_code) {
    select_aperture(out, codes.at(command.aperture.value()), selected_code);
    write_coordinate(out, command.points.front());
    out << "D03*\n";
}

void write_polyline(std::ostream &out, const GerberCommand &command,
                    const std::map<Aperture, int> &codes, std::optional<int> &selected_code) {
    select_aperture(out, codes.at(command.aperture.value()), selected_code);
    write_coordinate(out, command.points.front());
    out << "D02*\n";
    for (std::size_t index = 1; index < command.points.size(); ++index) {
        write_coordinate(out, command.points[index]);
        out << "D01*\n";
    }
}

void write_region(std::ostream &out, const GerberCommand &command) {
    out << "G36*\n";
    write_coordinate(out, command.points.front());
    out << "D02*\n";
    for (std::size_t index = 1; index < command.points.size(); ++index) {
        write_coordinate(out, command.points[index]);
        out << "D01*\n";
    }
    out << "G37*\n";
}

[[nodiscard]] std::string write_gerber(FabLayer layer, const GerberDocument &document) {
    const auto apertures = ordered_apertures(document);
    const auto codes = aperture_codes(apertures);

    auto out = std::ostringstream{};
    const auto filename = gerber_filename(layer);
    out << "G04 Volt Gerber RS-274X " << filename.substr(0, filename.size() - 4U) << "*\n";
    out << "%FSLAX46Y46*%\n";
    out << "%MOMM*%\n";
    out << "%LPD*%\n";
    out << "%TF.FileFunction," << file_function(layer) << "*%\n";
    out << "%TF.Part,Single*%\n";
    for (std::size_t index = 0; index < apertures.size(); ++index) {
        out << aperture_definition(apertures[index], static_cast<int>(10U + index));
    }
    auto selected_code = std::optional<int>{};
    for (const auto &command : document.commands) {
        switch (command.kind) {
        case GerberCommandKind::Flash:
            write_flash(out, command, codes, selected_code);
            break;
        case GerberCommandKind::Polyline:
            write_polyline(out, command, codes, selected_code);
            break;
        case GerberCommandKind::Region:
            write_region(out, command);
            break;
        }
    }
    out << "M02*\n";
    return out.str();
}

[[nodiscard]] long long drill_tool_key(const DrillHit &hit) { return scaled_key(hit.diameter_mm); }

[[nodiscard]] long long point_x_key(const DrillHit &hit) { return scaled_key(hit.position.x_mm()); }

[[nodiscard]] long long point_y_key(const DrillHit &hit) { return scaled_key(hit.position.y_mm()); }

void sort_drills(std::vector<DrillHit> &drills) {
    std::sort(drills.begin(), drills.end(), [](const DrillHit &lhs, const DrillHit &rhs) {
        if (drill_tool_key(lhs) != drill_tool_key(rhs)) {
            return drill_tool_key(lhs) < drill_tool_key(rhs);
        }
        if (point_x_key(lhs) != point_x_key(rhs)) {
            return point_x_key(lhs) < point_x_key(rhs);
        }
        return point_y_key(lhs) < point_y_key(rhs);
    });
}

[[nodiscard]] std::vector<long long> drill_tools(std::vector<DrillHit> drills) {
    sort_drills(drills);
    auto tools = std::vector<long long>{};
    for (const auto &hit : drills) {
        const auto tool = drill_tool_key(hit);
        if (tools.empty() || tools.back() != tool) {
            tools.push_back(tool);
        }
    }
    return tools;
}

[[nodiscard]] std::string write_excellon(std::vector<DrillHit> drills) {
    sort_drills(drills);
    const auto tools = drill_tools(drills);

    auto out = std::ostringstream{};
    out << "M48\n";
    out << ";DRILL file generated by Volt\n";
    out << "METRIC,TZ\n";
    for (std::size_t index = 0; index < tools.size(); ++index) {
        out << 'T' << std::setw(2) << std::setfill('0') << (index + 1U) << std::setfill(' ') << 'C'
            << decimal_from_scaled_key(tools[index]) << "\n";
    }
    out << "%\n";
    out << "G05\n";
    for (std::size_t tool_index = 0; tool_index < tools.size(); ++tool_index) {
        out << 'T' << std::setw(2) << std::setfill('0') << (tool_index + 1U) << std::setfill(' ')
            << "\n";
        for (const auto &hit : drills) {
            if (drill_tool_key(hit) != tools[tool_index]) {
                continue;
            }
            write_coordinate(out, hit.position);
            out << "\n";
        }
    }
    out << "M30\n";
    return out.str();
}

void append_file_if_present(std::vector<PcbFabricationFile> &files, const FabAccumulator &fab,
                            FabLayer layer) {
    const auto match = fab.gerbers.find(layer);
    if (match == fab.gerbers.end() || match->second.commands.empty()) {
        return;
    }
    files.push_back(PcbFabricationFile{gerber_filename(layer), write_gerber(layer, match->second)});
}

[[nodiscard]] FabAccumulator collect_fabrication_data(const Board &board,
                                                      const FootprintLibrary &footprints) {
    auto fab = FabAccumulator{};
    build_layer_plan(fab, board);
    append_placements(fab, board, footprints);
    append_tracks(fab, board);
    append_vias(fab, board);
    append_zones(fab, board);
    append_features(fab, board);
    append_text_diagnostics(fab, board);
    append_outline(fab, board);
    return fab;
}

} // namespace

[[nodiscard]] PcbFabricationPackage
write_pcb_fabrication_package(const Board &board, const FootprintLibrary &footprints) {
    auto fab = collect_fabrication_data(board, footprints);
    auto files = std::vector<PcbFabricationFile>{};
    append_file_if_present(files, fab, FabLayer::FrontCopper);
    append_file_if_present(files, fab, FabLayer::BackCopper);
    append_file_if_present(files, fab, FabLayer::FrontMask);
    append_file_if_present(files, fab, FabLayer::BackMask);
    append_file_if_present(files, fab, FabLayer::FrontPaste);
    append_file_if_present(files, fab, FabLayer::BackPaste);
    append_file_if_present(files, fab, FabLayer::FrontSilk);
    append_file_if_present(files, fab, FabLayer::BackSilk);
    append_file_if_present(files, fab, FabLayer::EdgeCuts);
    if (!fab.plated_drills.empty()) {
        files.push_back(PcbFabricationFile{"PTH.drl", write_excellon(fab.plated_drills)});
    }
    if (!fab.nonplated_drills.empty()) {
        files.push_back(PcbFabricationFile{"NPTH.drl", write_excellon(fab.nonplated_drills)});
    }
    return PcbFabricationPackage{std::move(files), std::move(fab.diagnostics)};
}

} // namespace volt::io
