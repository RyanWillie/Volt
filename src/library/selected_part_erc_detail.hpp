#pragma once
#include <span>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/validation/validation.hpp>
#include <volt/library/part_library.hpp>
#include <volt/library/selected_part_ratings.hpp>

namespace volt::detail {
/** Private bridge: owners verify full exact identity and supply original-index pin mapping. */
struct SelectedPartErcSubject {
    ComponentId component;
    const PartDefinition *part;
    const ComponentDefinition *definition;
    std::vector<PinId> pins;
};

/** Shared ERC owner; compiled caller supplies only verified closure and retained continuity. */
[[nodiscard]] SelectedPartRatings
assess_selected_part_ratings(const Circuit &circuit, const NetContinuityView &continuity,
                             std::span<const SelectedPartErcSubject> subjects,
                             bool continuity_complete);
} // namespace volt::detail
