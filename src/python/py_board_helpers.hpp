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
