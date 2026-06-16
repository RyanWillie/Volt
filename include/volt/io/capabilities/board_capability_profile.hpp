#pragma once

#include <istream>
#include <ostream>
#include <string>
#include <string_view>

#include <volt/pcb/copper/board_copper.hpp>

namespace volt::io {

/** Return the canonical v1 capability profile document format name. */
[[nodiscard]] inline constexpr std::string_view capability_profile_format_name() noexcept {
    return "volt.capability_profile";
}

/** Return the canonical capability profile document format version written by this library. */
[[nodiscard]] inline constexpr int capability_profile_format_version() noexcept { return 1; }

/** Write a deterministic standalone capability profile JSON document to an output stream. */
void write_capability_profile(std::ostream &out, const BoardCapabilityProfile &profile);

/** Return a deterministic standalone capability profile JSON document. */
[[nodiscard]] std::string write_capability_profile(const BoardCapabilityProfile &profile);

/** Read a standalone capability profile JSON document from text. */
[[nodiscard]] BoardCapabilityProfile read_capability_profile_text(std::string_view text);

/** Read a standalone capability profile JSON document from a stream. */
[[nodiscard]] BoardCapabilityProfile read_capability_profile(std::istream &input);

} // namespace volt::io
