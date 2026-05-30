#pragma once

#include "binding_enum_conversions.hpp"

namespace volt::python {

namespace {

[[nodiscard]] inline std::string entity_kind_name(volt::EntityKind kind) {
    switch (kind) {
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
    case volt::EntityKind::FootprintDef:
        return "footprint_def";
    case volt::EntityKind::FootprintPad:
        return "footprint_pad";
    case volt::EntityKind::ComponentPlacement:
        return "component_placement";
    }

    throw std::logic_error{"Unhandled diagnostic entity kind"};
}

[[nodiscard]] inline py::dict diagnostic_to_dict(const volt::Diagnostic &diagnostic) {
    auto result = py::dict{};
    result["severity"] = severity_name(diagnostic.severity());
    result["code"] = diagnostic.code().value();
    result["message"] = diagnostic.message();

    auto entities = py::list{};
    for (const auto entity : diagnostic.entities()) {
        auto entity_dict = py::dict{};
        entity_dict["kind"] = entity_kind_name(entity.kind());
        entity_dict["index"] = entity.index();
        entities.append(std::move(entity_dict));
    }
    result["entities"] = std::move(entities);

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
