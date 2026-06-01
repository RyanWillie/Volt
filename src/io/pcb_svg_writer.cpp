#include <volt/io/pcb_svg_writer.hpp>

namespace volt::io::detail {

[[nodiscard]] std::string pcb_svg_escape(std::string_view value) {
    auto result = std::string{};
    for (const auto character : value) {
        switch (character) {
        case '&':
            result += "&amp;";
            break;
        case '<':
            result += "&lt;";
            break;
        case '>':
            result += "&gt;";
            break;
        case '"':
            result += "&quot;";
            break;
        case '\'':
            result += "&apos;";
            break;
        default:
            result += character;
            break;
        }
    }
    return result;
}
void write_pcb_svg_number(std::ostream &out, double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument{"PCB SVG numeric values must be finite"};
    }
    if (std::abs(value) < 1.0e-12) {
        value = 0.0;
    }

    auto formatted = std::ostringstream{};
    formatted << std::setprecision(15) << value;
    out << formatted.str();
}
[[nodiscard]] PcbSvgBounds bounds_from_outline(const Board &board) {
    if (!board.outline().has_value()) {
        return PcbSvgBounds{0.0, 0.0, pcb_svg_default_width_mm, pcb_svg_default_height_mm};
    }

    const auto &vertices = board.outline()->vertices();
    auto bounds = PcbSvgBounds{vertices.front().x_mm(), vertices.front().y_mm(),
                               vertices.front().x_mm(), vertices.front().y_mm()};
    for (const auto point : vertices) {
        bounds.min_x = std::min(bounds.min_x, point.x_mm());
        bounds.min_y = std::min(bounds.min_y, point.y_mm());
        bounds.max_x = std::max(bounds.max_x, point.x_mm());
        bounds.max_y = std::max(bounds.max_y, point.y_mm());
    }
    return bounds;
}
[[nodiscard]] double preview_width(const PcbSvgBounds &bounds) {
    return (bounds.max_x - bounds.min_x) + (pcb_svg_margin_mm * 2.0);
}
[[nodiscard]] double preview_height(const PcbSvgBounds &bounds, const DiagnosticReport &diagnostics,
                                    PcbPlacementSvgOptions options) {
    auto height = (bounds.max_y - bounds.min_y) + (pcb_svg_margin_mm * 2.0);
    if (options.diagnostic_overlays && !diagnostics.diagnostics().empty()) {
        height += 3.0 * static_cast<double>(diagnostics.diagnostics().size());
    }
    return height;
}
[[nodiscard]] std::string entity_ref_svg_id(EntityRef entity) {
    switch (entity.kind()) {
    case EntityKind::ComponentDef:
        return encode_local_id(ComponentDefId{entity.index()});
    case EntityKind::Component:
        return encode_local_id(ComponentId{entity.index()});
    case EntityKind::PinDef:
        return encode_local_id(PinDefId{entity.index()});
    case EntityKind::Pin:
        return encode_local_id(PinId{entity.index()});
    case EntityKind::Net:
        return encode_local_id(NetId{entity.index()});
    case EntityKind::ModuleDef:
        return encode_local_id(ModuleDefId{entity.index()});
    case EntityKind::ModuleInstance:
        return encode_local_id(ModuleInstanceId{entity.index()});
    case EntityKind::PortDef:
        return encode_local_id(PortDefId{entity.index()});
    case EntityKind::SymbolDef:
        return encode_local_id(SymbolDefId{entity.index()});
    case EntityKind::Sheet:
        return encode_local_id(SheetId{entity.index()});
    case EntityKind::SymbolInstance:
        return encode_local_id(SymbolInstanceId{entity.index()});
    case EntityKind::WireRun:
        return encode_local_id(WireRunId{entity.index()});
    case EntityKind::NetLabel:
        return encode_local_id(NetLabelId{entity.index()});
    case EntityKind::Junction:
        return encode_local_id(JunctionId{entity.index()});
    case EntityKind::PowerPort:
        return encode_local_id(PowerPortId{entity.index()});
    case EntityKind::NoConnectMarker:
        return encode_local_id(NoConnectMarkerId{entity.index()});
    case EntityKind::SheetPort:
        return encode_local_id(SheetPortId{entity.index()});
    case EntityKind::SymbolField:
        return encode_local_id(SymbolFieldId{entity.index()});
    case EntityKind::BoardLayer:
        return encode_local_id(BoardLayerId{entity.index()});
    case EntityKind::BoardFeature:
        return encode_local_id(BoardFeatureId{entity.index()});
    case EntityKind::BoardTrack:
        return encode_local_id(BoardTrackId{entity.index()});
    case EntityKind::BoardVia:
        return encode_local_id(BoardViaId{entity.index()});
    case EntityKind::BoardZone:
        return encode_local_id(BoardZoneId{entity.index()});
    case EntityKind::BoardKeepout:
        return encode_local_id(BoardKeepoutId{entity.index()});
    case EntityKind::BoardText:
        return encode_local_id(BoardTextId{entity.index()});
    case EntityKind::FootprintDef:
        return encode_local_id(FootprintDefId{entity.index()});
    case EntityKind::FootprintPad:
        return encode_local_id(FootprintPadId{entity.index()});
    case EntityKind::ComponentPlacement:
        return encode_local_id(ComponentPlacementId{entity.index()});
    }
    throw std::logic_error{"Unhandled diagnostic entity kind"};
}
[[nodiscard]] std::string entity_ref_list(const Diagnostic &diagnostic) {
    auto result = std::string{};
    for (std::size_t index = 0; index < diagnostic.entities().size(); ++index) {
        if (index != 0U) {
            result += " ";
        }
        result += entity_ref_svg_id(diagnostic.entities()[index]);
    }
    return result;
}
[[nodiscard]] std::string severity_class(Severity severity) {
    switch (severity) {
    case Severity::Info:
        return "info";
    case Severity::Warning:
        return "warning";
    case Severity::Error:
        return "error";
    }
    throw std::logic_error{"Unhandled diagnostic severity"};
}
[[nodiscard]] std::string footprint_ref_token(const FootprintRef &ref) {
    return ref.library() + ":" + ref.name();
}
[[nodiscard]] FootprintLibrary preview_footprint_library(const Board &board,
                                                         const FootprintLibrary &footprints) {
    auto library = FootprintLibrary{};
    for (std::size_t index = 0; index < board.footprint_definition_count(); ++index) {
        library.add(board.footprint_definition(FootprintDefId{index}));
    }
    for (const auto &definition : footprints.definitions()) {
        if (library.find(definition.ref()) == nullptr) {
            library.add(definition);
        }
    }
    return library;
}
[[nodiscard]] const FootprintDefinition *
resolve_definition_for_placement(const Board &board, const ComponentPlacement &placement,
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
[[nodiscard]] bool contains_footprint_ref(const std::vector<FootprintRef> &refs,
                                          const FootprintRef &ref) {
    return std::find(refs.begin(), refs.end(), ref) != refs.end();
}
[[nodiscard]] std::optional<FootprintDefId> projection_footprint_definition_id_for_placement(
    const Board &board, ComponentPlacementId placement_id, const FootprintLibrary &footprints) {
    const auto &placement = board.placement(placement_id);
    const auto &selected_part = board.circuit().selected_physical_part(placement.component());
    if (!selected_part.has_value()) {
        return std::nullopt;
    }

    auto refs = std::vector<FootprintRef>{};
    refs.reserve(board.footprint_definition_count() + placement_id.index() + 1U);
    for (std::size_t index = 0; index < board.footprint_definition_count(); ++index) {
        refs.push_back(board.footprint_definition(FootprintDefId{index}).ref());
    }

    for (std::size_t index = 0; index <= placement_id.index(); ++index) {
        const auto &candidate_placement = board.placement(ComponentPlacementId{index});
        const auto &candidate_part =
            board.circuit().selected_physical_part(candidate_placement.component());
        if (!candidate_part.has_value() ||
            contains_footprint_ref(refs, candidate_part->footprint()) ||
            footprints.find(candidate_part->footprint()) == nullptr) {
            continue;
        }
        refs.push_back(candidate_part->footprint());
    }

    const auto match = std::find(refs.begin(), refs.end(), selected_part->footprint());
    if (match == refs.end()) {
        return std::nullopt;
    }
    return FootprintDefId{static_cast<std::size_t>(std::distance(refs.begin(), match))};
}
[[nodiscard]] const PadResolution *
find_pad_resolution(const std::vector<PadResolution> &resolutions, ComponentPlacementId placement,
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
[[nodiscard]] PcbSvgBounds footprint_local_bounds(const FootprintDefinition &definition) {
    auto bounds = PcbSvgBounds{0.0, 0.0, 0.0, 0.0};
    auto initialized = false;
    for (std::size_t index = 0; index < definition.pad_count(); ++index) {
        const auto &pad = definition.pad(FootprintPadId{index});
        const auto half_width = pad.size().width_mm() / 2.0;
        const auto half_height = pad.size().height_mm() / 2.0;
        const auto min_x = pad.position().x_mm() - half_width;
        const auto max_x = pad.position().x_mm() + half_width;
        const auto min_y = pad.position().y_mm() - half_height;
        const auto max_y = pad.position().y_mm() + half_height;

        if (!initialized) {
            bounds = PcbSvgBounds{min_x, min_y, max_x, max_y};
            initialized = true;
            continue;
        }

        bounds.min_x = std::min(bounds.min_x, min_x);
        bounds.min_y = std::min(bounds.min_y, min_y);
        bounds.max_x = std::max(bounds.max_x, max_x);
        bounds.max_y = std::max(bounds.max_y, max_y);
    }
    return PcbSvgBounds{bounds.min_x - 0.5, bounds.min_y - 0.5, bounds.max_x + 0.5,
                        bounds.max_y + 0.5};
}
void include_board_point(PcbSvgBounds &bounds, BoardPoint point) {
    bounds.min_x = std::min(bounds.min_x, point.x_mm());
    bounds.min_y = std::min(bounds.min_y, point.y_mm());
    bounds.max_x = std::max(bounds.max_x, point.x_mm());
    bounds.max_y = std::max(bounds.max_y, point.y_mm());
}
void include_footprint_bounds(PcbSvgBounds &bounds, const ComponentPlacement &placement,
                              const FootprintDefinition &definition) {
    const auto local_bounds = footprint_local_bounds(definition);
    include_board_point(bounds,
                        volt::detail::transform_footprint_point(
                            placement, FootprintPoint{local_bounds.min_x, local_bounds.min_y}));
    include_board_point(bounds,
                        volt::detail::transform_footprint_point(
                            placement, FootprintPoint{local_bounds.max_x, local_bounds.min_y}));
    include_board_point(bounds,
                        volt::detail::transform_footprint_point(
                            placement, FootprintPoint{local_bounds.max_x, local_bounds.max_y}));
    include_board_point(bounds,
                        volt::detail::transform_footprint_point(
                            placement, FootprintPoint{local_bounds.min_x, local_bounds.max_y}));
}
[[nodiscard]] PcbSvgBounds bounds_from_board(const Board &board,
                                             const FootprintLibrary &footprints) {
    auto bounds = bounds_from_outline(board);
    for (std::size_t index = 0; index < board.feature_count(); ++index) {
        const auto &feature = board.feature(BoardFeatureId{index});
        const auto radius = feature.diameter_mm() / 2.0;
        include_board_point(
            bounds, BoardPoint{feature.position().x_mm() - radius, feature.position().y_mm()});
        include_board_point(
            bounds, BoardPoint{feature.position().x_mm() + radius, feature.position().y_mm()});
        include_board_point(
            bounds, BoardPoint{feature.position().x_mm(), feature.position().y_mm() - radius});
        include_board_point(
            bounds, BoardPoint{feature.position().x_mm(), feature.position().y_mm() + radius});
    }
    for (std::size_t index = 0; index < board.track_count(); ++index) {
        const auto &track = board.track(BoardTrackId{index});
        const auto half_width = track.width_mm() / 2.0;
        for (const auto point : track.points()) {
            include_board_point(bounds, BoardPoint{point.x_mm() - half_width, point.y_mm()});
            include_board_point(bounds, BoardPoint{point.x_mm() + half_width, point.y_mm()});
            include_board_point(bounds, BoardPoint{point.x_mm(), point.y_mm() - half_width});
            include_board_point(bounds, BoardPoint{point.x_mm(), point.y_mm() + half_width});
        }
    }
    for (std::size_t index = 0; index < board.via_count(); ++index) {
        const auto &via = board.via(BoardViaId{index});
        const auto radius = via.annular_diameter_mm() / 2.0;
        include_board_point(bounds,
                            BoardPoint{via.position().x_mm() - radius, via.position().y_mm()});
        include_board_point(bounds,
                            BoardPoint{via.position().x_mm() + radius, via.position().y_mm()});
        include_board_point(bounds,
                            BoardPoint{via.position().x_mm(), via.position().y_mm() - radius});
        include_board_point(bounds,
                            BoardPoint{via.position().x_mm(), via.position().y_mm() + radius});
    }
    for (std::size_t index = 0; index < board.zone_count(); ++index) {
        for (const auto point : board.zone(BoardZoneId{index}).outline()) {
            include_board_point(bounds, point);
        }
    }
    for (std::size_t index = 0; index < board.keepout_count(); ++index) {
        for (const auto point : board.keepout(BoardKeepoutId{index}).outline()) {
            include_board_point(bounds, point);
        }
    }
    for (std::size_t index = 0; index < board.text_count(); ++index) {
        const auto &text = board.text(BoardTextId{index});
        include_board_point(bounds, text.position());
    }
    for (std::size_t index = 0; index < board.placement_count(); ++index) {
        const auto &placement = board.placement(ComponentPlacementId{index});
        include_board_point(bounds, placement.position());
        const auto *definition = resolve_definition_for_placement(board, placement, footprints);
        if (definition != nullptr) {
            include_footprint_bounds(bounds, placement, *definition);
        }
    }
    return bounds;
}
[[nodiscard]] std::string pad_shape_class(FootprintPadShape shape) {
    switch (shape) {
    case FootprintPadShape::Rectangle:
        return "rectangle";
    case FootprintPadShape::RoundedRectangle:
        return "rounded-rectangle";
    case FootprintPadShape::Circle:
        return "circle";
    case FootprintPadShape::Oval:
        return "oval";
    }
    throw std::logic_error{"Unhandled PCB footprint pad shape"};
}
void write_style(std::ostream &out, bool include_copper, bool include_zones, bool include_keepouts,
                 bool include_texts) {
    out << "  <style>\n";
    out << "    .board-background{fill:#f8faf8}\n";
    out << "    .board-outline{fill:#d7ead0;stroke:#1f5f3a;stroke-width:0.28}\n";
    out << "    .board-feature{fill:none;stroke:#6b7280;stroke-width:0.24}\n";
    if (include_copper) {
        out << "    "
               ".pcb-track{fill:none;stroke:#b45309;stroke-linecap:round;stroke-linejoin:round}\n";
        out << "    .pcb-via-annular{fill:#b45309;stroke:#7c2d12;stroke-width:0.08}\n";
        out << "    .pcb-via-drill{fill:#f8faf8;stroke:#7c2d12;stroke-width:0.06}\n";
    }
    if (include_zones) {
        out << "    .pcb-zone{fill:#b45309;fill-opacity:0.22;stroke:#92400e;stroke-width:0.12}\n";
    }
    if (include_keepouts) {
        out << "    "
               ".pcb-keepout{fill:#b42318;fill-opacity:0.12;stroke:#b42318;stroke-width:0.18;"
               "stroke-dasharray:0.8 0.45}\n";
    }
    if (include_texts) {
        out << "    .board-text{font-family:sans-serif;fill:#172026;text-anchor:middle;"
               "dominant-baseline:middle}\n";
    }
    out << "    "
           ".board-feature-label,.reference-designator,.pad-net-label,.diagnostic-label{font:1.8px "
           "sans-serif;fill:#172026}\n";
    out << "    .footprint-body{fill:#fff8db;stroke:#8a6a16;stroke-width:0.18}\n";
    out << "    .footprint-pad{fill:#d99822;stroke:#5a3a08;stroke-width:0.14}\n";
    out << "    .pad-overlay{fill:#175cd3;stroke:#ffffff;stroke-width:0.12}\n";
    out << "    .ratsnest{fill:none;stroke:#175cd3;stroke-width:0.16;stroke-dasharray:0.8 "
           "0.55}\n";
    out << "    .diagnostic-marker{fill:none;stroke:#b42318;stroke-width:0.28;stroke-dasharray:0.9 "
           "0.55}\n";
    out << "    .diagnostic-label{fill:#b42318}\n";
    out << "  </style>\n";
}
void write_pcb_svg_outline(std::ostream &out, const Board &board) {
    if (!board.outline().has_value()) {
        return;
    }

    out << "    <polygon class=\"board-outline\" data-board=\"board:0\" points=\"";
    const auto &vertices = board.outline()->vertices();
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        if (index != 0U) {
            out << ' ';
        }
        write_pcb_svg_number(out, vertices[index].x_mm());
        out << ',';
        write_pcb_svg_number(out, vertices[index].y_mm());
    }
    out << "\"/>\n";
}
void write_pcb_svg_features(std::ostream &out, const Board &board) {
    out << "    <g class=\"layer layer-board-features\">\n";
    for (std::size_t index = 0; index < board.feature_count(); ++index) {
        const auto id = BoardFeatureId{index};
        const auto &feature = board.feature(id);
        if (feature.kind() != BoardFeatureKind::MountingHole) {
            continue;
        }

        out << "      <circle class=\"board-feature mounting-hole\" data-board-feature=\""
            << encode_local_id(id) << "\" cx=\"";
        write_pcb_svg_number(out, feature.position().x_mm());
        out << "\" cy=\"";
        write_pcb_svg_number(out, feature.position().y_mm());
        out << "\" r=\"";
        write_pcb_svg_number(out, feature.diameter_mm() / 2.0);
        out << "\"/>\n";
        if (!feature.label().empty()) {
            out << "      <text class=\"board-feature-label\" data-board-feature=\""
                << encode_local_id(id) << "\" x=\"";
            write_pcb_svg_number(out, feature.position().x_mm());
            out << "\" y=\"";
            write_pcb_svg_number(out,
                                 feature.position().y_mm() + (feature.diameter_mm() / 2.0) + 2.0);
            out << "\" text-anchor=\"middle\">" << pcb_svg_escape(feature.label()) << "</text>\n";
        }
    }
    out << "    </g>\n";
}
void write_pcb_point_list(std::ostream &out, const std::vector<BoardPoint> &points) {
    for (std::size_t point_index = 0; point_index < points.size(); ++point_index) {
        if (point_index != 0U) {
            out << ' ';
        }
        write_pcb_svg_number(out, points[point_index].x_mm());
        out << ',';
        write_pcb_svg_number(out, points[point_index].y_mm());
    }
}
[[nodiscard]] std::string board_layer_list_attr(const std::vector<BoardLayerId> &layers) {
    auto result = std::string{};
    for (std::size_t index = 0; index < layers.size(); ++index) {
        if (index != 0U) {
            result += " ";
        }
        result += encode_local_id(layers[index]);
    }
    return result;
}
[[nodiscard]] std::string
keepout_restriction_list_attr(const std::vector<BoardKeepoutRestriction> &restrictions) {
    auto result = std::string{};
    for (std::size_t index = 0; index < restrictions.size(); ++index) {
        if (index != 0U) {
            result += " ";
        }
        result += board_keepout_restriction_name(restrictions[index]);
    }
    return result;
}
void write_zones(std::ostream &out, const Board &board) {
    if (board.zone_count() == 0U) {
        return;
    }

    out << "    <g class=\"layer layer-zones\">\n";
    for (std::size_t index = 0; index < board.zone_count(); ++index) {
        const auto id = BoardZoneId{index};
        const auto &zone = board.zone(id);
        out << "      <polygon id=\"pcb-zone-" << index << "\" class=\"pcb-zone fill-"
            << board_zone_fill_name(zone.fill()) << "\" data-zone=\"" << encode_local_id(id)
            << "\" data-layer=\"" << encode_local_id(zone.layers().front()) << "\" data-layers=\""
            << pcb_svg_escape(board_layer_list_attr(zone.layers())) << "\"";
        if (zone.net().has_value()) {
            out << " data-net=\"" << encode_local_id(zone.net().value()) << "\"";
        }
        out << " data-priority=\"" << zone.priority() << "\" points=\"";
        write_pcb_point_list(out, zone.outline());
        out << "\"/>\n";
    }
    out << "    </g>\n";
}
void write_keepouts(std::ostream &out, const Board &board) {
    if (board.keepout_count() == 0U) {
        return;
    }

    out << "    <g class=\"layer layer-keepouts\">\n";
    for (std::size_t index = 0; index < board.keepout_count(); ++index) {
        const auto id = BoardKeepoutId{index};
        const auto &keepout = board.keepout(id);
        const auto restrictions = keepout_restriction_list_attr(keepout.restrictions());
        out << "      <polygon id=\"pcb-keepout-" << index << "\" class=\"pcb-keepout "
            << pcb_svg_escape(restrictions) << "\" data-keepout=\"" << encode_local_id(id)
            << "\" data-layer=\"" << encode_local_id(keepout.layers().front())
            << "\" data-layers=\"" << pcb_svg_escape(board_layer_list_attr(keepout.layers()))
            << "\" data-restrictions=\"" << pcb_svg_escape(restrictions) << "\" points=\"";
        write_pcb_point_list(out, keepout.outline());
        out << "\"/>\n";
    }
    out << "    </g>\n";
}
void write_texts(std::ostream &out, const Board &board) {
    if (board.text_count() == 0U) {
        return;
    }

    out << "    <g class=\"layer layer-board-text\">\n";
    for (std::size_t index = 0; index < board.text_count(); ++index) {
        const auto id = BoardTextId{index};
        const auto &text = board.text(id);
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
void write_copper(std::ostream &out, const Board &board) {
    if (board.track_count() == 0U && board.via_count() == 0U) {
        return;
    }

    out << "    <g class=\"layer layer-copper\">\n";
    for (std::size_t index = 0; index < board.track_count(); ++index) {
        const auto id = BoardTrackId{index};
        const auto &track = board.track(id);
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
        out << "      <g id=\"pcb-via-" << index << "\" class=\"pcb-via\" data-via=\""
            << encode_local_id(id) << "\" data-net=\"" << encode_local_id(via.net())
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
void write_pad(std::ostream &out, const FootprintPad &pad, FootprintPadId pad_id,
               const PadResolution *resolution) {
    const auto status = resolution == nullptr ? std::string{"invalid"}
                                              : pad_resolution_status_name(resolution->status());
    const auto class_name = "footprint-pad " + pad_shape_class(pad.shape()) + " " + status;

    const auto write_common_attributes = [&]() {
        out << " class=\"" << class_name << "\"";
        if (resolution != nullptr) {
            out << " data-pad-projection=\""
                << pcb_pad_projection_id(resolution->placement(), resolution->pad()) << "\"";
        }
        out << " data-pad=\"" << encode_local_id(pad_id) << "\"";
        if (resolution != nullptr && resolution->pin().has_value()) {
            out << " data-pin=\"" << encode_local_id(resolution->pin().value()) << "\"";
        }
        if (resolution != nullptr && resolution->net().has_value()) {
            out << " data-net=\"" << encode_local_id(resolution->net().value()) << "\"";
        }
    };

    const auto x = pad.position().x_mm() - (pad.size().width_mm() / 2.0);
    const auto y = pad.position().y_mm() - (pad.size().height_mm() / 2.0);
    if (pad.shape() == FootprintPadShape::Circle) {
        out << "        <circle";
        write_common_attributes();
        out << " cx=\"";
        write_pcb_svg_number(out, pad.position().x_mm());
        out << "\" cy=\"";
        write_pcb_svg_number(out, pad.position().y_mm());
        out << "\" r=\"";
        write_pcb_svg_number(out, std::min(pad.size().width_mm(), pad.size().height_mm()) / 2.0);
        out << "\"/>\n";
        return;
    }
    if (pad.shape() == FootprintPadShape::Oval) {
        out << "        <ellipse";
        write_common_attributes();
        out << " cx=\"";
        write_pcb_svg_number(out, pad.position().x_mm());
        out << "\" cy=\"";
        write_pcb_svg_number(out, pad.position().y_mm());
        out << "\" rx=\"";
        write_pcb_svg_number(out, pad.size().width_mm() / 2.0);
        out << "\" ry=\"";
        write_pcb_svg_number(out, pad.size().height_mm() / 2.0);
        out << "\"/>\n";
        return;
    }

    out << "        <rect";
    write_common_attributes();
    out << " x=\"";
    write_pcb_svg_number(out, x);
    out << "\" y=\"";
    write_pcb_svg_number(out, y);
    out << "\" width=\"";
    write_pcb_svg_number(out, pad.size().width_mm());
    out << "\" height=\"";
    write_pcb_svg_number(out, pad.size().height_mm());
    out << '"';
    if (pad.shape() == FootprintPadShape::RoundedRectangle) {
        out << " rx=\"0.1\" ry=\"0.1\"";
    }
    out << "/>\n";
}
void write_placements(std::ostream &out, const Board &board, const FootprintLibrary &footprints,
                      const std::vector<PadResolution> &resolutions) {
    out << "    <g class=\"layer layer-footprints\">\n";
    for (std::size_t index = 0; index < board.placement_count(); ++index) {
        const auto placement_id = ComponentPlacementId{index};
        const auto &placement = board.placement(placement_id);
        const auto *definition = resolve_definition_for_placement(board, placement, footprints);
        if (definition == nullptr) {
            continue;
        }

        const auto local_bounds = footprint_local_bounds(*definition);
        const auto &component = board.circuit().component(placement.component());
        out << "      <g class=\"component-placement " << board_side_name(placement.side());
        if (placement.locked()) {
            out << " locked";
        }
        out << "\" data-placement=\"" << encode_local_id(placement_id) << "\" data-component=\""
            << encode_local_id(placement.component()) << '"';
        const auto footprint_definition_id =
            projection_footprint_definition_id_for_placement(board, placement_id, footprints);
        if (footprint_definition_id.has_value()) {
            out << " data-footprint-def=\"" << encode_local_id(footprint_definition_id.value())
                << '"';
        }
        out << " data-footprint=\"" << pcb_svg_escape(footprint_ref_token(definition->ref()))
            << "\" transform=\"translate(";
        write_pcb_svg_number(out, placement.position().x_mm());
        out << ' ';
        write_pcb_svg_number(out, placement.position().y_mm());
        out << ") rotate(";
        write_pcb_svg_number(out, placement.rotation().degrees());
        out << ')';
        if (placement.side() == BoardSide::Bottom) {
            out << " scale(-1 1)";
        }
        out << "\">\n";
        out << "        <rect class=\"footprint-body\" x=\"";
        write_pcb_svg_number(out, local_bounds.min_x);
        out << "\" y=\"";
        write_pcb_svg_number(out, local_bounds.min_y);
        out << "\" width=\"";
        write_pcb_svg_number(out, local_bounds.max_x - local_bounds.min_x);
        out << "\" height=\"";
        write_pcb_svg_number(out, local_bounds.max_y - local_bounds.min_y);
        out << "\"/>\n";
        for (std::size_t pad_index = 0; pad_index < definition->pad_count(); ++pad_index) {
            const auto pad_id = FootprintPadId{pad_index};
            write_pad(out, definition->pad(pad_id), pad_id,
                      find_pad_resolution(resolutions, placement_id, pad_id));
        }
        out << "        <text class=\"reference-designator\" data-component=\""
            << encode_local_id(placement.component()) << "\" x=\"0\" y=\"";
        write_pcb_svg_number(out, local_bounds.min_y - 1.0);
        out << "\" text-anchor=\"middle\">" << pcb_svg_escape(component.reference().value())
            << "</text>\n";
        out << "      </g>\n";
    }
    out << "    </g>\n";
}
void write_pad_overlays(std::ostream &out, const Board &board,
                        const std::vector<PadResolution> &resolutions,
                        PcbPlacementSvgOptions options) {
    out << "    <g class=\"layer layer-pad-overlays\">\n";
    if (options.pad_net_overlays) {
        for (const auto &resolution : resolutions) {
            const auto status = pad_resolution_status_name(resolution.status());
            out << "      <circle class=\"pad-overlay " << status << "\" data-pad-projection=\""
                << pcb_pad_projection_id(resolution.placement(), resolution.pad()) << "\"";
            if (resolution.net().has_value()) {
                out << " data-net=\"" << encode_local_id(resolution.net().value()) << "\"";
            }
            out << " cx=\"";
            write_pcb_svg_number(out, resolution.position().x_mm());
            out << "\" cy=\"";
            write_pcb_svg_number(out, resolution.position().y_mm());
            out << "\" r=\"0.35\"/>\n";
            if (!resolution.net().has_value()) {
                continue;
            }

            out << "      <text class=\"pad-net-label " << status << "\" data-pad-projection=\""
                << pcb_pad_projection_id(resolution.placement(), resolution.pad())
                << "\" data-net=\"" << encode_local_id(resolution.net().value()) << "\" x=\"";
            write_pcb_svg_number(out, resolution.position().x_mm() + 0.7);
            out << "\" y=\"";
            write_pcb_svg_number(out, resolution.position().y_mm() - 0.45);
            out << "\">"
                << pcb_svg_escape(board.circuit().net(resolution.net().value()).name().value())
                << "</text>\n";
        }
    }
    out << "    </g>\n";
}
void write_ratsnest(std::ostream &out, const std::vector<RatsnestEdge> &edges) {
    out << "    <g class=\"layer layer-ratsnest\">\n";
    auto current_net = std::optional<NetId>{};
    std::size_t net_edge_index = 0;
    for (const auto &edge : edges) {
        if (!current_net.has_value() || current_net.value() != edge.net()) {
            current_net = edge.net();
            net_edge_index = 0;
        }

        out << "      <line id=\"ratsnest-edge-net-" << edge.net().index() << '-' << net_edge_index
            << "\" class=\"ratsnest ratsnest-edge\" data-ratsnest-edge=\""
            << pcb_ratsnest_edge_id(edge.net(), net_edge_index) << "\" data-net=\""
            << encode_local_id(edge.net()) << "\" data-from-pad=\""
            << pcb_pad_projection_id(edge.from().placement(), edge.from().pad())
            << "\" data-to-pad=\"" << pcb_pad_projection_id(edge.to().placement(), edge.to().pad())
            << "\" x1=\"";
        write_pcb_svg_number(out, edge.from().position().x_mm());
        out << "\" y1=\"";
        write_pcb_svg_number(out, edge.from().position().y_mm());
        out << "\" x2=\"";
        write_pcb_svg_number(out, edge.to().position().x_mm());
        out << "\" y2=\"";
        write_pcb_svg_number(out, edge.to().position().y_mm());
        out << "\"/>\n";
        ++net_edge_index;
    }
    out << "    </g>\n";
}
void write_diagnostics(std::ostream &out, const Board &board, const DiagnosticReport &diagnostics,
                       const PcbSvgBounds &bounds, PcbPlacementSvgOptions options) {
    out << "    <g class=\"layer layer-diagnostics\">\n";
    if (options.diagnostic_overlays) {
        for (std::size_t index = 0; index < diagnostics.diagnostics().size(); ++index) {
            const auto &diagnostic = diagnostics.diagnostics()[index];
            const auto severity = severity_class(diagnostic.severity());
            const auto entities = entity_ref_list(diagnostic);
            const auto y = bounds.max_y + 2.8 + (static_cast<double>(index) * 3.0);
            out << "      <text class=\"diagnostic-label " << severity
                << "\" data-diagnostic-code=\"" << pcb_svg_escape(diagnostic.code().value())
                << "\" data-entities=\"" << pcb_svg_escape(entities) << "\" x=\"";
            write_pcb_svg_number(out, bounds.min_x);
            out << "\" y=\"";
            write_pcb_svg_number(out, y);
            out << "\">" << pcb_svg_escape(diagnostic.code().value()) << "</text>\n";

            for (const auto entity : diagnostic.entities()) {
                if (entity.kind() == EntityKind::BoardTrack) {
                    const auto track_id = BoardTrackId{entity.index()};
                    if (track_id.index() >= board.track_count()) {
                        continue;
                    }
                    const auto &track = board.track(track_id);
                    out << "      <polyline class=\"diagnostic-marker " << severity
                        << "\" data-diagnostic-code=\"" << pcb_svg_escape(diagnostic.code().value())
                        << "\" data-entities=\"" << pcb_svg_escape(entities) << "\" data-track=\""
                        << encode_local_id(track_id) << "\" points=\"";
                    for (std::size_t point_index = 0; point_index < track.points().size();
                         ++point_index) {
                        if (point_index != 0U) {
                            out << ' ';
                        }
                        write_pcb_svg_number(out, track.points()[point_index].x_mm());
                        out << ',';
                        write_pcb_svg_number(out, track.points()[point_index].y_mm());
                    }
                    out << "\" stroke-width=\"";
                    write_pcb_svg_number(out, track.width_mm() + 0.6);
                    out << "\"/>\n";
                    continue;
                }
                if (entity.kind() == EntityKind::BoardVia) {
                    const auto via_id = BoardViaId{entity.index()};
                    if (via_id.index() >= board.via_count()) {
                        continue;
                    }
                    const auto &via = board.via(via_id);
                    out << "      <circle class=\"diagnostic-marker " << severity
                        << "\" data-diagnostic-code=\"" << pcb_svg_escape(diagnostic.code().value())
                        << "\" data-entities=\"" << pcb_svg_escape(entities) << "\" data-via=\""
                        << encode_local_id(via_id) << "\" cx=\"";
                    write_pcb_svg_number(out, via.position().x_mm());
                    out << "\" cy=\"";
                    write_pcb_svg_number(out, via.position().y_mm());
                    out << "\" r=\"";
                    write_pcb_svg_number(out, (via.annular_diameter_mm() / 2.0) + 0.6);
                    out << "\"/>\n";
                    continue;
                }
                if (entity.kind() == EntityKind::BoardKeepout) {
                    const auto keepout_id = BoardKeepoutId{entity.index()};
                    if (keepout_id.index() >= board.keepout_count()) {
                        continue;
                    }
                    const auto &keepout = board.keepout(keepout_id);
                    out << "      <polygon class=\"diagnostic-marker " << severity
                        << "\" data-diagnostic-code=\"" << pcb_svg_escape(diagnostic.code().value())
                        << "\" data-entities=\"" << pcb_svg_escape(entities) << "\" data-keepout=\""
                        << encode_local_id(keepout_id) << "\" points=\"";
                    write_pcb_point_list(out, keepout.outline());
                    out << "\"/>\n";
                    continue;
                }
                if (entity.kind() == EntityKind::BoardZone) {
                    const auto zone_id = BoardZoneId{entity.index()};
                    if (zone_id.index() >= board.zone_count()) {
                        continue;
                    }
                    const auto &zone = board.zone(zone_id);
                    out << "      <polygon class=\"diagnostic-marker " << severity
                        << "\" data-diagnostic-code=\"" << pcb_svg_escape(diagnostic.code().value())
                        << "\" data-entities=\"" << pcb_svg_escape(entities) << "\" data-zone=\""
                        << encode_local_id(zone_id) << "\" points=\"";
                    write_pcb_point_list(out, zone.outline());
                    out << "\"/>\n";
                    continue;
                }
                if (entity.kind() != EntityKind::Component &&
                    entity.kind() != EntityKind::ComponentPlacement) {
                    continue;
                }

                auto placement_id = std::optional<ComponentPlacementId>{};
                if (entity.kind() == EntityKind::ComponentPlacement) {
                    placement_id = ComponentPlacementId{entity.index()};
                } else {
                    placement_id = board.placement_for_component(ComponentId{entity.index()});
                }
                if (!placement_id.has_value() || placement_id->index() >= board.placement_count()) {
                    continue;
                }

                const auto &placement = board.placement(placement_id.value());
                out << "      <circle class=\"diagnostic-marker " << severity
                    << "\" data-diagnostic-code=\"" << pcb_svg_escape(diagnostic.code().value())
                    << "\" data-entities=\"" << pcb_svg_escape(entities) << "\" cx=\"";
                write_pcb_svg_number(out, placement.position().x_mm());
                out << "\" cy=\"";
                write_pcb_svg_number(out, placement.position().y_mm());
                out << "\" r=\"1.2\"/>\n";
            }
        }
    }
    out << "    </g>\n";
}

} // namespace volt::io::detail

namespace volt::io {

void write_pcb_placement_svg(std::ostream &out, const Board &board,
                             const FootprintLibrary &footprints, PcbPlacementSvgOptions options) {
    const auto preview_footprints = detail::preview_footprint_library(board, footprints);
    const auto diagnostics = validate_board(board, preview_footprints);
    const auto bounds = detail::bounds_from_board(board, preview_footprints);
    const auto width = detail::preview_width(bounds);
    const auto height = detail::preview_height(bounds, diagnostics, options);
    const auto translate_x = detail::pcb_svg_margin_mm - bounds.min_x;
    const auto translate_y = detail::pcb_svg_margin_mm - bounds.min_y;
    const auto resolutions = board.resolve_pads(preview_footprints);
    const auto ratsnest_edges = derive_ratsnest_edges(resolutions);
    const auto has_copper = board.track_count() != 0U || board.via_count() != 0U;
    const auto has_zones = board.zone_count() != 0U;
    const auto has_keepouts = board.keepout_count() != 0U;
    const auto has_texts = board.text_count() != 0U;

    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" class=\"pcb-placement-preview\" viewBox=\"0 "
           "0 ";
    detail::write_pcb_svg_number(out, width);
    out << ' ';
    detail::write_pcb_svg_number(out, height);
    out << "\" width=\"";
    detail::write_pcb_svg_number(out, width);
    out << "mm\" height=\"";
    detail::write_pcb_svg_number(out, height);
    out << "mm\" data-board=\"board:0\" data-board-name=\""
        << detail::pcb_svg_escape(board.name().value()) << "\" data-units=\""
        << detail::board_units_name(board.units()) << "\">\n";
    detail::write_style(out, has_copper, has_zones, has_keepouts, has_texts);
    out << "  <rect class=\"board-background\" x=\"0\" y=\"0\" width=\"";
    detail::write_pcb_svg_number(out, width);
    out << "\" height=\"";
    detail::write_pcb_svg_number(out, height);
    out << "\"/>\n";
    out << "  <g class=\"board-content\" transform=\"translate(";
    detail::write_pcb_svg_number(out, translate_x);
    out << ' ';
    detail::write_pcb_svg_number(out, translate_y);
    out << ")\">\n";
    detail::write_pcb_svg_outline(out, board);
    detail::write_pcb_svg_features(out, board);
    detail::write_zones(out, board);
    detail::write_copper(out, board);
    detail::write_keepouts(out, board);
    detail::write_texts(out, board);
    detail::write_placements(out, board, preview_footprints, resolutions);
    detail::write_ratsnest(out, ratsnest_edges);
    detail::write_pad_overlays(out, board, resolutions, options);
    detail::write_diagnostics(out, board, diagnostics, bounds, options);
    out << "  </g>\n";
    out << "</svg>\n";
}
[[nodiscard]] std::string write_pcb_placement_svg(const Board &board,
                                                  const FootprintLibrary &footprints,
                                                  PcbPlacementSvgOptions options) {
    auto out = std::ostringstream{};
    write_pcb_placement_svg(out, board, footprints, options);
    return out.str();
}

} // namespace volt::io
