#include <volt/io/part_definition_writer.hpp>

#include <sstream>

#include <volt/io/logical_circuit_writer.hpp>

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
            << ", \"hash\": " << detail::json_string(symbol.hash().value()) << " }";
        if (index + 1U != symbols.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ]";
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
    out << "    \"footprint\": { \"library\": "
        << detail::json_string(part.footprint().footprint().library())
        << ", \"name\": " << detail::json_string(part.footprint().footprint().name())
        << ", \"hash\": " << detail::json_string(part.footprint().hash().value()) << " },\n";
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
