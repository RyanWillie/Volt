#include <volt/pcb/board.hpp>
#include <volt/pcb/routing/board_spatial_index.hpp>

#include "../validation/board_capability_validation.hpp"
#include "../validation/board_footprint_drc.hpp"
#include "board_room_rules.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/circuit/constraints/net_class_resolution.hpp>
#include <volt/core/rule_set.hpp>

namespace volt {

BoardZone::BoardZone(std::vector<BoardPoint> outline, std::vector<BoardLayerId> layers,
                     std::optional<NetId> net, BoardZoneFill fill, int priority)
    : outline_{std::move(outline)}, layers_{std::move(layers)}, net_{net}, fill_{fill},
      priority_{priority} {
    validate_layers();
}

[[nodiscard]] const std::vector<BoardPoint> &BoardZone::outline() const noexcept {
    return outline_.vertices();
}

void BoardZone::validate_layers() const {
    if (layers_.empty()) {
        throw std::invalid_argument{"Board zone layers must not be empty"};
    }
    auto sorted = layers_;
    std::sort(sorted.begin(), sorted.end(),
              [](BoardLayerId lhs, BoardLayerId rhs) { return lhs.index() < rhs.index(); });
    const auto duplicate = std::adjacent_find(sorted.begin(), sorted.end());
    if (duplicate != sorted.end()) {
        throw std::invalid_argument{"Board zone layers must not contain duplicates"};
    }
}

BoardRoom::BoardRoom(std::string name, BoardOutline outline, std::vector<BoardLayerId> layers,
                     int priority)
    : name_{std::move(name)}, outline_{std::move(outline)}, layers_{std::move(layers)},
      priority_{priority} {
    if (name_.empty()) {
        throw std::invalid_argument{"Board room name must not be empty"};
    }
    validate_layers();
}

void BoardRoom::set_copper_clearance_mm(double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument{"Board room copper clearance must be finite"};
    }
    if (value < 0.0) {
        throw std::invalid_argument{"Board room copper clearance must not be negative"};
    }
    copper_clearance_mm_ = value;
}

void BoardRoom::set_track_width_mm(double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument{"Board room track width must be finite"};
    }
    if (value <= 0.0) {
        throw std::invalid_argument{"Board room track width must be positive"};
    }
    track_width_mm_ = value;
}

void BoardRoom::validate_layers() const {
    if (layers_.empty()) {
        throw std::invalid_argument{"Board room layers must not be empty"};
    }
    auto sorted = layers_;
    std::sort(sorted.begin(), sorted.end(),
              [](BoardLayerId lhs, BoardLayerId rhs) { return lhs.index() < rhs.index(); });
    const auto duplicate = std::adjacent_find(sorted.begin(), sorted.end());
    if (duplicate != sorted.end()) {
        throw std::invalid_argument{"Board room layers must not contain duplicates"};
    }
}

BoardKeepout::BoardKeepout(std::vector<BoardPoint> outline, std::vector<BoardLayerId> layers,
                           std::vector<BoardKeepoutRestriction> restrictions)
    : outline_{std::move(outline)}, layers_{std::move(layers)},
      restrictions_{std::move(restrictions)} {
    validate_layers();
    validate_restrictions();
}

[[nodiscard]] const std::vector<BoardPoint> &BoardKeepout::outline() const noexcept {
    return outline_.vertices();
}

[[nodiscard]] const std::vector<BoardKeepoutRestriction> &
BoardKeepout::restrictions() const noexcept {
    return restrictions_;
}

void BoardKeepout::validate_layers() const {
    if (layers_.empty()) {
        throw std::invalid_argument{"Board keepout layers must not be empty"};
    }
    auto sorted = layers_;
    std::sort(sorted.begin(), sorted.end(),
              [](BoardLayerId lhs, BoardLayerId rhs) { return lhs.index() < rhs.index(); });
    const auto duplicate = std::adjacent_find(sorted.begin(), sorted.end());
    if (duplicate != sorted.end()) {
        throw std::invalid_argument{"Board keepout layers must not contain duplicates"};
    }
}

