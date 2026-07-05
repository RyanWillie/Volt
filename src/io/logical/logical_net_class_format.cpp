#include "logical_net_class_format.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/core/errors.hpp>

namespace volt::io::detail {
namespace {

void require(bool condition, const std::string &message) {
    if (!condition) {
        throw KernelLogicError{ErrorCode::InvalidArgument, message};
    }
}

const nlohmann::json &field(const nlohmann::json &object, const char *name) {
    require(object.is_object(), std::string{"Expected object while reading "} + name);
    const auto it = object.find(name);
    require(it != object.end(), std::string{"Missing required field: "} + name);
    return *it;
}

std::string string_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_string(), std::string{"Field must be a string: "} + name);
    return value.get<std::string>();
}

double number_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_number(), std::string{"Field must be a number: "} + name);
    return value.get<double>();
}

const nlohmann::json &array_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_array(), std::string{"Field must be an array: "} + name);
    return value;
}

[[nodiscard]] std::string json_string(std::string_view value) {
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

void write_json_number(std::ostream &out, double value) {
    if (!std::isfinite(value)) {
        throw KernelLogicError{ErrorCode::InvalidArgument, "Cannot write non-finite JSON number"};
    }
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
}

void write_derivation_input(std::ostream &out, const NetClassDerivationInput &input) {
    out << "{ \"name\": " << json_string(input.name) << ", \"value\": ";
    if (input.text_value.empty()) {
        write_json_number(out, input.value);
    } else {
        out << json_string(input.text_value);
    }
    out << ", \"unit\": " << json_string(input.unit) << " }";
}

} // namespace

[[nodiscard]] DerivedNetClassRuleValue
read_derived_net_class_rule_value(const nlohmann::json &object) {
    require(object.is_object(), "Derived net-class rule value must be an object");
    const auto &calculator = field(object, "calculator");
    require(calculator.is_object(), "Derived net-class calculator must be an object");
    auto inputs = std::vector<NetClassDerivationInput>{};
    for (const auto &input : array_field(object, "inputs")) {
        require(input.is_object(), "Derived net-class input must be an object");
        auto entry = NetClassDerivationInput{};
        entry.name = string_field(input, "name");
        const auto &value = field(input, "value");
        if (value.is_string()) {
            entry.text_value = value.get<std::string>();
            require(!entry.text_value.empty(),
                    "Derived net-class string input value must be non-empty");
        } else {
            require(value.is_number(), "Derived net-class input value must be a number or string");
            entry.value = value.get<double>();
        }
        entry.unit = string_field(input, "unit");
        inputs.push_back(std::move(entry));
    }

    return DerivedNetClassRuleValue{
        number_field(object, "value_mm"),
        NetClassRuleDerivation{
            string_field(calculator, "id"),
            string_field(calculator, "name"),
            string_field(calculator, "standard"),
            string_field(calculator, "reference"),
            std::move(inputs),
        },
    };
}

void write_derived_net_class_rule_value(std::ostream &out, const DerivedNetClassRuleValue &value) {
    out << "{ \"value_mm\": ";
    write_json_number(out, value.value_mm);
    out << ", \"calculator\": { \"id\": " << json_string(value.derivation.calculator_id)
        << ", \"name\": " << json_string(value.derivation.calculator_name)
        << ", \"standard\": " << json_string(value.derivation.standard)
        << ", \"reference\": " << json_string(value.derivation.reference) << " }, \"inputs\": [";
    for (std::size_t index = 0; index < value.derivation.inputs.size(); ++index) {
        if (index != 0) {
            out << ", ";
        }
        write_derivation_input(out, value.derivation.inputs[index]);
    }
    out << "] }";
}

} // namespace volt::io::detail
