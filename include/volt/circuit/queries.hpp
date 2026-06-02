#pragma once

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/core/ids.hpp>

namespace volt::queries {

[[nodiscard]] std::optional<NetId> net_of(const Circuit &circuit, PinId pin);

[[nodiscard]] std::optional<ComponentId>
component_by_reference(const Circuit &circuit, const ReferenceDesignator &reference);

[[nodiscard]] std::optional<ModuleDefId> module_definition_by_name(const Circuit &circuit,
                                                                   const ModuleName &name);

[[nodiscard]] std::optional<ModuleInstanceId>
module_instance_by_name(const Circuit &circuit, const ModuleInstanceName &name);

[[nodiscard]] std::optional<TemplateNetDefId>
template_net_by_name(const Circuit &circuit, ModuleDefId module, const NetName &name);

[[nodiscard]] std::optional<PortDefId> port_by_name(const Circuit &circuit, ModuleDefId module,
                                                    const PortName &name);

[[nodiscard]] std::optional<ModuleComponentId>
module_component_by_reference(const Circuit &circuit, ModuleDefId module,
                              const ReferenceDesignator &reference);

[[nodiscard]] std::optional<TemplateNetDefId> template_net_for(const Circuit &circuit,
                                                               ModuleDefId module,
                                                               ModuleComponentId component,
                                                               PinDefId pin);

[[nodiscard]] std::optional<PortBindingId>
port_binding_for(const Circuit &circuit, ModuleInstanceId instance, PortDefId port);

[[nodiscard]] std::vector<PortBindingId> port_bindings_for(const Circuit &circuit,
                                                           ModuleInstanceId instance);

[[nodiscard]] std::optional<ComponentId> concrete_component_for(const Circuit &circuit,
                                                                ModuleInstanceId instance,
                                                                ModuleComponentId component);

[[nodiscard]] std::optional<NetId>
concrete_net_for(const Circuit &circuit, ModuleInstanceId instance, TemplateNetDefId template_net);

[[nodiscard]] std::vector<std::pair<TemplateNetDefId, NetId>>
module_net_origins(const Circuit &circuit, ModuleInstanceId instance);

[[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>>
module_component_origins(const Circuit &circuit, ModuleInstanceId instance);

[[nodiscard]] bool is_module_origin_net(const Circuit &circuit, NetId net);

[[nodiscard]] bool is_module_origin_component(const Circuit &circuit, ComponentId component);

[[nodiscard]] std::optional<NetId> net_by_name(const Circuit &circuit, const NetName &name);

[[nodiscard]] std::vector<PinId> pins_for(const Circuit &circuit, ComponentId component);

[[nodiscard]] std::optional<PinId> pin_by_name(const Circuit &circuit, ComponentId component,
                                               std::string_view name);

[[nodiscard]] std::optional<PinId> pin_by_definition(const Circuit &circuit, ComponentId component,
                                                     PinDefId definition);

[[nodiscard]] std::optional<PinId> pin_by_number(const Circuit &circuit, ComponentId component,
                                                 std::string_view number);

} // namespace volt::queries
