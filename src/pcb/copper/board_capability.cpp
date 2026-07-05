#include <volt/pcb/copper/board_copper.hpp>

#include <volt/core/errors.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
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

[[nodiscard]] bool valid_range(BoardCapabilityRange range) noexcept {
    return finite_positive(range.minimum_mm) && finite_positive(range.maximum_mm) &&
           range.minimum_mm <= range.maximum_mm;
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
    std::vector<BoardCapabilityCopperWeightRefinement> copper_weight_refinements,
    std::vector<int> supported_copper_layer_counts,
    std::optional<BoardCapabilityRange> board_thickness_range_mm,
    std::vector<double> available_copper_weights_oz,
    std::optional<BoardCapabilityRange> drill_diameter_range_mm)
    : name_{std::move(name)}, provenance_{std::move(provenance)},
      minimum_track_width_mm_{minimum_track_width_mm}, minimum_via_drill_mm_{minimum_via_drill_mm},
      minimum_via_annular_mm_{minimum_via_annular_mm},
      minimum_clearances_{std::move(minimum_clearances)},
      copper_weight_refinements_{std::move(copper_weight_refinements)},
      supported_copper_layer_counts_{std::move(supported_copper_layer_counts)},
      board_thickness_range_mm_{board_thickness_range_mm},
      available_copper_weights_oz_{std::move(available_copper_weights_oz)},
      drill_diameter_range_mm_{drill_diameter_range_mm} {
    if (name_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Board capability profile name must not be empty"};
    }
    if (provenance_.source.empty() || provenance_.as_of.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Board capability profile provenance must be complete"};
    }
    if (!finite_positive(minimum_track_width_mm_) || !finite_positive(minimum_via_drill_mm_) ||
        !finite_positive(minimum_via_annular_mm_)) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Board capability profile minimum dimensions must be positive"};
    }
    if (minimum_via_annular_mm_ <= minimum_via_drill_mm_) {
        throw KernelArgumentError{
            ErrorCode::InvalidArgument,
            "Board capability profile via annular minimum must exceed drill minimum"};
    }

    for (auto &entry : minimum_clearances_) {
        if (entry.first == BoardClearanceKind::BoardEdge &&
            entry.second == BoardClearanceKind::BoardEdge) {
            throw KernelArgumentError{
                ErrorCode::InvalidArgument,
                "Board capability profile cannot pair the board edge with itself"};
        }
        if (!finite_non_negative(entry.clearance_mm)) {
            throw KernelArgumentError{
                ErrorCode::InvalidArgument,
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
        throw KernelArgumentError{
            ErrorCode::InvalidArgument,
            "Board capability profile minimum clearances must not duplicate pairs"};
    }

    auto previous_weight = std::optional<double>{};
    for (const auto &refinement : copper_weight_refinements_) {
        if (!finite_positive(refinement.copper_weight_oz) ||
            !finite_positive(refinement.minimum_track_width_mm) ||
            !finite_non_negative(refinement.minimum_clearance_mm)) {
            throw KernelArgumentError{
                ErrorCode::InvalidArgument,
                "Board capability profile copper-weight refinements must be finite and positive"};
        }
        if (previous_weight.has_value() && refinement.copper_weight_oz <= previous_weight.value()) {
            throw KernelArgumentError{
                ErrorCode::InvalidArgument,
                "Board capability profile copper weights must be unique and ascending"};
        }
        previous_weight = refinement.copper_weight_oz;
    }

    auto previous_layer_count = std::optional<int>{};
    for (const auto count : supported_copper_layer_counts_) {
        if (count <= 0) {
            throw KernelArgumentError{
                ErrorCode::InvalidArgument,
                "Board capability profile supported copper layer counts must be positive"};
        }
        if (previous_layer_count.has_value() && count <= previous_layer_count.value()) {
            throw KernelArgumentError{
                ErrorCode::InvalidArgument,
                "Board capability profile supported copper layer counts must be unique and "
                "ascending"};
        }
        previous_layer_count = count;
    }

    if (board_thickness_range_mm_.has_value() && !valid_range(board_thickness_range_mm_.value())) {
        throw KernelArgumentError{
            ErrorCode::InvalidArgument,
            "Board capability profile board thickness range must be positive and ordered"};
    }

    auto previous_available_weight = std::optional<double>{};
    for (const auto weight : available_copper_weights_oz_) {
        if (!finite_positive(weight)) {
            throw KernelArgumentError{
                ErrorCode::InvalidArgument,
                "Board capability profile available copper weights must be positive"};
        }
        if (previous_available_weight.has_value() && weight <= previous_available_weight.value()) {
            throw KernelArgumentError{
                ErrorCode::InvalidArgument,
                "Board capability profile available copper weights must be unique and ascending"};
        }
        previous_available_weight = weight;
    }

    if (drill_diameter_range_mm_.has_value() && !valid_range(drill_diameter_range_mm_.value())) {
        throw KernelArgumentError{
            ErrorCode::InvalidArgument,
            "Board capability profile drill diameter range must be positive and ordered"};
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
