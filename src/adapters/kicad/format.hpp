#pragma once

#include <ostream>
#include <string>
#include <string_view>

#include <volt/core/properties.hpp>

namespace volt::adapters::kicad::detail {

[[nodiscard]] std::string sexpr_string(std::string_view value);

void write_number(std::ostream &out, double value);

[[nodiscard]] std::string property_value_to_string(const PropertyValue &value);

[[nodiscard]] std::string uuid_from_path(std::string_view path);

} // namespace volt::adapters::kicad::detail
