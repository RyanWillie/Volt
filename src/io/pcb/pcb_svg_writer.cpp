#include <volt/io/pcb/pcb_svg_writer.hpp>

#include "../detail/entity_ref_format.hpp"

#include <volt/core/errors.hpp>

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
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "PCB SVG numeric values must be finite"};
    }
    const auto rounded = std::round(value * 1.0e12) / 1.0e12;
    if (std::abs(value - rounded) < 1.0e-12) {
        value = rounded;
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

[[nodiscard]] double preview_height(const PcbSvgBounds &bounds, const Board &board,
                                    const DiagnosticReport &diagnostics,
                                    PcbPlacementSvgOptions options) {
    auto height = (bounds.max_y - bounds.min_y) + (pcb_svg_margin_mm * 2.0);
    if (options.diagnostic_overlays && !diagnostics.diagnostics().empty()) {
        auto selected_count = std::size_t{0};
        for (const auto &diagnostic : diagnostics.diagnostics()) {
            if (diagnostic_selected(board, diagnostic, options)) {
                ++selected_count;
            }
        }
        height += 3.0 * static_cast<double>(selected_count);
    }
    return height;
}

[[nodiscard]] std::string entity_ref_svg_id(EntityRef entity) {
    return entity_ref_serialized_id(entity);
}

[[nodiscard]] std::string entity_ref_list(const std::vector<EntityRef> &entities) {
    auto result = std::string{};
    for (std::size_t index = 0; index < entities.size(); ++index) {
        if (index != 0U) {
            result += " ";
        }
        result += entity_ref_serialized_id(entities[index]);
    }
    return result;
}

[[nodiscard]] std::string entity_ref_list(const Diagnostic &diagnostic) {
    return entity_ref_list(diagnostic.entities());
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
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled diagnostic severity"};
}

[[nodiscard]] std::string overlay_kind_class(DiagnosticOverlayKind kind) {
    switch (kind) {
    case DiagnosticOverlayKind::BoundingBox:
        return "bounding-box";
    case DiagnosticOverlayKind::Point:
        return "point";
    case DiagnosticOverlayKind::Polygon:
        return "polygon";
    case DiagnosticOverlayKind::Segment:
        return "segment";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled diagnostic overlay kind"};
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
        const auto *existing = library.find(definition.ref());
        if (existing == nullptr) {
            library.add(definition);
            continue;
        }
        if (::volt::detail::footprint_library_definition_conflicts(*existing, definition)) {
            throw KernelLogicError{
                ErrorCode::InvalidState,
                "Board footprint definition conflicts with footprint library definition"};
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

void include_board_point(PcbSvgBounds &bounds, BoardPoint point) {
    bounds.min_x = std::min(bounds.min_x, point.x_mm());
    bounds.min_y = std::min(bounds.min_y, point.y_mm());
    bounds.max_x = std::max(bounds.max_x, point.x_mm());
    bounds.max_y = std::max(bounds.max_y, point.y_mm());
}

[[nodiscard]] PcbSvgBounds
bounds_from_board(const Board &board, const FootprintLibrary &footprints,
                  const std::vector<ProjectedFootprintGeometry> &footprint_geometries) {
    auto bounds = bounds_from_outline(board);
    for (std::size_t index = 0; index < board.feature_count(); ++index) {
        include_feature_bounds(bounds, board.feature(BoardFeatureId{index}));
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
        const auto placement_id = ComponentPlacementId{index};
        const auto &placement = board.placement(placement_id);
        include_board_point(bounds, placement.position());
        const auto *definition = resolve_definition_for_placement(board, placement, footprints);
        if (definition != nullptr) {
            include_footprint_bounds(
                bounds, placement, *definition,
                projected_footprint_geometry_for_placement(footprint_geometries, placement_id));
        }
    }
    return bounds;
}

[[nodiscard]] bool overlay_selected(const DiagnosticOverlay &overlay,
                                    PcbPlacementSvgOptions options) {
    if (!options.layer_filter.has_value()) {
        return true;
    }
    return overlay.layers().empty() ||
           layer_list_contains(overlay.layers(), options.layer_filter.value());
}

[[nodiscard]] bool has_selected_diagnostic_overlays(const Board &board,
                                                    const DiagnosticReport &diagnostics,
                                                    PcbPlacementSvgOptions options) {
    if (!options.diagnostic_overlays) {
        return false;
    }
    for (const auto &diagnostic : diagnostics.diagnostics()) {
        if (!diagnostic_selected(board, diagnostic, options)) {
            continue;
        }
        for (const auto &overlay : diagnostic.overlays()) {
            if (overlay_selected(overlay, options)) {
                return true;
            }
        }
    }
    return false;
}

void include_selected_diagnostic_overlay_bounds(PcbSvgBounds &bounds, const Board &board,
                                                const DiagnosticReport &diagnostics,
                                                PcbPlacementSvgOptions options) {
    if (!options.diagnostic_overlays) {
        return;
    }
    for (const auto &diagnostic : diagnostics.diagnostics()) {
        if (!diagnostic_selected(board, diagnostic, options)) {
            continue;
        }
        for (const auto &overlay : diagnostic.overlays()) {
            if (!overlay_selected(overlay, options)) {
                continue;
            }
            for (const auto point : overlay.points()) {
                include_board_point(bounds, BoardPoint{point.x_mm, point.y_mm});
            }
        }
    }
}

void write_style(std::ostream &out, bool include_copper, bool include_zones, bool include_keepouts,
                 bool include_texts, bool include_diagnostic_overlays) {
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
    out << "    .footprint-courtyard{fill:none;stroke:#64748b;stroke-width:0.12;"
           "stroke-dasharray:0.7 0.45}\n";
    out << "    .footprint-body.declared{fill:#fff8db;stroke:#8a6a16;stroke-width:0.18}\n";
    out << "    .footprint-fabrication.declared{fill:none;stroke:#475569;stroke-width:0.1}\n";
    out << "    .footprint-assembly.declared{fill:none;stroke:#7c3aed;stroke-width:0.1;"
           "stroke-dasharray:0.55 0.35}\n";
    out << "    .footprint-marking.declared{fill:#172026;fill-opacity:0.45;stroke:#172026;"
           "stroke-width:0.08}\n";
    out << "    .footprint-marking.kind-pin_1,.footprint-marking.kind-polarity{"
           "fill:#b42318;stroke:#7a271a}\n";
    out << "    .footprint-envelope.synthetic{fill:#fff8db;fill-opacity:0.24;"
           "stroke:#8a6a16;stroke-width:0.18;stroke-dasharray:0.9 0.55}\n";
    out << "    .footprint-pad{fill:#d99822;stroke:#5a3a08;stroke-width:0.14}\n";
    out << "    .pad-overlay{fill:#175cd3;stroke:#ffffff;stroke-width:0.12}\n";
    out << "    .ratsnest{fill:none;stroke:#175cd3;stroke-width:0.16;stroke-dasharray:0.8 "
           "0.55}\n";
    out << "    .diagnostic-marker{fill:none;stroke:#b42318;stroke-width:0.28;stroke-dasharray:0.9 "
           "0.55}\n";
    out << "    .diagnostic-label{fill:#b42318}\n";
    if (include_diagnostic_overlays) {
        out << "    .diagnostic-overlay{stroke-width:0.32;stroke-linecap:round;"
               "stroke-linejoin:round;stroke-dasharray:1.1 0.55}\n";
        out << "    .diagnostic-overlay.error{fill:#b42318;fill-opacity:0.1;stroke:#b42318}\n";
        out << "    .diagnostic-overlay.warning{fill:#d97706;fill-opacity:0.1;stroke:#d97706}\n";
        out << "    .diagnostic-overlay.info{fill:#175cd3;fill-opacity:0.08;stroke:#175cd3}\n";
        out << "    .diagnostic-overlay.segment{fill:none}\n";
        out << "    .diagnostic-overlay.point{fill-opacity:0.22}\n";
        out << "    .diagnostic-marker.warning{stroke:#d97706}\n";
        out << "    .diagnostic-marker.info{stroke:#175cd3}\n";
        out << "    .diagnostic-label.warning{fill:#d97706}\n";
        out << "    .diagnostic-label.info{fill:#175cd3}\n";
    }
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

void write_pcb_point_list(std::ostream &out, const std::vector<BoardPoint> &points);

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

void write_pad_overlays(std::ostream &out, const Board &board,
                        const std::vector<PadResolution> &resolutions,
                        const FootprintLibrary &footprints, PcbPlacementSvgOptions options) {
    out << "    <g class=\"layer layer-pad-overlays\">\n";
    if (options.pad_net_overlays) {
        for (const auto &resolution : resolutions) {
            if (!pad_resolution_selected(board, resolution, footprints, options)) {
                continue;
            }
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
                << pcb_svg_escape(board.circuit().get(resolution.net().value()).name().value())
                << "</text>\n";
        }
    }
    out << "    </g>\n";
}

void write_ratsnest(std::ostream &out, const Board &board, const std::vector<RatsnestEdge> &edges,
                    const FootprintLibrary &footprints, PcbPlacementSvgOptions options) {
    out << "    <g class=\"layer layer-ratsnest\">\n";
    auto current_net = std::optional<NetId>{};
    std::size_t net_edge_index = 0;
    for (const auto &edge : edges) {
        if (options.layer_filter.has_value()) {
            const auto layer = options.layer_filter.value();
            const auto from_selected = placement_pad_selected_for_layer(
                board, footprints, edge.from().placement(), edge.from().pad(), layer);
            const auto to_selected = placement_pad_selected_for_layer(
                board, footprints, edge.to().placement(), edge.to().pad(), layer);
            if (!from_selected || !to_selected) {
                continue;
            }
        }
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

void write_diagnostic_overlay_attrs(std::ostream &out, const Diagnostic &diagnostic,
                                    const DiagnosticOverlay &overlay, std::size_t diagnostic_index,
                                    std::size_t overlay_index, const std::string &severity,
                                    const std::string &diagnostic_entities) {
    out << " id=\"diagnostic-overlay-" << diagnostic_index << '-' << overlay_index
        << "\" class=\"diagnostic-overlay " << severity << ' ' << overlay_kind_class(overlay.kind())
        << "\" data-diagnostic-code=\"" << pcb_svg_escape(diagnostic.code().value())
        << "\" data-diagnostic-index=\"" << diagnostic_index << "\" data-overlay-index=\""
        << overlay_index << "\" data-entities=\"" << pcb_svg_escape(diagnostic_entities)
        << "\" data-overlay-entities=\"" << pcb_svg_escape(entity_ref_list(overlay.entities()))
        << "\" data-layers=\"" << pcb_svg_escape(board_layer_list_attr(overlay.layers())) << "\"";
}

void write_diagnostic_point_list(std::ostream &out, const std::vector<DiagnosticPoint> &points) {
    for (std::size_t point_index = 0; point_index < points.size(); ++point_index) {
        if (point_index != 0U) {
            out << ' ';
        }
        write_pcb_svg_number(out, points[point_index].x_mm);
        out << ',';
        write_pcb_svg_number(out, points[point_index].y_mm);
    }
}

void write_diagnostic_overlay(std::ostream &out, const Diagnostic &diagnostic,
                              const DiagnosticOverlay &overlay, std::size_t diagnostic_index,
                              std::size_t overlay_index, const std::string &severity,
                              const std::string &diagnostic_entities) {
    switch (overlay.kind()) {
    case DiagnosticOverlayKind::BoundingBox: {
        const auto &points = overlay.points();
        const auto min_x = std::min(points[0].x_mm, points[1].x_mm);
        const auto min_y = std::min(points[0].y_mm, points[1].y_mm);
        const auto max_x = std::max(points[0].x_mm, points[1].x_mm);
        const auto max_y = std::max(points[0].y_mm, points[1].y_mm);
        out << "      <rect";
        write_diagnostic_overlay_attrs(out, diagnostic, overlay, diagnostic_index, overlay_index,
                                       severity, diagnostic_entities);
        out << " x=\"";
        write_pcb_svg_number(out, min_x);
        out << "\" y=\"";
        write_pcb_svg_number(out, min_y);
        out << "\" width=\"";
        write_pcb_svg_number(out, max_x - min_x);
        out << "\" height=\"";
        write_pcb_svg_number(out, max_y - min_y);
        out << "\"/>\n";
        return;
    }
    case DiagnosticOverlayKind::Point:
        out << "      <circle";
        write_diagnostic_overlay_attrs(out, diagnostic, overlay, diagnostic_index, overlay_index,
                                       severity, diagnostic_entities);
        out << " cx=\"";
        write_pcb_svg_number(out, overlay.points()[0].x_mm);
        out << "\" cy=\"";
        write_pcb_svg_number(out, overlay.points()[0].y_mm);
        out << "\" r=\"0.45\"/>\n";
        return;
    case DiagnosticOverlayKind::Polygon:
        out << "      <polygon";
        write_diagnostic_overlay_attrs(out, diagnostic, overlay, diagnostic_index, overlay_index,
                                       severity, diagnostic_entities);
        out << " points=\"";
        write_diagnostic_point_list(out, overlay.points());
        out << "\"/>\n";
        return;
    case DiagnosticOverlayKind::Segment:
        out << "      <line";
        write_diagnostic_overlay_attrs(out, diagnostic, overlay, diagnostic_index, overlay_index,
                                       severity, diagnostic_entities);
        out << " x1=\"";
        write_pcb_svg_number(out, overlay.points()[0].x_mm);
        out << "\" y1=\"";
        write_pcb_svg_number(out, overlay.points()[0].y_mm);
        out << "\" x2=\"";
        write_pcb_svg_number(out, overlay.points()[1].x_mm);
        out << "\" y2=\"";
        write_pcb_svg_number(out, overlay.points()[1].y_mm);
        out << "\"/>\n";
        return;
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled diagnostic overlay kind"};
}

void write_diagnostics(std::ostream &out, const Board &board, const DiagnosticReport &diagnostics,
                       const PcbSvgBounds &bounds, PcbPlacementSvgOptions options) {
    out << "    <g class=\"layer layer-diagnostics\">\n";
    if (options.diagnostic_overlays) {
        auto selected_index = std::size_t{0};
        for (std::size_t index = 0; index < diagnostics.diagnostics().size(); ++index) {
            const auto &diagnostic = diagnostics.diagnostics()[index];
            if (!diagnostic_selected(board, diagnostic, options)) {
                continue;
            }
            const auto severity = severity_class(diagnostic.severity());
            const auto entities = entity_ref_list(diagnostic);
            const auto y = bounds.max_y + 2.8 + (static_cast<double>(selected_index) * 3.0);
            ++selected_index;
            out << "      <text class=\"diagnostic-label " << severity
                << "\" data-diagnostic-code=\"" << pcb_svg_escape(diagnostic.code().value())
                << "\" data-entities=\"" << pcb_svg_escape(entities) << "\" x=\"";
            write_pcb_svg_number(out, bounds.min_x);
            out << "\" y=\"";
            write_pcb_svg_number(out, y);
            out << "\">" << pcb_svg_escape(diagnostic.code().value()) << "</text>\n";

            for (std::size_t overlay_index = 0; overlay_index < diagnostic.overlays().size();
                 ++overlay_index) {
                const auto &overlay = diagnostic.overlays()[overlay_index];
                if (!overlay_selected(overlay, options)) {
                    continue;
                }
                write_diagnostic_overlay(out, diagnostic, overlay, index, overlay_index, severity,
                                         entities);
            }

            for (const auto entity : diagnostic.entities()) {
                if (entity.kind() == EntityKind::BoardTrack) {
                    const auto track_id = BoardTrackId{entity.index()};
                    if (track_id.index() >= board.track_count()) {
                        continue;
                    }
                    const auto &track = board.track(track_id);
                    if (!layer_selected(options, track.layer())) {
                        continue;
                    }
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
                    if (options.layer_filter.has_value() &&
                        !via_intersects_layer(board, via, options.layer_filter.value())) {
                        continue;
                    }
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
                    if (options.layer_filter.has_value() &&
                        !layer_list_contains(keepout.layers(), options.layer_filter.value())) {
                        continue;
                    }
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
                    if (options.layer_filter.has_value() &&
                        !layer_list_contains(zone.layers(), options.layer_filter.value())) {
                        continue;
                    }
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
                if (!placement_selected(board, placement, options)) {
                    continue;
                }
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
    const auto footprint_geometries = board.project_footprint_geometries(preview_footprints);
    auto bounds = detail::bounds_from_board(board, preview_footprints, footprint_geometries);
    detail::include_selected_diagnostic_overlay_bounds(bounds, board, diagnostics, options);
    const auto width = detail::preview_width(bounds);
    const auto height = detail::preview_height(bounds, board, diagnostics, options);
    const auto translate_x = detail::pcb_svg_margin_mm - bounds.min_x;
    const auto translate_y = detail::pcb_svg_margin_mm - bounds.min_y;
    const auto resolutions = board.resolve_pads(preview_footprints);
    const auto ratsnest_edges = derive_ratsnest_edges(board.circuit(), resolutions);
    const auto has_copper = board.track_count() != 0U || board.via_count() != 0U;
    const auto has_zones = board.zone_count() != 0U;
    const auto has_keepouts = board.keepout_count() != 0U;
    const auto has_texts = board.text_count() != 0U;
    const auto has_diagnostic_overlays =
        detail::has_selected_diagnostic_overlays(board, diagnostics, options);

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
    detail::write_style(out, has_copper, has_zones, has_keepouts, has_texts,
                        has_diagnostic_overlays);
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
    for (std::size_t index = 0; index < board.layer_count(); ++index) {
        const auto layer = BoardLayerId{index};
        if (!detail::layer_selected(options, layer)) {
            continue;
        }
        detail::write_board_layer_group_open(out, board, layer);
        detail::write_zones(out, board, layer);
        detail::write_copper(out, board, layer);
        detail::write_keepouts(out, board, layer);
        detail::write_texts(out, board, layer);
        out << "    </g>\n";
    }
    detail::write_placements(out, board, preview_footprints, resolutions, footprint_geometries,
                             diagnostics, options);
    if (options.ratsnest_edges) {
        detail::write_ratsnest(out, board, ratsnest_edges, preview_footprints, options);
    }
    detail::write_pad_overlays(out, board, resolutions, preview_footprints, options);
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
