#pragma once
#include <string>
#include <string_view>
#include <volt/electrical/transient_request.hpp>
#include <volt/io/electrical/electrical_input_io.hpp>

namespace volt::io {
/** Current native transient request wire format. */
[[nodiscard]] inline constexpr std::string_view transient_request_format_name() noexcept {
    return "volt.transient-request";
}

/** Sole admitted transient request wire version. */
[[nodiscard]] inline constexpr int transient_request_format_version() noexcept { return 1; }

/** Deterministic native request transport including waveforms and exact storage provenance. */
[[nodiscard]] std::string write_transient_request(const TransientRequest &request);
/** Validate and bind native request bytes against one exact input, with no implicit solve. */
[[nodiscard]] TransientRequest read_transient_request(std::string_view bytes,
                                                      const ElectricalInput &input);
} // namespace volt::io
