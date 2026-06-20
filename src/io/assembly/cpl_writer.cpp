#include <volt/io/assembly/cpl_writer.hpp>

#include "../detail/entity_ref_format.hpp"

#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/io/pcb/pcb_writer.hpp>

namespace volt::io {
namespace {

void write_csv_field(std::ostream &out, std::string_view value) {
    const auto needs_quotes = value.find_first_of(",\"\n\r") != std::string_view::npos;
    if (!needs_quotes) {
        out << value;
        return;
    }
    out << '"';
    for (const auto ch : value) {
        if (ch == '"') {
            out << "\"\"";
        } else {
            out << ch;
        }
    }
    out << '"';
}

[[nodiscard]] std::string board_side_name(BoardSide side) {
    switch (side) {
    case BoardSide::Top:
        return "top";
    case BoardSide::Bottom:
        return "bottom";
    }
    throw std::logic_error{"Unhandled board side"};
}

[[nodiscard]] std::string jlcpcb_layer_name(BoardSide side) {
    switch (side) {
    case BoardSide::Top:
        return "Top";
    case BoardSide::Bottom:
        return "Bottom";
    }
    throw std::logic_error{"Unhandled board side"};
}

void write_number(std::ostream &out, double value) { detail::write_json_number(out, value); }

void write_footprint(std::ostream &out, const std::optional<FootprintRef> &footprint) {
    if (!footprint.has_value()) {
        out << "null";
        return;
    }
    out << "{\"library\": " << detail::json_string(footprint->library())
        << ", \"name\": " << detail::json_string(footprint->name()) << '}';
}

void write_part_identity(std::ostream &out, const std::optional<CplPartIdentity> &part) {
    if (!part.has_value()) {
        out << "null";
        return;
    }
    out << "{\"manufacturer\": " << detail::json_string(part->manufacturer())
        << ", \"mpn\": " << detail::json_string(part->mpn())
        << ", \"package\": " << detail::json_string(part->package()) << '}';
}

void write_entity_refs(std::ostream &out, const std::vector<EntityRef> &entities) {
    out << '[';
    for (std::size_t index = 0; index < entities.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        out << detail::json_string(detail::entity_ref_serialized_id(entities[index]));
    }
    out << ']';
}

void write_diagnostic(std::ostream &out, const Diagnostic &diagnostic) {
    out << "    {\"severity\": "
        << detail::json_string(detail::severity_name(diagnostic.severity()))
        << ", \"category\": " << detail::json_string(diagnostic.category().value())
        << ", \"code\": " << detail::json_string(diagnostic.code().value())
        << ", \"message\": " << detail::json_string(diagnostic.message()) << ", \"entities\": ";
    write_entity_refs(out, diagnostic.entities());
    out << '}';
}

void write_row(std::ostream &out, const CplRow &row) {
    out << "    {\n";
    out << "      \"designator\": " << detail::json_string(row.reference()) << ",\n";
    out << "      \"component\": " << detail::json_string(detail::encode_local_id(row.component()))
        << ",\n";
    out << "      \"placement\": " << detail::json_string(detail::encode_local_id(row.placement()))
        << ",\n";
    out << "      \"footprint\": ";
    write_footprint(out, row.footprint());
    out << ",\n";
    out << "      \"side\": " << detail::json_string(board_side_name(row.side())) << ",\n";
    out << "      \"position_mm\": [";
    write_number(out, row.position().x_mm());
    out << ", ";
    write_number(out, row.position().y_mm());
    out << "],\n";
    out << "      \"authored_rotation_deg\": ";
    write_number(out, row.authored_rotation_deg());
    out << ",\n";
    out << "      \"rotation_offset_deg\": ";
    write_number(out, row.rotation_offset_deg());
    out << ",\n";
    out << "      \"rotation_deg\": ";
    write_number(out, row.rotation_deg());
    out << ",\n";
    out << "      \"part\": ";
    write_part_identity(out, row.part_identity());
    out << "\n";
    out << "    }";
}

} // namespace

void write_cpl_json(std::ostream &out, const Cpl &cpl) {
    out << "{\n";
    out << "  \"format\": " << detail::json_string(cpl_format_name()) << ",\n";
    out << "  \"version\": " << cpl_format_version() << ",\n";
    out << "  \"metadata\": {\n";
    out << "    \"units\": \"mm\",\n";
    out << "    \"origin\": {\n";
    out << "      \"convention\": \"board_origin\",\n";
    out << "      \"description\": \"Positions are board coordinates in millimeters from the "
           "authored board origin\"\n";
    out << "    },\n";
    out << "    \"rotation\": {\n";
    out << "      \"convention\": \"degrees_counterclockwise_board_coordinates\",\n";
    out << "      \"description\": \"rotation_deg is authored placement rotation plus any "
           "data-supplied footprint rotation offset, normalized to [0, 360)\",\n";
    out << "      \"includes_rotation_offsets\": true\n";
    out << "    }\n";
    out << "  },\n";
    out << "  \"rows\": [\n";
    for (std::size_t index = 0; index < cpl.rows().size(); ++index) {
        write_row(out, cpl.rows()[index]);
        if (index + 1U != cpl.rows().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";
    out << "  \"diagnostics\": [\n";
    for (std::size_t index = 0; index < cpl.diagnostics().diagnostics().size(); ++index) {
        write_diagnostic(out, cpl.diagnostics().diagnostics()[index]);
        if (index + 1U != cpl.diagnostics().diagnostics().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ]\n";
    out << "}\n";
}

[[nodiscard]] std::string write_cpl_json(const Cpl &cpl) {
    auto out = std::ostringstream{};
    write_cpl_json(out, cpl);
    return out.str();
}

void write_cpl_csv(std::ostream &out, const Cpl &cpl) {
    out << "Designator,Mid X,Mid Y,Layer,Rotation\n";
    for (const auto &row : cpl.rows()) {
        write_csv_field(out, row.reference());
        out << ',';
        write_number(out, row.position().x_mm());
        out << ',';
        write_number(out, row.position().y_mm());
        out << ',';
        write_csv_field(out, jlcpcb_layer_name(row.side()));
        out << ',';
        write_number(out, row.rotation_deg());
        out << '\n';
    }
}

[[nodiscard]] std::string write_cpl_csv(const Cpl &cpl) {
    auto out = std::ostringstream{};
    write_cpl_csv(out, cpl);
    return out.str();
}

} // namespace volt::io
