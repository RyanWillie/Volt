#include <volt/pcb/board_copper.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace volt {
namespace {

[[nodiscard]] std::pair<BoardClearanceKind, BoardClearanceKind>
canonical_clearance_pair(BoardClearanceKind first, BoardClearanceKind second) {
    if (static_cast<int>(first) > static_cast<int>(second)) {
        return {second, first};
    }
    return {first, second};
}

[[nodiscard]] bool finite_positive(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] bool finite_non_negative(double value) noexcept {
    return std::isfinite(value) && value >= 0.0;
}

[[nodiscard]] bool clearance_pair_less(const BoardClearancePair &lhs,
                                       const BoardClearancePair &rhs) noexcept {
    return static_cast<int>(lhs.first) < static_cast<int>(rhs.first) ||
           (lhs.first == rhs.first && static_cast<int>(lhs.second) < static_cast<int>(rhs.second));
}

} // namespace

BoardCapabilityProfile::BoardCapabilityProfile(
    std::string name, BoardCapabilityProvenance provenance, double minimum_track_width_mm,
    double minimum_via_drill_mm, double minimum_via_annular_mm,
    std::vector<BoardClearancePair> minimum_clearances,
    std::vector<BoardCapabilityCopperWeightRefinement> copper_weight_refinements)
    : name_{std::move(name)}, provenance_{std::move(provenance)},
      minimum_track_width_mm_{minimum_track_width_mm}, minimum_via_drill_mm_{minimum_via_drill_mm},
      minimum_via_annular_mm_{minimum_via_annular_mm},
      minimum_clearances_{std::move(minimum_clearances)},
      copper_weight_refinements_{std::move(copper_weight_refinements)} {
    if (name_.empty()) {
        throw std::invalid_argument{"Board capability profile name must not be empty"};
    }
    if (provenance_.source.empty() || provenance_.as_of.empty()) {
        throw std::invalid_argument{"Board capability profile provenance must be complete"};
    }
    if (!finite_positive(minimum_track_width_mm_) || !finite_positive(minimum_via_drill_mm_) ||
        !finite_positive(minimum_via_annular_mm_)) {
        throw std::invalid_argument{"Board capability profile minimum dimensions must be positive"};
    }
    if (minimum_via_annular_mm_ <= minimum_via_drill_mm_) {
        throw std::invalid_argument{
            "Board capability profile via annular minimum must exceed drill minimum"};
    }

    for (auto &entry : minimum_clearances_) {
        if (entry.first == BoardClearanceKind::BoardEdge &&
            entry.second == BoardClearanceKind::BoardEdge) {
            throw std::invalid_argument{
                "Board capability profile cannot pair the board edge with itself"};
        }
        if (!finite_non_negative(entry.clearance_mm)) {
            throw std::invalid_argument{
                "Board capability profile clearances must be finite and non-negative"};
        }
        const auto pair = canonical_clearance_pair(entry.first, entry.second);
        entry.first = pair.first;
        entry.second = pair.second;
    }
    std::sort(minimum_clearances_.begin(), minimum_clearances_.end(), clearance_pair_less);
    const auto duplicate_clearance =
        std::adjacent_find(minimum_clearances_.begin(), minimum_clearances_.end(),
                           [](const BoardClearancePair &lhs, const BoardClearancePair &rhs) {
                               return lhs.first == rhs.first && lhs.second == rhs.second;
                           });
    if (duplicate_clearance != minimum_clearances_.end()) {
        throw std::invalid_argument{
            "Board capability profile minimum clearances must not duplicate pairs"};
    }

    auto previous_weight = std::optional<double>{};
    for (const auto &refinement : copper_weight_refinements_) {
        if (!finite_positive(refinement.copper_weight_oz) ||
            !finite_positive(refinement.minimum_track_width_mm) ||
            !finite_non_negative(refinement.minimum_clearance_mm)) {
            throw std::invalid_argument{
                "Board capability profile copper-weight refinements must be finite and positive"};
        }
        if (previous_weight.has_value() && refinement.copper_weight_oz <= previous_weight.value()) {
            throw std::invalid_argument{
                "Board capability profile copper weights must be unique and ascending"};
        }
        previous_weight = refinement.copper_weight_oz;
    }
}

[[nodiscard]] BoardCapabilityProfile BoardCapabilityProfile::conservative_default() {
    auto clearances = std::vector<BoardClearancePair>{};
    constexpr auto board_edge_index = static_cast<int>(BoardClearanceKind::BoardEdge);
    for (auto first = 0; first <= board_edge_index; ++first) {
        for (auto second = first; second <= board_edge_index; ++second) {
            const auto low = static_cast<BoardClearanceKind>(first);
            const auto high = static_cast<BoardClearanceKind>(second);
            if (low == BoardClearanceKind::BoardEdge && high == BoardClearanceKind::BoardEdge) {
                continue;
            }
            const auto clearance = high == BoardClearanceKind::BoardEdge ? 0.30 : 0.20;
            clearances.push_back(BoardClearancePair{low, high, clearance});
        }
    }
    return BoardCapabilityProfile{
        "volt.conservative",
        BoardCapabilityProvenance{"Volt built-in conservative fallback", "2026-06-11"},
        0.20,
        0.30,
        0.60,
        std::move(clearances),
    };
}

[[nodiscard]] std::optional<double>
BoardCapabilityProfile::minimum_clearance(BoardClearanceKind first,
                                          BoardClearanceKind second) const noexcept {
    const auto pair = canonical_clearance_pair(first, second);
    for (const auto &entry : minimum_clearances_) {
        if (entry.first == pair.first && entry.second == pair.second) {
            return entry.clearance_mm;
        }
    }
    return std::nullopt;
}

[[nodiscard]] double
BoardCapabilityProfile::minimum_clearance_mm(BoardClearanceKind first,
                                             BoardClearanceKind second) const noexcept {
    return minimum_clearance(first, second).value_or(0.0);
}

} // namespace volt
