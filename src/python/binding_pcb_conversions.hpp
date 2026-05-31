#pragma once

#include "binding_common.hpp"

#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints.hpp>

namespace volt::python {

namespace {

[[nodiscard]] inline volt::BoardLayerRole parse_board_layer_role(const std::string &value) {
    if (value == "copper" || value == "Copper") {
        return volt::BoardLayerRole::Copper;
    }
    if (value == "solder_mask" || value == "solder-mask" || value == "SolderMask") {
        return volt::BoardLayerRole::SolderMask;
    }
    if (value == "paste" || value == "Paste") {
        return volt::BoardLayerRole::Paste;
    }
    if (value == "silkscreen" || value == "Silkscreen") {
        return volt::BoardLayerRole::Silkscreen;
    }
    if (value == "fabrication" || value == "Fabrication") {
        return volt::BoardLayerRole::Fabrication;
    }
    if (value == "edge_cuts" || value == "edge-cuts" || value == "EdgeCuts") {
        return volt::BoardLayerRole::EdgeCuts;
    }
    if (value == "drill" || value == "Drill") {
        return volt::BoardLayerRole::Drill;
    }
    if (value == "mechanical" || value == "Mechanical") {
        return volt::BoardLayerRole::Mechanical;
    }
    if (value == "courtyard" || value == "Courtyard") {
        return volt::BoardLayerRole::Courtyard;
    }
    if (value == "keepout" || value == "Keepout") {
        return volt::BoardLayerRole::Keepout;
    }
    throw std::invalid_argument{"Unknown board layer role"};
}

[[nodiscard]] inline volt::BoardLayerSide parse_board_layer_side(const std::string &value) {
    if (value == "top" || value == "Top") {
        return volt::BoardLayerSide::Top;
    }
    if (value == "bottom" || value == "Bottom") {
        return volt::BoardLayerSide::Bottom;
    }
    if (value == "inner" || value == "Inner") {
        return volt::BoardLayerSide::Inner;
    }
    if (value == "both" || value == "Both") {
        return volt::BoardLayerSide::Both;
    }
    if (value == "none" || value == "None") {
        return volt::BoardLayerSide::None;
    }
    throw std::invalid_argument{"Unknown board layer side"};
}

[[nodiscard]] inline volt::BoardSide parse_board_side(const std::string &value) {
    if (value == "top" || value == "Top") {
        return volt::BoardSide::Top;
    }
    if (value == "bottom" || value == "Bottom") {
        return volt::BoardSide::Bottom;
    }
    throw std::invalid_argument{"Unknown board side"};
}

[[nodiscard]] inline volt::BoardZoneFill parse_board_zone_fill(const std::string &value) {
    if (value == "solid" || value == "Solid") {
        return volt::BoardZoneFill::Solid;
    }
    throw std::invalid_argument{"Unknown board zone fill"};
}

[[nodiscard]] inline volt::BoardKeepoutRestriction
parse_board_keepout_restriction(const std::string &value) {
    if (value == "copper" || value == "Copper") {
        return volt::BoardKeepoutRestriction::Copper;
    }
    if (value == "via" || value == "Via") {
        return volt::BoardKeepoutRestriction::Via;
    }
    if (value == "placement" || value == "Placement") {
        return volt::BoardKeepoutRestriction::Placement;
    }
    if (value == "all" || value == "All") {
        return volt::BoardKeepoutRestriction::All;
    }
    throw std::invalid_argument{"Unknown board keepout restriction"};
}

[[nodiscard]] inline volt::FootprintPadShape parse_footprint_pad_shape(const std::string &value) {
    if (value == "rectangle" || value == "Rectangle") {
        return volt::FootprintPadShape::Rectangle;
    }
    if (value == "rounded_rectangle" || value == "rounded-rectangle" ||
        value == "RoundedRectangle") {
        return volt::FootprintPadShape::RoundedRectangle;
    }
    if (value == "circle" || value == "Circle") {
        return volt::FootprintPadShape::Circle;
    }
    if (value == "oval" || value == "Oval") {
        return volt::FootprintPadShape::Oval;
    }
    throw std::invalid_argument{"Unknown footprint pad shape"};
}

[[nodiscard]] inline volt::FootprintPadPlating
parse_footprint_pad_plating(const std::string &value) {
    if (value == "plated" || value == "Plated") {
        return volt::FootprintPadPlating::Plated;
    }
    if (value == "non_plated" || value == "non-plated" || value == "NonPlated") {
        return volt::FootprintPadPlating::NonPlated;
    }
    throw std::invalid_argument{"Unknown footprint pad plating"};
}

[[nodiscard]] inline std::optional<volt::FootprintPadMechanicalRole>
parse_optional_footprint_mechanical_role(py::handle value) {
    if (value.is_none()) {
        return std::nullopt;
    }
    const auto role = py::cast<std::string>(value);
    if (role == "mounting" || role == "Mounting") {
        return volt::FootprintPadMechanicalRole::Mounting;
    }
    if (role == "fiducial" || role == "Fiducial") {
        return volt::FootprintPadMechanicalRole::Fiducial;
    }
    if (role == "mechanical_support" || role == "mechanical-support" ||
        role == "MechanicalSupport") {
        return volt::FootprintPadMechanicalRole::MechanicalSupport;
    }
    throw std::invalid_argument{"Unknown footprint pad mechanical role"};
}

[[nodiscard]] inline volt::FootprintLayerSet
footprint_layer_set_from_string(const std::string &value) {
    if (value == "front_smd" || value == "front-smd" || value == "FrontSmd") {
        return volt::FootprintLayerSet::front_smd();
    }
    if (value == "back_smd" || value == "back-smd" || value == "BackSmd") {
        return volt::FootprintLayerSet::back_smd();
    }
    if (value == "through_hole" || value == "through-hole" || value == "ThroughHole") {
        return volt::FootprintLayerSet::through_hole();
    }
    if (value == "mechanical_hole" || value == "mechanical-hole" || value == "MechanicalHole") {
        return volt::FootprintLayerSet::mechanical_hole();
    }
    throw std::invalid_argument{"Unknown footprint layer set"};
}

[[nodiscard]] inline volt::FootprintDrill footprint_drill_from_dict(const py::dict &data) {
    return volt::FootprintDrill{
        py::cast<double>(data["diameter"]),
        parse_footprint_pad_plating(py::cast<std::string>(data["plating"]))};
}

[[nodiscard]] inline volt::FootprintPad footprint_pad_from_dict(const py::dict &data) {
    const auto kind = py::cast<std::string>(data["kind"]);
    const auto label = py::cast<std::string>(data["label"]);
    const auto shape = parse_footprint_pad_shape(py::cast<std::string>(data["shape"]));
    const auto position = py::cast<std::pair<double, double>>(data["position"]);
    const auto size = py::cast<std::pair<double, double>>(data["size"]);
    auto layers = footprint_layer_set_from_string(py::cast<std::string>(data["layers"]));
    const auto mechanical_role = parse_optional_footprint_mechanical_role(data["mechanical_role"]);

    if (kind == "surface_mount" || kind == "surface-mount" || kind == "SurfaceMount") {
        return volt::FootprintPad::surface_mount(
            label, shape, volt::FootprintPoint{position.first, position.second},
            volt::FootprintSize{size.first, size.second}, std::move(layers), mechanical_role);
    }
    if (kind == "through_hole" || kind == "through-hole" || kind == "ThroughHole") {
        return volt::FootprintPad::through_hole(
            label, shape, volt::FootprintPoint{position.first, position.second},
            volt::FootprintSize{size.first, size.second}, std::move(layers),
            footprint_drill_from_dict(py::cast<py::dict>(data["drill"])), mechanical_role);
    }
    throw std::invalid_argument{"Unknown footprint pad kind"};
}

[[nodiscard]] inline volt::FootprintDefinition
footprint_definition_from_dict(const py::dict &data) {
    const auto ref = py::cast<std::pair<std::string, std::string>>(data["ref"]);
    auto pads = std::vector<volt::FootprintPad>{};
    const auto pad_data = py::cast<py::list>(data["pads"]);
    pads.reserve(static_cast<std::size_t>(py::len(pad_data)));
    for (const auto item : pad_data) {
        pads.push_back(footprint_pad_from_dict(py::cast<py::dict>(item)));
    }
    return volt::FootprintDefinition{volt::FootprintRef{ref.first, ref.second}, std::move(pads)};
}

[[nodiscard]] inline std::string pad_resolution_status_name(volt::PadResolutionStatus status) {
    switch (status) {
    case volt::PadResolutionStatus::Connected:
        return "connected";
    case volt::PadResolutionStatus::Unconnected:
        return "unconnected";
    case volt::PadResolutionStatus::NonElectrical:
        return "non_electrical";
    case volt::PadResolutionStatus::Invalid:
        return "invalid";
    }
    throw std::logic_error{"Unhandled pad resolution status"};
}

} // namespace

} // namespace volt::python
