#include <volt/io/parts/part_definition_writer.hpp>

#include <sstream>

#include <volt/core/errors.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/io/parts/electrical_records_io.hpp>

namespace volt::io {

namespace {

void write_identity(std::ostream &out, const PartIdentity &identity) {
    out << "  \"identity\": {\n";
    out << "    \"namespace\": " << detail::json_string(identity.namespace_name()) << ",\n";
    out << "    \"name\": " << detail::json_string(identity.name()) << ",\n";
    out << "    \"version\": " << detail::json_string(identity.version()) << "\n";
    out << "  },\n";
}

void write_embedded_electrical_records(std::ostream &out, const ElectricalRecordSet &records) {
    const auto bytes = write_electrical_records(records);
    out << "  \"electrical_records\": ";
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const auto character = bytes[index];
        if (character == '\n') {
            if (index + 1U != bytes.size()) {
                out << "\n  ";
            }
            continue;
        }
        out << character;
    }
    out << ",\n";
}

void write_pin_terminal_mappings(std::ostream &out, const PartDefinition &part) {
    out << "  \"pin_terminal_mappings\": [\n";
    for (std::size_t index = 0; index < part.pin_terminal_mappings().size(); ++index) {
        const auto &mapping = part.pin_terminal_mappings()[index];
        out << "    { \"pin_key\": " << detail::json_string(mapping.pin().value())
            << ", \"terminals\": [";
        for (std::size_t terminal_index = 0; terminal_index < mapping.terminals().size();
             ++terminal_index) {
            if (terminal_index != 0U) {
                out << ", ";
            }
            out << detail::json_string(mapping.terminals()[terminal_index].value());
        }
        out << "] }";
        if (index + 1U != part.pin_terminal_mappings().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";
}

[[nodiscard]] std::string_view terminal_disposition_name(PackageTerminalDisposition disposition) {
    switch (disposition) {
    case PackageTerminalDisposition::NoConnect:
        return "no_connect";
    case PackageTerminalDisposition::NonElectrical:
        return "non_electrical";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled package terminal disposition"};
}

void write_terminal_dispositions(std::ostream &out, const PartDefinition &part) {
    out << "  \"terminal_dispositions\": [\n";
    for (std::size_t index = 0; index < part.terminal_dispositions().size(); ++index) {
        const auto &terminal = part.terminal_dispositions()[index];
        out << "    { \"terminal\": " << detail::json_string(terminal.terminal().value())
            << ", \"disposition\": "
            << detail::json_string(terminal_disposition_name(terminal.disposition())) << " }";
        if (index + 1U != part.terminal_dispositions().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";
}

void write_provenance(std::ostream &out, const PartProvenance &provenance) {
    out << "  \"provenance\": {\n";
    out << "    \"datasheet\": " << detail::json_string(provenance.datasheet()) << ",\n";
    out << "    \"authored_by\": " << detail::json_string(provenance.authored_by()) << ",\n";
    out << "    \"derived_from\": " << detail::json_string(provenance.derived_from()) << "\n";
    out << "  },\n";
}

void write_schematic_assets(std::ostream &out, const PartDefinition &part) {
    out << "  \"schematic_assets\": [\n";
    for (std::size_t index = 0; index < part.schematic_assets().size(); ++index) {
        const auto &asset = part.schematic_assets()[index];
        out << "    { \"name\": " << detail::json_string(asset.name())
            << ", \"variant\": " << detail::json_string(asset.variant())
            << ", \"hash\": " << detail::json_string(asset.hash().value()) << " }";
        if (index + 1U != part.schematic_assets().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";
}

[[nodiscard]] std::string_view part_footprint_pad_role_name(PartFootprintPadRole role) {
    switch (role) {
    case PartFootprintPadRole::Mechanical:
        return "mechanical";
    case PartFootprintPadRole::Thermal:
        return "thermal";
    case PartFootprintPadRole::NonElectrical:
        return "non_electrical";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled part footprint pad role"};
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
    out << ",\n      " << detail::json_string(name) << ": [\n";
    for (std::size_t index = 0; index < polygon.vertices().size(); ++index) {
        const auto point = polygon.vertices()[index];
        out << "        { \"x_mm\": ";
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

[[nodiscard]] std::string_view part_footprint_marking_kind_name(PartFootprintMarkingKind kind) {
    switch (kind) {
    case PartFootprintMarkingKind::Silkscreen:
        return "silkscreen";
    case PartFootprintMarkingKind::Polarity:
        return "polarity";
    case PartFootprintMarkingKind::PinOne:
        return "pin_1";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled part footprint marking kind"};
}

void write_footprint_markings(std::ostream &out,
                              const std::vector<PartFootprintMarking> &markings) {
    out << ",\n      \"markings\": [\n";
    for (std::size_t index = 0; index < markings.size(); ++index) {
        const auto &marking = markings[index];
        out << "        { \"kind\": "
            << detail::json_string(part_footprint_marking_kind_name(marking.kind()))
            << ", \"polygon\": [\n";
        for (std::size_t point_index = 0; point_index < marking.polygon().vertices().size();
             ++point_index) {
            const auto point = marking.polygon().vertices()[point_index];
            out << "          { \"x_mm\": ";
            detail::write_json_number(out, point.x_mm());
            out << ", \"y_mm\": ";
            detail::write_json_number(out, point.y_mm());
            out << " }";
            if (point_index + 1U != marking.polygon().vertices().size()) {
                out << ',';
            }
            out << '\n';
        }
        out << "        ] }";
        if (index + 1U != markings.size()) {
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
        if (index != 0U) {
            out << ", ";
        }
        detail::write_json_number(out, model.translation_mm()[index]);
    }
    out << "],\n      \"rotation_deg\": ";
    detail::write_json_number(out, model.rotation_deg());
    out << "\n    }\n";
}

void write_terminal_pad_mappings(std::ostream &out, const OrderablePart &part) {
    out << "    \"terminal_pad_mappings\": [\n";
    for (std::size_t index = 0; index < part.terminal_pad_mappings().size(); ++index) {
        const auto &mapping = part.terminal_pad_mappings()[index];
        out << "      { \"terminal\": " << detail::json_string(mapping.terminal().value())
            << ", \"pads\": [";
        for (std::size_t pad_index = 0; pad_index < mapping.pads().size(); ++pad_index) {
            if (pad_index != 0U) {
                out << ", ";
            }
            out << detail::json_string(mapping.pads()[pad_index].value());
        }
        out << "] }";
        if (index + 1U != part.terminal_pad_mappings().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "    ],\n";
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
        write_footprint_polygon(out, "courtyard", *part.footprint_courtyard());
    }
    if (part.footprint_body().has_value()) {
        write_footprint_polygon(out, "body", *part.footprint_body());
    }
    if (part.footprint_fabrication_outline().has_value()) {
        write_footprint_polygon(out, "fabrication_outline", *part.footprint_fabrication_outline());
    }
    if (part.footprint_assembly_outline().has_value()) {
        write_footprint_polygon(out, "assembly_outline", *part.footprint_assembly_outline());
    }
    if (!part.footprint_markings().empty()) {
        write_footprint_markings(out, part.footprint_markings());
    }
    out << "\n    },\n";
    write_terminal_pad_mappings(out, part);
    out << "    \"approved_alternate_mpns\": [";
    for (std::size_t index = 0; index < part.approved_alternate_mpns().size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        out << detail::json_string(part.approved_alternate_mpns()[index]);
    }
    out << ']';
    if (part.model_3d().has_value()) {
        out << ",\n";
        write_model_3d(out, *part.model_3d());
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
    out << "  \"content_identity\": " << detail::json_string(part.content_identity().value())
        << ",\n";
    out << "  \"implements\": " << detail::json_string(part.implemented_component().value())
        << ",\n";
    write_identity(out, part.identity());
    write_embedded_electrical_records(out, part.electrical_records());
    write_pin_terminal_mappings(out, part);
    write_terminal_dispositions(out, part);
    write_provenance(out, part.provenance());
    write_schematic_assets(out, part);
    write_orderable_part(out, part.orderable_part());
    out << "}\n";
}

std::string write_part_definition(const PartDefinition &part) {
    auto out = std::ostringstream{};
    write_part_definition(out, part);
    return out.str();
}

ContentHash part_definition_content_hash(const PartDefinition &part) {
    return sha256_content_hash(write_part_definition(part));
}

} // namespace volt::io
