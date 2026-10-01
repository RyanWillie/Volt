#pragma once

#include <string>
#include <string_view>

#include <volt/electrical/dc_request.hpp>
#include <volt/io/electrical/electrical_input_io.hpp>

namespace volt::io {

/** Return the current standalone native DC request format. */
[[nodiscard]] inline constexpr std::string_view dc_request_format_name() noexcept {
    return "volt.dc-request";
}

/** Return the sole supported DC request wire version. */
[[nodiscard]] inline constexpr int dc_request_format_version() noexcept { return 1; }

/** Serialize deterministic request data and exact input identities without runtime state. */
[[nodiscard]] std::string write_dc_request(const DcRequest &request);

/** Rebind current request data to an explicit exact input before publishing a value. */
[[nodiscard]] DcRequest read_dc_request(std::string_view bytes, const ElectricalInput &input);

} // namespace volt::io
