#include <volt/io/bom/bom_writer.hpp>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <volt/io/logical/logical_circuit_writer.hpp>

namespace volt::io {

namespace {

void write_string_array(std::ostream &out, const std::vector<std::string> &values) {
    out << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) {
            out << ", ";
        }
        out << detail::json_string(values[index]);
    }
    out << ']';
}

void write_selected_part(std::ostream &out, const BomSelectedPart &part, std::string_view indent) {
    out << "{\n";
    out << indent << "  \"manufacturer\": " << detail::json_string(part.manufacturer()) << ",\n";
    out << indent << "  \"mpn\": " << detail::json_string(part.mpn()) << ",\n";
    out << indent << "  \"package\": " << detail::json_string(part.package()) << ",\n";
    out << indent << "  \"approved_alternate_mpns\": ";
    write_string_array(out, part.approved_alternate_mpns());
    out << "\n" << indent << "}";
}

void write_sourcing_properties(std::ostream &out, const PropertyMap &properties);

void write_line(std::ostream &out, const BomLine &line) {
    out << "    {\n";
    out << "      \"manufacturer\": " << detail::json_string(line.manufacturer()) << ",\n";
    out << "      \"mpn\": " << detail::json_string(line.mpn()) << ",\n";
    out << "      \"package\": " << detail::json_string(line.package()) << ",\n";
    out << "      \"quantity\": " << line.quantity() << ",\n";
    out << "      \"references\": ";
    write_string_array(out, line.references());
    out << ",\n";
    out << "      \"dnp\": " << (line.dnp() ? "true" : "false") << ",\n";
    out << "      \"approved_alternate_mpns\": ";
    write_string_array(out, line.approved_alternate_mpns());
    out << ",\n";
    out << "      \"selection_override_references\": ";
    write_string_array(out, line.selection_override_references());
    out << ",\n";
    out << "      \"sourcing\": ";
    write_sourcing_properties(out, line.sourcing());
    out << "\n";
    out << "    }";
}

void write_component(std::ostream &out, const BomComponent &component) {
    out << "    {\n";
    out << "      \"component\": "
        << detail::json_string(detail::component_id(component.component())) << ",\n";
    out << "      \"reference\": " << detail::json_string(component.reference()) << ",\n";
    out << "      \"dnp\": " << (component.dnp() ? "true" : "false") << ",\n";
    out << "      \"dnp_explicit\": " << (component.dnp_explicit() ? "true" : "false") << ",\n";
    out << "      \"selection_override\": " << (component.selection_override() ? "true" : "false");
    if (component.selected_part().has_value()) {
        out << ",\n";
        out << "      \"selected_part\": ";
        write_selected_part(out, component.selected_part().value(), "      ");
        out << "\n";
    } else {
        out << "\n";
    }
    out << "    }";
}

[[nodiscard]] std::string property_value_to_string(const PropertyValue &value) {
    auto out = std::ostringstream{};
    switch (value.kind()) {
    case PropertyValueKind::String:
        return value.as_string();
    case PropertyValueKind::Boolean:
        return value.as_bool() ? "true" : "false";
    case PropertyValueKind::Integer:
        out << value.as_integer();
        return out.str();
    case PropertyValueKind::Number:
        detail::write_json_number(out, value.as_number());
        return out.str();
    }
    throw std::logic_error{"Unhandled property value kind"};
}

void write_sourcing_property_value(std::ostream &out, const PropertyValue &value) {
    switch (value.kind()) {
    case PropertyValueKind::String:
        out << detail::json_string(value.as_string());
        return;
    case PropertyValueKind::Boolean:
        out << (value.as_bool() ? "true" : "false");
        return;
    case PropertyValueKind::Integer:
        out << value.as_integer();
        return;
    case PropertyValueKind::Number:
        detail::write_json_number(out, value.as_number());
        return;
    }
    throw std::logic_error{"Unhandled property value kind"};
}

[[nodiscard]] std::vector<std::pair<PropertyKey, PropertyValue>>
sorted_property_entries(const PropertyMap &properties) {
    return {properties.entries().begin(), properties.entries().end()};
}

void write_sourcing_properties(std::ostream &out, const PropertyMap &properties) {
    out << '{';
    if (!properties.empty()) {
        out << '\n';
        const auto entries = sorted_property_entries(properties);
        auto index = std::size_t{0};
        for (const auto &[key, value] : entries) {
            out << "        " << detail::json_string(key.value()) << ": ";
            write_sourcing_property_value(out, value);
            if (++index != entries.size()) {
                out << ',';
            }
            out << '\n';
        }
        out << "      ";
    }
    out << '}';
}

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

