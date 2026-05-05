#pragma once

#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <volt/circuit/circuit.hpp>

namespace volt::io {

/** Return the canonical v1 logical circuit format name. */
[[nodiscard]] inline constexpr std::string_view logical_circuit_format_name() noexcept {
    return "volt.logical_circuit";
}

/** Return the canonical logical circuit format version written by this library. */
[[nodiscard]] inline constexpr int logical_circuit_format_version() noexcept { return 1; }

namespace detail {

[[nodiscard]] inline std::string json_string(std::string_view value) {
    auto result = std::string{"\""};
    for (const auto character : value) {
        switch (character) {
        case '\"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                constexpr auto hex = std::string_view{"0123456789abcdef"};
                const auto byte = static_cast<unsigned char>(character);
                result += "\\u00";
                result += hex[(byte >> 4U) & 0x0FU];
                result += hex[byte & 0x0FU];
            } else {
                result += character;
            }
            break;
        }
    }
    result += '"';
    return result;
}

[[nodiscard]] inline std::string pin_role_name(PinRole role) {
    switch (role) {
    case PinRole::Passive:
        return "Passive";
    case PinRole::PowerInput:
        return "PowerInput";
    case PinRole::PowerOutput:
        return "PowerOutput";
    case PinRole::Ground:
        return "Ground";
    case PinRole::DigitalInput:
        return "DigitalInput";
    case PinRole::DigitalOutput:
        return "DigitalOutput";
    case PinRole::Bidirectional:
        return "Bidirectional";
    case PinRole::AnalogInput:
        return "AnalogInput";
    case PinRole::AnalogOutput:
        return "AnalogOutput";
    case PinRole::NoConnect:
        return "NoConnect";
    }
    throw std::logic_error{"Unhandled pin role"};
}

[[nodiscard]] inline std::string connection_requirement_name(ConnectionRequirement requirement) {
    switch (requirement) {
    case ConnectionRequirement::Optional:
        return "Optional";
    case ConnectionRequirement::Required:
        return "Required";
    case ConnectionRequirement::MustNotConnect:
        return "MustNotConnect";
    }
    throw std::logic_error{"Unhandled connection requirement"};
}

[[nodiscard]] inline std::string net_kind_name(NetKind kind) {
    switch (kind) {
    case NetKind::Signal:
        return "Signal";
    case NetKind::Power:
        return "Power";
    case NetKind::Ground:
        return "Ground";
    case NetKind::Clock:
        return "Clock";
    case NetKind::Analog:
        return "Analog";
    case NetKind::HighCurrent:
        return "HighCurrent";
    }
    throw std::logic_error{"Unhandled net kind"};
}

inline void write_property_value(std::ostream &out, const PropertyValue &value) {
    out << "{ \"type\": ";
    switch (value.kind()) {
    case PropertyValueKind::String:
        out << "\"string\", \"value\": " << json_string(value.as_string());
        break;
    case PropertyValueKind::Boolean:
        out << "\"boolean\", \"value\": " << (value.as_bool() ? "true" : "false");
        break;
    case PropertyValueKind::Integer:
        out << "\"integer\", \"value\": " << value.as_integer();
        break;
    case PropertyValueKind::Number:
        if (!std::isfinite(value.as_number())) {
            throw std::logic_error{"Cannot write non-finite JSON number"};
        }
        out << "\"number\", \"value\": "
            << std::setprecision(std::numeric_limits<double>::max_digits10) << value.as_number();
        break;
    }
    out << " }";
}

inline void write_properties(std::ostream &out, const PropertyMap &properties) {
    out << '{';
    if (!properties.empty()) {
        out << '\n';
        auto index = std::size_t{0};
        for (const auto &[key, value] : properties.entries()) {
            out << "        " << json_string(key.value()) << ": ";
            write_property_value(out, value);
            if (++index != properties.size()) {
                out << ',';
            }
            out << '\n';
        }
        out << "      ";
    }
    out << '}';
}

[[nodiscard]] inline std::string pin_def_id(PinDefId id) {
    return "pin_def:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string component_def_id(ComponentDefId id) {
    return "component_def:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string component_id(ComponentId id) {
    return "component:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string pin_id(PinId id) { return "pin:" + std::to_string(id.index()); }

[[nodiscard]] inline std::string net_id(NetId id) { return "net:" + std::to_string(id.index()); }

inline void write_selected_physical_part(std::ostream &out, const PhysicalPart &part) {
    out << "{\n";
    out << "      \"manufacturer_part\": { \"manufacturer\": "
        << json_string(part.manufacturer_part().manufacturer())
        << ", \"part_number\": " << json_string(part.manufacturer_part().part_number()) << " },\n";
    out << "      \"package\": " << json_string(part.package().value()) << ",\n";
    out << "      \"footprint\": { \"library\": " << json_string(part.footprint().library())
        << ", \"name\": " << json_string(part.footprint().name()) << " },\n";
    out << "      \"pin_pad_mappings\": [\n";
    for (std::size_t index = 0; index < part.pin_pad_mappings().size(); ++index) {
        const auto &mapping = part.pin_pad_mappings()[index];
        out << "        { \"pin\": " << json_string(pin_def_id(mapping.pin()))
            << ", \"pad\": " << json_string(mapping.pad()) << " }";
        if (index + 1 != part.pin_pad_mappings().size()) {
            out << ',';
        }
        out << '\n';
    }
    out << "      ],\n";
    out << "      \"properties\": ";
    write_properties(out, part.properties());
    out << "\n    }";
}

} // namespace detail

/** Write a deterministic JSON representation of the logical circuit to an output stream. */
inline void write_logical_circuit(std::ostream &out, const Circuit &circuit) {
    out << "{\n";
    out << "  \"format\": " << detail::json_string(logical_circuit_format_name()) << ",\n";
    out << "  \"version\": " << logical_circuit_format_version() << ",\n";

    out << "  \"pin_definitions\": [\n";
    for (std::size_t index = 0; index < circuit.pin_definition_count(); ++index) {
        const auto id = PinDefId{index};
        const auto &pin = circuit.pin_definition(id);
        out << "    { \"id\": " << detail::json_string(detail::pin_def_id(id))
            << ", \"name\": " << detail::json_string(pin.name())
            << ", \"number\": " << detail::json_string(pin.number())
            << ", \"role\": " << detail::json_string(detail::pin_role_name(pin.role()))
            << ", \"connection_requirement\": "
            << detail::json_string(
                   detail::connection_requirement_name(pin.connection_requirement()))
            << " }";
        if (index + 1 != circuit.pin_definition_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"component_definitions\": [\n";
    for (std::size_t index = 0; index < circuit.component_definition_count(); ++index) {
        const auto id = ComponentDefId{index};
        const auto &definition = circuit.component_definition(id);
        out << "    { \"id\": " << detail::json_string(detail::component_def_id(id))
            << ", \"name\": " << detail::json_string(definition.name());
        if (definition.source().has_value()) {
            out << ", \"source\": { \"namespace\": "
                << detail::json_string(definition.source()->namespace_name())
                << ", \"name\": " << detail::json_string(definition.source()->name())
                << ", \"version\": " << detail::json_string(definition.source()->version()) << " }";
        }
        out << ", \"pins\": [";
        for (std::size_t pin_index = 0; pin_index < definition.pins().size(); ++pin_index) {
            out << detail::json_string(detail::pin_def_id(definition.pins()[pin_index]));
            if (pin_index + 1 != definition.pins().size()) {
                out << ", ";
            }
        }
        out << "], \"properties\": ";
        detail::write_properties(out, definition.properties());
        out << " }";
        if (index + 1 != circuit.component_definition_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"components\": [\n";
    for (std::size_t index = 0; index < circuit.component_count(); ++index) {
        const auto id = ComponentId{index};
        const auto &component = circuit.component(id);
        out << "    { \"id\": " << detail::json_string(detail::component_id(id))
            << ", \"definition\": "
            << detail::json_string(detail::component_def_id(component.definition()))
            << ", \"reference\": " << detail::json_string(component.reference().value())
            << ", \"properties\": ";
        detail::write_properties(out, component.properties());
        if (component.selected_physical_part().has_value()) {
            out << ", \"selected_physical_part\": ";
            detail::write_selected_physical_part(out, component.selected_physical_part().value());
        }
        out << " }";
        if (index + 1 != circuit.component_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"pins\": [\n";
    for (std::size_t index = 0; index < circuit.pin_count(); ++index) {
        const auto id = PinId{index};
        const auto &pin = circuit.pin(id);
        out << "    { \"id\": " << detail::json_string(detail::pin_id(id))
            << ", \"component\": " << detail::json_string(detail::component_id(pin.component()))
            << ", \"definition\": " << detail::json_string(detail::pin_def_id(pin.definition()))
            << " }";
        if (index + 1 != circuit.pin_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"nets\": [\n";
    for (std::size_t index = 0; index < circuit.net_count(); ++index) {
        const auto id = NetId{index};
        const auto &net = circuit.net(id);
        out << "    { \"id\": " << detail::json_string(detail::net_id(id))
            << ", \"name\": " << detail::json_string(net.name().value())
            << ", \"kind\": " << detail::json_string(detail::net_kind_name(net.kind()))
            << ", \"pins\": [";
        for (std::size_t pin_index = 0; pin_index < net.pins().size(); ++pin_index) {
            out << detail::json_string(detail::pin_id(net.pins()[pin_index]));
            if (pin_index + 1 != net.pins().size()) {
                out << ", ";
            }
        }
        out << "] }";
        if (index + 1 != circuit.net_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ]\n";
    out << "}\n";
}

/** Return a deterministic JSON representation of the logical circuit. */
[[nodiscard]] inline std::string write_logical_circuit(const Circuit &circuit) {
    auto out = std::ostringstream{};
    write_logical_circuit(out, circuit);
    return out.str();
}

} // namespace volt::io
