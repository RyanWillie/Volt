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
enum class RouteTrackEnd { First, Last };

struct RouteNetRef {
    const Circuit *owner;
    NetId net;
};

struct RouteLayerRef {
    const Board *owner;
    BoardLayerId layer;
};

struct RoutePoint {
    BoardPoint position{0.0, 0.0};
};

/** Complete native resolved definition identity, copied across independent view resolutions. */
struct RoutePad {
    const Board *owner;
    ComponentPlacementId placement;
    FootprintDefinition footprint;
    FootprintPadId pad;
};

struct RouteViaRef {
    const Board *owner;
    BoardViaId via;
};

struct RouteTrackRef {
    const Board *owner;
    BoardTrackId track;
    RouteTrackEnd end;
};

using RouteEndpoint = std::variant<RoutePoint, RoutePad, RouteViaRef, RouteTrackRef>;

struct RouteThrough {
    RouteEndpoint endpoint;
    CornerStyle corner = CornerStyle::Chamfer45;
};

struct RouteToX {
    double coordinate_mm;
};

struct RouteToY {
    double coordinate_mm;
};

struct RouteVia {
    RouteLayerRef destination;
};

struct RouteTo {
    RouteEndpoint endpoint;
    CornerStyle corner = CornerStyle::Chamfer45;
};

using RouteStep = std::variant<RouteThrough, RouteToX, RouteToY, RouteVia, RouteTo>;

struct RouteViaOverride {
    double drill_mm;
    double diameter_mm;
};

/** Copied/const-consumed synchronous description of one existing-net route. */
struct RouteSpec {
    const Board *board;
    std::optional<RouteNetRef> net;
    RouteEndpoint start;
    RouteLayerRef layer;
    std::vector<RouteStep> steps;
    std::optional<double> width_mm = std::nullopt;
    std::optional<RouteViaOverride> via_size = std::nullopt;
};

enum class RouteFailureKind {
    Blocked,
    EndpointUnreachable,
    LayerNotAllowed,
    RuleViolation,
    RuleUnresolvable,
    GeometryUnresolvable,
    NoCandidate
};
enum class RouteRuleSourceKind { BoardRule, NetClassRule, RoomRule, ExplicitOverride, Keepout };
enum class RouteRuleField { Width, ViaDrill, ViaDiameter, Clearance, AllowedLayers };

/** Native source identity; IDs are reporting-only and build-local. */
struct RouteRuleSource {
    RouteRuleSourceKind kind = RouteRuleSourceKind::BoardRule;
    RouteRuleField field = RouteRuleField::Width;
    std::optional<NetClassId> net_class;
    std::optional<BoardRoomId> room;
    std::optional<BoardKeepoutId> keepout;
    std::optional<BoardClearanceKind> first_kind;
    std::optional<BoardClearanceKind> second_kind;
    std::optional<NetClassRuleDerivation> derivation;
};

struct RouteRuleValue {
    double value_mm = 0.0;
    double default_mm = 0.0;
    std::optional<double> requested_mm;
    std::vector<RouteRuleSource> sources;
    std::vector<RouteRuleSource> enforced_sources;
};

struct RoutePrimitiveRules {
    std::size_t candidate_ordinal;
    std::optional<RouteRuleValue> width;
    std::optional<RouteRuleValue> drill;
    std::optional<RouteRuleValue> diameter;
};

struct RouteLayerRules {
    std::vector<BoardLayerId> allowed;
    std::vector<BoardLayerId> excluded;
    RouteRuleSource source;
    NetClassLayerScope scope = NetClassLayerScope::AnyCopper;
    std::vector<std::string> requested_names;
};

struct RouteCandidate {
    std::size_t step_index;
    std::variant<BoardTrack, BoardVia> copper;
};

struct RouteResolvedStep {
    std::size_t step_index;
    BoardPoint start;
    BoardPoint end;
    BoardLayerId incoming_layer;
    BoardLayerId outgoing_layer;
    std::vector<std::size_t> candidates;
};

struct RouteFailure {
    RouteFailureKind kind;
    std::optional<std::size_t> step_index;
    std::string message;
};

struct RouteBlocker {
    std::size_t step_index;
    std::size_t candidate_ordinal;
    BoardSpatialBlockerKind kind;
    std::optional<BoardLayerId> layer;
    std::string message;
    std::optional<BoardPoint> witness;
    std::optional<double> required_mm;
    std::optional<double> actual_mm;
    std::vector<RouteRuleSource> sources;
    std::vector<EntityRef> entities;
    std::string obstacle_label;
    std::optional<std::size_t> earlier_candidate;
    std::size_t obstacle_order = 0;
};

struct RouteClearanceCheck {
    std::size_t candidate_ordinal;
    BoardLayerId layer;
    RouteRuleValue clearance;
    double actual_mm;
    std::vector<EntityRef> obstacle_entities;
    std::optional<std::size_t> earlier_candidate;
};

/** Owned runtime report; no index-backed/borrowed views survive in this value. */
struct RouteResult {
    bool committed = false;
    bool candidate_complete = false;
    std::optional<NetId> net;
    std::vector<RouteCandidate> candidates;
    std::vector<RouteResolvedStep> resolved_steps;
    std::vector<RoutePrimitiveRules> resolved_rules;
    std::vector<RouteClearanceCheck> clearance_checks;
    RouteLayerRules layer_rules;
    std::optional<RouteFailure> failure;
    std::vector<RouteBlocker> blockers;
    std::vector<BoardTrackId> tracks;
    std::vector<BoardViaId> vias;
    std::vector<std::variant<BoardTrackId, BoardViaId>> committed_order;
    std::string message;
};

/** Design rejection, separate from structural KernelError and resource exceptions. */
class RouteRejected : public std::runtime_error {
  public:
    explicit RouteRejected(RouteResult result)
        : std::runtime_error{result.message}, result_{std::move(result)} {}

    [[nodiscard]] const RouteResult &result() const noexcept { return result_; }

  private:
    RouteResult result_;
};

} // namespace volt
