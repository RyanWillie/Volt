#pragma once
#include <volt/pcb/resolution/board_resolution.hpp>
#include <volt/pcb/routing/route.hpp>

namespace volt::route_detail {
void invalid(const std::string &message);
void owner(bool matches);
void fail(RouteResult &, RouteFailureKind, std::optional<std::size_t>, std::string);
RouteRuleSource board_source(RouteRuleField);
std::vector<RouteRuleSource> clearance_sources(const Board &, NetId, BoardClearanceKind,
                                               const BoardSpatialObstacle &, double,
                                               std::optional<BoardRoomId>);
RouteRuleValue resolve_width(const Board &, NetId, BoardLayerId, const std::vector<BoardPoint> &,
                             std::optional<double>, bool &);
bool is_symbolic(const RouteStep &);
void preflight_target(const Board &, const ResolvedBoardView &, const RouteExit &);
void preflight_target(const Board &, const ResolvedBoardView &, const RouteBeside &);
void preflight_target(const Board &, const ResolvedBoardView &, const RouteGap &);
void resolve_target(const Board &, const ResolvedBoardView &, const std::vector<PadResolution> &,
                    const RouteSpec &, const RouteExit &, BoardPoint, BoardLayerId, std::size_t,
                    RouteResult &);
void resolve_target(const Board &, const ResolvedBoardView &, const std::vector<PadResolution> &,
                    const RouteSpec &, const RouteBeside &, BoardPoint, BoardLayerId, std::size_t,
                    RouteResult &);
void resolve_target(const Board &, const ResolvedBoardView &, const std::vector<PadResolution> &,
                    const RouteSpec &, const RouteGap &, BoardPoint, BoardLayerId, std::size_t,
                    RouteResult &);
} // namespace volt::route_detail
