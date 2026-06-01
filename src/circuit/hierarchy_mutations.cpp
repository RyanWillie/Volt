#include <volt/circuit/hierarchy_mutations.hpp>

#include <utility>

namespace volt {

[[nodiscard]] ModuleDefId CircuitHierarchy::add_module_definition(ModuleDefinition definition) {
    return circuit_->add_module_definition(std::move(definition));
}
[[nodiscard]] TemplateNetDefId CircuitHierarchy::add_template_net(ModuleDefId module,
                                                                  TemplateNetDefinition net) {
    return circuit_->add_template_net(module, std::move(net));
}
[[nodiscard]] PortDefId CircuitHierarchy::add_port_definition(ModuleDefId module,
                                                              PortDefinition port) {
    return circuit_->add_port_definition(module, std::move(port));
}
[[nodiscard]] ModuleComponentId
CircuitHierarchy::add_module_component(ModuleDefId module, ModuleComponentTemplate component) {
    return circuit_->add_module_component(module, std::move(component));
}
bool CircuitHierarchy::connect_module_pin(ModuleDefId module, TemplateNetDefId net,
                                          ModuleComponentId component, PinDefId pin) {
    return circuit_->connect_module_pin(module, net, component, pin);
}
[[nodiscard]] ModuleInstanceId CircuitHierarchy::instantiate_root_module(ModuleDefId definition,
                                                                         ModuleInstanceName name) {
    return circuit_->instantiate_root_module(definition, std::move(name));
}
[[nodiscard]] PortBindingId CircuitHierarchy::bind_port(ModuleInstanceId instance, PortDefId port,
                                                        NetId parent_net) {
    return circuit_->bind_port(instance, port, parent_net);
}
[[nodiscard]] ModuleInstanceId CircuitHierarchy::restore_root_module_instance(
    ModuleDefId definition, ModuleInstanceName name,
    const std::vector<std::pair<TemplateNetDefId, NetId>> &origins,
    const std::vector<std::pair<ModuleComponentId, ComponentId>> &component_origins) {
    return circuit_->restore_root_module_instance(definition, std::move(name), origins,
                                                  component_origins);
}

} // namespace volt