void BoardKeepout::validate_restrictions() const {
    if (restrictions_.empty()) {
        throw std::invalid_argument{"Board keepout restrictions must not be empty"};
    }
    auto sorted = restrictions_;
    std::sort(sorted.begin(), sorted.end());
    const auto duplicate = std::adjacent_find(sorted.begin(), sorted.end());
    if (duplicate != sorted.end()) {
        throw std::invalid_argument{"Board keepout restrictions must not contain duplicates"};
    }
}

BoardText::BoardText(std::string text, BoardPoint position, BoardRotation rotation,
                     BoardLayerId layer, double size_mm, bool locked)
    : text_{std::move(text)}, position_{position}, rotation_{rotation}, layer_{layer},
      size_mm_{size_mm}, locked_{locked} {
    if (text_.empty()) {
        throw std::invalid_argument{"Board text must not be empty"};
    }
    if (!std::isfinite(size_mm_)) {
        throw std::invalid_argument{"Board text size must be finite"};
    }
    if (size_mm_ <= 0.0) {
        throw std::invalid_argument{"Board text size must be positive"};
    }
}

BoardTrack::BoardTrack(NetId net, BoardLayerId layer, std::vector<BoardPoint> points,
                       double width_mm)
    : net_{net}, layer_{layer}, points_{std::move(points)}, width_mm_{width_mm} {
    if (points_.size() < 2U) {
        throw std::invalid_argument{"Board track must contain at least two points"};
    }
    if (!std::isfinite(width_mm_)) {
        throw std::invalid_argument{"Board track width must be finite"};
    }
    if (width_mm_ <= 0.0) {
        throw std::invalid_argument{"Board track width must be positive"};
    }
    for (std::size_t index = 1; index < points_.size(); ++index) {
        if (points_[index - 1U] == points_[index]) {
            throw std::invalid_argument{"Board track points must not repeat adjacent vertices"};
        }
    }
}

BoardVia::BoardVia(NetId net, BoardPoint position, BoardLayerId start_layer, BoardLayerId end_layer,
                   double drill_diameter_mm, double annular_diameter_mm)
    : net_{net}, position_{position}, start_layer_{start_layer}, end_layer_{end_layer},
      drill_diameter_mm_{drill_diameter_mm}, annular_diameter_mm_{annular_diameter_mm} {
    if (start_layer_ == end_layer_) {
        throw std::invalid_argument{"Board via layer span must reference distinct layers"};
    }
    if (!std::isfinite(drill_diameter_mm_) || !std::isfinite(annular_diameter_mm_)) {
        throw std::invalid_argument{"Board via diameters must be finite"};
    }
    if (drill_diameter_mm_ <= 0.0 || annular_diameter_mm_ <= 0.0) {
        throw std::invalid_argument{"Board via diameters must be positive"};
    }
    if (annular_diameter_mm_ <= drill_diameter_mm_) {
        throw std::invalid_argument{
            "Board via annular diameter must be greater than drill diameter"};
    }
}