[[nodiscard]] std::string join_strings(const std::vector<std::string> &values) {
    auto out = std::ostringstream{};
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) {
            out << ' ';
        }
        out << values[index];
    }
    return out.str();
}

[[nodiscard]] std::vector<std::string> sourcing_columns(const Bom &bom) {
    auto columns = std::vector<std::string>{};
    for (const auto &line : bom.lines()) {
        for (const auto &[key, _] : line.sourcing().entries()) {
            if (std::find(columns.begin(), columns.end(), key.value()) == columns.end()) {
                columns.push_back(key.value());
            }
        }
    }
    std::sort(columns.begin(), columns.end());
    return columns;
}

[[nodiscard]] std::vector<std::pair<std::string, PropertyMap>>
sorted_sourcing_entries(const BomSourcingSnapshot &snapshot) {
    auto entries = snapshot.entries();
    std::sort(entries.begin(), entries.end(),
              [](const auto &lhs, const auto &rhs) { return lhs.first < rhs.first; });
    return entries;
}

} // namespace

void write_bom_json(std::ostream &out, const Bom &bom) {
    out << "{\n";
    out << "  \"format\": " << detail::json_string(bom_format_name()) << ",\n";
    out << "  \"version\": " << bom_format_version() << ",\n";
    out << "  \"lines\": [\n";
    for (std::size_t index = 0; index < bom.lines().size(); ++index) {
        write_line(out, bom.lines()[index]);
        if (index + 1U != bom.lines().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";
    out << "  \"components\": [\n";
    for (std::size_t index = 0; index < bom.components().size(); ++index) {
        write_component(out, bom.components()[index]);
        if (index + 1U != bom.components().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ]\n";
    out << "}\n";
}

[[nodiscard]] std::string write_bom_json(const Bom &bom) {
    auto out = std::ostringstream{};
    write_bom_json(out, bom);
    return out.str();
}

void write_bom_csv(std::ostream &out, const Bom &bom) {
    const auto columns = sourcing_columns(bom);
    const auto base_columns = std::vector<std::string>{
        "manufacturer",
        "mpn",
        "package",
        "quantity",
        "references",
        "dnp",
        "approved_alternate_mpns",
        "selection_override_references",
    };
    for (std::size_t index = 0; index < base_columns.size(); ++index) {
        if (index != 0U) {
            out << ',';
        }
        write_csv_field(out, base_columns[index]);
    }
    for (const auto &column : columns) {
        out << ',';
        write_csv_field(out, "sourcing." + column);
    }
    out << '\n';

    for (const auto &line : bom.lines()) {
        write_csv_field(out, line.manufacturer());
        out << ',';
        write_csv_field(out, line.mpn());
        out << ',';
        write_csv_field(out, line.package());
        out << ',' << line.quantity() << ',';
        write_csv_field(out, join_strings(line.references()));
        out << ',' << (line.dnp() ? "true" : "false") << ',';
        write_csv_field(out, join_strings(line.approved_alternate_mpns()));
        out << ',';
        write_csv_field(out, join_strings(line.selection_override_references()));
        for (const auto &column : columns) {
            out << ',';
            if (line.sourcing().contains(PropertyKey{column})) {
                write_csv_field(out,
                                property_value_to_string(line.sourcing().get(PropertyKey{column})));
            }
        }
        out << '\n';
    }
}

[[nodiscard]] std::string write_bom_csv(const Bom &bom) {
    auto out = std::ostringstream{};
    write_bom_csv(out, bom);
    return out.str();
}

void write_bom_sourcing_snapshot_json(std::ostream &out, const BomSourcingSnapshot &snapshot) {
    const auto entries = sorted_sourcing_entries(snapshot);
    out << "{\n";
    out << "  \"format\": " << detail::json_string(bom_sourcing_snapshot_format_name()) << ",\n";
    out << "  \"version\": " << bom_sourcing_snapshot_format_version() << ",\n";
    out << "  \"entries\": [\n";
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto &[mpn, properties] = entries[index];
        out << "    {\n";
        out << "      \"mpn\": " << detail::json_string(mpn) << ",\n";
        out << "      \"sourcing\": ";
        write_sourcing_properties(out, properties);
        out << "\n";
        out << "    }";
        if (index + 1U != entries.size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ]\n";
    out << "}\n";
}

[[nodiscard]] std::string write_bom_sourcing_snapshot_json(const BomSourcingSnapshot &snapshot) {
    auto out = std::ostringstream{};
    write_bom_sourcing_snapshot_json(out, snapshot);
    return out.str();
}

} // namespace volt::io
