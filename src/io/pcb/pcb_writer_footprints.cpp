#include <volt/io/pcb/pcb_writer.hpp>

namespace volt::io::detail {

void write_footprint_point(std::ostream &out, FootprintPoint point) {
    out << '[';
    write_number(out, point.x_mm());
    out << ", ";
    write_number(out, point.y_mm());
    out << ']';
}

void write_footprint_polygon(std::ostream &out, const FootprintPolygon &polygon) {
    out << '[';
    for (std::size_t index = 0; index < polygon.vertices().size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        write_footprint_point(out, polygon.vertices()[index]);
    }
    out << ']';
}

void write_footprint_marking(std::ostream &out, FootprintMarkingId marking_id,
                             const FootprintMarking &marking) {
    out << "{\"id\": " << json_string(encode_local_id(marking_id))
        << ", \"kind\": " << json_string(footprint_marking_kind_name(marking.kind()))
        << ", \"polygon\": ";
    write_footprint_polygon(out, marking.polygon());
    out << '}';
}

void write_footprint_markings(std::ostream &out, const std::vector<FootprintMarking> &markings) {
    out << '[';
    for (std::size_t index = 0; index < markings.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        write_footprint_marking(out, FootprintMarkingId{index}, markings[index]);
    }
    out << ']';
}

void write_footprint_size(std::ostream &out, FootprintSize size) {
    out << '[';
    write_number(out, size.width_mm());
    out << ", ";
    write_number(out, size.height_mm());
    out << ']';
}

void write_footprint_ref(std::ostream &out, const FootprintRef &ref) {
    out << "{\"library\": " << json_string(ref.library())
        << ", \"name\": " << json_string(ref.name()) << '}';
}

void write_footprint_layers(std::ostream &out, const FootprintLayerSet &layers) {
    out << '[';
    for (std::size_t index = 0; index < layers.layers().size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        out << json_string(footprint_layer_name(layers.layers()[index]));
    }
    out << ']';
}

void write_drill(std::ostream &out, const std::optional<FootprintDrill> &drill) {
    if (!drill.has_value()) {
        out << "null";
        return;
    }

    out << "{\"diameter_mm\": ";
    write_number(out, drill->diameter_mm());
    out << ", \"plating\": " << json_string(footprint_pad_plating_name(drill->plating())) << '}';
}

void write_mechanical_role(std::ostream &out,
                           const std::optional<FootprintPadMechanicalRole> &mechanical_role) {
    if (!mechanical_role.has_value()) {
        out << "null";
        return;
    }
    out << json_string(footprint_pad_mechanical_role_name(mechanical_role.value()));
}

void write_pad_geometry_fields(std::ostream &out, const FootprintPad &pad) {
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

void write_footprint_definitions(std::ostream &out,
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
        out << "        ]";
        if (definition.courtyard().has_value()) {
            out << ",\n";
            out << "        \"courtyard\": ";
            write_footprint_polygon(out, definition.courtyard().value());
        }
        if (definition.body().has_value()) {
            out << ",\n";
            out << "        \"body\": ";
            write_footprint_polygon(out, definition.body().value());
        }
        if (definition.fabrication_outline().has_value()) {
            out << ",\n";
            out << "        \"fabrication_outline\": ";
            write_footprint_polygon(out, definition.fabrication_outline().value());
        }
        if (definition.assembly_outline().has_value()) {
            out << ",\n";
            out << "        \"assembly_outline\": ";
            write_footprint_polygon(out, definition.assembly_outline().value());
        }
        if (!definition.markings().empty()) {
            out << ",\n";
            out << "        \"markings\": ";
            write_footprint_markings(out, definition.markings());
        }
        out << '\n';
        out << "      }";
        if (definition_index + 1U != definitions.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ],\n";
}

} // namespace volt::io::detail
