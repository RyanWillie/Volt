#pragma once

#include <string>
#include <string_view>

#include <volt/pcb/footprints/footprints.hpp>

namespace volt::io {

/** Decode one canonical exact-part footprint asset into complete native PCB meaning. */
[[nodiscard]] FootprintDefinition read_footprint_asset(std::string_view bytes);

/** Encode complete native PCB footprint meaning as deterministic exact-part asset bytes. */
[[nodiscard]] std::string write_footprint_asset(const FootprintDefinition &definition);

} // namespace volt::io
