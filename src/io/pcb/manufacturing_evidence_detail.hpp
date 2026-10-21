#pragma once
#include <nlohmann/json.hpp>
#include <volt/io/pcb/manufacturing_evidence.hpp>

namespace volt::io::detail {
[[nodiscard]] nlohmann::json manufacturing_binding_json(const ManufacturingExportBinding &binding);
[[nodiscard]] nlohmann::json
manufacturing_assertion_json(const ManufacturingReviewAssertion &assertion);
void validate_manufacturing_assertions(std::span<const ManufacturingReviewAssertion> assertions);
} // namespace volt::io::detail
