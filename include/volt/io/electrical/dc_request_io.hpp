#pragma once

#include <string>
#include <string_view>

#include <volt/electrical/dc_input.hpp>
#include <volt/electrical/dc_request.hpp>
#include <volt/library/part_library.hpp>

namespace volt {

/** Native IO preparation boundary for an owning exact electrical input. */
class DcInput::Codec final {
  public:
    /** Capture canonical logical identity and resolve each selected exact Part once. */
    [[nodiscard]] static DcInput prepare(const Circuit &circuit,
                                         const PartDefinitionResolver &resolver);
};

} // namespace volt

namespace volt::io {

/** Capture an immutable logical/Part input using only the explicitly supplied resolver. */
[[nodiscard]] DcInput prepare_dc_input(const Circuit &circuit,
                                       const PartDefinitionResolver &resolver);

/** Return the current standalone native DC request format. */
[[nodiscard]] inline constexpr std::string_view dc_request_format_name() noexcept {
    return "volt.dc-request";
}

/** Return the sole supported DC request wire version. */
[[nodiscard]] inline constexpr int dc_request_format_version() noexcept { return 1; }

/** Serialize deterministic request data and exact input identities without runtime state. */
[[nodiscard]] std::string write_dc_request(const DcRequest &request);

/** Rebind current request data to an explicit exact input before publishing a value. */
[[nodiscard]] DcRequest read_dc_request(std::string_view bytes, const DcInput &input);

} // namespace volt::io
