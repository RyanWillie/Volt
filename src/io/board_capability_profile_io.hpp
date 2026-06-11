#pragma once

#include <iosfwd>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include <volt/pcb/board_copper.hpp>

namespace volt::io::detail {

[[nodiscard]] std::string capability_clearance_kind_name(BoardClearanceKind kind);

[[nodiscard]] BoardClearanceKind capability_clearance_kind_from_name(const std::string &value);

void write_capability_profile_payload(std::ostream &out, const BoardCapabilityProfile &profile);

[[nodiscard]] BoardCapabilityProfile read_capability_profile_payload(const nlohmann::json &object);

} // namespace volt::io::detail
