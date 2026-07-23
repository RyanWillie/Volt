#pragma once

#include "binding_common.hpp"

#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints/footprints.hpp>

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

[[nodiscard]] inline volt::BoardClearanceKind parse_board_clearance_kind(const std::string &value) {
    if (value == "track" || value == "Track") {
        return volt::BoardClearanceKind::Track;
    }
    if (value == "pad" || value == "Pad") {
        return volt::BoardClearanceKind::Pad;
    }
    if (value == "via" || value == "Via") {
        return volt::BoardClearanceKind::Via;
    }
    if (value == "zone" || value == "Zone") {
        return volt::BoardClearanceKind::Zone;
    }
    if (value == "board_edge" || value == "board-edge" || value == "BoardEdge") {
        return volt::BoardClearanceKind::BoardEdge;
    }
    throw std::invalid_argument{"Unknown board clearance kind"};
}

[[nodiscard]] inline std::string board_clearance_kind_name(volt::BoardClearanceKind kind) {
    switch (kind) {
    case volt::BoardClearanceKind::Track:
        return "track";
    case volt::BoardClearanceKind::Pad:
        return "pad";
    case volt::BoardClearanceKind::Via:
        return "via";
    case volt::BoardClearanceKind::Zone:
        return "zone";
    case volt::BoardClearanceKind::BoardEdge:
        return "board_edge";
    }
    throw std::logic_error{"Unhandled board clearance kind"};
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
    if (role == "thermal" || role == "Thermal") {
        return volt::FootprintPadMechanicalRole::Thermal;
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

[[nodiscard]] inline volt::FootprintPoint footprint_point_from_object(py::handle value) {
    const auto point = py::cast<std::pair<double, double>>(value);
    return volt::FootprintPoint{point.first, point.second};
}

[[nodiscard]] inline std::optional<volt::FootprintPolygon>
optional_footprint_polygon_from_dict(const py::dict &data, const char *name) {
    if (!data.contains(name) || data[name].is_none()) {
        return std::nullopt;
    }

    const auto points = py::cast<py::iterable>(data[name]);
    auto vertices = std::vector<volt::FootprintPoint>{};
    vertices.reserve(static_cast<std::size_t>(py::len(points)));
    for (const auto item : points) {
        vertices.push_back(footprint_point_from_object(item));
    }
    return volt::FootprintPolygon{std::move(vertices)};
}

[[nodiscard]] inline volt::FootprintMarkingKind
footprint_marking_kind_from_string(const std::string &kind) {
    if (kind == "silkscreen" || kind == "Silkscreen") {
        return volt::FootprintMarkingKind::Silkscreen;
    }
    if (kind == "polarity" || kind == "Polarity") {
        return volt::FootprintMarkingKind::Polarity;
    }
    if (kind == "pin_1" || kind == "pin-1" || kind == "PinOne") {
        return volt::FootprintMarkingKind::PinOne;
    }
    throw std::invalid_argument{"Unknown footprint marking kind"};
}

[[nodiscard]] inline volt::FootprintMarking footprint_marking_from_dict(const py::dict &data) {
    return volt::FootprintMarking{
        footprint_marking_kind_from_string(py::cast<std::string>(data["kind"])),
        volt::FootprintPolygon{[&]() {
            const auto points = py::cast<py::iterable>(data["polygon"]);
            auto vertices = std::vector<volt::FootprintPoint>{};
            vertices.reserve(static_cast<std::size_t>(py::len(points)));
            for (const auto item : points) {
                vertices.push_back(footprint_point_from_object(item));
            }
            return vertices;
        }()}};
}

[[nodiscard]] inline std::vector<volt::FootprintMarking>
footprint_markings_from_dict(const py::dict &data) {
    if (!data.contains("markings") || data["markings"].is_none()) {
        return {};
    }
    const auto markings_data = py::cast<py::iterable>(data["markings"]);
    auto markings = std::vector<volt::FootprintMarking>{};
    markings.reserve(static_cast<std::size_t>(py::len(markings_data)));
    for (const auto item : markings_data) {
        markings.push_back(footprint_marking_from_dict(py::cast<py::dict>(item)));
    }
    return markings;
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
    return volt::FootprintDefinition{
        volt::FootprintRef{ref.first, ref.second}, std::move(pads),
        volt::FootprintPackageGeometry{
            optional_footprint_polygon_from_dict(data, "courtyard"),
            optional_footprint_polygon_from_dict(data, "body"),
            optional_footprint_polygon_from_dict(data, "fabrication_outline"),
            optional_footprint_polygon_from_dict(data, "assembly_outline"),
            footprint_markings_from_dict(data)}};
}

[[nodiscard]] inline std::vector<int> optional_int_vector_from_dict(const py::dict &data,
                                                                    const char *name) {
    auto values = std::vector<int>{};
    if (!data.contains(name)) {
        return values;
    }
    const auto items = py::cast<py::iterable>(data[name]);
    for (const auto item : items) {
        values.push_back(py::cast<int>(item));
    }
    return values;
}

[[nodiscard]] inline std::vector<double> optional_double_vector_from_dict(const py::dict &data,
                                                                          const char *name) {
    auto values = std::vector<double>{};
    if (!data.contains(name)) {
        return values;
    }
    const auto items = py::cast<py::iterable>(data[name]);
    for (const auto item : items) {
        values.push_back(py::cast<double>(item));
    }
    return values;
}

[[nodiscard]] inline std::optional<volt::BoardCapabilityRange>
optional_capability_range_from_dict(const py::dict &data, const char *name) {
    if (!data.contains(name)) {
        return std::nullopt;
    }
    const auto range = py::cast<py::dict>(data[name]);
    return volt::BoardCapabilityRange{
        py::cast<double>(range["minimum_mm"]),
        py::cast<double>(range["maximum_mm"]),
    };
}

[[nodiscard]] inline volt::BoardCapabilityProfile
board_capability_profile_from_dict(const py::dict &data) {
    const auto provenance = py::cast<py::dict>(data["provenance"]);
    auto clearances = std::vector<volt::BoardClearancePair>{};
    const auto clearance_data = py::cast<py::iterable>(data["minimum_clearances"]);
    for (const auto item : clearance_data) {
        const auto entry = py::cast<py::dict>(item);
        clearances.push_back(volt::BoardClearancePair{
            parse_board_clearance_kind(py::cast<std::string>(entry["first"])),
            parse_board_clearance_kind(py::cast<std::string>(entry["second"])),
            py::cast<double>(entry["clearance_mm"]),
        });
    }

    auto refinements = std::vector<volt::BoardCapabilityCopperWeightRefinement>{};
    if (data.contains("copper_weight_refinements")) {
        const auto refinement_data = py::cast<py::iterable>(data["copper_weight_refinements"]);
        for (const auto item : refinement_data) {
            const auto entry = py::cast<py::dict>(item);
            refinements.push_back(volt::BoardCapabilityCopperWeightRefinement{
                py::cast<double>(entry["copper_weight_oz"]),
                py::cast<double>(entry["minimum_track_width_mm"]),
                py::cast<double>(entry["minimum_clearance_mm"]),
            });
        }
    }

    return volt::BoardCapabilityProfile{
        py::cast<std::string>(data["name"]),
        volt::BoardCapabilityProvenance{
            py::cast<std::string>(provenance["source"]),
            py::cast<std::string>(provenance["as_of"]),
        },
        py::cast<double>(data["minimum_track_width_mm"]),
        py::cast<double>(data["minimum_via_drill_mm"]),
        py::cast<double>(data["minimum_via_annular_mm"]),
        std::move(clearances),
        std::move(refinements),
        optional_int_vector_from_dict(data, "supported_copper_layer_counts"),
        optional_capability_range_from_dict(data, "board_thickness_range_mm"),
        optional_double_vector_from_dict(data, "available_copper_weights_oz"),
        optional_capability_range_from_dict(data, "drill_diameter_range_mm"),
    };
}

inline void set_range_payload(py::dict &payload, const char *name,
                              volt::BoardCapabilityRange range) {
    auto item = py::dict{};
    item["minimum_mm"] = range.minimum_mm;
    item["maximum_mm"] = range.maximum_mm;
    payload[name] = std::move(item);
}

[[nodiscard]] inline py::dict
board_capability_profile_to_dict(const volt::BoardCapabilityProfile &profile) {
    auto payload = py::dict{};
    payload["name"] = profile.name();
    auto provenance = py::dict{};
    provenance["source"] = profile.provenance().source;
    provenance["as_of"] = profile.provenance().as_of;
    payload["provenance"] = std::move(provenance);
    payload["minimum_track_width_mm"] = profile.minimum_track_width_mm();
    payload["minimum_via_drill_mm"] = profile.minimum_via_drill_mm();
    payload["minimum_via_annular_mm"] = profile.minimum_via_annular_mm();

    auto clearances = py::list{};
    for (const auto &entry : profile.minimum_clearances()) {
        auto clearance = py::dict{};
        clearance["first"] = board_clearance_kind_name(entry.first);
        clearance["second"] = board_clearance_kind_name(entry.second);
        clearance["clearance_mm"] = entry.clearance_mm;
        clearances.append(std::move(clearance));
    }
    payload["minimum_clearances"] = std::move(clearances);

    auto refinements = py::list{};
    for (const auto &entry : profile.copper_weight_refinements()) {
        auto refinement = py::dict{};
        refinement["copper_weight_oz"] = entry.copper_weight_oz;
        refinement["minimum_track_width_mm"] = entry.minimum_track_width_mm;
        refinement["minimum_clearance_mm"] = entry.minimum_clearance_mm;
        refinements.append(std::move(refinement));
    }
    payload["copper_weight_refinements"] = std::move(refinements);

    if (!profile.supported_copper_layer_counts().empty()) {
        auto counts = py::list{};
        for (const auto count : profile.supported_copper_layer_counts()) {
            counts.append(count);
        }
        payload["supported_copper_layer_counts"] = std::move(counts);
    }
    if (profile.board_thickness_range_mm().has_value()) {
        set_range_payload(payload, "board_thickness_range_mm",
                          profile.board_thickness_range_mm().value());
    }
    if (!profile.available_copper_weights_oz().empty()) {
        auto weights = py::list{};
        for (const auto weight : profile.available_copper_weights_oz()) {
            weights.append(weight);
        }
        payload["available_copper_weights_oz"] = std::move(weights);
    }
    if (profile.drill_diameter_range_mm().has_value()) {
        set_range_payload(payload, "drill_diameter_range_mm",
                          profile.drill_diameter_range_mm().value());
    }
    return payload;
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
