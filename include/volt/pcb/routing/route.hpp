#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <volt/circuit/constraints/net_classes.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/routing/board_spatial_index.hpp>

namespace volt {

/** Native corner convention for one explicit route leg. */
enum class CornerStyle { Chamfer45, Orthogonal, Direct };
/** Report returns design rejections; neither policy permits violating publication. */
enum class RoutePolicy { Strict, Report };
/** Endpoint of an existing track used as a route target. */
enum class RouteTrackEnd { First, Last };

/** Existing logical net together with its owning circuit. */
struct RouteNetRef {
    /** Circuit that owns the net; must outlive the synchronous call. */
    const Circuit *owner;
    /** Build-local logical net identity. */
    NetId net;
};

/** Active copper layer together with its owning board. */
struct RouteLayerRef {
    /** Board that owns the layer; must outlive the synchronous call. */
    const Board *owner;
    /** Build-local board layer identity. */
    BoardLayerId layer;
};

/** Literal board-coordinate endpoint with no implied logical net. */
struct RoutePoint {
    /** Position in board millimeters. */
    BoardPoint position{0.0, 0.0};
};

/** Complete native resolved definition identity, copied across independent view resolutions. */
struct RoutePad {
    /** Board that owns the placement. */
    const Board *owner;
    /** Placed component whose selected footprint contains the pad. */
    ComponentPlacementId placement;
    /** Exact native footprint definition checked against the resolved placement. */
    FootprintDefinition footprint;
    /** Pad identity local to the exact footprint definition. */
    FootprintPadId pad;
};

/** Existing via endpoint together with its owning board. */
struct RouteViaRef {
    /** Board that owns the via. */
    const Board *owner;
    /** Build-local existing via identity. */
    BoardViaId via;
};

/** Endpoint of an existing track together with its owning board. */
struct RouteTrackRef {
    /** Board that owns the track. */
    const Board *owner;
    /** Build-local existing track identity. */
    BoardTrackId track;
    /** First or last stored track point. */
    RouteTrackEnd end;
};

/** Native endpoint variants accepted by explicit routing. */
using RouteEndpoint = std::variant<RoutePoint, RoutePad, RouteViaRef, RouteTrackRef>;

/** Route through an endpoint and continue with subsequent steps. */
struct RouteThrough {
    /** Destination resolved by the native planner. */
    RouteEndpoint endpoint;
    /** Corner convention for this leg only. */
    CornerStyle corner = CornerStyle::Chamfer45;
};

/** Literal horizontal leg; adjoining axis steps remain sharp. */
struct RouteToX {
    /** Absolute target X coordinate in board millimeters. */
    double coordinate_mm;
};

/** Literal vertical leg; adjoining axis steps remain sharp. */
struct RouteToY {
    /** Absolute target Y coordinate in board millimeters. */
    double coordinate_mm;
};

/** Via at the current position, switching the active copper layer. */
struct RouteVia {
    /** Destination layer; must differ from the active layer. */
    RouteLayerRef destination;
};

/** Required terminal endpoint of an explicit route. */
struct RouteTo {
    /** Terminal destination resolved by the native planner. */
    RouteEndpoint endpoint;
    /** Corner convention for the terminal leg only. */
    CornerStyle corner = CornerStyle::Chamfer45;
};

/** Board-coordinate axis; targets retain the transverse current coordinate. */
enum class RouteAxis { X, Y };
/** Explicit board-coordinate direction; never inferred from geometry. */
enum class RouteDirection { PositiveX, NegativeX, PositiveY, NegativeY };

/** Entire existing track copper, distinct from an endpoint. */
struct RouteTrackCopper {
    /** Borrowed Board identity. */
    const Board *owner;
    /** Existing polyline track. */
    BoardTrackId track{0};
};

/** Active-layer pad copper of a placement; retains each pad's logical net. */
struct RoutePlacedCopper {
    /** Borrowed Board identity. */
    const Board *owner;
    /** Placed component identity. */
    ComponentPlacementId placement;
    /** Full selected footprint identity, as for RoutePad. */
    FootprintDefinition footprint;
};

/** Mechanical keepout exterior; never carries an invented net. */
struct RouteKeepout {
    /** Borrowed Board identity. */
    const Board *owner;
    /** Existing keepout identity. */
    BoardKeepoutId keepout;
};

/** One actual outline segment, in stored vertex order. */
struct RouteBoardEdge {
    /** Borrowed Board identity. */
    const Board *owner;
    /** Segment starts at this vertex and ends at the next cyclic vertex. */
    std::size_t edge_index;
};

/** Closed symbolic geometry operand family. */
using RouteOperand = std::variant<RouteTrackCopper, RoutePad, RoutePlacedCopper, RouteViaRef,
                                  RouteKeepout, RouteBoardEdge>;

/** Move out of an explicitly occupied projected envelope. */
struct RouteExit {
    /** Active geometry from which to exit. */
    RouteOperand operand;
    /** Explicit axis and side. */
    RouteDirection direction;
    /** Additional edge separation in millimeters. */
    double distance_mm = 0.0;
};

/** Move beside an operand, without Exit's current-point containment requirement. */
struct RouteBeside {
    /** Geometry whose selected side controls the target. */
    RouteOperand operand;
    /** Explicit axis and side. */
    RouteDirection direction;
    /** Additional edge separation in millimeters. */
    double distance_mm = 0.0;
};

/** Resolve the centre of a constrained interval for one route only. */
struct RouteGap {
    /** Occupied lower side, or an edge with positive interior half-plane. */
    RouteOperand lower;
    /** Occupied upper side, or an edge with negative interior half-plane. */
    RouteOperand upper;
    /** Axis of separation. */
    RouteAxis axis;
};

/** Ordered native route grammar variants. */
using RouteStep = std::variant<RouteThrough, RouteToX, RouteToY, RouteVia, RouteTo, RouteExit,
                               RouteBeside, RouteGap>;

/** Explicit route-wide via sizing, checked against native minimum rules. */
struct RouteViaOverride {
    /** Requested drill diameter in millimeters. */
    double drill_mm;
    /** Requested outer copper diameter in millimeters. */
    double diameter_mm;
};

/** Copied/const-consumed synchronous description of one existing-net route. */
struct RouteSpec {
    /** Board to receive a complete legal route. */
    const Board *board;
    /** Existing logical net, or absent for exact endpoint-based inference. */
    std::optional<RouteNetRef> net;
    /** Starting endpoint. */
    RouteEndpoint start;
    /** Initial active copper layer. */
    RouteLayerRef layer;
    /** Nonempty ordered steps ending in exactly one terminal RouteTo. */
    std::vector<RouteStep> steps;
    /** Explicit track width in millimeters, or native inherited sizing. */
    std::optional<double> width_mm = std::nullopt;
    /** Explicit drill/diameter pair, or native inherited via sizing. */
    std::optional<RouteViaOverride> via_size = std::nullopt;
};

/** Primary design rejection category, ordered within the same failing step. */
enum class RouteFailureKind {
    Blocked,
    EndpointUnreachable,
    LayerNotAllowed,
    RuleViolation,
    RuleUnresolvable,
    GeometryUnresolvable,
    NoCandidate
};
/** Native provenance source category. */
enum class RouteRuleSourceKind { BoardRule, NetClassRule, RoomRule, ExplicitOverride, Keepout };
/** Rule field whose value or applicability is reported. */
enum class RouteRuleField { Width, ViaDrill, ViaDiameter, Clearance, AllowedLayers };

/** Native source identity; IDs are reporting-only and build-local. */
struct RouteRuleSource {
    /** Category of the native controlling or enforced source. */
    RouteRuleSourceKind kind = RouteRuleSourceKind::BoardRule;
    /** Rule field controlled by this source. */
    RouteRuleField field = RouteRuleField::Width;
    /** Source net class, when applicable. */
    std::optional<NetClassId> net_class;
    /** Source board room, when applicable. */
    std::optional<BoardRoomId> room;
    /** Source keepout, when applicable. */
    std::optional<BoardKeepoutId> keepout;
    /** First primitive category for a pair-specific clearance rule. */
    std::optional<BoardClearanceKind> first_kind;
    /** Second primitive category for a pair-specific clearance rule. */
    std::optional<BoardClearanceKind> second_kind;
    /** Native calculator derivation attached to a net-class rule. */
    std::optional<NetClassRuleDerivation> derivation;
};

/** Resolved physical rule value with ordered controlling and enforced sources. */
struct RouteRuleValue {
    /** Effective selected or explicitly requested value in millimeters. */
    double value_mm = 0.0;
    /** Native inherited value before an explicit override. */
    double default_mm = 0.0;
    /** Explicit requested value, when supplied. */
    std::optional<double> requested_mm;
    /** Ordered sources controlling the selected value. */
    std::vector<RouteRuleSource> sources;
    /** Ordered native minimum sources enforced against the request. */
    std::vector<RouteRuleSource> enforced_sources;
};

/** Width or via sizing resolved for one candidate primitive. */
struct RoutePrimitiveRules {
    /** Ordinal in RouteResult::candidates. */
    std::size_t candidate_ordinal;
    /** Track width evidence; absent for a via. */
    std::optional<RouteRuleValue> width;
    /** Via drill evidence; absent for a track. */
    std::optional<RouteRuleValue> drill;
    /** Via outer copper diameter evidence; absent for a track. */
    std::optional<RouteRuleValue> diameter;
};

/** Native allowed-layer resolution for the route's logical net. */
struct RouteLayerRules {
    /** Enabled committed-stack copper layers allowed by the native rule. */
    std::vector<BoardLayerId> allowed;
    /** Board layers excluded by the native rule or stack configuration. */
    std::vector<BoardLayerId> excluded;
    /** Source of the allowed-layer rule. */
    RouteRuleSource source;
    /** Native net-class layer scope. */
    NetClassLayerScope scope = NetClassLayerScope::AnyCopper;
    /** Authored explicit layer names, when the native scope uses them. */
    std::vector<std::string> requested_names;
};

/** Representable native copper primitive before publication. */
struct RouteCandidate {
    /** Request step that generated this primitive. */
    std::size_t step_index;
    /** Track or via geometry carrying its actual existing logical net. */
    std::variant<BoardTrack, BoardVia> copper;
};

/** One independently enforced target-side constraint with owned provenance. */
struct RouteTargetConstraint {
    /** True for the upper/negative occupied side. */
    bool upper_side;
    /** Reporting-only existing object identities. */
    std::vector<EntityRef> entities;
    /** Final native pair clearance or mechanical reserve, with sources. */
    RouteRuleValue clearance;
    /** Historical continuity-group copper exemption; never true for mechanical geometry. */
    bool exempt;
};

/** Owned final analytic target and constrained interval; no evidence is fabricated on failure. */
struct RouteTargetResolution {
    /** Request step that generated this target. */
    std::size_t step_index;
    /** Final board-coordinate target. */
    BoardPoint position;
    /** Board axis whose coordinate changed. */
    RouteAxis axis;
    /** Raw occupied lower boundary. */
    double raw_lower_mm;
    /** Raw occupied upper boundary. */
    double raw_upper_mm;
    /** Lower permitted route centre coordinate. */
    double lower_mm;
    /** Upper permitted route centre coordinate. */
    double upper_mm;
    /** Final native width evidence. */
    RouteRuleValue width;
    /** Ordered lower-then-upper per-shape constraints. */
    std::vector<RouteTargetConstraint> constraints;
};

/** Native endpoint and layer resolution for one request step. */
struct RouteResolvedStep {
    /** Ordinal in the request's ordered steps. */
    std::size_t step_index;
    /** Resolved starting position. */
    BoardPoint start;
    /** Resolved ending position. */
    BoardPoint end;
    /** Active copper layer before this step. */
    BoardLayerId incoming_layer;
    /** Active copper layer after this step. */
    BoardLayerId outgoing_layer;
    /** Candidate primitive ordinals generated by this step. */
    std::vector<std::size_t> candidates;
};

/** Owned primary design rejection, distinct from structural kernel errors. */
struct RouteFailure {
    /** Native design rejection category. */
    RouteFailureKind kind;
    /** Failing step, or absent for a start/whole-request diagnosis. */
    std::optional<std::size_t> step_index;
    /** Native diagnosis without the route summary prefix. */
    std::string message;
};

/** Owned native legality blocker and its reporting-only evidence. */
struct RouteBlocker {
    /** Request step that generated the blocked primitive. */
    std::size_t step_index;
    /** Ordinal of the blocked candidate primitive. */
    std::size_t candidate_ordinal;
    /** Shared native spatial legality category. */
    BoardSpatialBlockerKind kind;
    /** Applicable copper layer, when layer-specific. */
    std::optional<BoardLayerId> layer;
    /** Native blocker diagnosis. */
    std::string message;
    /** Geometric witness point, when supplied by the native predicate. */
    std::optional<BoardPoint> witness;
    /** Required clearance in millimeters, when applicable. */
    std::optional<double> required_mm;
    /** Measured clearance in millimeters, when applicable. */
    std::optional<double> actual_mm;
    /** Ordered native rule provenance. */
    std::vector<RouteRuleSource> sources;
    /** Reporting-only existing board entity references. */
    std::vector<EntityRef> entities;
    /** Native label for the existing obstacle or earlier candidate. */
    std::string obstacle_label;
    /** Earlier candidate ordinal for intra-candidate legality checks. */
    std::optional<std::size_t> earlier_candidate;
    /** Stable native obstacle ordinal used for deterministic ordering. */
    std::size_t obstacle_order = 0;
};

/** Participating native copper-clearance check, including legal pairs. */
struct RouteClearanceCheck {
    /** Ordinal of the candidate primitive. */
    std::size_t candidate_ordinal;
    /** Shared copper layer on which the predicate participated. */
    BoardLayerId layer;
    /** Resolved clearance and its controlling sources. */
    RouteRuleValue clearance;
    /** Measured copper separation in millimeters. */
    double actual_mm;
    /** Reporting-only existing obstacle entity references. */
    std::vector<EntityRef> obstacle_entities;
    /** Earlier candidate ordinal, when the obstacle is staged copper. */
    std::optional<std::size_t> earlier_candidate;
};

/** Owned runtime report; no index-backed/borrowed views survive in this value. */
struct RouteResult {
    /** Whether complete legal copper was atomically published. */
    bool committed = false;
    /** Whether all requested copper could be represented. */
    bool candidate_complete = false;
    /** Existing logical net selected by the native request resolution. */
    std::optional<NetId> net;
    /** Owned candidate copper in primitive order. */
    std::vector<RouteCandidate> candidates;
    /** Owned endpoint/layer resolution in request-step order. */
    std::vector<RouteResolvedStep> resolved_steps;
    /** Owned symbolic target evidence in step order. */
    std::vector<RouteTargetResolution> target_resolutions;
    /** Native width/via sizing evidence in candidate order. */
    std::vector<RoutePrimitiveRules> resolved_rules;
    /** Native participating copper-clearance checks. */
    std::vector<RouteClearanceCheck> clearance_checks;
    /** Native allowed-layer evidence. */
    RouteLayerRules layer_rules;
    /** Primary design rejection, absent on committed success. */
    std::optional<RouteFailure> failure;
    /** Deterministically ordered owned blockers. */
    std::vector<RouteBlocker> blockers;
    /** Published track IDs; empty for an uncommitted result. */
    std::vector<BoardTrackId> tracks;
    /** Published via IDs; empty for an uncommitted result. */
    std::vector<BoardViaId> vias;
    /** Published typed IDs in candidate order. */
    std::vector<std::variant<BoardTrackId, BoardViaId>> committed_order;
    /** Native UTF-8 success or rejection summary. */
    std::string message;
};

/** Design rejection, separate from structural KernelError and resource exceptions. */
class RouteRejected : public std::runtime_error {
  public:
    /** Own a native rejected result and use its summary as exception text. */
    explicit RouteRejected(RouteResult result)
        : std::runtime_error{result.message}, result_{std::move(result)} {}

    /** Borrow the immutable owned rejection report. */
    [[nodiscard]] const RouteResult &result() const noexcept { return result_; }

  private:
    RouteResult result_;
};

} // namespace volt
