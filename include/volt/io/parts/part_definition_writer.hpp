#pragma once

#include <ostream>
#include <string>
#include <string_view>

#include <volt/circuit/parts/part_definition.hpp>

namespace volt::io {

/** Return the canonical part definition artifact format name. */
[[nodiscard]] inline constexpr std::string_view part_definition_format_name() noexcept {
    return "volt.part";
}

/** Return the canonical part definition artifact schema version. */
[[nodiscard]] inline constexpr int part_definition_format_version() noexcept { return 3; }

/** Write a deterministic JSON representation of a part definition artifact. */
void write_part_definition(std::ostream &out, const PartDefinition &part);

/** Return deterministic JSON bytes for a part definition artifact. */
[[nodiscard]] std::string write_part_definition(const PartDefinition &part);

/** Hash the deterministic part definition artifact bytes. */
[[nodiscard]] ContentHash part_definition_content_hash(const PartDefinition &part);

} // namespace volt::io
