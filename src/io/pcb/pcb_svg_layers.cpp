#include <volt/io/pcb/pcb_svg_writer.hpp>

#include <algorithm>
#include <ostream>

#include <volt/core/errors.hpp>
#include <volt/io/detail/typed_id.hpp>
#include <volt/io/pcb/pcb_schema.hpp>
#include <volt/pcb/board.hpp>

namespace volt::io::detail {
namespace {

[[nodiscard]] bool board_side_matches_layer(const Board &board, BoardSide side,
                                            BoardLayerId layer_id) {
    const auto &layer = board.layer(layer_id);
    switch (layer.side()) {
    case BoardLayerSide::Top:
        return side == BoardSide::Top;
    case BoardLayerSide::Bottom:
        return side == BoardSide::Bottom;
    case BoardLayerSide::Both:
        return true;
    case BoardLayerSide::Inner:
    case BoardLayerSide::None:
        return false;
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled PCB board layer side"};
}

[[nodiscard]] std::size_t layer_token_collision_index(const Board &board, BoardLayerId layer_id) {
    const auto token = pcb_svg_layer_filename_token(board.layer(layer_id).name());
    auto collision_index = std::size_t{1};
    for (std::size_t index = 0; index < layer_id.index(); ++index) {
        if (pcb_svg_layer_filename_token(board.layer(BoardLayerId{index}).name()) == token) {
            ++collision_index;
        }
    }
    return collision_index;
}

[[nodiscard]] bool board_layer_is_copper(const Board &board, BoardLayerId layer_id) {
    return board.layer(layer_id).role() == BoardLayerRole::Copper;
}

} // namespace

[[nodiscard]] bool pad_selected_for_layer(const Board &board, const FootprintPad &pad,
                                          BoardSide placement_side, BoardLayerId layer_id) {
    if (!board_layer_is_copper(board, layer_id)) {
        return false;
    }
    return layer_list_contains(volt::detail::pad_copper_layers(board, pad, placement_side),
                               layer_id);
}

[[nodiscard]] bool placement_pad_selected_for_layer(const Board &board,
                                                    const FootprintLibrary &footprints,
                                                    ComponentPlacementId placement_id,
                                                    FootprintPadId pad_id, BoardLayerId layer_id) {
    if (placement_id.index() >= board.placement_count()) {
        return false;
    }
    const auto &placement = board.placement(placement_id);
    const auto *definition = resolve_definition_for_placement(board, placement, footprints);
    if (definition == nullptr || pad_id.index() >= definition->pad_count()) {
        return false;
    }
    return pad_selected_for_layer(board, definition->pad(pad_id), placement.side(), layer_id);
}

[[nodiscard]] std::string pcb_svg_layer_filename_token(std::string_view layer_name) {
    auto result = std::string{};
    auto previous_was_separator = false;
    for (const auto character : layer_name) {
        const auto ascii_alnum = (character >= 'A' && character <= 'Z') ||
                                 (character >= 'a' && character <= 'z') ||
                                 (character >= '0' && character <= '9');
        if (ascii_alnum) {
            result += character;
            previous_was_separator = false;
            continue;
        }
        if (!result.empty() && !previous_was_separator) {
            result += '_';
            previous_was_separator = true;
        }
    }
    if (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    return result.empty() ? std::string{"layer"} : result;
}

[[nodiscard]] std::string pcb_svg_layer_token(const Board &board, BoardLayerId layer_id) {
    const auto token = pcb_svg_layer_filename_token(board.layer(layer_id).name());
    const auto collision_index = layer_token_collision_index(board, layer_id);
    if (collision_index == 1U) {
        return token;
    }
    return token + "_" + std::to_string(collision_index);
}

[[nodiscard]] bool layer_list_contains(const std::vector<BoardLayerId> &layers,
                                       BoardLayerId layer) {
    return std::find(layers.begin(), layers.end(), layer) != layers.end();
}

[[nodiscard]] bool layer_selected(PcbPlacementSvgOptions options, BoardLayerId layer) {
    return !options.layer_filter.has_value() || options.layer_filter.value() == layer;
}

[[nodiscard]] bool placement_selected(const Board &board, const ComponentPlacement &placement,
                                      PcbPlacementSvgOptions options) {
    return !options.layer_filter.has_value() ||
           board_side_matches_layer(board, placement.side(), options.layer_filter.value());
}

[[nodiscard]] bool pad_resolution_selected(const Board &board, const PadResolution &resolution,
                                           const FootprintLibrary &footprints,
                                           PcbPlacementSvgOptions options) {
    if (!options.layer_filter.has_value()) {
        return true;
    }
    return placement_pad_selected_for_layer(board, footprints, resolution.placement(),
                                            resolution.pad(), options.layer_filter.value());
}

[[nodiscard]] bool via_intersects_layer(const Board &board, const BoardVia &via,
                                        BoardLayerId layer) {
    return layer_list_contains(volt::detail::via_copper_layers(board, via), layer);
}

[[nodiscard]] bool diagnostic_selected(const Board &board, const Diagnostic &diagnostic,
                                       PcbPlacementSvgOptions options) {
    if (!options.layer_filter.has_value()) {
        return true;
    }
    const auto layer = options.layer_filter.value();
    if (!diagnostic.overlays().empty()) {
        auto has_layered_overlay = false;
        for (const auto &overlay : diagnostic.overlays()) {
            if (overlay.layers().empty()) {
                return true;
            }
            has_layered_overlay = true;
            if (layer_list_contains(overlay.layers(), layer)) {
                return true;
            }
        }
        if (has_layered_overlay) {
            return false;
        }
    }
    for (const auto entity : diagnostic.entities()) {
        switch (entity.kind()) {
        case EntityKind::Board:
            return true;
        case EntityKind::BoardLayer:
            if (entity.index() == layer.index()) {
                return true;
            }
            break;
        case EntityKind::BoardTrack: {
            const auto id = BoardTrackId{entity.index()};
            if (id.index() < board.track_count() && board.track(id).layer() == layer) {
                return true;
            }
            break;
        }
        case EntityKind::BoardVia: {
            const auto id = BoardViaId{entity.index()};
            if (id.index() < board.via_count() &&
                via_intersects_layer(board, board.via(id), layer)) {
                return true;
            }
            break;
        }
        case EntityKind::BoardZone: {
            const auto id = BoardZoneId{entity.index()};
            if (id.index() < board.zone_count() &&
                layer_list_contains(board.zone(id).layers(), layer)) {
                return true;
            }
            break;
        }
        case EntityKind::BoardKeepout: {
            const auto id = BoardKeepoutId{entity.index()};
            if (id.index() < board.keepout_count() &&
                layer_list_contains(board.keepout(id).layers(), layer)) {
                return true;
            }
            break;
        }
        case EntityKind::BoardText: {
            const auto id = BoardTextId{entity.index()};
            if (id.index() < board.text_count() && board.text(id).layer() == layer) {
                return true;
            }
            break;
        }
        case EntityKind::ComponentPlacement: {
            const auto id = ComponentPlacementId{entity.index()};
            if (id.index() < board.placement_count() &&
                placement_selected(board, board.placement(id), options)) {
                return true;
            }
            break;
        }
        case EntityKind::Component: {
            const auto placement = board.placement_for_component(ComponentId{entity.index()});
            if (placement.has_value() && placement->index() < board.placement_count() &&
                placement_selected(board, board.placement(placement.value()), options)) {
                return true;
            }
            break;
        }
        default:
            break;
        }
    }
    return false;
}

void write_board_layer_group_open(std::ostream &out, const Board &board, BoardLayerId layer_id) {
    const auto &layer = board.layer(layer_id);
    const auto token = pcb_svg_layer_token(board, layer_id);
    out << "    <g id=\"pcb-layer-" << token << "\" class=\"pcb-layer board-layer layer-" << token
        << "\" data-layer=\"" << encode_local_id(layer_id) << "\" data-layer-name=\""
        << pcb_svg_escape(layer.name()) << "\" data-layer-role=\""
        << board_layer_role_name(layer.role()) << "\" data-layer-side=\""
        << board_layer_side_name(layer.side()) << "\">\n";
}

void write_zones(std::ostream &out, const Board &board, BoardLayerId layer) {
    if (board.zone_count() == 0U) {
        return;
    }

    out << "    <g class=\"layer layer-zones\">\n";
    for (std::size_t index = 0; index < board.zone_count(); ++index) {
        const auto id = BoardZoneId{index};
        const auto &zone = board.zone(id);
        if (!layer_list_contains(zone.layers(), layer)) {
            continue;
        }
        out << "      <polygon id=\"pcb-zone-" << index;
        if (zone.layers().size() > 1U) {
            out << "-layer-" << pcb_svg_layer_token(board, layer);
        }
        out << "\" class=\"pcb-zone fill-" << board_zone_fill_name(zone.fill()) << "\" data-zone=\""
            << encode_local_id(id) << "\" data-layer=\"" << encode_local_id(layer)
            << "\" data-layers=\"" << pcb_svg_escape(board_layer_list_attr(zone.layers())) << "\"";
        if (zone.net().has_value()) {
            out << " data-net=\"" << encode_local_id(zone.net().value()) << "\"";
        }
        out << " data-priority=\"" << zone.priority() << "\" points=\"";
        write_pcb_point_list(out, zone.outline());
        out << "\"/>\n";
    }
    out << "    </g>\n";
}

void write_keepouts(std::ostream &out, const Board &board, BoardLayerId layer) {
    if (board.keepout_count() == 0U) {
        return;
    }

    out << "    <g class=\"layer layer-keepouts\">\n";
    for (std::size_t index = 0; index < board.keepout_count(); ++index) {
        const auto id = BoardKeepoutId{index};
        const auto &keepout = board.keepout(id);
        if (!layer_list_contains(keepout.layers(), layer)) {
            continue;
        }
        const auto restrictions = keepout_restriction_list_attr(keepout.restrictions());
        out << "      <polygon id=\"pcb-keepout-" << index;
        if (keepout.layers().size() > 1U) {
            out << "-layer-" << pcb_svg_layer_token(board, layer);
        }
        out << "\" class=\"pcb-keepout " << pcb_svg_escape(restrictions) << "\" data-keepout=\""
            << encode_local_id(id) << "\" data-layer=\"" << encode_local_id(layer)
            << "\" data-layers=\"" << pcb_svg_escape(board_layer_list_attr(keepout.layers()))
            << "\" data-restrictions=\"" << pcb_svg_escape(restrictions) << "\" points=\"";
        write_pcb_point_list(out, keepout.outline());
        out << "\"/>\n";
    }
    out << "    </g>\n";
}

void write_texts(std::ostream &out, const Board &board, BoardLayerId layer) {
    if (board.text_count() == 0U) {
        return;
    }

    out << "    <g class=\"layer layer-board-text\">\n";
    for (std::size_t index = 0; index < board.text_count(); ++index) {
        const auto id = BoardTextId{index};
        const auto &text = board.text(id);
        if (text.layer() != layer) {
            continue;
        }
        out << "      <text id=\"pcb-text-" << index << "\" class=\"board-text";
        if (text.locked()) {
            out << " locked";
        }
        out << "\" data-text=\"" << encode_local_id(id) << "\" data-layer=\""
            << encode_local_id(text.layer()) << "\" x=\"";
        write_pcb_svg_number(out, text.position().x_mm());
        out << "\" y=\"";
        write_pcb_svg_number(out, text.position().y_mm());
        out << "\" font-size=\"";
        write_pcb_svg_number(out, text.size_mm());
        out << "\" transform=\"rotate(";
        write_pcb_svg_number(out, text.rotation().degrees());
        out << ' ';
        write_pcb_svg_number(out, text.position().x_mm());
        out << ' ';
        write_pcb_svg_number(out, text.position().y_mm());
        out << ")\">" << pcb_svg_escape(text.text()) << "</text>\n";
    }
    out << "    </g>\n";
}

void write_copper(std::ostream &out, const Board &board, BoardLayerId layer) {
    if (board.track_count() == 0U && board.via_count() == 0U) {
        return;
    }

    out << "    <g class=\"layer layer-copper\">\n";
    for (std::size_t index = 0; index < board.track_count(); ++index) {
        const auto id = BoardTrackId{index};
        const auto &track = board.track(id);
        if (track.layer() != layer) {
            continue;
        }
        out << "      <polyline id=\"pcb-track-" << index << "\" class=\"pcb-track\" data-track=\""
            << encode_local_id(id) << "\" data-layer=\"" << encode_local_id(track.layer())
            << "\" data-net=\"" << encode_local_id(track.net()) << "\" points=\"";
        for (std::size_t point_index = 0; point_index < track.points().size(); ++point_index) {
            if (point_index != 0U) {
                out << ' ';
            }
            write_pcb_svg_number(out, track.points()[point_index].x_mm());
            out << ',';
            write_pcb_svg_number(out, track.points()[point_index].y_mm());
        }
        out << "\" stroke-width=\"";
        write_pcb_svg_number(out, track.width_mm());
        out << "\"/>\n";
    }

    for (std::size_t index = 0; index < board.via_count(); ++index) {
        const auto id = BoardViaId{index};
        const auto &via = board.via(id);
        if (!via_intersects_layer(board, via, layer)) {
            continue;
        }
        out << "      <g id=\"pcb-via-" << index << "-layer-" << pcb_svg_layer_token(board, layer)
            << "\" class=\"pcb-via\" data-via=\"" << encode_local_id(id) << "\" data-net=\""
            << encode_local_id(via.net()) << "\" data-layer=\"" << encode_local_id(layer)
            << "\" data-start-layer=\"" << encode_local_id(via.start_layer())
            << "\" data-end-layer=\"" << encode_local_id(via.end_layer()) << "\">\n";
        out << "        <circle class=\"pcb-via-annular\" cx=\"";
        write_pcb_svg_number(out, via.position().x_mm());
        out << "\" cy=\"";
        write_pcb_svg_number(out, via.position().y_mm());
        out << "\" r=\"";
        write_pcb_svg_number(out, via.annular_diameter_mm() / 2.0);
        out << "\"/>\n";
        out << "        <circle class=\"pcb-via-drill\" cx=\"";
        write_pcb_svg_number(out, via.position().x_mm());
        out << "\" cy=\"";
        write_pcb_svg_number(out, via.position().y_mm());
        out << "\" r=\"";
        write_pcb_svg_number(out, via.drill_diameter_mm() / 2.0);
        out << "\"/>\n";
        out << "      </g>\n";
    }
    out << "    </g>\n";
}

} // namespace volt::io::detail
