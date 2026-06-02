#include <volt/circuit/queries.hpp>

namespace volt::queries {

[[nodiscard]] std::optional<NetId> net_of(const Circuit &circuit, PinId pin) {
    return circuit.connectivity_model().net_of(pin);
}

[[nodiscard]] std::optional<ComponentId>
component_by_reference(const Circuit &circuit, const ReferenceDesignator &reference) {
    return circuit.connectivity_model().component_by_reference(reference);
}

[[nodiscard]] std::optional<ModuleDefId> module_definition_by_name(const Circuit &circuit,
                                                                   const ModuleName &name) {
    return circuit.hierarchy_model().module_definition_by_name(name);
}

[[nodiscard]] std::optional<ModuleInstanceId>
module_instance_by_name(const Circuit &circuit, const ModuleInstanceName &name) {
    return circuit.hierarchy_model().module_instance_by_name(name);
}

[[nodiscard]] std::optional<TemplateNetDefId>
template_net_by_name(const Circuit &circuit, ModuleDefId module, const NetName &name) {
    return circuit.hierarchy_model().template_net_by_name(module, name);
}

[[nodiscard]] std::optional<PortDefId> port_by_name(const Circuit &circuit, ModuleDefId module,
                                                    const PortName &name) {
    return circuit.hierarchy_model().port_by_name(module, name);
}

[[nodiscard]] std::optional<ModuleComponentId>
module_component_by_reference(const Circuit &circuit, ModuleDefId module,
                              const ReferenceDesignator &reference) {
    return circuit.hierarchy_model().module_component_by_reference(module, reference);
}

[[nodiscard]] std::optional<TemplateNetDefId> template_net_for(const Circuit &circuit,
                                                               ModuleDefId module,
                                                               ModuleComponentId component,
                                                               PinDefId pin) {
    return circuit.hierarchy_model().template_net_for(module, component, pin);
}

[[nodiscard]] std::optional<PortBindingId>
port_binding_for(const Circuit &circuit, ModuleInstanceId instance, PortDefId port) {
    return circuit.hierarchy_model().port_binding_for(instance, port);
}

[[nodiscard]] std::vector<PortBindingId> port_bindings_for(const Circuit &circuit,
                                                           ModuleInstanceId instance) {
    return circuit.hierarchy_model().port_bindings_for(instance);
}

[[nodiscard]] std::optional<ComponentId> concrete_component_for(const Circuit &circuit,
                                                                ModuleInstanceId instance,
                                                                ModuleComponentId component) {
    return circuit.hierarchy_model().concrete_component_for(instance, component);
}

[[nodiscard]] std::optional<NetId>
concrete_net_for(const Circuit &circuit, ModuleInstanceId instance, TemplateNetDefId template_net) {
    return circuit.hierarchy_model().concrete_net_for(instance, template_net);
}

[[nodiscard]] std::vector<std::pair<TemplateNetDefId, NetId>>
module_net_origins(const Circuit &circuit, ModuleInstanceId instance) {
    return circuit.hierarchy_model().module_net_origins(instance);
}

[[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>>
module_component_origins(const Circuit &circuit, ModuleInstanceId instance) {
    return circuit.hierarchy_model().module_component_origins(instance);
}

[[nodiscard]] bool is_module_origin_net(const Circuit &circuit, NetId net) {
    static_cast<void>(circuit.net(net));
    return circuit.hierarchy_model().is_module_origin_net(net);
}

[[nodiscard]] bool is_module_origin_component(const Circuit &circuit, ComponentId component) {
    static_cast<void>(circuit.component(component));
    return circuit.hierarchy_model().is_module_origin_component(component);
}

[[nodiscard]] std::optional<NetId> net_by_name(const Circuit &circuit, const NetName &name) {
    return circuit.connectivity_model().net_by_name(name);
}

[[nodiscard]] std::vector<PinId> pins_for(const Circuit &circuit, ComponentId component) {
    return circuit.connectivity_model().pins_for(component);
}

[[nodiscard]] std::optional<PinId> pin_by_name(const Circuit &circuit, ComponentId component,
                                               std::string_view name) {
    return circuit.connectivity_model().pin_by_name(component, name);
}

[[nodiscard]] std::optional<PinId> pin_by_definition(const Circuit &circuit, ComponentId component,
                                                     PinDefId definition) {
    return circuit.connectivity_model().pin_by_definition(component, definition);
}

[[nodiscard]] std::optional<PinId> pin_by_number(const Circuit &circuit, ComponentId component,
                                                 std::string_view number) {
    return circuit.connectivity_model().pin_by_number(component, number);
}

} // namespace volt::queries
