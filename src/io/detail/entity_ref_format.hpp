#pragma once

#include <string>

#include <volt/core/diagnostics.hpp>

namespace volt::io::detail {

[[nodiscard]] std::string entity_ref_serialized_id(EntityRef entity);

} // namespace volt::io::detail
