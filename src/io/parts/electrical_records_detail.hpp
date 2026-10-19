#pragma once
#include <nlohmann/json.hpp>
#include <volt/circuit/electrical/records.hpp>

namespace volt::io::detail {
/** Encode one already owner-validated record without rebuilding a partial record owner. */
[[nodiscard]] nlohmann::json electrical_record_json(const ElectricalRecord &record);
} // namespace volt::io::detail
