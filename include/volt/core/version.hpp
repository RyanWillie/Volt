#pragma once

#include <string_view>

namespace volt {

/** Return the configured Volt project version string. */
[[nodiscard]] std::string_view version_string() noexcept;

} // namespace volt
