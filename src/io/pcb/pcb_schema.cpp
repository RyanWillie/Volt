#include <volt/io/pcb/pcb_schema.hpp>

#include <volt/core/errors.hpp>

namespace volt::io::detail {

[[nodiscard]] std::string pcb_pad_projection_id(ComponentPlacementId placement,
                                                FootprintPadId pad) {
    return "pcb_pad:" + std::to_string(placement.index()) + ":" + std::to_string(pad.index());
}

[[nodiscard]] std::string pcb_ratsnest_edge_id(NetId net, std::size_t edge) {
    return "ratsnest:" + std::to_string(net.index()) + ":" + std::to_string(edge);
}

[[nodiscard]] std::string board_units_name(BoardUnits units) {
    switch (units) {
    case BoardUnits::Millimeters:
        return "mm";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled board units"};
}

[[nodiscard]] BoardUnits board_units_from_name(const std::string &value) {
    if (value == "mm") {
        return BoardUnits::Millimeters;
    }
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid PCB board units"};
}

[[nodiscard]] std::string board_side_name(BoardSide side) {
    switch (side) {
    case BoardSide::Top:
        return "top";
    case BoardSide::Bottom:
        return "bottom";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled board side"};
}

[[nodiscard]] BoardSide board_side_from_name(const std::string &value) {
    if (value == "top") {
        return BoardSide::Top;
    }
    if (value == "bottom") {
        return BoardSide::Bottom;
    }
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid PCB board side"};
}

[[nodiscard]] std::string board_layer_role_name(BoardLayerRole role) {
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
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled PCB board layer role"};
}

[[nodiscard]] BoardLayerRole board_layer_role_from_name(const std::string &value) {
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
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid PCB board layer role"};
}

[[nodiscard]] std::string board_layer_side_name(BoardLayerSide side) {
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
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled PCB board layer side"};
}

[[nodiscard]] BoardLayerSide board_layer_side_from_name(const std::string &value) {
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
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid PCB board layer side"};
}

[[nodiscard]] std::string board_feature_kind_name(BoardFeatureKind kind) {
    switch (kind) {
    case BoardFeatureKind::Hole:
        return "hole";
    case BoardFeatureKind::Slot:
        return "slot";
    case BoardFeatureKind::Cutout:
        return "cutout";
    case BoardFeatureKind::Circle:
        return "circle";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled PCB board feature kind"};
}

[[nodiscard]] BoardFeatureKind board_feature_kind_from_name(const std::string &value) {
    if (value == "hole") {
        return BoardFeatureKind::Hole;
    }
    if (value == "slot") {
        return BoardFeatureKind::Slot;
    }
    if (value == "cutout") {
        return BoardFeatureKind::Cutout;
    }
    if (value == "circle") {
        return BoardFeatureKind::Circle;
    }
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid PCB board feature kind"};
}

[[nodiscard]] std::string board_zone_fill_name(BoardZoneFill fill) {
    switch (fill) {
    case BoardZoneFill::Solid:
        return "solid";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled PCB board zone fill"};
}

[[nodiscard]] BoardZoneFill board_zone_fill_from_name(const std::string &value) {
    if (value == "solid") {
        return BoardZoneFill::Solid;
    }
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid PCB board zone fill"};
}

[[nodiscard]] std::string board_keepout_restriction_name(BoardKeepoutRestriction restriction) {
    switch (restriction) {
    case BoardKeepoutRestriction::Copper:
        return "copper";
    case BoardKeepoutRestriction::Via:
        return "via";
    case BoardKeepoutRestriction::Placement:
        return "placement";
    case BoardKeepoutRestriction::All:
        return "all";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled PCB board keepout restriction"};
}

[[nodiscard]] BoardKeepoutRestriction
board_keepout_restriction_from_name(const std::string &value) {
    if (value == "copper") {
        return BoardKeepoutRestriction::Copper;
    }
    if (value == "via") {
        return BoardKeepoutRestriction::Via;
    }
    if (value == "placement") {
        return BoardKeepoutRestriction::Placement;
    }
    if (value == "all") {
        return BoardKeepoutRestriction::All;
    }
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid PCB board keepout restriction"};
}

[[nodiscard]] std::string footprint_layer_name(FootprintLayer layer) {
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
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled PCB footprint layer"};
}

[[nodiscard]] FootprintLayer footprint_layer_from_name(const std::string &value) {
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
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid PCB footprint layer"};
}

[[nodiscard]] std::string footprint_pad_kind_name(FootprintPadKind kind) {
    switch (kind) {
    case FootprintPadKind::SurfaceMount:
        return "surface_mount";
    case FootprintPadKind::ThroughHole:
        return "through_hole";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled PCB footprint pad kind"};
}

[[nodiscard]] FootprintPadKind footprint_pad_kind_from_name(const std::string &value) {
    if (value == "surface_mount") {
        return FootprintPadKind::SurfaceMount;
    }
    if (value == "through_hole") {
        return FootprintPadKind::ThroughHole;
    }
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid PCB footprint pad kind"};
}

[[nodiscard]] std::string footprint_pad_shape_name(FootprintPadShape shape) {
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
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled PCB footprint pad shape"};
}

[[nodiscard]] FootprintPadShape footprint_pad_shape_from_name(const std::string &value) {
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
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid PCB footprint pad shape"};
}

[[nodiscard]] std::string footprint_pad_plating_name(FootprintPadPlating plating) {
    switch (plating) {
    case FootprintPadPlating::Plated:
        return "plated";
    case FootprintPadPlating::NonPlated:
        return "non_plated";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled PCB footprint pad plating"};
}

[[nodiscard]] FootprintPadPlating footprint_pad_plating_from_name(const std::string &value) {
    if (value == "plated") {
        return FootprintPadPlating::Plated;
    }
    if (value == "non_plated") {
        return FootprintPadPlating::NonPlated;
    }
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid PCB footprint pad plating"};
}

[[nodiscard]] std::string footprint_pad_mechanical_role_name(FootprintPadMechanicalRole role) {
    switch (role) {
    case FootprintPadMechanicalRole::Mounting:
        return "mounting";
    case FootprintPadMechanicalRole::Fiducial:
        return "fiducial";
    case FootprintPadMechanicalRole::MechanicalSupport:
        return "mechanical_support";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled PCB footprint pad mechanical role"};
}

[[nodiscard]] FootprintPadMechanicalRole
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
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid PCB footprint pad mechanical role"};
}

[[nodiscard]] std::string footprint_marking_kind_name(FootprintMarkingKind kind) {
    switch (kind) {
    case FootprintMarkingKind::Silkscreen:
        return "silkscreen";
    case FootprintMarkingKind::Polarity:
        return "polarity";
    case FootprintMarkingKind::PinOne:
        return "pin_1";
    }
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled PCB footprint marking kind"};
}

[[nodiscard]] FootprintMarkingKind footprint_marking_kind_from_name(const std::string &value) {
    if (value == "silkscreen") {
        return FootprintMarkingKind::Silkscreen;
    }
    if (value == "polarity") {
        return FootprintMarkingKind::Polarity;
    }
    if (value == "pin_1") {
        return FootprintMarkingKind::PinOne;
    }
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid PCB footprint marking kind"};
}

[[nodiscard]] std::string pad_resolution_status_name(PadResolutionStatus status) {
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
    throw KernelLogicError{ErrorCode::InvalidState, "Unhandled PCB pad resolution status"};
}

[[nodiscard]] PadResolutionStatus pad_resolution_status_from_name(const std::string &value) {
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
    throw KernelLogicError{ErrorCode::InvalidArgument, "Invalid PCB pad resolution status"};
}

} // namespace volt::io::detail
