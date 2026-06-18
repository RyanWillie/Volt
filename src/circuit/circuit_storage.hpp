#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/core/entity_table.hpp>

namespace volt::detail {

struct ConnectivityState {
    EntityTable<PinDefinition, PinDefId> pin_definitions;
    EntityTable<ComponentDefinition, ComponentDefId> component_definitions;
    EntityTable<ComponentInstance, ComponentId> components;
    EntityTable<PinInstance, PinId> pins;
    EntityTable<Net, NetId> nets;
};

struct ModuleDefinitionState {
    explicit ModuleDefinitionState(ModuleName module_name) : name{std::move(module_name)} {}

    ModuleName name;
    std::vector<TemplateNetDefId> template_nets;
    std::vector<PortDefId> ports;
    std::vector<ModuleComponentId> components;
};

class ModuleDefinitionStorage : public ModuleDefinition {
  public:
    explicit ModuleDefinitionStorage(ModuleName name);
    ModuleDefinitionStorage(const ModuleDefinitionStorage &other);
    ModuleDefinitionStorage(ModuleDefinitionStorage &&other) noexcept = default;
    ModuleDefinitionStorage &operator=(const ModuleDefinitionStorage &other);
    ModuleDefinitionStorage &operator=(ModuleDefinitionStorage &&other) noexcept = default;

    explicit ModuleDefinitionStorage(ModuleDefinition definition);

    void add_template_net(TemplateNetDefId net) { state_->template_nets.push_back(net); }

    void add_port(PortDefId port) { state_->ports.push_back(port); }

    void add_component(ModuleComponentId component) { state_->components.push_back(component); }

  private:
    explicit ModuleDefinitionStorage(std::shared_ptr<ModuleDefinitionState> state);

    [[nodiscard]] const ModuleDefinitionState &state() const noexcept { return *state_; }

    std::shared_ptr<ModuleDefinitionState> state_;
};

struct HierarchyState {
    EntityTable<ModuleDefinitionStorage, ModuleDefId> module_definitions;
    EntityTable<TemplateNetDefinition, TemplateNetDefId> template_net_definitions;
    EntityTable<ModuleComponentTemplate, ModuleComponentId> module_component_templates;
    EntityTable<PortDefinition, PortDefId> port_definitions;
    EntityTable<ModuleInstance, ModuleInstanceId> module_instances;
    EntityTable<PortBinding, PortBindingId> port_bindings;
    std::vector<ModulePinConnection> module_pin_connections;
    std::vector<ModuleNetOrigin> module_net_origins;
    std::vector<NetId> module_origin_nets;
    std::vector<ModuleComponentOrigin> module_component_origins;
    std::vector<ComponentId> module_origin_components;
};

struct ElectricalState {
    std::vector<std::pair<ComponentId, ElectricalAttributeMap>> component_attributes;
    std::vector<std::pair<PinDefId, ElectricalAttributeMap>> pin_definition_attributes;
    std::vector<std::pair<NetId, ElectricalAttributeMap>> net_attributes;
    std::vector<std::pair<ComponentId, std::optional<PhysicalPart>>> selected_physical_parts;
};

struct DesignIntentState {
    std::vector<NetId> intentional_stub_nets;
    std::vector<PinId> intentional_no_connect_pins;
    std::vector<ComponentAssemblyIntent> component_assembly_intents;
};

struct NetClassesState {
    EntityTable<NetClass, NetClassId> net_classes;
    std::vector<std::pair<NetId, NetClassId>> net_class_assignments;
};

template <typename Id>
[[nodiscard]] inline ElectricalAttributeMap &
mutable_attributes(std::vector<std::pair<Id, ElectricalAttributeMap>> &entries, Id owner) {
    const auto existing = std::find_if(entries.begin(), entries.end(),
                                       [owner](const auto &entry) { return entry.first == owner; });
    if (existing == entries.end()) {
        entries.emplace_back(owner, ElectricalAttributeMap{});
        return entries.back().second;
    }

    return existing->second;
}

void require_attribute_owner(const ElectricalAttributeSpec &spec,
                             ElectricalAttributeOwner expected);

void require_physical_part_matches_component_definition(const std::vector<PinDefId> &component_pins,
                                                        const PhysicalPart &physical_part);

} // namespace volt::detail
