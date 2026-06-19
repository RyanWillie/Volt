#pragma once

#include "py_circuit.hpp"

#include "py_circuit_py_helpers.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <volt/pcb/routing/board_router.hpp>

namespace volt::python {

namespace {

constexpr auto default_authoring_via_drill_mm = 0.30;
constexpr auto default_authoring_via_annular_mm = 0.70;

[[nodiscard]] inline volt::BoardRouteEndpoint
board_route_endpoint_from_tuple(const py::tuple &endpoint) {
    if (py::len(endpoint) != 4U) {
        throw py::value_error{
            "Board route endpoint payloads must contain x, y, placement, and pad"};
    }

    const auto x = py::cast<double>(endpoint[0]);
    const auto y = py::cast<double>(endpoint[1]);
    require_finite(x, "Board route endpoint coordinates must be finite");
    require_finite(y, "Board route endpoint coordinates must be finite");

    const auto placement =
        optional_index_from_py(endpoint[2], "Board route endpoint placements must be indexes");
    const auto pad =
        optional_index_from_py(endpoint[3], "Board route endpoint pads must be indexes");
    if (placement.has_value() != pad.has_value()) {
        throw py::value_error{"Board route pad endpoints require placement and pad IDs"};
    }

    const auto position = volt::BoardPoint{x, y};
    if (!placement.has_value()) {
        return volt::BoardRouteEndpoint::board_point(position);
    }
    return volt::BoardRouteEndpoint::footprint_pad(
        position, volt::ComponentPlacementId{placement.value()}, volt::FootprintPadId{pad.value()});
}

[[nodiscard]] inline std::vector<volt::BoardRouteEndpoint>
board_route_endpoints_from_list(const py::list &endpoints) {
    auto result = std::vector<volt::BoardRouteEndpoint>{};
    result.reserve(static_cast<std::size_t>(py::len(endpoints)));
    for (const auto item : endpoints) {
        result.push_back(board_route_endpoint_from_tuple(py::cast<py::tuple>(item)));
    }
    return result;
}

[[nodiscard]] inline std::string
board_spatial_blocker_kind_name(volt::BoardSpatialBlockerKind kind) {
    switch (kind) {
    case volt::BoardSpatialBlockerKind::CopperClearance:
        return "copper_clearance";
    case volt::BoardSpatialBlockerKind::BoardOutline:
        return "board_outline";
    case volt::BoardSpatialBlockerKind::Keepout:
        return "keepout";
    }
    throw std::logic_error{"Unhandled board spatial blocker kind"};
}

[[nodiscard]] inline py::object optional_size_to_object(const std::optional<std::size_t> &value) {
    if (!value.has_value()) {
        return py::none{};
    }
    return py::cast(value.value());
}

template <typename Id>
[[nodiscard]] inline py::object optional_id_to_object(const std::optional<Id> &value) {
    if (!value.has_value()) {
        return py::none{};
    }
    return py::cast(value->index());
}

[[nodiscard]] inline py::dict
board_spatial_blocker_to_dict(const volt::BoardSpatialBlocker &blocker) {
    auto result = py::dict{};
    result["kind"] = board_spatial_blocker_kind_name(blocker.kind);
    result["shape_index"] = optional_size_to_object(blocker.shape_index);
    result["keepout"] = optional_id_to_object(blocker.keepout);
    result["layer"] = optional_id_to_object(blocker.layer);
    result["required_clearance_mm"] = blocker.required_clearance_mm;
    result["actual_clearance_mm"] = blocker.actual_clearance_mm;
    result["room"] = optional_id_to_object(blocker.room);
    return result;
}

[[nodiscard]] inline std::string
board_escape_failure_reason_name(volt::BoardEscapeFailureReason reason) {
    switch (reason) {
    case volt::BoardEscapeFailureReason::None:
        return "none";
    case volt::BoardEscapeFailureReason::PadUnconnected:
        return "pad_unconnected";
    case volt::BoardEscapeFailureReason::NoCopperLayer:
        return "no_copper_layer";
    case volt::BoardEscapeFailureReason::DisallowedLayer:
        return "disallowed_layer";
    case volt::BoardEscapeFailureReason::NoLegalCandidate:
        return "no_legal_candidate";
    }
    throw std::logic_error{"Unhandled board escape failure reason"};
}

[[nodiscard]] inline std::string board_side_name(volt::BoardSide side) {
    switch (side) {
    case volt::BoardSide::Top:
        return "top";
    case volt::BoardSide::Bottom:
        return "bottom";
    }
    throw std::logic_error{"Unhandled board side"};
}

[[nodiscard]] inline std::string board_layer_side_name(volt::BoardLayerSide side) {
    switch (side) {
    case volt::BoardLayerSide::Top:
        return "top";
    case volt::BoardLayerSide::Bottom:
        return "bottom";
    case volt::BoardLayerSide::Inner:
        return "inner";
    case volt::BoardLayerSide::Both:
        return "both";
    case volt::BoardLayerSide::None:
        return "none";
    }
    throw std::logic_error{"Unhandled board layer side"};
}

[[nodiscard]] inline double layer_z_mm(const volt::Board &board, const volt::LayerStack &stack,
                                       std::size_t stack_index) {
    const auto layer_id = stack.layers()[stack_index];
    const auto &layer = board.layer(layer_id);
    const auto half_thickness = stack.board_thickness_mm() / 2.0;
    switch (layer.side()) {
    case volt::BoardLayerSide::Top:
        return half_thickness;
    case volt::BoardLayerSide::Bottom:
        return -half_thickness;
    case volt::BoardLayerSide::Inner:
    case volt::BoardLayerSide::Both:
    case volt::BoardLayerSide::None:
        break;
    }
    if (stack.layers().size() == 1U) {
        return 0.0;
    }
    return half_thickness - ((stack.board_thickness_mm() * static_cast<double>(stack_index)) /
                             static_cast<double>(stack.layers().size() - 1U));
}

} // namespace

} // namespace volt::python
