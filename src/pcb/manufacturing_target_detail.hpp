#pragma once
#include <volt/pcb/compiled/manufacturing_target.hpp>

namespace volt::detail {
[[nodiscard]] std::string manufacturing_process_bytes(const ManufacturingProcess &process);
[[nodiscard]] std::string manufacturing_capability_bytes(const BoardCapabilityProfile &profile);
} // namespace volt::detail
