#include <volt/core/version.hpp>

#include <volt/core/version_config.hpp>

namespace volt {

std::string_view version_string() noexcept { return VOLT_VERSION_STRING; }

} // namespace volt
