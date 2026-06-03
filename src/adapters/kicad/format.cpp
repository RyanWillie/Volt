#include "format.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace volt::adapters::kicad::detail {

namespace {

constexpr auto kFnvOffset = std::uint64_t{14695981039346656037ULL};
constexpr auto kFnvPrime = std::uint64_t{1099511628211ULL};

[[nodiscard]] std::uint64_t fnv1a64(std::string_view value, std::uint64_t seed) noexcept {
    auto hash = seed;
    for (const auto character : value) {
        hash ^= static_cast<unsigned char>(character);
        hash *= kFnvPrime;
    }
    return hash;
}

void write_uuid_byte(std::ostream &out, std::uint8_t byte) {
    out << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(byte);
}

} // namespace

[[nodiscard]] std::string sexpr_string(std::string_view value) {
    auto result = std::string{"\""};
    for (const auto character : value) {
        switch (character) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
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
            result += character;
            break;
        }
    }
    result += '"';
    return result;
}

void write_number(std::ostream &out, double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument{"KiCad numeric values must be finite"};
    }
    if (std::abs(value) < 1.0e-12) {
        value = 0.0;
    }

    auto formatted = std::ostringstream{};
    formatted << std::setprecision(15) << value;
    out << formatted.str();
}

[[nodiscard]] std::string property_value_to_string(const PropertyValue &value) {
    switch (value.kind()) {
    case PropertyValueKind::String:
        return value.as_string();
    case PropertyValueKind::Boolean:
        return value.as_bool() ? "true" : "false";
    case PropertyValueKind::Integer:
        return std::to_string(value.as_integer());
    case PropertyValueKind::Number: {
        auto out = std::ostringstream{};
        write_number(out, value.as_number());
        return out.str();
    }
    }
    throw std::logic_error{"Unhandled property value kind"};
}

[[nodiscard]] std::string uuid_from_path(std::string_view path) {
    const auto high = fnv1a64(path, kFnvOffset);
    const auto low = fnv1a64(path, kFnvOffset ^ 0x9e3779b97f4a7c15ULL);

    auto bytes = std::array<std::uint8_t, 16>{};
    for (std::size_t index = 0; index < 8; ++index) {
        bytes[index] = static_cast<std::uint8_t>(high >> ((7U - index) * 8U));
        bytes[index + 8U] = static_cast<std::uint8_t>(low >> ((7U - index) * 8U));
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0FU) | 0x50U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3FU) | 0x80U);

    auto out = std::ostringstream{};
    out << std::hex << std::nouppercase;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4U || index == 6U || index == 8U || index == 10U) {
            out << '-';
        }
        write_uuid_byte(out, bytes[index]);
    }
    return out.str();
}

} // namespace volt::adapters::kicad::detail
