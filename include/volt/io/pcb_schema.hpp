#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include <volt/core/diagnostics.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints.hpp>

namespace volt::io {

/** Return the canonical v1 PCB projection format name. */
[[nodiscard]] inline constexpr std::string_view pcb_format_name() noexcept { return "volt.pcb"; }

/** Return the canonical PCB projection format version written by this library. */
[[nodiscard]] inline constexpr int pcb_format_version() noexcept { return 1; }

namespace detail {

[[nodiscard]] inline std::string pcb_pad_projection_id(ComponentPlacementId placement,
                                                       FootprintPadId pad) {
    return "pcb_pad:" + std::to_string(placement.index()) + ":" + std::to_string(pad.index());
}

[[nodiscard]] inline std::string board_units_name(BoardUnits units) {
    switch (units) {
    case BoardUnits::Millimeters:
        return "mm";
    }
    throw std::logic_error{"Unhandled board units"};
}

[[nodiscard]] inline BoardUnits board_units_from_name(const std::string &value) {
    if (value == "mm") {
        return BoardUnits::Millimeters;
    }
    throw std::logic_error{"Invalid PCB board units"};
}

[[nodiscard]] inline std::string board_side_name(BoardSide side) {
    switch (side) {
    case BoardSide::Top:
        return "top";
    case BoardSide::Bottom:
        return "bottom";
    }
    throw std::logic_error{"Unhandled board side"};
}

[[nodiscard]] inline BoardSide board_side_from_name(const std::string &value) {
    if (value == "top") {
        return BoardSide::Top;
    }
    if (value == "bottom") {
        return BoardSide::Bottom;
    }
    throw std::logic_error{"Invalid PCB board side"};
}

[[nodiscard]] inline std::string board_layer_role_name(BoardLayerRole role) {
    switch (role) {
    case BoardLayerRole::Copper:
        return "copper";
    case BoardLayerRole::SolderMask:
        return "mask";
    case BoardLayerRole::Paste:
        return "paste";
    case BoardLayerRole::Silkscreen:
        return "silkscreen";
    case BoardLayerRole::Fabrication:
        return "fab";
    case BoardLayerRole::EdgeCuts:
        return "edge_cut";
    case BoardLayerRole::Drill:
        return "drill";
    case BoardLayerRole::Mechanical:
        return "mechanical";
    case BoardLayerRole::Courtyard:
        return "courtyard";
    case BoardLayerRole::Keepout:
        return "keepout";
    }
    throw std::logic_error{"Unhandled PCB board layer role"};
}

[[nodiscard]] inline BoardLayerRole board_layer_role_from_name(const std::string &value) {
    if (value == "copper") {
        return BoardLayerRole::Copper;
    }
    if (value == "mask") {
        return BoardLayerRole::SolderMask;
    }
    if (value == "paste") {
        return BoardLayerRole::Paste;
    }
    if (value == "silkscreen") {
        return BoardLayerRole::Silkscreen;
    }
    if (value == "fab") {
        return BoardLayerRole::Fabrication;
    }
    if (value == "edge_cut") {
        return BoardLayerRole::EdgeCuts;
    }
    if (value == "drill") {
        return BoardLayerRole::Drill;
    }
    if (value == "mechanical") {
        return BoardLayerRole::Mechanical;
    }
    if (value == "courtyard") {
        return BoardLayerRole::Courtyard;
    }
    if (value == "keepout") {
        return BoardLayerRole::Keepout;
    }
    throw std::logic_error{"Invalid PCB board layer role"};
}

[[nodiscard]] inline std::string board_layer_side_name(BoardLayerSide side) {
    switch (side) {
    case BoardLayerSide::Top:
        return "top";
    case BoardLayerSide::Bottom:
        return "bottom";
    case BoardLayerSide::Inner:
        return "inner";
    case BoardLayerSide::Both:
        return "both";
    case BoardLayerSide::None:
        return "none";
    }
    throw std::logic_error{"Unhandled PCB board layer side"};
}

[[nodiscard]] inline BoardLayerSide board_layer_side_from_name(const std::string &value) {
    if (value == "top") {
        return BoardLayerSide::Top;
    }
    if (value == "bottom") {
        return BoardLayerSide::Bottom;
    }
    if (value == "inner") {
        return BoardLayerSide::Inner;
    }
    if (value == "both") {
        return BoardLayerSide::Both;
    }
    if (value == "none") {
        return BoardLayerSide::None;
    }
    throw std::logic_error{"Invalid PCB board layer side"};
}

[[nodiscard]] inline std::string board_feature_kind_name(BoardFeatureKind kind) {
    switch (kind) {
    case BoardFeatureKind::MountingHole:
        return "mounting_hole";
    case BoardFeatureKind::Slot:
        return "slot";
    case BoardFeatureKind::Cutout:
        return "cutout";
    case BoardFeatureKind::Fiducial:
        return "fiducial";
    case BoardFeatureKind::ToolingHole:
        return "tooling_hole";
    case BoardFeatureKind::Text:
        return "text";
    case BoardFeatureKind::MechanicalKeepout:
        return "mechanical_keepout";
    }
    throw std::logic_error{"Unhandled PCB board feature kind"};
}

[[nodiscard]] inline BoardFeatureKind board_feature_kind_from_name(const std::string &value) {
    if (value == "mounting_hole") {
        return BoardFeatureKind::MountingHole;
    }
    if (value == "slot") {
        return BoardFeatureKind::Slot;
    }
    if (value == "cutout") {
        return BoardFeatureKind::Cutout;
    }
    if (value == "fiducial") {
        return BoardFeatureKind::Fiducial;
    }
    if (value == "tooling_hole") {
        return BoardFeatureKind::ToolingHole;
    }
    if (value == "text") {
        return BoardFeatureKind::Text;
    }
    if (value == "mechanical_keepout") {
        return BoardFeatureKind::MechanicalKeepout;
    }
    throw std::logic_error{"Invalid PCB board feature kind"};
}

[[nodiscard]] inline std::string footprint_layer_name(FootprintLayer layer) {
    switch (layer) {
    case FootprintLayer::FrontCopper:
        return "front_copper";
    case FootprintLayer::BackCopper:
        return "back_copper";
    case FootprintLayer::FrontSolderMask:
        return "front_solder_mask";
    case FootprintLayer::BackSolderMask:
        return "back_solder_mask";
    case FootprintLayer::FrontPaste:
        return "front_paste";
    case FootprintLayer::BackPaste:
        return "back_paste";
    }
    throw std::logic_error{"Unhandled PCB footprint layer"};
}

[[nodiscard]] inline FootprintLayer footprint_layer_from_name(const std::string &value) {
    if (value == "front_copper") {
        return FootprintLayer::FrontCopper;
    }
    if (value == "back_copper") {
        return FootprintLayer::BackCopper;
    }
    if (value == "front_solder_mask") {
        return FootprintLayer::FrontSolderMask;
    }
    if (value == "back_solder_mask") {
        return FootprintLayer::BackSolderMask;
    }
    if (value == "front_paste") {
        return FootprintLayer::FrontPaste;
    }
    if (value == "back_paste") {
        return FootprintLayer::BackPaste;
    }
    throw std::logic_error{"Invalid PCB footprint layer"};
}

[[nodiscard]] inline std::string footprint_pad_kind_name(FootprintPadKind kind) {
    switch (kind) {
    case FootprintPadKind::SurfaceMount:
        return "surface_mount";
    case FootprintPadKind::ThroughHole:
        return "through_hole";
    }
    throw std::logic_error{"Unhandled PCB footprint pad kind"};
}

[[nodiscard]] inline FootprintPadKind footprint_pad_kind_from_name(const std::string &value) {
    if (value == "surface_mount") {
        return FootprintPadKind::SurfaceMount;
    }
    if (value == "through_hole") {
        return FootprintPadKind::ThroughHole;
    }
    throw std::logic_error{"Invalid PCB footprint pad kind"};
}

[[nodiscard]] inline std::string footprint_pad_shape_name(FootprintPadShape shape) {
    switch (shape) {
    case FootprintPadShape::Rectangle:
        return "rectangle";
    case FootprintPadShape::RoundedRectangle:
        return "rounded_rectangle";
    case FootprintPadShape::Circle:
        return "circle";
    case FootprintPadShape::Oval:
        return "oval";
    }
    throw std::logic_error{"Unhandled PCB footprint pad shape"};
}

[[nodiscard]] inline FootprintPadShape footprint_pad_shape_from_name(const std::string &value) {
    if (value == "rectangle") {
        return FootprintPadShape::Rectangle;
    }
    if (value == "rounded_rectangle") {
        return FootprintPadShape::RoundedRectangle;
    }
    if (value == "circle") {
        return FootprintPadShape::Circle;
    }
    if (value == "oval") {
        return FootprintPadShape::Oval;
    }
    throw std::logic_error{"Invalid PCB footprint pad shape"};
}

[[nodiscard]] inline std::string footprint_pad_plating_name(FootprintPadPlating plating) {
    switch (plating) {
    case FootprintPadPlating::Plated:
        return "plated";
    case FootprintPadPlating::NonPlated:
        return "non_plated";
    }
    throw std::logic_error{"Unhandled PCB footprint pad plating"};
}

[[nodiscard]] inline FootprintPadPlating footprint_pad_plating_from_name(const std::string &value) {
    if (value == "plated") {
        return FootprintPadPlating::Plated;
    }
    if (value == "non_plated") {
        return FootprintPadPlating::NonPlated;
    }
    throw std::logic_error{"Invalid PCB footprint pad plating"};
}

[[nodiscard]] inline std::string
footprint_pad_mechanical_role_name(FootprintPadMechanicalRole role) {
    switch (role) {
    case FootprintPadMechanicalRole::Mounting:
        return "mounting";
    case FootprintPadMechanicalRole::Fiducial:
        return "fiducial";
    case FootprintPadMechanicalRole::MechanicalSupport:
        return "mechanical_support";
    }
    throw std::logic_error{"Unhandled PCB footprint pad mechanical role"};
}

[[nodiscard]] inline FootprintPadMechanicalRole
footprint_pad_mechanical_role_from_name(const std::string &value) {
    if (value == "mounting") {
        return FootprintPadMechanicalRole::Mounting;
    }
    if (value == "fiducial") {
        return FootprintPadMechanicalRole::Fiducial;
    }
    if (value == "mechanical_support") {
        return FootprintPadMechanicalRole::MechanicalSupport;
    }
    throw std::logic_error{"Invalid PCB footprint pad mechanical role"};
}

[[nodiscard]] inline std::string pad_resolution_status_name(PadResolutionStatus status) {
    switch (status) {
    case PadResolutionStatus::Connected:
        return "connected";
    case PadResolutionStatus::Unconnected:
        return "unconnected";
    case PadResolutionStatus::NonElectrical:
        return "non_electrical";
    case PadResolutionStatus::Invalid:
        return "invalid";
    }
    throw std::logic_error{"Unhandled PCB pad resolution status"};
}

[[nodiscard]] inline PadResolutionStatus pad_resolution_status_from_name(const std::string &value) {
    if (value == "connected") {
        return PadResolutionStatus::Connected;
    }
    if (value == "unconnected") {
        return PadResolutionStatus::Unconnected;
    }
    if (value == "non_electrical") {
        return PadResolutionStatus::NonElectrical;
    }
    if (value == "invalid") {
        return PadResolutionStatus::Invalid;
    }
    throw std::logic_error{"Invalid PCB pad resolution status"};
}

} // namespace detail

} // namespace volt::io
