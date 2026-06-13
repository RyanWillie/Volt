#pragma once

#include "binding_enum_conversions.hpp"

namespace volt::python {

namespace {

[[nodiscard]] inline std::string entity_kind_name(volt::EntityKind kind) {
    switch (kind) {
    case volt::EntityKind::Board:
        return "board";
    case volt::EntityKind::PartDefinition:
        return "part_definition";
    case volt::EntityKind::ComponentDef:
        return "component_definition";
    case volt::EntityKind::Component:
        return "component";
    case volt::EntityKind::PinDef:
        return "pin_definition";
    case volt::EntityKind::Pin:
        return "pin";
    case volt::EntityKind::Net:
        return "net";
    case volt::EntityKind::ModuleDef:
        return "module_definition";
    case volt::EntityKind::ModuleInstance:
        return "module_instance";
    case volt::EntityKind::PortDef:
        return "port_definition";
    case volt::EntityKind::SymbolDef:
        return "symbol_definition";
    case volt::EntityKind::Sheet:
        return "sheet";
    case volt::EntityKind::SymbolInstance:
        return "symbol_instance";
    case volt::EntityKind::WireRun:
        return "wire_run";
    case volt::EntityKind::NetLabel:
        return "net_label";
    case volt::EntityKind::Junction:
        return "junction";
    case volt::EntityKind::PowerPort:
        return "power_port";
    case volt::EntityKind::NoConnectMarker:
        return "no_connect_marker";
    case volt::EntityKind::SheetPort:
        return "sheet_port";
    case volt::EntityKind::SymbolField:
        return "symbol_field";
    case volt::EntityKind::BoardLayer:
        return "board_layer";
    case volt::EntityKind::BoardFeature:
        return "board_feature";
    case volt::EntityKind::BoardTrack:
        return "board_track";
    case volt::EntityKind::BoardVia:
        return "board_via";
    case volt::EntityKind::BoardZone:
        return "board_zone";
    case volt::EntityKind::BoardKeepout:
        return "board_keepout";
    case volt::EntityKind::BoardRoom:
        return "board_room";
    case volt::EntityKind::BoardText:
        return "board_text";
    case volt::EntityKind::FootprintDef:
        return "footprint_def";
    case volt::EntityKind::FootprintPad:
        return "footprint_pad";
    case volt::EntityKind::ComponentPlacement:
        return "component_placement";
    }

    throw std::logic_error{"Unhandled diagnostic entity kind"};
}

[[nodiscard]] inline std::string diagnostic_overlay_kind_name(volt::DiagnosticOverlayKind kind) {
    switch (kind) {
    case volt::DiagnosticOverlayKind::BoundingBox:
        return "bounding_box";
    case volt::DiagnosticOverlayKind::Point:
        return "point";
    case volt::DiagnosticOverlayKind::Polygon:
        return "polygon";
    case volt::DiagnosticOverlayKind::Segment:
        return "segment";
    }

    throw std::logic_error{"Unhandled diagnostic overlay kind"};
}

[[nodiscard]] inline py::list
diagnostic_entities_to_list(const std::vector<volt::EntityRef> &diagnostic_entities) {
    auto entities = py::list{};
    for (const auto entity : diagnostic_entities) {
        auto entity_dict = py::dict{};
        entity_dict["kind"] = entity_kind_name(entity.kind());
        entity_dict["index"] = entity.index();
        entities.append(std::move(entity_dict));
    }
    return entities;
}

[[nodiscard]] inline py::list
diagnostic_points_to_list(const std::vector<volt::DiagnosticPoint> &diagnostic_points) {
    auto points = py::list{};
    for (const auto point : diagnostic_points) {
        auto point_tuple = py::tuple{2};
        point_tuple[0] = point.x_mm;
        point_tuple[1] = point.y_mm;
        points.append(std::move(point_tuple));
    }
    return points;
}

[[nodiscard]] inline py::list
diagnostic_layers_to_list(const std::vector<volt::BoardLayerId> &diagnostic_layers) {
    auto layers = py::list{};
    for (const auto layer : diagnostic_layers) {
        auto layer_dict = py::dict{};
        layer_dict["kind"] = "board_layer";
        layer_dict["index"] = layer.index();
        layers.append(std::move(layer_dict));
    }
    return layers;
}

[[nodiscard]] inline py::list
diagnostic_overlays_to_list(const std::vector<volt::DiagnosticOverlay> &diagnostic_overlays) {
    auto overlays = py::list{};
    for (const auto &overlay : diagnostic_overlays) {
        auto overlay_dict = py::dict{};
        overlay_dict["kind"] = diagnostic_overlay_kind_name(overlay.kind());
        overlay_dict["points"] = diagnostic_points_to_list(overlay.points());
        overlay_dict["entities"] = diagnostic_entities_to_list(overlay.entities());
        overlay_dict["layers"] = diagnostic_layers_to_list(overlay.layers());
        overlays.append(std::move(overlay_dict));
    }
    return overlays;
}

[[nodiscard]] inline py::object
diagnostic_measurement_to_object(const std::optional<volt::DiagnosticMeasurement> &measurement) {
    if (!measurement.has_value()) {
        return py::none();
    }
    auto measurement_dict = py::dict{};
    measurement_dict["actual_mm"] = measurement->actual_mm;
    measurement_dict["required_mm"] = measurement->required_mm;
    return measurement_dict;
}

[[nodiscard]] inline py::dict diagnostic_to_dict(const volt::Diagnostic &diagnostic) {
    auto result = py::dict{};
    result["severity"] = severity_name(diagnostic.severity());
    result["category"] = diagnostic.category().value();
    result["code"] = diagnostic.code().value();
    result["message"] = diagnostic.message();
    result["entities"] = diagnostic_entities_to_list(diagnostic.entities());
    result["overlays"] = diagnostic_overlays_to_list(diagnostic.overlays());
    result["measurement"] = diagnostic_measurement_to_object(diagnostic.measurement());
    result["rule"] =
        diagnostic.rule().has_value() ? py::cast(diagnostic.rule().value()) : py::none();

    return result;
}

[[nodiscard]] inline py::list diagnostics_to_list(const volt::DiagnosticReport &report) {
    auto diagnostics = py::list{};
    for (const auto &diagnostic : report.diagnostics()) {
        diagnostics.append(diagnostic_to_dict(diagnostic));
    }

    return diagnostics;
}

} // namespace

} // namespace volt::python
