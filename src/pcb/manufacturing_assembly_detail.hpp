#pragma once
#include <volt/circuit/bom/bom.hpp>
#include <volt/library/selected_part_ratings.hpp>
#include <volt/pcb/compiled/manufacturing_target.hpp>

namespace volt::detail {
[[nodiscard]] SelectedPartRatings compiled_selected_part_ratings(const CompiledBoard &compiled);
void apply_manufacturing_ratings(ManufacturingCoverageRow &row, const SelectedPartRatings &ratings);
void check_manufacturing_assembly(const CompiledBoard &compiled, const ManufacturingTarget &target,
                                  const Bom &bom, const Cpl &cpl,
                                  std::span<ManufacturingCoverageRow> rows);
} // namespace volt::detail
