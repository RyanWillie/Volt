#pragma once

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include <volt/circuit/hierarchy.hpp>
#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>

namespace volt {

/** Owns module hierarchy storage, template connectivity, origin metadata, and port bindings. */
class HierarchyModel {
  public:
    [[nodiscard]] ModuleDefId add_module_definition(ModuleDefinition definition);

    [[nodiscard]] TemplateNetDefId add_template_net(ModuleDefId module, TemplateNetDefinition net);

    [[nodiscard]] PortDefId add_port_definition(ModuleDefId module, PortDefinition port);

    [[nodiscard]] ModuleComponentId add_module_component(ModuleDefId module,
                                                         ModuleComponentTemplate component);

    bool connect_module_pin(ModuleDefId module, TemplateNetDefId net, ModuleComponentId component,
                            PinDefId pin);

    [[nodiscard]] ModuleInstanceId instantiate_root_module(ModuleDefId definition,
                                                           ModuleInstanceName name);

    [[nodiscard]] ModuleInstanceId restore_root_module_instance(
        ModuleDefId definition, ModuleInstanceName name,
        const std::vector<std::pair<TemplateNetDefId, NetId>> &origins,
        const std::vector<std::pair<ModuleComponentId, ComponentId>> &component_origins);

    void record_module_net_origin(ModuleInstanceId instance, TemplateNetDefId template_net,
                                  NetId concrete_net);

    void record_module_component_origin(ModuleInstanceId instance, ModuleComponentId component,
                                        ComponentId concrete_component);

    [[nodiscard]] PortBindingId bind_port(ModuleInstanceId instance, PortDefId port,
                                          NetId internal_net, NetId parent_net);

    [[nodiscard]] std::optional<ModuleDefId>
    module_definition_by_name(const ModuleName &name) const;

    [[nodiscard]] std::optional<ModuleInstanceId>
    module_instance_by_name(const ModuleInstanceName &name) const;

    [[nodiscard]] std::optional<TemplateNetDefId> template_net_by_name(ModuleDefId module,
                                                                       const NetName &name) const;

    [[nodiscard]] std::optional<PortDefId> port_by_name(ModuleDefId module,
                                                        const PortName &name) const;

    [[nodiscard]] std::optional<ModuleComponentId>
    module_component_by_reference(ModuleDefId module, const ReferenceDesignator &reference) const;

    [[nodiscard]] std::optional<TemplateNetDefId>
    template_net_for(ModuleDefId module, ModuleComponentId component, PinDefId pin) const;

    [[nodiscard]] std::vector<ModulePinConnection> module_pin_connections(ModuleDefId module) const;

    [[nodiscard]] std::optional<PortBindingId> port_binding_for(ModuleInstanceId instance,
                                                                PortDefId port) const;

    [[nodiscard]] std::vector<PortBindingId> port_bindings_for(ModuleInstanceId instance) const;

    [[nodiscard]] std::optional<ComponentId>
    concrete_component_for(ModuleInstanceId instance, ModuleComponentId component) const;

    [[nodiscard]] std::optional<NetId> concrete_net_for(ModuleInstanceId instance,
                                                        TemplateNetDefId template_net) const;

    [[nodiscard]] std::vector<std::pair<TemplateNetDefId, NetId>>
    module_net_origins(ModuleInstanceId instance) const;

    [[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>>
    module_component_origins(ModuleInstanceId instance) const;

    [[nodiscard]] bool is_module_origin_net(NetId net) const;

    [[nodiscard]] bool is_module_origin_component(ComponentId component) const;

    [[nodiscard]] const ModuleDefinition &module_definition(ModuleDefId id) const;

    [[nodiscard]] const TemplateNetDefinition &template_net_definition(TemplateNetDefId id) const;

    [[nodiscard]] const PortDefinition &port_definition(PortDefId id) const;

    [[nodiscard]] const ModuleComponentTemplate &
    module_component_template(ModuleComponentId id) const;

    [[nodiscard]] const ModuleInstance &module_instance(ModuleInstanceId id) const;

    [[nodiscard]] const PortBinding &port_binding(PortBindingId id) const;

    [[nodiscard]] std::size_t module_definition_count() const noexcept;

    [[nodiscard]] std::size_t template_net_definition_count() const noexcept;

    [[nodiscard]] std::size_t port_definition_count() const noexcept;

    [[nodiscard]] std::size_t module_component_count() const noexcept;

    [[nodiscard]] std::size_t module_pin_connection_count() const noexcept;

    [[nodiscard]] std::size_t module_instance_count() const noexcept;

    [[nodiscard]] std::size_t port_binding_count() const noexcept;

    void require_module_definition(ModuleDefId module) const;

    void require_template_net(TemplateNetDefId net) const;

    void require_port(PortDefId port) const;

    void require_module_component(ModuleComponentId component) const;

    void require_module_instance(ModuleInstanceId instance) const;

    void require_template_net_in_module(ModuleDefId module, TemplateNetDefId net) const;

    void require_port_in_module(ModuleDefId module, PortDefId port) const;

    void require_module_component_in_module(ModuleDefId module, ModuleComponentId component) const;

    [[nodiscard]] bool template_net_belongs_to_module(ModuleDefId module,
                                                      TemplateNetDefId net) const;

    [[nodiscard]] bool module_component_belongs_to_module(ModuleDefId module,
                                                          ModuleComponentId component) const;

  private:
    EntityTable<ModuleDefinition, ModuleDefId> module_definitions_;
    EntityTable<TemplateNetDefinition, TemplateNetDefId> template_net_definitions_;
    EntityTable<ModuleComponentTemplate, ModuleComponentId> module_component_templates_;
    EntityTable<PortDefinition, PortDefId> port_definitions_;
    EntityTable<ModuleInstance, ModuleInstanceId> module_instances_;
    EntityTable<PortBinding, PortBindingId> port_bindings_;
    std::vector<ModulePinConnection> module_pin_connections_;
    std::vector<ModuleNetOrigin> module_net_origins_;
    std::vector<NetId> module_origin_nets_;
    std::vector<ModuleComponentOrigin> module_component_origins_;
    std::vector<ComponentId> module_origin_components_;
};

} // namespace volt
