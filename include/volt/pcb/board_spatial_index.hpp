#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <volt/core/ids.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/board_copper.hpp>
#include <volt/pcb/board_features.hpp>
#include <volt/pcb/footprints.hpp>

namespace volt {

namespace detail {

/** Axis-aligned board-space box used by the copper spatial index. */
struct BoardSpatialIndexBox {
    /** Minimum X coordinate in millimeters. */
    double min_x_mm = 0.0;
    /** Minimum Y coordinate in millimeters. */
    double min_y_mm = 0.0;
    /** Maximum X coordinate in millimeters. */
    double max_x_mm = 0.0;
    /** Maximum Y coordinate in millimeters. */
    double max_y_mm = 0.0;
};

/** Deterministic uniform-grid cell used by the copper spatial index. */
struct BoardSpatialIndexCell {
    /** Board copper layer bucket. */
    BoardLayerId layer;
    /** Integer X cell coordinate. */
    long long x = 0;
    /** Integer Y cell coordinate. */
    long long y = 0;
    /** Shape indices stored in insertion order within this cell. */
    std::vector<std::size_t> shape_indices;
};

} // namespace detail

/** Kind of object that makes a transient copper candidate illegal. */
enum class BoardSpatialBlockerKind {
    CopperClearance,
    BoardOutline,
    Keepout,
};

/** Deterministic candidate pair produced by the board copper spatial index. */
struct BoardSpatialCandidatePair {
    /** Earlier normalized copper shape index. */
    std::size_t lhs_index = 0;
    /** Later normalized copper shape index. */
    std::size_t rhs_index = 0;
};

/** One deterministic blocker reported by a routing-facing legality query. */
struct BoardSpatialBlocker {
    /** Broad reason the candidate is blocked. */
    BoardSpatialBlockerKind kind = BoardSpatialBlockerKind::CopperClearance;
    /** Existing indexed copper shape, for copper-clearance blockers. */
    std::optional<std::size_t> shape_index;
    /** Existing board keepout, for keepout blockers. */
    std::optional<BoardKeepoutId> keepout;
    /** Board layer where the blocker applies, when layer-specific. */
    std::optional<BoardLayerId> layer;
    /** Required clearance in millimeters, for clearance-style blockers. */
    double required_clearance_mm = 0.0;
    /** Measured clearance in millimeters, for clearance-style blockers. */
    double actual_clearance_mm = 0.0;
    /** Board room that supplied a copper-clearance override, if any. */
    std::optional<BoardRoomId> room;

    /** Return whether two blocker records are identical. */
    [[nodiscard]] friend bool operator==(const BoardSpatialBlocker &lhs,
                                         const BoardSpatialBlocker &rhs) = default;
};

/** Complete routing-facing answer for a transient copper candidate. */
struct BoardSpatialQueryResult {
    /** Whether the candidate satisfies copper, outline, and keepout rules. */
    bool legal = true;
    /** Deterministically ordered blockers that made the candidate illegal. */
    std::vector<BoardSpatialBlocker> blockers;

    /** Return whether two query results are identical. */
    [[nodiscard]] friend bool operator==(const BoardSpatialQueryResult &lhs,
                                         const BoardSpatialQueryResult &rhs) = default;
};

/**
 * Runtime-only spatial index over normalized board copper shapes.
 *
 * Responsibility: accelerates copper-clearance candidate discovery for DRC and routing while
 *   delegating the exact legality decision to the shared board rule predicate.
 * Invariants: stores derived geometry only; shapes reference existing board nets and copper
 *   layers; no serialization or authoring semantics are owned here.
 */
class BoardSpatialIndex {
  public:
    /** Build an index from board copper and placed footprint pads. */
    BoardSpatialIndex(const Board &board, const FootprintLibrary &footprints);

    /** Build an index from already-normalized board copper shapes. */
    BoardSpatialIndex(const Board &board, std::vector<detail::BoardCopperShape> shapes);

    /** Return normalized shapes in deterministic insertion order. */
    [[nodiscard]] const std::vector<detail::BoardCopperShape> &shapes() const noexcept {
        return shapes_;
    }

    /** Return the conservative board-wide copper-clearance bound used for pruning. */
    [[nodiscard]] double conservative_clearance_mm() const noexcept {
        return conservative_clearance_mm_;
    }

    /** Insert one accepted transient shape so later queries see it. */
    void insert(detail::BoardCopperShape shape);

    /** Return candidate copper-clearance pairs in ascending shape-index order. */
    [[nodiscard]] std::vector<BoardSpatialCandidatePair> copper_clearance_candidates() const;

    /** Return existing candidate obstacle indices for a transient shape. */
    [[nodiscard]] std::vector<std::size_t>
    candidate_obstacles(const detail::BoardCopperShape &candidate) const;

    /** Query whether a transient copper shape may exist with inferred object kind. */
    [[nodiscard]] BoardSpatialQueryResult
    query_legality(const detail::BoardCopperShape &candidate) const;

    /** Query whether a transient copper shape may exist with explicit routing object kind. */
    [[nodiscard]] BoardSpatialQueryResult
    query_legality(const detail::BoardCopperShape &candidate, BoardClearanceKind candidate_kind,
                   BoardKeepoutRestriction keepout_restriction) const;

  private:
    const Board *board_;
    std::vector<detail::BoardCopperShape> shapes_;
    std::vector<detail::BoardSpatialIndexBox> boxes_;
    std::vector<detail::BoardSpatialIndexCell> cells_;
    double conservative_clearance_mm_;
    double cell_size_mm_;

    void validate_shape(const detail::BoardCopperShape &shape) const;

    void index_shape(std::size_t shape_index);
};

namespace detail {

/** Exact shared copper-clearance predicate result. */
struct BoardCopperClearanceCheck {
    /** Whether the pair participates in copper-clearance checking. */
    bool participates = false;
    /** Whether the pair violates the required clearance. */
    bool violates = false;
    /** First common copper layer used for diagnostics, when participating. */
    std::optional<BoardLayerId> layer;
    /** Measured copper-to-copper clearance in millimeters. */
    double actual_clearance_mm = 0.0;
    /** Required copper clearance in millimeters after rooms, classes, and matrix rules. */
    double required_clearance_mm = 0.0;
    /** Room that supplied an override, if any. */
    std::optional<BoardRoomId> room;
};

/** Return the clearance kind represented by a normalized copper shape. */
[[nodiscard]] BoardClearanceKind shape_clearance_kind(const BoardCopperShape &shape);

/** Return the diagnostic message suffix for an unordered copper-clearance kind pair. */
[[nodiscard]] std::string clearance_pair_message(BoardClearanceKind lhs, BoardClearanceKind rhs);

/** Return the board-wide conservative pruning clearance for copper-to-copper queries. */
[[nodiscard]] double maximum_required_copper_clearance(const Board &board);

/** Check copper clearance using object kinds inferred from the shapes. */
[[nodiscard]] BoardCopperClearanceCheck check_copper_clearance(const Board &board,
                                                               const BoardCopperShape &lhs,
                                                               const BoardCopperShape &rhs);

/** Check copper clearance using explicit object kinds for transient routing candidates. */
[[nodiscard]] BoardCopperClearanceCheck
check_copper_clearance(const Board &board, const BoardCopperShape &lhs, BoardClearanceKind lhs_kind,
                       const BoardCopperShape &rhs, BoardClearanceKind rhs_kind);

} // namespace detail

} // namespace volt
