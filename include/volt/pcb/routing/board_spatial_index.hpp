#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include <volt/core/ids.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/copper/board_copper.hpp>
#include <volt/pcb/features/board_features.hpp>
#include <volt/pcb/footprints/footprints.hpp>

namespace volt {

namespace detail {
struct BoardSpatialIndexBox;
struct BoardSpatialIndexCell;
struct BoardSpatialIndexState;
} // namespace detail

/** Geometric primitive category for routing-facing spatial query candidates. */
enum class BoardSpatialQueryShapeKind {
    Disc,
    Segment,
    Polygon,
};

/** Stable public copper shape used by routing-facing spatial queries. */
struct BoardSpatialQueryShape {
    /** Candidate geometry category. */
    BoardSpatialQueryShapeKind kind = BoardSpatialQueryShapeKind::Segment;
    /** Logical net the candidate would implement. */
    NetId net;
    /** Copper layers the candidate would occupy. */
    std::vector<BoardLayerId> layers;
    /** Shape points: center, segment endpoints, or polygon vertices. */
    std::vector<BoardPoint> points;
    /** Radius used for circular and segment candidates. */
    double radius_mm = 0.0;
    /** Clearance matrix kind to use for copper and board-edge checks. */
    BoardClearanceKind clearance_kind = BoardClearanceKind::Track;
    /** Keepout restriction kind to apply to this candidate. */
    BoardKeepoutRestriction keepout_restriction = BoardKeepoutRestriction::Copper;
};

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

class BoardSpatialIndex;

namespace detail {
void insert_after_board_mutation(BoardSpatialIndex &index, BoardSpatialQueryShape shape,
                                 std::size_t previous_geometry_mutation_count);
} // namespace detail

/**
 * Runtime-only spatial index over normalized board copper shapes.
 *
 * Responsibility: accelerates copper-clearance candidate discovery for DRC and routing while
 *   delegating the exact legality decision to the shared board rule predicate.
 * Invariants: stores a snapshot of board geometry and the conservative clearance bound at
 *   construction; callers must rebuild after board geometry/rule mutations except for
 *   insertions made through this index; no serialization or authoring semantics are owned here.
 */
class BoardSpatialIndex {
  public:
    /** Build an empty index over the board's current rules for incremental routing. */
    explicit BoardSpatialIndex(const Board &board);
    /** Copy spatial-index state. */
    BoardSpatialIndex(const BoardSpatialIndex &other);
    /** Move spatial-index state. */
    BoardSpatialIndex(BoardSpatialIndex &&other) noexcept;
    /** Copy spatial-index state. */
    BoardSpatialIndex &operator=(const BoardSpatialIndex &other);
    /** Move spatial-index state. */
    BoardSpatialIndex &operator=(BoardSpatialIndex &&other) noexcept;
    /** Destroy spatial-index state. */
    ~BoardSpatialIndex();

    /** Reject temporary board bindings because the index stores a caller-owned board pointer. */
    explicit BoardSpatialIndex(const Board &&board) = delete;

    /** Build an index from board copper and placed footprint pads. */
    BoardSpatialIndex(const Board &board, const FootprintLibrary &footprints);

    /** Reject temporary board bindings because the index stores a caller-owned board pointer. */
    BoardSpatialIndex(const Board &&board, const FootprintLibrary &footprints) = delete;

    /** Return the conservative board-wide copper-clearance bound used for pruning. */
    [[nodiscard]] double conservative_clearance_mm() const noexcept;

    /** Insert one accepted transient shape so later queries see it. */
    void insert(BoardSpatialQueryShape shape);

    /** Return candidate copper-clearance pairs in ascending shape-index order. */
    [[nodiscard]] std::vector<BoardSpatialCandidatePair> copper_clearance_candidates() const;

    /** Return existing candidate obstacle indices for a transient shape. */
    [[nodiscard]] std::vector<std::size_t>
    candidate_obstacles(const BoardSpatialQueryShape &candidate) const;

    /** Query whether a transient copper shape may exist at its candidate location. */
    [[nodiscard]] BoardSpatialQueryResult
    query_legality(const BoardSpatialQueryShape &candidate) const;

  private:
    friend void
    detail::validate_copper_clearance(const Board &board,
                                      const std::vector<detail::BoardCopperShape> &shapes,
                                      DiagnosticReport &report);
    friend void detail::insert_after_board_mutation(BoardSpatialIndex &index,
                                                    BoardSpatialQueryShape shape,
                                                    std::size_t previous_geometry_mutation_count);

    BoardSpatialIndex(const Board &board, std::vector<detail::BoardCopperShape> shapes);
    BoardSpatialIndex(const Board &&board, std::vector<detail::BoardCopperShape> shapes) = delete;

    [[nodiscard]] detail::BoardSpatialIndexState &mutable_state() noexcept;
    [[nodiscard]] const detail::BoardSpatialIndexState &state() const noexcept;

    std::unique_ptr<detail::BoardSpatialIndexState> state_;

    [[nodiscard]] static bool cell_less(const detail::BoardSpatialIndexCell &lhs,
                                        const detail::BoardSpatialIndexCell &rhs);

    [[nodiscard]] static bool same_cell_key(const detail::BoardSpatialIndexCell &lhs,
                                            const detail::BoardSpatialIndexCell &rhs);

    [[nodiscard]] static detail::BoardSpatialIndexBox
    shape_box(const detail::BoardCopperShape &shape);

    [[nodiscard]] static detail::BoardSpatialIndexBox outline_box(const BoardOutline &outline);

    [[nodiscard]] static detail::BoardSpatialIndexBox merge_box(detail::BoardSpatialIndexBox lhs,
                                                                detail::BoardSpatialIndexBox rhs);

    [[nodiscard]] static detail::BoardSpatialIndexBox expanded_box(detail::BoardSpatialIndexBox box,
                                                                   double expansion_mm);

    [[nodiscard]] static bool boxes_intersect(detail::BoardSpatialIndexBox lhs,
                                              detail::BoardSpatialIndexBox rhs);

    [[nodiscard]] static long long cell_key(double value, double cell_size_mm);

    [[nodiscard]] static double
    extent_cell_size(const Board &board, const std::vector<detail::BoardSpatialIndexBox> &boxes);

    [[nodiscard]] static detail::BoardCopperShape to_copper_shape(BoardSpatialQueryShape candidate);

    void ensure_conservative_bound_current() const;

    void ensure_geometry_current() const;

    void validate_shape(const detail::BoardCopperShape &shape) const;

    void append_shape(detail::BoardCopperShape shape);

    void insert(detail::BoardCopperShape shape);

    [[nodiscard]] std::vector<std::size_t>
    candidate_obstacles(const detail::BoardCopperShape &candidate) const;

    [[nodiscard]] BoardSpatialQueryResult
    query_legality(const detail::BoardCopperShape &candidate, BoardClearanceKind candidate_kind,
                   BoardKeepoutRestriction keepout_restriction) const;

    void index_shape(std::size_t shape_index);
};

} // namespace volt
