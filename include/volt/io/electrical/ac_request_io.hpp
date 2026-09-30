#pragma once

#include <string>
#include <string_view>
#include <volt/electrical/ac_request.hpp>
#include <volt/io/electrical/dc_request_io.hpp>

namespace volt::io {
/** Return the standalone native AC request format name. */
[[nodiscard]] inline constexpr std::string_view ac_request_format_name() noexcept {
    return "volt.ac-request";
}

/** Return the sole supported native AC request wire version. */
[[nodiscard]] inline constexpr int ac_request_format_version() noexcept { return 1; }

/** Deterministic canonical AC request transport, including exact input and materialized sweep. */
[[nodiscard]] std::string write_ac_request(const AcRequest &request);
/** Validate and bind canonical AC request transport to one exact input. */
[[nodiscard]] AcRequest read_ac_request(std::string_view bytes, const DcInput &input);
} // namespace volt::io
