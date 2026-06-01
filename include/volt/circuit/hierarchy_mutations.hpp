#pragma once

#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>

namespace volt {

/** Kernel-owned hierarchy mutation surface over a Circuit invariant core. */
class CircuitHierarchy {
  public:
    /** Construct hierarchy mutations over an existing logical circuit. */
    explicit CircuitHierarchy(Circuit &circuit) noexcept : circuit_{&circuit} {}

    /** Store a reusable logical module definition and return its stable ID. */
    [[nodiscard]] ModuleDefId add_module_definition(ModuleDefinition definition);

    /** Add a template-local net to a reusable module definition. */
    [[nodiscard]] TemplateNetDefId add_template_net(ModuleDefId module, TemplateNetDefinition net);

    /** Add a boundary port to a reusable module definition. */
    [[nodiscard]] PortDefId add_port_definition(ModuleDefId module, PortDefinition port);

    /** Add a component occurrence to a reusable module definition. */
    [[nodiscard]] ModuleComponentId add_module_component(ModuleDefId module,
                                                         ModuleComponentTemplate component);

    /** Connect a module component template pin to a template-local net. */
    bool connect_module_pin(ModuleDefId module, TemplateNetDefId net, ModuleComponentId component,
                            PinDefId pin);

    /** Instantiate a module at the root and materialize concrete module contents. */
    [[nodiscard]] ModuleInstanceId instantiate_root_module(ModuleDefId definition,
                                                           ModuleInstanceName name);

    /** Record an explicit connectivity edge from an instance-local port net to a parent net. */
    [[nodiscard]] PortBindingId bind_port(ModuleInstanceId instance, PortDefId port,
                                          NetId parent_net);

    /** Restore a root module instance over existing concrete nets while loading JSON. */
    [[nodiscard]] ModuleInstanceId restore_root_module_instance(
        ModuleDefId definition, ModuleInstanceName name,
        const std::vector<std::pair<TemplateNetDefId, NetId>> &origins,
        const std::vector<std::pair<ModuleComponentId, ComponentId>> &component_origins = {});

  private:
    Circuit *circuit_;
};

} // namespace volt