BoardDesignRules::BoardDesignRules(double copper_clearance_mm, double minimum_track_width_mm,
                                   double minimum_via_drill_diameter_mm,
                                   double minimum_via_annular_diameter_mm,
                                   double board_outline_clearance_mm,
                                   double package_assembly_clearance_mm)
    : copper_clearance_mm_{copper_clearance_mm}, minimum_track_width_mm_{minimum_track_width_mm},
      minimum_via_drill_diameter_mm_{minimum_via_drill_diameter_mm},
      minimum_via_annular_diameter_mm_{minimum_via_annular_diameter_mm},
      board_outline_clearance_mm_{board_outline_clearance_mm},
      package_assembly_clearance_mm_{package_assembly_clearance_mm} {
    if (!std::isfinite(copper_clearance_mm_) || !std::isfinite(board_outline_clearance_mm_) ||
        !std::isfinite(package_assembly_clearance_mm_)) {
        throw std::invalid_argument{"Board design rule clearances must be finite"};
    }
    if (copper_clearance_mm_ < 0.0 || board_outline_clearance_mm_ < 0.0 ||
        package_assembly_clearance_mm_ < 0.0) {
        throw std::invalid_argument{"Board design rule clearances must not be negative"};
    }
    if (!std::isfinite(minimum_track_width_mm_) || !std::isfinite(minimum_via_drill_diameter_mm_) ||
        !std::isfinite(minimum_via_annular_diameter_mm_)) {
        throw std::invalid_argument{"Board design rule minimum dimensions must be finite"};
    }
    if (minimum_track_width_mm_ <= 0.0 || minimum_via_drill_diameter_mm_ <= 0.0 ||
        minimum_via_annular_diameter_mm_ <= 0.0) {
        throw std::invalid_argument{"Board design rule minimum dimensions must be positive"};
    }
    if (minimum_via_annular_diameter_mm_ <= minimum_via_drill_diameter_mm_) {
        throw std::invalid_argument{
            "Board design rule via annular diameter must be greater than drill diameter"};
    }
}

[[nodiscard]] double BoardDesignRules::minimum_via_drill_diameter_mm() const noexcept {
    return minimum_via_drill_diameter_mm_;
}

[[nodiscard]] double BoardDesignRules::minimum_via_annular_diameter_mm() const noexcept {
    return minimum_via_annular_diameter_mm_;
}

[[nodiscard]] double BoardDesignRules::board_outline_clearance_mm() const noexcept {
    return board_outline_clearance_mm_;
}

[[nodiscard]] double BoardDesignRules::package_assembly_clearance_mm() const noexcept {
    return package_assembly_clearance_mm_;
}

namespace {

[[nodiscard]] std::pair<BoardClearanceKind, BoardClearanceKind>
canonical_clearance_pair(BoardClearanceKind first, BoardClearanceKind second) {
    if (static_cast<int>(first) > static_cast<int>(second)) {
        return {second, first};
    }
    return {first, second};
}

} // namespace

void BoardDesignRules::set_clearance_mm(BoardClearanceKind first, BoardClearanceKind second,
                                        double clearance_mm) {
    if (first == BoardClearanceKind::BoardEdge && second == BoardClearanceKind::BoardEdge) {
        throw std::invalid_argument{"Clearance matrix cannot pair the board edge with itself"};
    }
    if (!std::isfinite(clearance_mm) || clearance_mm < 0.0) {
        throw std::invalid_argument{"Clearance matrix values must be finite and non-negative"};
    }

    const auto pair = canonical_clearance_pair(first, second);
    const auto low = pair.first;
    const auto high = pair.second;
    const auto position = std::find_if(
        clearance_matrix_.begin(), clearance_matrix_.end(),
        [low, high](const auto &entry) { return entry.first == low && entry.second == high; });
    if (position != clearance_matrix_.end()) {
        position->clearance_mm = clearance_mm;
        return;
    }

    const auto insert_before = std::find_if(
        clearance_matrix_.begin(), clearance_matrix_.end(), [low, high](const auto &entry) {
            return static_cast<int>(entry.first) > static_cast<int>(low) ||
                   (entry.first == low && static_cast<int>(entry.second) > static_cast<int>(high));
        });
    clearance_matrix_.insert(insert_before, BoardClearancePair{low, high, clearance_mm});
}

[[nodiscard]] double BoardDesignRules::clearance_mm(BoardClearanceKind first,
                                                    BoardClearanceKind second) const noexcept {
    const auto [low, high] = canonical_clearance_pair(first, second);
    for (const auto &entry : clearance_matrix_) {
        if (entry.first == low && entry.second == high) {
            return entry.clearance_mm;
        }
    }
    if (low == BoardClearanceKind::BoardEdge || high == BoardClearanceKind::BoardEdge) {
        return board_outline_clearance_mm_;
    }
    return copper_clearance_mm_;
}

} // namespace volt
