#pragma once

#include "binding_conversions.hpp"
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
    const auto &layer = board.get(layer_id);
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
