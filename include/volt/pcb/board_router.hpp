#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <volt/core/ids.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/board_geometry.hpp>
#include <volt/pcb/board_spatial_index.hpp>
#include <volt/pcb/footprints.hpp>

namespace volt {

/** Authoring request to connect two board points on one existing logical net. */
struct BoardRouteRequest {
    /** Existing logical net the route physically implements. */
    NetId net;
    /** Board-space start point, typically a resolved pad center. */
    BoardPoint start = BoardPoint{0.0, 0.0};
    /** Board-space end point, typically a resolved pad center. */
    BoardPoint end = BoardPoint{0.0, 0.0};
    /** Board copper layer the start point sits on. */
    BoardLayerId start_layer;
    /** Board copper layer the end point sits on. */
    BoardLayerId end_layer;
};

/** Resolved per-route copper sizing and layer constraints from the shared rule path. */
struct BoardRouteParameters {
    /** Routed track width in millimeters. */
    double track_width_mm = 0.0;
    /** Via drill diameter in millimeters. */
    double via_drill_mm = 0.0;
    /** Via outer annular copper diameter in millimeters. */
    double via_diameter_mm = 0.0;
    /** Board copper layers the net's class permits, in ascending layer-id order. */
    std::vector<BoardLayerId> allowed_layers;
};

/** Typed outcome of an assisted connection attempt. */
struct BoardRouteResult {
    /** Whether a legal route was found and committed. */
    bool routed = false;
    /** Board tracks created for a successful route, in commit order. */
    std::vector<BoardTrackId> tracks;
    /** Board vias created for a successful route, in commit order. */
    std::vector<BoardViaId> vias;
    /** Blockers from the final rejected candidate when no route was found. */
    std::vector<BoardSpatialBlocker> blockers;
};

/**
 * Authoring-time assisted connection solver over the board copper spatial index.
 *
 * Responsibility: finds a DRC-clean copper path between two points on one net using pattern
 *   routing (straight / L / Z) with a bounded deterministic walk-around fallback, committing
 *   ordinary kernel-owned tracks and vias only when every candidate primitive was accepted by
 *   the spatial index legality query.
 * Invariants: legality remains a DRC concern; a route that cannot be found is a result value,
 *   never an exception; on any failure the board is left unchanged; output is deterministic for
 *   a given board state because candidate ordering and effort caps are fixed.
 * Collaborators: mutates the Board through its public track/via APIs and mirrors each committed
 *   primitive into its own BoardSpatialIndex in the same step; resolves sizing through
 *   resolve_net_class_rules with BoardDesignRules minimums as the floor.
 */
class BoardRouter {
  public:
    /** Build a router with a spatial index over all current board copper. */
    BoardRouter(Board &board, const FootprintLibrary &footprints);

    /** Resolve the copper sizing and allowed layers a net would route with. */
    [[nodiscard]] BoardRouteParameters resolve_parameters(NetId net) const;

    /** Attempt to connect two points on a net, committing tracks/vias on success. */
    [[nodiscard]] BoardRouteResult connect(const BoardRouteRequest &request);

  private:
    /** One copper segment a candidate route would commit on a single layer. */
    struct SegmentStep {
        /** Layer the segment is routed on. */
        BoardLayerId layer;
        /** Segment start point in board coordinates. */
        BoardPoint start;
        /** Segment end point in board coordinates. */
        BoardPoint end;
    };

    /** One via a candidate route would commit between two layers at a point. */
    struct ViaStep {
        /** Board-space via center. */
        BoardPoint position;
        /** First layer of the via span. */
        BoardLayerId start_layer;
        /** Second layer of the via span. */
        BoardLayerId end_layer;
    };

    /** Ordered primitives forming one complete candidate route. */
    struct Candidate {
        /** Track segments in commit order. */
        std::vector<SegmentStep> segments;
        /** Vias in commit order. */
        std::vector<ViaStep> vias;
    };

    [[nodiscard]] bool layer_allowed(NetId net, BoardLayerId layer) const;

    [[nodiscard]] std::vector<Candidate>
    pattern_candidates(const BoardRouteRequest &request, const BoardRouteParameters &params) const;

    [[nodiscard]] std::vector<Candidate>
    walk_around_candidates(const BoardRouteRequest &request,
                           const BoardRouteParameters &params) const;

    [[nodiscard]] std::optional<std::vector<BoardSpatialBlocker>>
    evaluate(const Candidate &candidate, const BoardRouteRequest &request,
             const BoardRouteParameters &params) const;

    void commit(const Candidate &candidate, const BoardRouteRequest &request,
                const BoardRouteParameters &params, BoardRouteResult &result);

    Board *board_;
    BoardSpatialIndex index_;
};

} // namespace volt
