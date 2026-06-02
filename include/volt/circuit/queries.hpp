#pragma once

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/core/ids.hpp>

/** Read-only traversal helpers for logical circuit data. */
namespace volt::queries {

/** Return the logical net currently containing a concrete pin, if any. */
[[nodiscard]] std::optional<NetId> net_of(const Circuit &circuit, PinId pin);

/** Return a component by reference designator, if present. */
[[nodiscard]] std::optional<ComponentId>
component_by_reference(const Circuit &circuit, const ReferenceDesignator &reference);

/** Return a module definition by name, if present. */
[[nodiscard]] std::optional<ModuleDefId> module_definition_by_name(const Circuit &circuit,
                                                                   const ModuleName &name);

/** Return a module instance by name, if present. */
[[nodiscard]] std::optional<ModuleInstanceId>
module_instance_by_name(const Circuit &circuit, const ModuleInstanceName &name);

/** Return a template net by module-local name, if present. */
[[nodiscard]] std::optional<TemplateNetDefId>
template_net_by_name(const Circuit &circuit, ModuleDefId module, const NetName &name);

/** Return a port definition by module-local name, if present. */
[[nodiscard]] std::optional<PortDefId> port_by_name(const Circuit &circuit, ModuleDefId module,
                                                    const PortName &name);

/** Return a module component template by reference designator, if present. */
[[nodiscard]] std::optional<ModuleComponentId>
module_component_by_reference(const Circuit &circuit, ModuleDefId module,
                              const ReferenceDesignator &reference);

/** Return the template net connected to a module component pin, if present. */
[[nodiscard]] std::optional<TemplateNetDefId> template_net_for(const Circuit &circuit,
                                                               ModuleDefId module,
                                                               ModuleComponentId component,
                                                               PinDefId pin);

/** Return the binding for an instance port, if present. */
[[nodiscard]] std::optional<PortBindingId>
port_binding_for(const Circuit &circuit, ModuleInstanceId instance, PortDefId port);

/** Return all port bindings for a module instance in deterministic order. */
[[nodiscard]] std::vector<PortBindingId> port_bindings_for(const Circuit &circuit,
                                                           ModuleInstanceId instance);

/** Return the concrete component for a module component template, if present. */
[[nodiscard]] std::optional<ComponentId> concrete_component_for(const Circuit &circuit,
                                                                ModuleInstanceId instance,
                                                                ModuleComponentId component);

/** Return the concrete net for a module template net, if present. */
[[nodiscard]] std::optional<NetId>
concrete_net_for(const Circuit &circuit, ModuleInstanceId instance, TemplateNetDefId template_net);

/** Return concrete net origins for a module instance. */
[[nodiscard]] std::vector<std::pair<TemplateNetDefId, NetId>>
module_net_origins(const Circuit &circuit, ModuleInstanceId instance);

/** Return concrete component origins for a module instance. */
[[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>>
module_component_origins(const Circuit &circuit, ModuleInstanceId instance);

/** Return whether a concrete net originated from a module instantiation. */
[[nodiscard]] bool is_module_origin_net(const Circuit &circuit, NetId net);

/** Return whether a concrete component originated from a module instantiation. */
[[nodiscard]] bool is_module_origin_component(const Circuit &circuit, ComponentId component);

/** Return a net by name, if present. */
[[nodiscard]] std::optional<NetId> net_by_name(const Circuit &circuit, const NetName &name);

/** Return concrete pins owned by a component in deterministic order. */
[[nodiscard]] std::vector<PinId> pins_for(const Circuit &circuit, ComponentId component);

/** Return a component pin by reusable pin name, if present. */
[[nodiscard]] std::optional<PinId> pin_by_name(const Circuit &circuit, ComponentId component,
                                               std::string_view name);

/** Return a component pin by reusable pin definition, if present. */
[[nodiscard]] std::optional<PinId> pin_by_definition(const Circuit &circuit, ComponentId component,
                                                     PinDefId definition);

/** Return a component pin by reusable pin number, if present. */
[[nodiscard]] std::optional<PinId> pin_by_number(const Circuit &circuit, ComponentId component,
                                                 std::string_view number);

} // namespace volt::queries
