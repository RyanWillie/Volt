#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <volt/io/detail/typed_id.hpp>
#include <volt/io/logical_circuit_writer.hpp>
#include <volt/io/pcb_schema.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints.hpp>

namespace volt::io {

namespace detail {

[[nodiscard]] inline std::string severity_name(Severity severity) {
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

[[nodiscard]] inline std::string entity_ref_id(EntityRef entity) {
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

[[nodiscard]] inline std::optional<FootprintDefId>
find_footprint_definition(const std::vector<FootprintDefinition> &definitions,
                          const FootprintRef &ref) {
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        if (definitions[index].ref() == ref) {
            return FootprintDefId{index};
        }
    }
    return std::nullopt;
}

[[nodiscard]] inline std::vector<FootprintDefinition>
collect_footprint_definitions(const Board &board, const FootprintLibrary &footprints) {
    auto definitions = std::vector<FootprintDefinition>{};
    definitions.reserve(board.footprint_definition_count());
    for (std::size_t index = 0; index < board.footprint_definition_count(); ++index) {
        definitions.push_back(board.footprint_definition(FootprintDefId{index}));
    }

    for (std::size_t index = 0; index < board.placement_count(); ++index) {
        const auto &placement = board.placement(ComponentPlacementId{index});
        const auto &selected_part = board.circuit().selected_physical_part(placement.component());
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

[[nodiscard]] inline FootprintLibrary
footprint_library_from_definitions(const std::vector<FootprintDefinition> &definitions) {
    auto library = FootprintLibrary{};
    for (const auto &definition : definitions) {
        library.add(definition);
    }
    return library;
}

inline void write_number(std::ostream &out, double value) { write_json_number(out, value); }

inline void write_board_point(std::ostream &out, BoardPoint point) {
    out << '[';
    write_number(out, point.x_mm());
    out << ", ";
    write_number(out, point.y_mm());
    out << ']';
}

inline void write_board_points(std::ostream &out, const std::vector<BoardPoint> &points) {
    out << '[';
    for (std::size_t index = 0; index < points.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        write_board_point(out, points[index]);
    }
    out << ']';
}

inline void write_board_layers(std::ostream &out, const std::vector<BoardLayerId> &layers) {
    out << '[';
    for (std::size_t index = 0; index < layers.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        out << json_string(encode_local_id(layers[index]));
    }
    out << ']';
}

inline void write_footprint_point(std::ostream &out, FootprintPoint point) {
    out << '[';
    write_number(out, point.x_mm());
    out << ", ";
    write_number(out, point.y_mm());
    out << ']';
}

inline void write_footprint_size(std::ostream &out, FootprintSize size) {
    out << '[';
    write_number(out, size.width_mm());
    out << ", ";
    write_number(out, size.height_mm());
    out << ']';
}

inline void write_footprint_ref(std::ostream &out, const FootprintRef &ref) {
    out << "{\"library\": " << json_string(ref.library())
        << ", \"name\": " << json_string(ref.name()) << '}';
}

inline void write_footprint_layers(std::ostream &out, const FootprintLayerSet &layers) {
    out << '[';
    for (std::size_t index = 0; index < layers.layers().size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        out << json_string(footprint_layer_name(layers.layers()[index]));
    }
    out << ']';
}

inline void write_drill(std::ostream &out, const std::optional<FootprintDrill> &drill) {
    if (!drill.has_value()) {
        out << "null";
        return;
    }

    out << "{\"diameter_mm\": ";
    write_number(out, drill->diameter_mm());
    out << ", \"plating\": " << json_string(footprint_pad_plating_name(drill->plating())) << '}';
}

inline void
write_mechanical_role(std::ostream &out,
                      const std::optional<FootprintPadMechanicalRole> &mechanical_role) {
    if (!mechanical_role.has_value()) {
        out << "null";
        return;
    }
    out << json_string(footprint_pad_mechanical_role_name(mechanical_role.value()));
}

inline void write_pad_geometry_fields(std::ostream &out, const FootprintPad &pad) {
    out << "\"kind\": " << json_string(footprint_pad_kind_name(pad.kind())) << ",\n";
    out << "          \"shape\": " << json_string(footprint_pad_shape_name(pad.shape())) << ",\n";
    out << "          \"position\": ";
    write_footprint_point(out, pad.position());
    out << ",\n";
    out << "          \"size\": ";
    write_footprint_size(out, pad.size());
    out << ",\n";
    out << "          \"layers\": ";
    write_footprint_layers(out, pad.layers());
    out << ",\n";
    out << "          \"drill\": ";
    write_drill(out, pad.drill());
    out << ",\n";
    out << "          \"mechanical_role\": ";
    write_mechanical_role(out, pad.mechanical_role());
}

inline void write_layers(std::ostream &out, const Board &board) {
    out << "    \"layers\": [\n";
    for (std::size_t index = 0; index < board.layer_count(); ++index) {
        const auto id = BoardLayerId{index};
        const auto &layer = board.layer(id);
        out << "      {\"id\": " << json_string(encode_local_id(id))
            << ", \"name\": " << json_string(layer.name())
            << ", \"role\": " << json_string(board_layer_role_name(layer.role()))
            << ", \"side\": " << json_string(board_layer_side_name(layer.side()))
            << ", \"thickness_mm\": ";
        write_number(out, layer.thickness_mm());
        out << ", \"enabled\": " << (layer.enabled() ? "true" : "false") << '}';
        if (index + 1U != board.layer_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ],\n";
}

inline void write_layer_stack(std::ostream &out, const Board &board) {
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
    out << "]},\n";
}

inline void write_outline(std::ostream &out, const Board &board) {
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

inline void write_rules(std::ostream &out, const Board &board) {
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
    out << "},\n";
}

inline void write_features(std::ostream &out, const Board &board) {
    out << "    \"features\": [\n";
    for (std::size_t index = 0; index < board.feature_count(); ++index) {
        const auto id = BoardFeatureId{index};
        const auto &feature = board.feature(id);
        out << "      {\"id\": " << json_string(encode_local_id(id))
            << ", \"kind\": " << json_string(board_feature_kind_name(feature.kind()))
            << ", \"label\": " << json_string(feature.label()) << ", \"position\": ";
        write_board_point(out, feature.position());
        out << ", \"diameter_mm\": ";
        write_number(out, feature.diameter_mm());
        out << '}';
        if (index + 1U != board.feature_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ],\n";
}

inline void write_footprint_definitions(std::ostream &out,
                                        const std::vector<FootprintDefinition> &definitions) {
    out << "    \"footprint_definitions\": [\n";
    for (std::size_t definition_index = 0; definition_index < definitions.size();
         ++definition_index) {
        const auto definition_id = FootprintDefId{definition_index};
        const auto &definition = definitions[definition_index];
        out << "      {\n";
        out << "        \"id\": " << json_string(encode_local_id(definition_id)) << ",\n";
        out << "        \"ref\": ";
        write_footprint_ref(out, definition.ref());
        out << ",\n";
        out << "        \"pads\": [\n";
        for (std::size_t pad_index = 0; pad_index < definition.pad_count(); ++pad_index) {
            const auto pad_id = FootprintPadId{pad_index};
            const auto &pad = definition.pad(pad_id);
            out << "          {\n";
            out << "            \"id\": " << json_string(encode_local_id(pad_id)) << ",\n";
            out << "            \"label\": " << json_string(pad.label()) << ",\n";
            out << "          ";
            write_pad_geometry_fields(out, pad);
            out << '\n';
            out << "          }";
            if (pad_index + 1U != definition.pad_count()) {
                out << ',';
            }
            out << '\n';
        }
        out << "        ]\n";
        out << "      }";
        if (definition_index + 1U != definitions.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ],\n";
}

inline void write_placements(std::ostream &out, const Board &board,
                             const std::vector<FootprintDefinition> &definitions,
                             bool trailing_comma = false) {
    out << "    \"placements\": [\n";
    for (std::size_t index = 0; index < board.placement_count(); ++index) {
        const auto id = ComponentPlacementId{index};
        const auto &placement = board.placement(id);
        const auto &selected_part = board.circuit().selected_physical_part(placement.component());
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
        if (index + 1U != board.placement_count()) {
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

inline void write_tracks(std::ostream &out, const Board &board, bool trailing_comma = false) {
    out << "    \"tracks\": [\n";
    for (std::size_t index = 0; index < board.track_count(); ++index) {
        const auto id = BoardTrackId{index};
        const auto &track = board.track(id);
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
        if (index + 1U != board.track_count()) {
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

inline void write_vias(std::ostream &out, const Board &board, bool trailing_comma = false) {
    out << "    \"vias\": [\n";
    for (std::size_t index = 0; index < board.via_count(); ++index) {
        const auto id = BoardViaId{index};
        const auto &via = board.via(id);
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
        if (index + 1U != board.via_count()) {
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

inline void write_board_zones(std::ostream &out, const Board &board, bool trailing_comma = false) {
    out << "    \"zones\": [\n";
    for (std::size_t index = 0; index < board.zone_count(); ++index) {
        const auto id = BoardZoneId{index};
        const auto &zone = board.zone(id);
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
        if (index + 1U != board.zone_count()) {
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

inline void write_board_keepouts(std::ostream &out, const Board &board,
                                 bool trailing_comma = false) {
    out << "    \"keepouts\": [\n";
    for (std::size_t index = 0; index < board.keepout_count(); ++index) {
        const auto id = BoardKeepoutId{index};
        const auto &keepout = board.keepout(id);
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
        if (index + 1U != board.keepout_count()) {
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

inline void write_board_texts(std::ostream &out, const Board &board, bool trailing_comma = false) {
    out << "    \"texts\": [\n";
    for (std::size_t index = 0; index < board.text_count(); ++index) {
        const auto id = BoardTextId{index};
        const auto &text = board.text(id);
        out << "      {\"id\": " << json_string(encode_local_id(id))
            << ", \"text\": " << json_string(text.text()) << ", \"position\": ";
        write_board_point(out, text.position());
        out << ", \"rotation_deg\": ";
        write_number(out, text.rotation().degrees());
        out << ", \"layer\": " << json_string(encode_local_id(text.layer())) << ", \"size_mm\": ";
        write_number(out, text.size_mm());
        out << ", \"locked\": " << (text.locked() ? "true" : "false") << '}';
        if (index + 1U != board.text_count()) {
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

inline void write_pad_resolution(std::ostream &out, const Board &board,
                                 const std::vector<FootprintDefinition> &definitions,
                                 const PadResolution &resolution,
                                 const FootprintDefinition &definition) {
    const auto &pad = definition.pad(resolution.pad());
    out << "      {\n";
    out << "        \"id\": "
        << json_string(pcb_pad_projection_id(resolution.placement(), resolution.pad())) << ",\n";
    out << "        \"placement\": " << json_string(encode_local_id(resolution.placement()))
        << ",\n";
    out << "        \"component\": " << json_string(encode_local_id(resolution.component()))
        << ",\n";
    const auto &selected_part = board.circuit().selected_physical_part(resolution.component());
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

inline void write_diagnostic(std::ostream &out, const Diagnostic &diagnostic) {
    out << "      {\"severity\": " << json_string(severity_name(diagnostic.severity()))
        << ", \"code\": " << json_string(diagnostic.code().value())
        << ", \"message\": " << json_string(diagnostic.message()) << ", \"entities\": [";
    for (std::size_t index = 0; index < diagnostic.entities().size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        out << json_string(entity_ref_id(diagnostic.entities()[index]));
    }
    out << "]}";
}

inline void write_viewer(std::ostream &out, const Board &board,
                         const std::vector<FootprintDefinition> &definitions) {
    const auto footprint_library = footprint_library_from_definitions(definitions);
    const auto resolutions = board.resolve_pads(footprint_library);
    const auto diagnostics = validate_board(board, footprint_library);

    out << "  \"viewer\": {\n";
    out << "    \"pad_resolutions\": [\n";
    for (std::size_t index = 0; index < resolutions.size(); ++index) {
        const auto &resolution = resolutions[index];
        const auto &selected_part = board.circuit().selected_physical_part(resolution.component());
        const auto footprint_id =
            selected_part.has_value()
                ? find_footprint_definition(definitions, selected_part->footprint())
                : std::nullopt;
        if (!footprint_id.has_value()) {
            throw std::logic_error{"Resolved PCB pad references missing footprint definition"};
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

} // namespace detail

/** Write deterministic product-viewer-ready PCB projection JSON. */
inline void write_pcb_board(std::ostream &out, const Board &board,
                            const FootprintLibrary &footprints) {
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
    detail::write_layers(out, board);
    detail::write_layer_stack(out, board);
    detail::write_outline(out, board);
    detail::write_features(out, board);
    detail::write_footprint_definitions(out, definitions);
    detail::write_placements(out, board, definitions,
                             board.track_count() != 0U || board.via_count() != 0U ||
                                 board.zone_count() != 0U || board.keepout_count() != 0U ||
                                 board.text_count() != 0U);
    if (board.track_count() != 0U) {
        detail::write_tracks(out, board,
                             board.via_count() != 0U || board.zone_count() != 0U ||
                                 board.keepout_count() != 0U || board.text_count() != 0U);
    }
    if (board.via_count() != 0U) {
        detail::write_vias(out, board,
                           board.zone_count() != 0U || board.keepout_count() != 0U ||
                               board.text_count() != 0U);
    }
    if (board.zone_count() != 0U) {
        detail::write_board_zones(out, board,
                                  board.keepout_count() != 0U || board.text_count() != 0U);
    }
    if (board.keepout_count() != 0U) {
        detail::write_board_keepouts(out, board, board.text_count() != 0U);
    }
    if (board.text_count() != 0U) {
        detail::write_board_texts(out, board);
    }
    out << "  },\n";
    detail::write_viewer(out, board, definitions);
    out << "}\n";
}

/** Return deterministic product-viewer-ready PCB projection JSON. */
[[nodiscard]] inline std::string write_pcb_board(const Board &board,
                                                 const FootprintLibrary &footprints) {
    auto out = std::ostringstream{};
    write_pcb_board(out, board, footprints);
    return out.str();
}

} // namespace volt::io
