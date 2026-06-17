#pragma once

#include <istream>
#include <string_view>

#include <volt/circuit/parts/part_definition.hpp>

namespace volt::io {

/** Read a part definition artifact from JSON text, rejecting structurally invalid input. */
[[nodiscard]] PartDefinition read_part_definition_text(std::string_view text);

/** Read a part definition artifact from a JSON stream, rejecting structurally invalid input. */
[[nodiscard]] PartDefinition read_part_definition(std::istream &input);

} // namespace volt::io
