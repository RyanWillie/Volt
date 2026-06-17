#include <volt/io/parts/part_definition_writer.hpp>

#include <sstream>
#include <stdexcept>

#include <volt/io/logical/logical_circuit_writer.hpp>

namespace volt::io {

namespace {

void write_identity(std::ostream &out, const PartIdentity &identity) {
    out << "  \"identity\": {\n";
    out << "    \"namespace\": " << detail::json_string(identity.namespace_name()) << ",\n";
    out << "    \"name\": " << detail::json_string(identity.name()) << ",\n";
    out << "    \"version\": " << detail::json_string(identity.version()) << "\n";
    out << "  },\n";
}

void write_pin(std::ostream &out, const PartPin &pin) {
    const auto &definition = pin.definition();
    out << "    { \"name\": " << detail::json_string(definition.name())
        << ", \"number\": " << detail::json_string(definition.number())
        << ", \"connection_requirement\": "
        << detail::json_string(
               detail::connection_requirement_name(definition.connection_requirement()));
    detail::write_pin_definition_semantics(out, definition);
    if (!pin.electrical_attributes().empty()) {
        out << ", \"electrical_attributes\": ";
        detail::write_electrical_attributes(out, pin.electrical_attributes(), "        ", "      ");
    }
    out << " }";
}

void write_pins(std::ostream &out, const PartDefinition &part) {
    out << "  \"pins\": [\n";
    for (std::size_t index = 0; index < part.pins().size(); ++index) {
        write_pin(out, part.pins()[index]);
        if (index + 1U != part.pins().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ]";
}

void write_provenance(std::ostream &out, const PartProvenance &provenance) {
    out << "  \"provenance\": {\n";
    auto wrote = false;
    const auto write_field = [&](std::string_view name, const std::string &value) {
        if (value.empty()) {
            return;
        }
        if (wrote) {
            out << ",\n";
        }
        wrote = true;
        out << "    " << detail::json_string(name) << ": " << detail::json_string(value);
    };
    write_field("authored_by", provenance.authored_by());
    write_field("datasheet", provenance.datasheet());
    write_field("derived_from", provenance.derived_from());
    out << "\n  }";
}

void write_symbols(std::ostream &out, const std::vector<HashedSchematicSymbolReference> &symbols) {
    out << "  \"symbols\": [\n";
    for (std::size_t index = 0; index < symbols.size(); ++index) {
        const auto &symbol = symbols[index];
        out << "    { \"name\": " << detail::json_string(symbol.name())
            << ", \"variant\": " << detail::json_string(symbol.variant())
            << ", \"hash\": " << detail::json_string(symbol.hash().value()) << ", \"pins\": [";
        for (std::size_t pin_index = 0; pin_index < symbol.pins().size(); ++pin_index) {
            const auto &pin = symbol.pins()[pin_index];
            if (pin_index != 0U) {
                out << ", ";
            }
            out << "{ \"name\": " << detail::json_string(pin.name())
                << ", \"number\": " << detail::json_string(pin.number()) << " }";
        }
        out << "] }";
        if (index + 1U != symbols.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ]";
}

[[nodiscard]] std::string_view part_footprint_pad_role_name(PartFootprintPadRole role) {
    switch (role) {
    case PartFootprintPadRole::Mechanical:
        return "mechanical";
    case PartFootprintPadRole::Thermal:
        return "thermal";
    }
    throw std::logic_error{"Unhandled part footprint pad role"};
}

void write_footprint_pad(std::ostream &out, const PartFootprintPad &pad) {
    out << "      { \"label\": " << detail::json_string(pad.label()) << ", \"x_mm\": ";
    detail::write_json_number(out, pad.x_mm());
    out << ", \"y_mm\": ";
    detail::write_json_number(out, pad.y_mm());
    out << ", \"width_mm\": ";
    detail::write_json_number(out, pad.width_mm());
    out << ", \"height_mm\": ";
    detail::write_json_number(out, pad.height_mm());
    if (pad.role().has_value()) {
        out << ", \"role\": " << detail::json_string(part_footprint_pad_role_name(*pad.role()));
    }
    out << " }";
}

void write_footprint_pads(std::ostream &out, const std::vector<PartFootprintPad> &pads) {
    out << "      \"pads\": [\n";
    for (std::size_t index = 0; index < pads.size(); ++index) {
        write_footprint_pad(out, pads[index]);
        if (index + 1U != pads.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "      ]";
}

void write_footprint_polygon(std::ostream &out, std::string_view name,
                             const PartFootprintPolygon &polygon) {
    out << ",\n";
    out << "      " << detail::json_string(name) << ": [\n";
    for (std::size_t index = 0; index < polygon.vertices().size(); ++index) {
        const auto point = polygon.vertices()[index];
        out << "      { \"x_mm\": ";
        detail::write_json_number(out, point.x_mm());
        out << ", \"y_mm\": ";
        detail::write_json_number(out, point.y_mm());
        out << " }";
        if (index + 1U != polygon.vertices().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "      ]";
}

void write_model_3d(std::ostream &out, const PartModel3DReference &model) {
    out << "    \"model_3d\": {\n";
    out << "      \"format\": " << detail::json_string(model.format()) << ",\n";
    out << "      \"file_name\": " << detail::json_string(model.file_name()) << ",\n";
    out << "      \"hash\": " << detail::json_string(model.hash().value()) << ",\n";
    out << "      \"translation_mm\": [";
    for (std::size_t index = 0; index < model.translation_mm().size(); ++index) {
        detail::write_json_number(out, model.translation_mm()[index]);
        if (index + 1U != model.translation_mm().size()) {
            out << ", ";
        }
    }
    out << "],\n";
    out << "      \"rotation_deg\": ";
    detail::write_json_number(out, model.rotation_deg());
    out << "\n";
    out << "    }\n";
}

void write_orderable_part(std::ostream &out, const OrderablePart &part) {
    out << "  \"orderable_part\": {\n";
    out << "    \"manufacturer\": " << detail::json_string(part.manufacturer_part().manufacturer())
        << ",\n";
    out << "    \"mpn\": " << detail::json_string(part.manufacturer_part().part_number()) << ",\n";
    out << "    \"package\": " << detail::json_string(part.package().value()) << ",\n";
    out << "    \"footprint\": {\n";
    out << "      \"library\": " << detail::json_string(part.footprint().footprint().library())
        << ",\n";
    out << "      \"name\": " << detail::json_string(part.footprint().footprint().name()) << ",\n";
    out << "      \"hash\": " << detail::json_string(part.footprint().hash().value()) << ",\n";
    write_footprint_pads(out, part.footprint_pads());
    if (part.footprint_courtyard().has_value()) {
        write_footprint_polygon(out, "courtyard", part.footprint_courtyard().value());
    }
    if (part.footprint_body().has_value()) {
        write_footprint_polygon(out, "body", part.footprint_body().value());
    }
    out << '\n';
    out << "    },\n";
    out << "    \"pin_pad_mappings\": [\n";
    for (std::size_t index = 0; index < part.pin_pad_mappings().size(); ++index) {
        const auto &mapping = part.pin_pad_mappings()[index];
        out << "      { \"pin_number\": " << detail::json_string(mapping.pin_number())
            << ", \"pad\": " << detail::json_string(mapping.pad()) << " }";
        if (index + 1U != part.pin_pad_mappings().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ],\n";
    out << "    \"approved_alternate_mpns\": [";
    for (std::size_t index = 0; index < part.approved_alternate_mpns().size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        out << detail::json_string(part.approved_alternate_mpns()[index]);
    }
    out << "]";
    if (part.model_3d().has_value()) {
        out << ",\n";
        write_model_3d(out, part.model_3d().value());
    } else {
        out << '\n';
    }
    out << "  }\n";
}

} // namespace

void write_part_definition(std::ostream &out, const PartDefinition &part) {
    out << "{\n";
    out << "  \"format\": " << detail::json_string(part_definition_format_name()) << ",\n";
    out << "  \"version\": " << part_definition_format_version() << ",\n";
    write_identity(out, part.identity());
    write_pins(out, part);
    out << ",\n";
    if (!part.electrical_attributes().empty()) {
        out << "  \"electrical_attributes\": ";
        detail::write_electrical_attributes(out, part.electrical_attributes(), "    ", "  ");
        out << ",\n";
    }
    if (!part.provenance().empty()) {
        write_provenance(out, part.provenance());
        out << ",\n";
    }
    write_symbols(out, part.symbols());
    out << ",\n";
    write_orderable_part(out, part.orderable_part());
    out << "}\n";
}

[[nodiscard]] std::string write_part_definition(const PartDefinition &part) {
    auto out = std::ostringstream{};
    write_part_definition(out, part);
    return out.str();
}

[[nodiscard]] ContentHash part_definition_content_hash(const PartDefinition &part) {
    return sha256_content_hash(write_part_definition(part));
}

} // namespace volt::io
