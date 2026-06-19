#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <volt/pcb/board.hpp>

namespace volt::detail {

[[nodiscard]] Diagnostic
drc_diagnostic(std::string_view code, std::string message, std::vector<EntityRef> entities = {},
               std::vector<DiagnosticOverlay> overlays = {},
               std::optional<DiagnosticMeasurement> measurement = std::nullopt);

[[nodiscard]] Diagnostic drc_warning(std::string_view code, std::string message,
                                     std::vector<EntityRef> entities = {},
                                     std::vector<DiagnosticOverlay> overlays = {});

[[nodiscard]] DiagnosticPoint to_diagnostic_point(const BoardPoint &point);

[[nodiscard]] DiagnosticOverlay shape_overlay(const BoardCopperShape &shape, BoardLayerId layer);

[[nodiscard]] std::vector<BoardLayerId> via_copper_layers(const Board &board, const BoardVia &via);

[[nodiscard]] std::vector<DiagnosticOverlay> track_overlays(const BoardTrack &track,
                                                            BoardTrackId track_id);

[[nodiscard]] DiagnosticOverlay via_overlay(const Board &board, const BoardVia &via,
                                            BoardViaId via_id);

[[nodiscard]] std::string clearance_pair_message(BoardClearanceKind lhs, BoardClearanceKind rhs);

} // namespace volt::detail
