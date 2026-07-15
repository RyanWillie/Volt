#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <volt/core/ids.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints/footprints.hpp>
#include <volt/pcb/geometry/board_geometry.hpp>
#include <volt/pcb/routing/board_spatial_index.hpp>

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

/** Resolved via copper sizing after applying board and net-class floors. */
struct BoardViaSize {
    /** Via drill diameter in millimeters. */
    double drill_diameter_mm = 0.0;
    /** Via outer annular copper diameter in millimeters. */
    double annular_diameter_mm = 0.0;
};

/** Resolve via sizing for a net, flooring caller defaults against board and net-class rules. */
[[nodiscard]] BoardViaSize resolve_via_size(const Board &board, NetId net,
                                            double fallback_drill_diameter_mm,
                                            double fallback_annular_diameter_mm);

/** Typed outcome of an assisted connection attempt. */
struct BoardRouteResult {
    /** Whether a legal route was found and committed. */
    bool routed = false;
    /** Board tracks created for a successful route, in commit order. */
    std::vector<BoardTrackId> tracks;
    /** Board vias created for a successful route, in commit order. */
    std::vector<BoardViaId> vias;
    /** Blockers from the primary rejected candidate when no route was found. */
    std::vector<BoardSpatialBlocker> blockers;
};

/** Broad reason one pad could not be escaped. */
enum class BoardEscapeFailureReason {
    /** The pad escaped successfully. */
    None,
    /** The footprint pad is not connected to a logical net. */
    PadUnconnected,
    /** The pad has no copper layer on this board. */
    NoCopperLayer,
    /** The pad's net class does not permit the pad layer. */
    DisallowedLayer,
    /** Every deterministic escape candidate was rejected by the spatial index. */
    NoLegalCandidate,
};

/** Per-pad outcome of an escape/fanout attempt. */
struct BoardEscapePadResult {
    /** Footprint-local pad label, stable within the selected footprint. */
    std::string pad_label;
    /** Footprint-local pad ID. */
    FootprintPadId pad = FootprintPadId{0};
    /** Board-space pad center used as the stub start. */
    BoardPoint pad_position = BoardPoint{0.0, 0.0};
    /** Existing logical pin resolved for this pad, if any. */
    std::optional<PinId> pin;
    /** Existing logical net resolved for this pad, if any. */
    std::optional<NetId> net;
    /** Connectable endpoint created by a successful escape. */
    BoardPoint endpoint = BoardPoint{0.0, 0.0};
    /** Whether this pad escaped successfully. */
    bool escaped = false;
    /** Failure reason when escaped is false. */
    BoardEscapeFailureReason failure_reason = BoardEscapeFailureReason::None;
    /** Board tracks created for this pad, in commit order. */
    std::vector<BoardTrackId> tracks;
    /** Board vias created for this pad, in commit order. */
    std::vector<BoardViaId> vias;
    /** Blockers from the primary rejected candidate when this pad failed. */
    std::vector<BoardSpatialBlocker> blockers;
};

/** Typed outcome of an escape/fanout attempt for one component. */
struct BoardEscapeResult {
    /** Component requested for escape routing. */
    ComponentId component = ComponentId{0};
    /** Placement escaped for the component, if one exists. */
    std::optional<ComponentPlacementId> placement;
    /** Explicit board room created for the escape envelope, when applicable. */
    std::optional<BoardRoomId> room;
    /** Per-pad outcomes in selected-footprint pad order. */
    std::vector<BoardEscapePadResult> pads;

    /** Return true only when at least one pad was reported and every reported pad escaped. */
    [[nodiscard]] bool complete() const noexcept;
};

/**
 * Authoring-time assisted connection solver over the board copper spatial index.
 *
 * Responsibility: finds DRC-clean copper for explicit point-to-point routes and deterministic
 *   short escape/fanout stubs, committing ordinary kernel-owned tracks/vias only after the
 *   spatial index legality query accepts each candidate primitive.
 * Invariants: legality remains a DRC concern. connect() is all-or-nothing: an unroutable request
 *   is a result value and leaves the board unchanged. escape() rejects unattemptable component
 *   requests at the boundary, then reports per-pad failures while preserving successful stubs and
 *   the escape room. Output is deterministic for a given board state because candidate ordering
 *   and effort caps are fixed.
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

    /** Commit one endpoint-aware routed track after resolving its existing logical net. */
    [[nodiscard]] BoardTrackRouteResult add_track(BoardTrackRouteRequest request);

    /** Attempt to connect two points on a net, committing tracks/vias on success. */
    [[nodiscard]] BoardRouteResult connect(const BoardRouteRequest &request);

    /**
     * Escape one placed component by committing deterministic single-layer short pad fanout stubs.
     *
     * Throws std::invalid_argument when the component is not placed, has no selected physical
     * part, or the selected part cannot resolve to a footprint. Escape vias are deferred for v1.
     */
    [[nodiscard]] BoardEscapeResult escape(ComponentId component);

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

    void require_routable_layer(BoardLayerId layer) const;

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

    [[nodiscard]] BoardSpatialIndex &index() const;

    Board *board_;
    FootprintLibrary footprints_;
    mutable std::optional<BoardSpatialIndex> index_;
};

} // namespace volt
