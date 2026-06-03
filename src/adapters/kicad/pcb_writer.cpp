#include <volt/adapters/kicad/pcb_writer.hpp>

#include "format.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/core/properties.hpp>

namespace volt::adapters::kicad::detail {

struct PcbLayer {
    int index;
    std::string name;
    std::string kind;
};

struct LayerMap {
    std::vector<std::optional<PcbLayer>> board_layers;
    std::vector<PcbLayer> output_layers;

    [[nodiscard]] const std::optional<PcbLayer> &find(BoardLayerId id) const {
        return board_layers.at(id.index());
    }
};

struct PlacementExport {
    ComponentPlacementId id;
    const ComponentPlacement *placement;
    const FootprintDefinition *definition;
    std::vector<PadResolution> pad_resolutions;
};

void write_at(std::ostream &out, BoardPoint point, double rotation_degrees = 0.0) {
    out << "(at ";
    write_number(out, point.x_mm());
    out << ' ';
    write_number(out, point.y_mm());
    if (std::abs(rotation_degrees) >= 1.0e-12) {
        out << ' ';
        write_number(out, rotation_degrees);
    }
    out << ')';
}

[[nodiscard]] std::string pcb_uuid(std::string_view entity, std::size_t id) {
    auto out = std::ostringstream{};
    out << "pcb/" << entity << '/' << id;
    return uuid_from_path(out.str());
}

[[nodiscard]] std::string pcb_uuid(std::string_view entity, std::size_t id,
                                   std::string_view child) {
    auto out = std::ostringstream{};
    out << "pcb/" << entity << '/' << id << '/' << child;
    return uuid_from_path(out.str());
}

[[nodiscard]] std::string pcb_uuid(std::string_view entity, std::size_t id, std::string_view child,
                                   std::size_t child_id) {
    auto out = std::ostringstream{};
    out << "pcb/" << entity << '/' << id << '/' << child << '/' << child_id;
    return uuid_from_path(out.str());
}

[[nodiscard]] int kicad_net(NetId net) { return static_cast<int>(net.index() + 1U); }

[[nodiscard]] std::optional<PcbLayer> candidate_layer_for(const BoardLayer &layer) {
    if (!layer.enabled()) {
        return std::nullopt;
    }

    switch (layer.role()) {
    case BoardLayerRole::Copper:
        switch (layer.side()) {
        case BoardLayerSide::Top:
            return PcbLayer{0, "F.Cu", "signal"};
        case BoardLayerSide::Bottom:
            return PcbLayer{31, "B.Cu", "signal"};
        case BoardLayerSide::Inner:
        case BoardLayerSide::Both:
        case BoardLayerSide::None:
            return std::nullopt;
        }
        break;
    case BoardLayerRole::Silkscreen:
        switch (layer.side()) {
        case BoardLayerSide::Top:
            return PcbLayer{37, "F.SilkS", "user"};
        case BoardLayerSide::Bottom:
            return PcbLayer{36, "B.SilkS", "user"};
        case BoardLayerSide::Inner:
        case BoardLayerSide::Both:
        case BoardLayerSide::None:
            return std::nullopt;
        }
        break;
    case BoardLayerRole::EdgeCuts:
        return PcbLayer{44, "Edge.Cuts", "user"};
    case BoardLayerRole::SolderMask:
    case BoardLayerRole::Paste:
    case BoardLayerRole::Fabrication:
    case BoardLayerRole::Drill:
    case BoardLayerRole::Mechanical:
    case BoardLayerRole::Courtyard:
    case BoardLayerRole::Keepout:
        return std::nullopt;
    }
    throw std::logic_error{"Unhandled board layer role"};
}

[[nodiscard]] bool contains_layer(const std::vector<PcbLayer> &layers, int index) {
    return std::any_of(layers.begin(), layers.end(),
                       [index](const PcbLayer &layer) { return layer.index == index; });
}

void add_layer(std::vector<PcbLayer> &layers, PcbLayer layer) {
    if (!contains_layer(layers, layer.index)) {
        layers.push_back(std::move(layer));
    }
}

void report_layer_mapping_collision(const Board &board, BoardLayerId current, BoardLayerId existing,
                                    const PcbLayer &candidate, LossReport &loss_report) {
    auto message = std::ostringstream{};
    message << "Board layer '" << board.layer(current).name() << "' also maps to KiCad layer '"
            << candidate.name << "', already claimed by board layer '"
            << board.layer(existing).name() << "'; constructs on the duplicate layer are omitted";
    loss_report.add_warning(LossKind::UnsupportedConstruct, "board.layer.mapping", message.str());
}

[[nodiscard]] LayerMap build_layer_map(const Board &board, LossReport &loss_report) {
    auto layer_map = LayerMap{};
    layer_map.board_layers.resize(board.layer_count());
    auto kicad_layer_owners = std::map<int, BoardLayerId>{};

    for (std::size_t index = 0; index < board.layer_count(); ++index) {
        const auto id = BoardLayerId{index};
        const auto layer = candidate_layer_for(board.layer(id));
        if (!layer.has_value()) {
            continue;
        }

        const auto existing = kicad_layer_owners.find(layer->index);
        if (existing != kicad_layer_owners.end()) {
            report_layer_mapping_collision(board, id, existing->second, layer.value(), loss_report);
            continue;
        }

        kicad_layer_owners.emplace(layer->index, id);
        layer_map.board_layers[index] = layer.value();
        add_layer(layer_map.output_layers, layer.value());
    }
    add_layer(layer_map.output_layers, PcbLayer{34, "B.Paste", "user"});
    add_layer(layer_map.output_layers, PcbLayer{35, "F.Paste", "user"});
    add_layer(layer_map.output_layers, PcbLayer{36, "B.SilkS", "user"});
    add_layer(layer_map.output_layers, PcbLayer{37, "F.SilkS", "user"});
    add_layer(layer_map.output_layers, PcbLayer{38, "B.Mask", "user"});
    add_layer(layer_map.output_layers, PcbLayer{39, "F.Mask", "user"});
    add_layer(layer_map.output_layers, PcbLayer{49, "F.Fab", "user"});
    if (board.outline().has_value()) {
        add_layer(layer_map.output_layers, PcbLayer{44, "Edge.Cuts", "user"});
    }

    std::sort(layer_map.output_layers.begin(), layer_map.output_layers.end(),
              [](const PcbLayer &lhs, const PcbLayer &rhs) { return lhs.index < rhs.index; });
    return layer_map;
}

[[nodiscard]] std::string pcb_component_value(const ComponentInstance &component,
                                              const ComponentDefinition &definition) {
    const auto value_key = PropertyKey{"Value"};
    if (component.properties().contains(value_key)) {
        return property_value_to_string(component.properties().get(value_key));
    }
    const auto lowercase_value_key = PropertyKey{"value"};
    if (component.properties().contains(lowercase_value_key)) {
        return property_value_to_string(component.properties().get(lowercase_value_key));
    }
    return definition.name();
}

[[nodiscard]] const FootprintDefinition *
definition_for_placement(const Board &board, const ComponentPlacement &placement,
                         const FootprintLibrary &footprints) {
    const auto &selected_part = board.circuit().selected_physical_part(placement.component());
    if (!selected_part.has_value()) {
        return nullptr;
    }

    const auto cached = board.footprint_definition_id(selected_part->footprint());
    if (cached.has_value()) {
        return &board.footprint_definition(cached.value());
    }
    return footprints.find(selected_part->footprint());
}

[[nodiscard]] const PadResolution *pad_resolution_for(const std::vector<PadResolution> &resolutions,
                                                      ComponentPlacementId placement,
                                                      FootprintPadId pad) {
    const auto match = std::find_if(
        resolutions.begin(), resolutions.end(), [placement, pad](const PadResolution &candidate) {
            return candidate.placement() == placement && candidate.pad() == pad;
        });
    if (match == resolutions.end()) {
        return nullptr;
    }
    return &*match;
}

[[nodiscard]] std::string pad_shape_name(FootprintPadShape shape) {
    switch (shape) {
    case FootprintPadShape::Rectangle:
        return "rect";
    case FootprintPadShape::RoundedRectangle:
        return "roundrect";
    case FootprintPadShape::Circle:
        return "circle";
    case FootprintPadShape::Oval:
        return "oval";
    }
    throw std::logic_error{"Unhandled footprint pad shape"};
}

[[nodiscard]] bool all_surface_mount(const FootprintDefinition &definition) {
    for (std::size_t index = 0; index < definition.pad_count(); ++index) {
        if (definition.pad(FootprintPadId{index}).kind() != FootprintPadKind::SurfaceMount) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string pad_kind_name(const FootprintPad &pad) {
    if (pad.kind() == FootprintPadKind::SurfaceMount) {
        return "smd";
    }
    if (pad.drill().has_value() && pad.drill()->plating() == FootprintPadPlating::NonPlated &&
        !pad.requires_pin_mapping()) {
        return "np_thru_hole";
    }
    return "thru_hole";
}

[[nodiscard]] std::vector<std::string> pad_layers(const FootprintPad &pad) {
    if (pad.kind() == FootprintPadKind::ThroughHole) {
        return {"*.Cu", "*.Mask"};
    }

    auto layers = std::vector<std::string>{};
    const auto &footprint_layers = pad.layers();
    if (footprint_layers.contains(FootprintLayer::FrontCopper)) {
        layers.push_back("F.Cu");
    }
    if (footprint_layers.contains(FootprintLayer::FrontPaste)) {
        layers.push_back("F.Paste");
    }
    if (footprint_layers.contains(FootprintLayer::FrontSolderMask)) {
        layers.push_back("F.Mask");
    }
    if (footprint_layers.contains(FootprintLayer::BackCopper)) {
        layers.push_back("B.Cu");
    }
    if (footprint_layers.contains(FootprintLayer::BackPaste)) {
        layers.push_back("B.Paste");
    }
    if (footprint_layers.contains(FootprintLayer::BackSolderMask)) {
        layers.push_back("B.Mask");
    }
    return layers;
}

void report_invalid_pad_resolution(const PadResolution &resolution, const Circuit &circuit,
                                   LossReport &loss_report) {
    if (resolution.status() != PadResolutionStatus::Invalid) {
        return;
    }

    auto message = std::ostringstream{};
    message << "Pad '" << resolution.pad_label() << "' on "
            << circuit.component(resolution.component()).reference().value()
            << " has invalid selected-part pin-pad mapping; KiCad pad is emitted without a net";
    loss_report.add_warning(LossKind::IncompleteConstruct, "pad_resolution", message.str());
}

void write_effects(std::ostream &out, double size_mm) {
    out << "(effects (font (size ";
    write_number(out, size_mm);
    out << ' ';
    write_number(out, size_mm);
    out << ")) (justify left))";
}

void write_property(std::ostream &out, std::string_view name, std::string_view value, double x,
                    double y, std::string_view layer, std::string_view uuid) {
    out << "    (property " << sexpr_string(name) << ' ' << sexpr_string(value) << "\n";
    out << "      (at ";
    write_number(out, x);
    out << ' ';
    write_number(out, y);
    out << " 0)\n";
    out << "      (layer " << sexpr_string(layer) << ")\n";
    out << "      (uuid " << sexpr_string(uuid) << ")\n";
    out << "      ";
    write_effects(out, 1.0);
    out << "\n";
    out << "    )\n";
}

void write_layers(std::ostream &out, const LayerMap &layer_map) {
    out << "  (layers\n";
    for (const auto &layer : layer_map.output_layers) {
        out << "    (" << layer.index << ' ' << sexpr_string(layer.name) << ' ' << layer.kind
            << ")\n";
    }
    out << "  )\n";
}

void write_setup(std::ostream &out) {
    out << "  (setup\n";
    out << "    (pad_to_mask_clearance 0)\n";
    out << "    (allow_soldermask_bridges_in_footprints no)\n";
    out << "  )\n";
}

void write_nets(std::ostream &out, const Circuit &circuit) {
    out << "  (net 0 \"\")\n";
    for (std::size_t index = 0; index < circuit.net_count(); ++index) {
        const auto id = NetId{index};
        out << "  (net " << kicad_net(id) << ' ' << sexpr_string(circuit.net(id).name().value())
            << ")\n";
    }
}

void write_board_hole(std::ostream &out, const BoardFeature &feature, BoardFeatureId id) {
    const auto &hole = feature.hole();
    const auto footprint_name =
        feature.kind() == BoardFeatureKind::ToolingHole ? "ToolingHole_NPTH" : "BoardHole_NPTH";
    out << "  (footprint \"" << footprint_name << "\"\n";
    out << "    (layer \"F.Cu\")\n";
    out << "    (uuid " << sexpr_string(pcb_uuid("board-feature", id.index(), "footprint"))
        << ")\n";
    out << "    ";
    write_at(out, hole.center());
    out << "\n";
    out << "    (attr exclude_from_pos_files exclude_from_bom)\n";
    out << "    (pad \"\" np_thru_hole circle\n";
    out << "      (at 0 0)\n";
    out << "      (size ";
    write_number(out, hole.drill_diameter_mm());
    out << ' ';
    write_number(out, hole.drill_diameter_mm());
    out << ")\n";
    out << "      (drill ";
    write_number(out, hole.drill_diameter_mm());
    out << ")\n";
    out << "      (layers \"*.Cu\" \"*.Mask\")\n";
    out << "      (uuid " << sexpr_string(pcb_uuid("board-feature", id.index(), "pad", 0U))
        << ")\n";
    out << "    )\n";
    out << "  )\n";
}

void write_board_features(std::ostream &out, const Board &board, LossReport &loss_report) {
    for (std::size_t index = 0; index < board.feature_count(); ++index) {
        const auto id = BoardFeatureId{index};
        const auto &feature = board.feature(id);
        if (feature.kind() == BoardFeatureKind::Hole ||
            feature.kind() == BoardFeatureKind::ToolingHole) {
            if (feature.hole().plated()) {
                loss_report.add_warning(
                    LossKind::UnsupportedConstruct, "board.feature.hole.plated",
                    "The first KiCad PCB writer subset does not export plated board-feature holes");
                continue;
            }
            write_board_hole(out, feature, id);
            continue;
        }
        if (feature.kind() == BoardFeatureKind::Text ||
            feature.kind() == BoardFeatureKind::MechanicalKeepout) {
            continue;
        }
        if (feature.kind() == BoardFeatureKind::Slot) {
            loss_report.add_warning(
                LossKind::UnsupportedConstruct, "board.feature.slot",
                "The first KiCad PCB writer subset does not export board slots");
            continue;
        }
        if (feature.kind() == BoardFeatureKind::Cutout) {
            loss_report.add_warning(
                LossKind::UnsupportedConstruct, "board.feature.cutout",
                "The first KiCad PCB writer subset does not export board cutouts");
            continue;
        }
        if (feature.kind() == BoardFeatureKind::Fiducial) {
            loss_report.add_warning(
                LossKind::UnsupportedConstruct, "board.feature.fiducial",
                "The first KiCad PCB writer subset does not export board fiducials");
            continue;
        }
    }
}

[[nodiscard]] std::vector<PlacementExport>
build_placement_exports(const Board &board, const FootprintLibrary &footprints,
                        LossReport &loss_report) {
    const auto resolutions = board.resolve_pads(footprints);
    auto exports = std::vector<PlacementExport>{};

    for (std::size_t index = 0; index < board.placement_count(); ++index) {
        const auto id = ComponentPlacementId{index};
        const auto &placement = board.placement(id);
        if (placement.side() != BoardSide::Top) {
            loss_report.add_warning(
                LossKind::UnsupportedConstruct, "component_placement.side",
                "The first KiCad PCB writer subset exports top-side component placements");
            continue;
        }

        const auto *definition = definition_for_placement(board, placement, footprints);
        if (definition == nullptr) {
            loss_report.add_warning(
                LossKind::IncompleteConstruct, "footprint",
                "Component placement has no resolved footprint definition for KiCad export");
            continue;
        }

        auto placement_export =
            PlacementExport{id, &placement, definition, std::vector<PadResolution>{}};
        placement_export.pad_resolutions.reserve(definition->pad_count());
        for (std::size_t pad_index = 0; pad_index < definition->pad_count(); ++pad_index) {
            const auto pad_id = FootprintPadId{pad_index};
            const auto *resolution = pad_resolution_for(resolutions, id, pad_id);
            if (resolution == nullptr) {
                throw std::logic_error{
                    "KiCad placement export requires an explicit pad resolution for every pad"};
            }
            placement_export.pad_resolutions.push_back(*resolution);
        }
        exports.push_back(std::move(placement_export));
    }

    return exports;
}

void write_pad(std::ostream &out, const FootprintPad &pad, const PadResolution &resolution,
               const Circuit &circuit, std::string_view uuid, LossReport &loss_report) {
    report_invalid_pad_resolution(resolution, circuit, loss_report);

    out << "    (pad " << sexpr_string(pad.label()) << ' ' << pad_kind_name(pad) << ' '
        << pad_shape_name(pad.shape()) << "\n";
    out << "      (at ";
    write_number(out, pad.position().x_mm());
    out << ' ';
    write_number(out, pad.position().y_mm());
    out << ")\n";
    out << "      (size ";
    write_number(out, pad.size().width_mm());
    out << ' ';
    write_number(out, pad.size().height_mm());
    out << ")\n";
    if (pad.drill().has_value()) {
        out << "      (drill ";
        write_number(out, pad.drill()->diameter_mm());
        out << ")\n";
    }
    out << "      (layers";
    for (const auto &layer : pad_layers(pad)) {
        out << ' ' << sexpr_string(layer);
    }
    out << ")\n";
    if (pad.shape() == FootprintPadShape::RoundedRectangle) {
        out << "      (roundrect_rratio 0.125)\n";
    }
    if (resolution.status() == PadResolutionStatus::Connected && resolution.net().has_value()) {
        const auto net = resolution.net().value();
        out << "      (net " << kicad_net(net) << ' '
            << sexpr_string(circuit.net(net).name().value()) << ")\n";
    }
    out << "      (uuid " << sexpr_string(uuid) << ")\n";
    out << "    )\n";
}

void write_component_footprints(std::ostream &out, const Board &board,
                                const FootprintLibrary &footprints, LossReport &loss_report) {
    for (const auto &placement_export : build_placement_exports(board, footprints, loss_report)) {
        const auto &placement = *placement_export.placement;
        const auto &definition = *placement_export.definition;
        const auto &component = board.circuit().component(placement.component());
        const auto &component_definition =
            board.circuit().component_definition(component.definition());

        out << "  (footprint " << sexpr_string(definition.ref().name()) << "\n";
        out << "    (layer \"F.Cu\")\n";
        out << "    (uuid "
            << sexpr_string(
                   pcb_uuid("component-placement", placement_export.id.index(), "footprint"))
            << ")\n";
        out << "    ";
        write_at(out, placement.position(), placement.rotation().degrees());
        out << "\n";
        write_property(
            out, "Reference", component.reference().value(), 0.0, -1.5, "F.Fab",
            pcb_uuid("component-placement", placement_export.id.index(), "property/Reference"));
        write_property(
            out, "Value", pcb_component_value(component, component_definition), 0.0, 1.5, "F.Fab",
            pcb_uuid("component-placement", placement_export.id.index(), "property/Value"));
        out << "    (attr " << (all_surface_mount(definition) ? "smd" : "through_hole") << ")\n";
        for (std::size_t pad_index = 0; pad_index < definition.pad_count(); ++pad_index) {
            const auto pad_id = FootprintPadId{pad_index};
            write_pad(
                out, definition.pad(pad_id), placement_export.pad_resolutions.at(pad_index),
                board.circuit(),
                pcb_uuid("component-placement", placement_export.id.index(), "pad", pad_id.index()),
                loss_report);
        }
        out << "  )\n";
    }
}

void write_tracks(std::ostream &out, const Board &board, const LayerMap &layer_map,
                  LossReport &loss_report) {
    for (std::size_t track_index = 0; track_index < board.track_count(); ++track_index) {
        const auto &track = board.track(BoardTrackId{track_index});
        const auto layer = layer_map.find(track.layer());
        if (!layer.has_value() || layer->kind != "signal") {
            loss_report.add_warning(
                LossKind::UnsupportedConstruct, "board.track.layer",
                "The first KiCad PCB writer subset exports tracks only on top or bottom copper");
            continue;
        }

        for (std::size_t point_index = 1; point_index < track.points().size(); ++point_index) {
            out << "  (segment\n";
            out << "    (start ";
            write_number(out, track.points()[point_index - 1U].x_mm());
            out << ' ';
            write_number(out, track.points()[point_index - 1U].y_mm());
            out << ")\n";
            out << "    (end ";
            write_number(out, track.points()[point_index].x_mm());
            out << ' ';
            write_number(out, track.points()[point_index].y_mm());
            out << ")\n";
            out << "    (width ";
            write_number(out, track.width_mm());
            out << ")\n";
            out << "    (layer " << sexpr_string(layer->name) << ")\n";
            out << "    (net " << kicad_net(track.net()) << ")\n";
            out << "    (uuid "
                << sexpr_string(pcb_uuid("board-track", track_index, "segment", point_index - 1U))
                << ")\n";
            out << "  )\n";
        }
    }
}

void write_vias(std::ostream &out, const Board &board, const LayerMap &layer_map,
                LossReport &loss_report) {
    for (std::size_t index = 0; index < board.via_count(); ++index) {
        const auto &via = board.via(BoardViaId{index});
        const auto start_layer = layer_map.find(via.start_layer());
        const auto end_layer = layer_map.find(via.end_layer());
        if (!start_layer.has_value() || !end_layer.has_value() || start_layer->kind != "signal" ||
            end_layer->kind != "signal") {
            loss_report.add_warning(
                LossKind::UnsupportedConstruct, "board.via.layer_span",
                "The first KiCad PCB writer subset exports vias only between copper layers");
            continue;
        }

        out << "  (via\n";
        out << "    ";
        write_at(out, via.position());
        out << "\n";
        out << "    (size ";
        write_number(out, via.annular_diameter_mm());
        out << ")\n";
        out << "    (drill ";
        write_number(out, via.drill_diameter_mm());
        out << ")\n";
        out << "    (layers " << sexpr_string(start_layer->name) << ' '
            << sexpr_string(end_layer->name) << ")\n";
        out << "    (net " << kicad_net(via.net()) << ")\n";
        out << "    (uuid " << sexpr_string(pcb_uuid("board-via", index)) << ")\n";
        out << "  )\n";
    }
}

void write_texts(std::ostream &out, const Board &board, const LayerMap &layer_map,
                 LossReport &loss_report) {
    for (std::size_t index = 0; index < board.text_count(); ++index) {
        const auto &text = board.text(BoardTextId{index});
        const auto layer = layer_map.find(text.layer());
        if (!layer.has_value()) {
            loss_report.add_warning(
                LossKind::UnsupportedConstruct, "board.text.layer",
                "The first KiCad PCB writer subset exports board text only on mapped KiCad layers");
            continue;
        }

        out << "  (gr_text " << sexpr_string(text.text()) << "\n";
        out << "    ";
        write_at(out, text.position(), text.rotation().degrees());
        out << "\n";
        out << "    (layer " << sexpr_string(layer->name) << ")\n";
        out << "    (uuid " << sexpr_string(pcb_uuid("board-text", index)) << ")\n";
        out << "    ";
        write_effects(out, text.size_mm());
        out << "\n";
        out << "  )\n";
    }
}

void write_outline(std::ostream &out, const Board &board) {
    if (!board.outline().has_value()) {
        return;
    }

    const auto &vertices = board.outline()->vertices();
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        const auto next = (index + 1U) % vertices.size();
        out << "  (gr_line\n";
        out << "    (start ";
        write_number(out, vertices[index].x_mm());
        out << ' ';
        write_number(out, vertices[index].y_mm());
        out << ")\n";
        out << "    (end ";
        write_number(out, vertices[next].x_mm());
        out << ' ';
        write_number(out, vertices[next].y_mm());
        out << ")\n";
        out << "    (stroke (width 0.1) (type default))\n";
        out << "    (layer \"Edge.Cuts\")\n";
        out << "    (uuid " << sexpr_string(pcb_uuid("board-outline", 0U, "edge", index)) << ")\n";
        out << "  )\n";
    }
}

void report_unsupported_board_constructs(const Board &board, LossReport &loss_report) {
    for (std::size_t index = 0; index < board.zone_count(); ++index) {
        loss_report.add_warning(LossKind::UnsupportedConstruct, "board.zone",
                                "The first KiCad PCB writer subset does not export copper zones");
    }
    for (std::size_t index = 0; index < board.keepout_count(); ++index) {
        loss_report.add_warning(LossKind::UnsupportedConstruct, "board.keepout",
                                "The first KiCad PCB writer subset does not export board keepouts");
    }
}

} // namespace volt::adapters::kicad::detail

namespace volt::adapters::kicad {

[[nodiscard]] BoardExportResult write_board(const Board &board,
                                            const FootprintLibrary &footprints) {
    auto result = BoardExportResult{};
    detail::report_unsupported_board_constructs(board, result.loss_report);
    const auto layer_map = detail::build_layer_map(board, result.loss_report);

    auto out = std::ostringstream{};
    out << "(kicad_pcb\n";
    out << "  (version 20240108)\n";
    out << "  (generator \"Volt\")\n";
    out << "  (general\n";
    out << "    (thickness ";
    detail::write_number(
        out, board.layer_stack().has_value() ? board.layer_stack()->board_thickness_mm() : 1.6);
    out << ")\n";
    out << "  )\n";
    out << "  (paper \"A4\")\n";
    detail::write_layers(out, layer_map);
    detail::write_setup(out);
    detail::write_nets(out, board.circuit());
    detail::write_board_features(out, board, result.loss_report);
    detail::write_component_footprints(out, board, footprints, result.loss_report);
    detail::write_tracks(out, board, layer_map, result.loss_report);
    detail::write_vias(out, board, layer_map, result.loss_report);
    detail::write_texts(out, board, layer_map, result.loss_report);
    detail::write_outline(out, board);
    out << ")\n";

    result.text = out.str();
    return result;
}

} // namespace volt::adapters::kicad
