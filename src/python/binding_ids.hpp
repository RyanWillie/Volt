#pragma once

#include "binding_common.hpp"

namespace volt::python {

namespace {

[[nodiscard]] inline volt::ComponentDefId component_def_id(std::size_t index) {
    return volt::ComponentDefId{index};
}

[[nodiscard]] inline volt::ComponentId component_id(std::size_t index) {
    return volt::ComponentId{index};
}

[[nodiscard]] inline volt::PinId pin_id(std::size_t index) { return volt::PinId{index}; }

[[nodiscard]] inline volt::NetId net_id(std::size_t index) { return volt::NetId{index}; }

[[nodiscard]] inline volt::SheetId sheet_id(std::size_t index) { return volt::SheetId{index}; }

[[nodiscard]] inline volt::ModuleDefId module_def_id(std::size_t index) {
    return volt::ModuleDefId{index};
}

[[nodiscard]] inline volt::TemplateNetDefId template_net_def_id(std::size_t index) {
    return volt::TemplateNetDefId{index};
}

[[nodiscard]] inline volt::PortDefId port_def_id(std::size_t index) {
    return volt::PortDefId{index};
}

[[nodiscard]] inline volt::ModuleComponentId module_component_id(std::size_t index) {
    return volt::ModuleComponentId{index};
}

[[nodiscard]] inline volt::ModuleInstanceId module_instance_id(std::size_t index) {
    return volt::ModuleInstanceId{index};
}

} // namespace

} // namespace volt::python
