#include <volt/io/pcb/pcb_writer.hpp>

#include "../capabilities/board_capability_profile_io.hpp"
#include "../detail/entity_ref_format.hpp"

#include <array>
#include <string_view>
#include <variant>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/errors.hpp>
#include <volt/pcb/projection/board_geometry_projection.hpp>
#include <volt/pcb/queries/board_queries.hpp>

namespace volt::io::detail {

[[nodiscard]] std::string severity_name(Severity severity) {
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

[[nodiscard]] std::string entity_ref_id(EntityRef entity) {
    return entity_ref_serialized_id(entity);
}

[[nodiscard]] std::string overlay_kind_name(DiagnosticOverlayKind kind) {
    switch (kind) {
    case DiagnosticOverlayKind::BoundingBox:
        return "bounding_box";
    case DiagnosticOverlayKind::Point:
        return "point";
    case DiagnosticOverlayKind::Polygon:
        return "polygon";
    case DiagnosticOverlayKind::Segment:
        return "segment";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled diagnostic overlay kind"};
}

[[nodiscard]] std::optional<FootprintDefId>
find_footprint_definition(const std::vector<FootprintDefinition> &definitions,
                          const FootprintRef &ref) {
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        if (definitions[index].ref() == ref) {
            return FootprintDefId{index};
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<FootprintDefinition>
collect_footprint_definitions(const Board &board, const FootprintLibrary &footprints) {
    auto definitions = std::vector<FootprintDefinition>{};
    definitions.reserve(board.all<volt::FootprintDefId>().size());
    for (std::size_t index = 0; index < board.all<volt::FootprintDefId>().size(); ++index) {
        definitions.push_back(board.get(FootprintDefId{index}));
    }

    for (const auto &definition : footprints.definitions()) {
        const auto existing = find_footprint_definition(definitions, definition.ref());
        if (existing.has_value() && ::volt::queries::footprint_definition_conflicts(
                                        definitions[existing->index()], definition)) {
            throw KernelLogicError{
                ErrorCode::InvalidState,
                "Board footprint definition conflicts with footprint library definition"};
        }
    }

    for (std::size_t index = 0; index < board.all<volt::ComponentPlacementId>().size(); ++index) {
        const auto &placement = board.get(ComponentPlacementId{index});
        const auto &selected_part =
            volt::queries::selected_physical_part(board.circuit(), placement.component());
        if (!selected_part.has_value()) {
            continue;
        }
        if (find_footprint_definition(definitions, selected_part->footprint()).has_value()) {
            continue;
        }

        const auto resolution = resolve_footprint(selected_part.value(), footprints);
        const auto *definition = resolution.definition();
        if (definition != nullptr) {
            definitions.push_back(*definition);
        }
    }

    return definitions;
}

[[nodiscard]] FootprintLibrary
footprint_library_from_definitions(const std::vector<FootprintDefinition> &definitions) {
    auto library = FootprintLibrary{};
    for (const auto &definition : definitions) {
        library.add(definition);
    }
    return library;
}

void write_number(std::ostream &out, double value) { write_json_number(out, value); }

void write_board_point(std::ostream &out, BoardPoint point) {
    out << '[';
    write_number(out, point.x_mm());
    out << ", ";
    write_number(out, point.y_mm());
    out << ']';
}

void write_diagnostic_point(std::ostream &out, DiagnosticPoint point) {
    out << '[';
    write_number(out, point.x_mm);
    out << ", ";
    write_number(out, point.y_mm);
    out << ']';
}

void write_diagnostic_points(std::ostream &out, const std::vector<DiagnosticPoint> &points) {
    out << '[';
    for (std::size_t index = 0; index < points.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        write_diagnostic_point(out, points[index]);
    }
    out << ']';
}

void write_entity_refs(std::ostream &out, const std::vector<EntityRef> &entities) {
    out << '[';
    for (std::size_t index = 0; index < entities.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        out << json_string(entity_ref_serialized_id(entities[index]));
    }
    out << ']';
}

void write_board_layer_refs(std::ostream &out, const std::vector<BoardLayerId> &layers) {
    out << '[';
    for (std::size_t index = 0; index < layers.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        out << json_string(encode_local_id(layers[index]));
    }
    out << ']';
}

void write_board_points(std::ostream &out, const std::vector<BoardPoint> &points) {
    out << '[';
    for (std::size_t index = 0; index < points.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        write_board_point(out, points[index]);
    }
    out << ']';
}

void write_board_layers(std::ostream &out, const std::vector<BoardLayerId> &layers) {
    out << '[';
    for (std::size_t index = 0; index < layers.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        out << json_string(encode_local_id(layers[index]));
    }
    out << ']';
}

void write_layers(std::ostream &out, const Board &board) {
    out << "    \"layers\": [\n";
    for (std::size_t index = 0; index < board.all<volt::BoardLayerId>().size(); ++index) {
        const auto id = BoardLayerId{index};
        const auto &layer = board.get(id);
        out << "      {\"id\": " << json_string(encode_local_id(id))
            << ", \"name\": " << json_string(layer.name())
            << ", \"role\": " << json_string(board_layer_role_name(layer.role()))
            << ", \"side\": " << json_string(board_layer_side_name(layer.side()))
            << ", \"thickness_mm\": ";
        write_number(out, layer.thickness_mm());
        out << ", \"enabled\": " << (layer.enabled() ? "true" : "false");
        if (layer.copper_weight_oz().has_value()) {
            out << ", \"copper_weight_oz\": ";
            write_number(out, layer.copper_weight_oz().value());
        }
        out << '}';
        if (index + 1U != board.all<volt::BoardLayerId>().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ],\n";
}

void write_layer_stack(std::ostream &out, const Board &board) {
    out << "    \"layer_stack\": ";
    if (!board.layer_stack().has_value()) {
        out << "null,\n";
        return;
    }

    out << "{\"board_thickness_mm\": ";
    write_number(out, board.layer_stack()->board_thickness_mm());
    out << ", \"layers\": [";
    const auto &layers = board.layer_stack()->layers();
    for (std::size_t index = 0; index < layers.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        out << json_string(encode_local_id(layers[index]));
    }
    out << ']';
    const auto &dielectrics = board.layer_stack()->dielectrics();
    if (!dielectrics.empty()) {
        out << ", \"dielectrics\": [";
        for (std::size_t index = 0; index < dielectrics.size(); ++index) {
            if (index != 0U) {
                out << ", ";
            }
            out << "{\"thickness_mm\": ";
            write_number(out, dielectrics[index].thickness_mm());
            out << ", \"relative_permittivity\": ";
            write_number(out, dielectrics[index].relative_permittivity());
            out << '}';
        }
        out << ']';
    }
    out << "},\n";
}

void write_outline(std::ostream &out, const Board &board) {
    out << "    \"outline\": ";
    if (!board.outline().has_value()) {
        out << "null,\n";
        return;
    }

    out << "{\"kind\": \"polygon\", \"vertices\": [";
    const auto &vertices = board.outline()->vertices();
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        write_board_point(out, vertices[index]);
    }
    out << "]},\n";
}

void write_geometry_outline(std::ostream &out, const BoardGeometryProjection &geometry) {
    if (!geometry.outline.has_value()) {
        out << "null";
        return;
    }

    out << "{\"kind\": \"polygon\", \"vertices\": ";
    write_board_points(out, geometry.outline.value());
    out << '}';
}

void write_geometry_stackup(std::ostream &out, const BoardGeometryProjection &geometry) {
    out << '[';
    for (std::size_t index = 0; index < geometry.stackup.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        const auto &layer = geometry.stackup[index];
        out << "{\"layer\": " << json_string(encode_local_id(layer.layer))
            << ", \"order\": " << layer.order << ", \"name\": " << json_string(layer.name)
            << ", \"role\": " << json_string(board_layer_role_name(layer.role))
            << ", \"side\": " << json_string(board_layer_side_name(layer.side)) << ", \"z_mm\": ";
        write_number(out, layer.z_mm);
        out << ", \"thickness_mm\": ";
        write_number(out, layer.thickness_mm);
        out << ", \"enabled\": " << (layer.enabled ? "true" : "false") << '}';
    }
    out << ']';
}

void write_geometry_feature_common(std::ostream &out, BoardFeatureId id, BoardFeatureKind kind,
                                   const std::string &label, const std::string &role) {
    out << "\"id\": " << json_string(encode_local_id(id))
        << ", \"kind\": " << json_string(board_feature_kind_name(kind))
        << ", \"label\": " << json_string(label) << ", \"role\": " << json_string(role);
}

void write_geometry_openings(std::ostream &out, const BoardGeometryProjection &geometry) {
    out << "      \"openings\": [\n";
    for (std::size_t index = 0; index < geometry.openings.size(); ++index) {
        const auto &opening = geometry.openings[index];
        if (index != 0U) {
            out << ",\n";
        }
        out << "        {";
        if (std::holds_alternative<BoardGeometryHoleOpening>(opening.shape)) {
            const auto &hole = std::get<BoardGeometryHoleOpening>(opening.shape);
            write_geometry_feature_common(out, opening.feature, BoardFeatureKind::Hole,
                                          opening.label, opening.role);
            out << ", \"center\": ";
            write_board_point(out, hole.center);
            out << ", \"drill_diameter_mm\": ";
            write_number(out, hole.drill_diameter_mm);
            out << ", \"finished_diameter_mm\": ";
            if (hole.finished_diameter_mm.has_value()) {
                write_number(out, hole.finished_diameter_mm.value());
            } else {
                out << "null";
            }
            out << ", \"plated\": " << (hole.plated ? "true" : "false");
        } else {
            const auto &slot = std::get<BoardGeometrySlotOpening>(opening.shape);
            write_geometry_feature_common(out, opening.feature, BoardFeatureKind::Slot,
                                          opening.label, opening.role);
            out << ", \"start\": ";
            write_board_point(out, slot.start);
            out << ", \"end\": ";
            write_board_point(out, slot.end);
            out << ", \"width_mm\": ";
            write_number(out, slot.width_mm);
            out << ", \"plated\": " << (slot.plated ? "true" : "false");
        }
        out << ", \"side\": \"through_board\"}";
    }
    if (!geometry.openings.empty()) {
        out << '\n';
    }
    out << "      ],\n";
}

void write_geometry_cutouts(std::ostream &out, const BoardGeometryProjection &geometry) {
    out << "      \"cutouts\": [\n";
    for (std::size_t index = 0; index < geometry.cutouts.size(); ++index) {
        const auto &cutout = geometry.cutouts[index];
        if (index != 0U) {
            out << ",\n";
        }
        out << "        {";
        write_geometry_feature_common(out, cutout.feature, BoardFeatureKind::Cutout, cutout.label,
                                      cutout.role);
        out << ", \"outline\": ";
        write_board_points(out, cutout.outline);
        out << ", \"side\": \"through_board\"}";
    }
    if (!geometry.cutouts.empty()) {
        out << '\n';
    }
    out << "      ],\n";
}

void write_geometry_surface_features(std::ostream &out, const BoardGeometryProjection &geometry) {
    out << "      \"surface_features\": [\n";
    for (std::size_t index = 0; index < geometry.surface_features.size(); ++index) {
        const auto &feature = geometry.surface_features[index];
        if (index != 0U) {
            out << ",\n";
        }
        out << "        {";
        write_geometry_feature_common(out, feature.feature, feature.kind, feature.label,
                                      feature.role);
        out << ", \"center\": ";
        write_board_point(out, feature.center);
        out << ", \"diameter_mm\": ";
        write_number(out, feature.diameter_mm);
        out << ", \"side\": " << json_string(board_side_name(feature.side)) << '}';
    }
    if (!geometry.surface_features.empty()) {
        out << '\n';
    }
    out << "      ]\n";
}

void write_board_geometry(std::ostream &out, const Board &board) {
    const auto geometry = project_board_geometry(board);
    out << "    \"geometry\": {\n";
    out << "      \"units\": " << json_string(board_units_name(geometry.units)) << ",\n";
    out << "      \"thickness_mm\": ";
    if (geometry.thickness_mm.has_value()) {
        write_number(out, geometry.thickness_mm.value());
    } else {
        out << "null";
    }
    out << ",\n";
    out << "      \"outline\": ";
    write_geometry_outline(out, geometry);
    out << ",\n";
    out << "      \"stackup\": ";
    write_geometry_stackup(out, geometry);
    out << ",\n";
    write_geometry_openings(out, geometry);
    write_geometry_cutouts(out, geometry);
    write_geometry_surface_features(out, geometry);
    out << "    },\n";
}

[[nodiscard]] std::string clearance_kind_name(BoardClearanceKind kind) {
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
        return "board_edge";
    }
    return "track";
}

void write_rules(std::ostream &out, const Board &board) {
    const auto &rules = board.design_rules();
    out << "    \"rules\": {\"copper_clearance_mm\": ";
    write_number(out, rules.copper_clearance_mm());
    out << ", \"minimum_track_width_mm\": ";
    write_number(out, rules.minimum_track_width_mm());
    out << ", \"minimum_via_drill_diameter_mm\": ";
    write_number(out, rules.minimum_via_drill_diameter_mm());
    out << ", \"minimum_via_annular_diameter_mm\": ";
    write_number(out, rules.minimum_via_annular_diameter_mm());
    out << ", \"board_outline_clearance_mm\": ";
    write_number(out, rules.board_outline_clearance_mm());
    out << ", \"package_assembly_clearance_mm\": ";
    write_number(out, rules.package_assembly_clearance_mm());
    if (!rules.clearance_matrix().empty()) {
        out << ", \"clearance_matrix\": [";
        for (std::size_t index = 0; index < rules.clearance_matrix().size(); ++index) {
            const auto &entry = rules.clearance_matrix()[index];
            if (index != 0U) {
                out << ", ";
            }
            out << "{\"first\": " << json_string(clearance_kind_name(entry.first))
                << ", \"second\": " << json_string(clearance_kind_name(entry.second))
                << ", \"clearance_mm\": ";
            write_number(out, entry.clearance_mm);
            out << '}';
        }
        out << ']';
    }
    out << "},\n";
}

void write_placements(std::ostream &out, const Board &board,
                      const std::vector<FootprintDefinition> &definitions, bool trailing_comma) {
    out << "    \"placements\": [\n";
    for (std::size_t index = 0; index < board.all<volt::ComponentPlacementId>().size(); ++index) {
        const auto id = ComponentPlacementId{index};
        const auto &placement = board.get(id);
        const auto &selected_part =
            volt::queries::selected_physical_part(board.circuit(), placement.component());
        auto footprint = std::optional<FootprintDefId>{};
        if (selected_part.has_value()) {
            footprint = find_footprint_definition(definitions, selected_part->footprint());
        }

        out << "      {\"id\": " << json_string(encode_local_id(id))
            << ", \"component\": " << json_string(encode_local_id(placement.component()))
            << ", \"footprint\": ";
        if (footprint.has_value()) {
            out << json_string(encode_local_id(footprint.value()));
        } else {
            out << "null";
        }
        out << ", \"position\": ";
        write_board_point(out, placement.position());
        out << ", \"rotation_deg\": ";
        write_number(out, placement.rotation().degrees());
        out << ", \"side\": " << json_string(board_side_name(placement.side()))
            << ", \"locked\": " << (placement.locked() ? "true" : "false") << '}';
        if (index + 1U != board.all<volt::ComponentPlacementId>().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ]";
    if (trailing_comma) {
        out << ',';
    }
    out << '\n';
}

void write_tracks(std::ostream &out, const Board &board, bool trailing_comma) {
    out << "    \"tracks\": [\n";
    for (std::size_t index = 0; index < board.all<volt::BoardTrackId>().size(); ++index) {
        const auto id = BoardTrackId{index};
        const auto &track = board.get(id);
        out << "      {\"id\": " << json_string(encode_local_id(id))
            << ", \"net\": " << json_string(encode_local_id(track.net()))
            << ", \"layer\": " << json_string(encode_local_id(track.layer())) << ", \"points\": [";
        for (std::size_t point_index = 0; point_index < track.points().size(); ++point_index) {
            if (point_index != 0U) {
                out << ", ";
            }
            write_board_point(out, track.points()[point_index]);
        }
        out << "], \"width_mm\": ";
        write_number(out, track.width_mm());
        out << '}';
        if (index + 1U != board.all<volt::BoardTrackId>().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ]";
    if (trailing_comma) {
        out << ',';
    }
    out << '\n';
}

void write_vias(std::ostream &out, const Board &board, bool trailing_comma) {
    out << "    \"vias\": [\n";
    for (std::size_t index = 0; index < board.all<volt::BoardViaId>().size(); ++index) {
        const auto id = BoardViaId{index};
        const auto &via = board.get(id);
        out << "      {\"id\": " << json_string(encode_local_id(id))
            << ", \"net\": " << json_string(encode_local_id(via.net())) << ", \"position\": ";
        write_board_point(out, via.position());
        out << ", \"start_layer\": " << json_string(encode_local_id(via.start_layer()))
            << ", \"end_layer\": " << json_string(encode_local_id(via.end_layer()))
            << ", \"drill_diameter_mm\": ";
        write_number(out, via.drill_diameter_mm());
        out << ", \"annular_diameter_mm\": ";
        write_number(out, via.annular_diameter_mm());
        out << '}';
        if (index + 1U != board.all<volt::BoardViaId>().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ]";
    if (trailing_comma) {
        out << ',';
    }
    out << '\n';
}

void write_board_zones(std::ostream &out, const Board &board, bool trailing_comma) {
    out << "    \"zones\": [\n";
    for (std::size_t index = 0; index < board.all<volt::BoardZoneId>().size(); ++index) {
        const auto id = BoardZoneId{index};
        const auto &zone = board.get(id);
        out << "      {\"id\": " << json_string(encode_local_id(id)) << ", \"outline\": ";
        write_board_points(out, zone.outline());
        out << ", \"layers\": ";
        write_board_layers(out, zone.layers());
        out << ", \"net\": ";
        if (zone.net().has_value()) {
            out << json_string(encode_local_id(zone.net().value()));
        } else {
            out << "null";
        }
        out << ", \"fill\": " << json_string(board_zone_fill_name(zone.fill()))
            << ", \"priority\": " << zone.priority() << '}';
        if (index + 1U != board.all<volt::BoardZoneId>().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ]";
    if (trailing_comma) {
        out << ',';
    }
    out << '\n';
}

void write_board_keepouts(std::ostream &out, const Board &board, bool trailing_comma) {
    out << "    \"keepouts\": [\n";
    for (std::size_t index = 0; index < board.all<volt::BoardKeepoutId>().size(); ++index) {
        const auto id = BoardKeepoutId{index};
        const auto &keepout = board.get(id);
        out << "      {\"id\": " << json_string(encode_local_id(id)) << ", \"outline\": ";
        write_board_points(out, keepout.outline());
        out << ", \"layers\": ";
        write_board_layers(out, keepout.layers());
        out << ", \"restrictions\": [";
        for (std::size_t restriction_index = 0; restriction_index < keepout.restrictions().size();
             ++restriction_index) {
            if (restriction_index != 0U) {
                out << ", ";
            }
            out << json_string(
                board_keepout_restriction_name(keepout.restrictions()[restriction_index]));
        }
        out << "]}";
        if (index + 1U != board.all<volt::BoardKeepoutId>().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ]";
    if (trailing_comma) {
        out << ',';
    }
    out << '\n';
}

void write_board_texts(std::ostream &out, const Board &board, bool trailing_comma) {
    out << "    \"texts\": [\n";
    for (std::size_t index = 0; index < board.all<volt::BoardTextId>().size(); ++index) {
        const auto id = BoardTextId{index};
        const auto &text = board.get(id);
        out << "      {\"id\": " << json_string(encode_local_id(id))
            << ", \"text\": " << json_string(text.text()) << ", \"position\": ";
        write_board_point(out, text.position());
        out << ", \"rotation_deg\": ";
        write_number(out, text.rotation().degrees());
        out << ", \"layer\": " << json_string(encode_local_id(text.layer())) << ", \"size_mm\": ";
        write_number(out, text.size_mm());
        out << ", \"locked\": " << (text.locked() ? "true" : "false") << '}';
        if (index + 1U != board.all<volt::BoardTextId>().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ]";
    if (trailing_comma) {
        out << ',';
    }
    out << '\n';
}

void write_pad_resolution(std::ostream &out, const Board &board,
                          const std::vector<FootprintDefinition> &definitions,
                          const PadResolution &resolution, const FootprintDefinition &definition) {
    const auto &pad = definition.pad(resolution.pad());
    out << "      {\n";
    out << "        \"id\": "
        << json_string(pcb_pad_projection_id(resolution.placement(), resolution.pad())) << ",\n";
    out << "        \"placement\": " << json_string(encode_local_id(resolution.placement()))
        << ",\n";
    out << "        \"component\": " << json_string(encode_local_id(resolution.component()))
        << ",\n";
    const auto &selected_part =
        volt::queries::selected_physical_part(board.circuit(), resolution.component());
    const auto footprint = selected_part.has_value()
                               ? find_footprint_definition(definitions, selected_part->footprint())
                               : std::nullopt;
    out << "        \"footprint\": ";
    if (footprint.has_value()) {
        out << json_string(encode_local_id(footprint.value()));
    } else {
        out << "null";
    }
    out << ",\n";
    out << "        \"pad\": " << json_string(encode_local_id(resolution.pad())) << ",\n";
    out << "        \"label\": " << json_string(resolution.pad_label()) << ",\n";
    out << "        \"position\": ";
    write_board_point(out, resolution.position());
    out << ",\n";
    out << "        \"pin\": ";
    if (resolution.pin().has_value()) {
        out << json_string(encode_local_id(resolution.pin().value()));
    } else {
        out << "null";
    }
    out << ",\n";
    out << "        \"net\": ";
    if (resolution.net().has_value()) {
        out << json_string(encode_local_id(resolution.net().value()));
    } else {
        out << "null";
    }
    out << ",\n";
    out << "        \"status\": " << json_string(pad_resolution_status_name(resolution.status()))
        << ",\n";
    out << "        \"geometry\": {";
    out << "\"kind\": " << json_string(footprint_pad_kind_name(pad.kind())) << ", ";
    out << "\"shape\": " << json_string(footprint_pad_shape_name(pad.shape())) << ", ";
    out << "\"size\": ";
    write_footprint_size(out, pad.size());
    out << ", \"layers\": ";
    write_footprint_layers(out, pad.layers());
    out << ", \"drill\": ";
    write_drill(out, pad.drill());
    out << ", \"mechanical_role\": ";
    write_mechanical_role(out, pad.mechanical_role());
    out << "}\n";
    out << "      }";
}

void write_diagnostic(std::ostream &out, const Diagnostic &diagnostic) {
    out << "      {\"severity\": " << json_string(severity_name(diagnostic.severity()))
        << ", \"category\": " << json_string(diagnostic.category().value())
        << ", \"code\": " << json_string(diagnostic.code().value())
        << ", \"message\": " << json_string(diagnostic.message()) << ", \"entities\": ";
    write_entity_refs(out, diagnostic.entities());
    out << ", \"overlays\": [";
    for (std::size_t index = 0; index < diagnostic.overlays().size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        const auto &overlay = diagnostic.overlays()[index];
        out << "{\"kind\": " << json_string(overlay_kind_name(overlay.kind())) << ", \"points\": ";
        write_diagnostic_points(out, overlay.points());
        out << ", \"entities\": ";
        write_entity_refs(out, overlay.entities());
        out << ", \"layers\": ";
        write_board_layer_refs(out, overlay.layers());
        out << '}';
    }
    out << "], \"measurement\": ";
    if (diagnostic.measurement().has_value()) {
        out << "{\"actual_mm\": ";
        write_number(out, diagnostic.measurement()->actual_mm);
        out << ", \"required_mm\": ";
        write_number(out, diagnostic.measurement()->required_mm);
        out << '}';
    } else {
        out << "null";
    }
    out << ", \"rule\": ";
    if (diagnostic.rule().has_value()) {
        out << json_string(diagnostic.rule().value());
    } else {
        out << "null";
    }
    out << '}';
}

void write_viewer_layers(std::ostream &out) {
    struct ViewerLayer {
        std::string_view id;
        std::string_view kind;
        std::string_view name;
    };

    constexpr auto layers = std::array{
        ViewerLayer{"viewer_layer:board_outline", "board_outline", "Board outline"},
        ViewerLayer{"viewer_layer:copper", "copper", "Copper"},
        ViewerLayer{"viewer_layer:pads", "pads", "Pads"},
        ViewerLayer{"viewer_layer:package_bodies", "package_bodies", "Package bodies"},
        ViewerLayer{"viewer_layer:package_courtyards", "package_courtyards", "Package courtyards"},
        ViewerLayer{"viewer_layer:package_fabrication", "package_fabrication",
                    "Fabrication outlines"},
        ViewerLayer{"viewer_layer:package_assembly", "package_assembly", "Assembly outlines"},
        ViewerLayer{"viewer_layer:annotations", "annotations", "Annotations"},
        ViewerLayer{"viewer_layer:diagnostics", "diagnostics", "Diagnostics"},
    };

    out << "    \"layers\": [\n";
    for (std::size_t index = 0; index < layers.size(); ++index) {
        const auto &layer = layers[index];
        out << "      {\"id\": " << json_string(layer.id)
            << ", \"kind\": " << json_string(layer.kind)
            << ", \"name\": " << json_string(layer.name) << '}';
        if (index + 1U != layers.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ],\n";
}

void write_viewer(std::ostream &out, const Board &board,
                  const std::vector<FootprintDefinition> &definitions) {
    const auto footprint_library = footprint_library_from_definitions(definitions);
    const auto resolutions = queries::resolve_pads(board, footprint_library);
    const auto diagnostics = validate_board(board, footprint_library);

    out << "  \"viewer\": {\n";
    write_viewer_layers(out);
    out << "    \"pad_resolutions\": [\n";
    for (std::size_t index = 0; index < resolutions.size(); ++index) {
        const auto &resolution = resolutions[index];
        const auto &selected_part =
            volt::queries::selected_physical_part(board.circuit(), resolution.component());
        const auto footprint_id =
            selected_part.has_value()
                ? find_footprint_definition(definitions, selected_part->footprint())
                : std::nullopt;
        if (!footprint_id.has_value()) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Resolved PCB pad references missing footprint definition"};
        }
        write_pad_resolution(out, board, definitions, resolution,
                             definitions[footprint_id->index()]);
        if (index + 1U != resolutions.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ],\n";
    out << "    \"diagnostics\": [\n";
    for (std::size_t index = 0; index < diagnostics.diagnostics().size(); ++index) {
        write_diagnostic(out, diagnostics.diagnostics()[index]);
        if (index + 1U != diagnostics.diagnostics().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ]\n";
    out << "  }\n";
}

} // namespace volt::io::detail

namespace volt::io {

void write_pcb_board(std::ostream &out, const Board &board, const FootprintLibrary &footprints) {
    const auto definitions = detail::collect_footprint_definitions(board, footprints);
    out << "{\n";
    out << "  \"format\": " << detail::json_string(pcb_format_name()) << ",\n";
    out << "  \"version\": " << pcb_format_version() << ",\n";
    out << "  \"board\": {\n";
    // v1 stores one board per document; this stable ID anchors viewer references.
    out << "    \"id\": \"board:0\",\n";
    out << "    \"name\": " << detail::json_string(board.name().value()) << ",\n";
    out << "    \"units\": " << detail::json_string(detail::board_units_name(board.units()))
        << ",\n";
    detail::write_rules(out, board);
    if (board.capability_profile().has_value()) {
        out << "    \"capability_profile\": ";
        detail::write_capability_profile_payload(out, board.capability_profile().value());
        out << ",\n";
    }
    detail::write_layers(out, board);
    detail::write_layer_stack(out, board);
    detail::write_outline(out, board);
    detail::write_board_geometry(out, board);
    detail::write_features(out, board);
    detail::write_footprint_definitions(out, definitions);
    detail::write_placements(out, board, definitions,
                             board.all<volt::BoardTrackId>().size() != 0U ||
                                 board.all<volt::BoardViaId>().size() != 0U ||
                                 board.all<volt::BoardZoneId>().size() != 0U ||
                                 board.all<volt::BoardKeepoutId>().size() != 0U ||
                                 board.all<volt::BoardRoomId>().size() != 0U ||
                                 board.all<volt::BoardTextId>().size() != 0U);
    if (board.all<volt::BoardTrackId>().size() != 0U) {
        detail::write_tracks(out, board,
                             board.all<volt::BoardViaId>().size() != 0U ||
                                 board.all<volt::BoardZoneId>().size() != 0U ||
                                 board.all<volt::BoardKeepoutId>().size() != 0U ||
                                 board.all<volt::BoardRoomId>().size() != 0U ||
                                 board.all<volt::BoardTextId>().size() != 0U);
    }
    if (board.all<volt::BoardViaId>().size() != 0U) {
        detail::write_vias(out, board,
                           board.all<volt::BoardZoneId>().size() != 0U ||
                               board.all<volt::BoardKeepoutId>().size() != 0U ||
                               board.all<volt::BoardRoomId>().size() != 0U ||
                               board.all<volt::BoardTextId>().size() != 0U);
    }
    if (board.all<volt::BoardZoneId>().size() != 0U) {
        detail::write_board_zones(out, board,
                                  board.all<volt::BoardKeepoutId>().size() != 0U ||
                                      board.all<volt::BoardRoomId>().size() != 0U ||
                                      board.all<volt::BoardTextId>().size() != 0U);
    }
    if (board.all<volt::BoardKeepoutId>().size() != 0U) {
        detail::write_board_keepouts(out, board,
                                     board.all<volt::BoardRoomId>().size() != 0U ||
                                         board.all<volt::BoardTextId>().size() != 0U);
    }
    if (board.all<volt::BoardRoomId>().size() != 0U) {
        detail::write_board_rooms(out, board, board.all<volt::BoardTextId>().size() != 0U);
    }
    if (board.all<volt::BoardTextId>().size() != 0U) {
        detail::write_board_texts(out, board, false);
    }
    out << "  },\n";
    detail::write_viewer(out, board, definitions);
    out << "}\n";
}

[[nodiscard]] std::string write_pcb_board(const Board &board, const FootprintLibrary &footprints) {
    auto out = std::ostringstream{};
    write_pcb_board(out, board, footprints);
    return out.str();
}

} // namespace volt::io
