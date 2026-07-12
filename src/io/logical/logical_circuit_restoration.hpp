#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>

namespace volt::io::detail {

struct RestoredPinDefinition {
    PinDefinition definition;
    ElectricalAttributeMap electrical_attributes;
};

struct RestoredComponentDefinition {
    ComponentDefinition definition;
};

struct RestoredComponentInstance {
    ComponentInstance instance;
    ElectricalAttributeMap electrical_attributes;
};

struct ConnectivityRestoration {
    std::vector<RestoredPinDefinition> pin_definitions;
    std::vector<RestoredComponentDefinition> component_definitions;
    std::vector<RestoredComponentInstance> components;
    std::vector<PinInstance> pins;
};

struct RestoredNet {
    NetId id;
    Net net;
    ElectricalAttributeMap electrical_attributes;
};

struct RestoredModuleDefinition {
    ModuleDefId id;
    ModuleDefinition definition;
};

struct RestoredTemplateNetDefinition {
    TemplateNetDefId id;
    ModuleDefId module;
    TemplateNetDefinition definition;
};

struct RestoredModuleComponent {
    ModuleComponentId id;
    ModuleDefId module;
    ModuleComponentTemplate component;
};

struct RestoredModulePinConnection {
    ModuleDefId module;
    TemplateNetDefId net;
    ModuleComponentId component;
    PinDefId pin;
};

struct RestoredPortDefinition {
    PortDefId id;
    ModuleDefId module;
    PortDefinition definition;
};

struct HierarchyDefinitionRestoration {
    std::vector<RestoredModuleDefinition> module_definitions;
    std::vector<RestoredTemplateNetDefinition> template_nets;
    std::vector<RestoredModuleComponent> components;
    std::vector<RestoredModulePinConnection> connections;
    std::vector<RestoredPortDefinition> ports;
};

struct ModuleInstanceRestoration {
    ModuleDefId definition;
    ModuleInstanceName name;
    std::vector<std::pair<TemplateNetDefId, NetId>> net_origins;
    std::vector<std::pair<ModuleComponentId, ComponentId>> component_origins;
};

struct RestoredPortBinding {
    PortDefId port;
    NetId parent_net;
};

struct RestoredModuleInstance {
    ModuleInstanceId id;
    ModuleInstanceRestoration instance;
    std::vector<RestoredPortBinding> bindings;
};

struct RestoredNetClassAssignment {
    NetId net;
    NetClassId net_class;
};

struct RestoredAssemblyIntent {
    ComponentId component;
    std::optional<bool> dnp;
    bool selection_override;
};

struct RestoredSelectedPhysicalPart {
    ComponentId component;
    PhysicalPart physical_part;
    ElectricalAttributeMap electrical_attributes;
};

/** Complete validated input for privately reconstructing one persisted logical circuit. */
struct LogicalCircuitRestorationPlan {
    ConnectivityRestoration connectivity;
    std::vector<RestoredNet> nets;
    std::vector<NetClass> net_classes;
    std::vector<RestoredNetClassAssignment> net_class_assignments;
    std::vector<NetId> intentional_stub_nets;
    std::vector<PinId> intentional_no_connect_pins;
    std::vector<RestoredAssemblyIntent> assembly_intent;
    HierarchyDefinitionRestoration hierarchy;
    std::vector<RestoredModuleInstance> module_instances;
    std::vector<RestoredSelectedPhysicalPart> selected_physical_parts;
};

/** Apply one complete persistence plan through Circuit's single private restoration boundary. */
[[nodiscard]] Circuit restore_logical_circuit(LogicalCircuitRestorationPlan plan);

} // namespace volt::io::detail
