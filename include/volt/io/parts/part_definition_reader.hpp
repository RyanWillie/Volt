#pragma once

#include <istream>
#include <string>
#include <string_view>

#include <volt/circuit/parts/part_definition.hpp>

namespace volt::io {

/** Read a current exact part artifact against the one component digest it must implement. */
[[nodiscard]] PartDefinition read_part_definition_text(std::string_view text,
                                                       const ComponentDefinition &component);

/** Read a current exact part artifact against the one component digest it must implement. */
[[nodiscard]] PartDefinition read_part_definition(std::istream &input,
                                                  const ComponentDefinition &component);

} // namespace volt::io
