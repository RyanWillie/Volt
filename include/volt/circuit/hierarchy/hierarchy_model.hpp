#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include <volt/circuit/hierarchy/hierarchy.hpp>
#include <volt/core/ids.hpp>

namespace volt {

class Circuit;

namespace detail {
struct HierarchyState;
}

/**
 * Owns module hierarchy storage: module/template definitions, ports, components, instances,
 * port bindings, and origin metadata.
 *
 * Responsibility: stores reusable module hierarchy and the orchestration for materializing a
 *   module instance over concrete nets and components.
 * Invariants: template/port/component IDs resolve within their module; instance origins map to
 *   existing concrete entities. Violations throw.
 * Collaborators: composed by Circuit, which pre-flights cross-references before delegating;
 *   never references Circuit back (acyclic).
 */
class HierarchyModel {
  public:
    /** Construct an empty hierarchy model facade. */
    HierarchyModel();
    /** Copy hierarchy model state. */
    HierarchyModel(const HierarchyModel &other);
    /** Move hierarchy model state. */
    HierarchyModel(HierarchyModel &&other) noexcept;
    /** Copy hierarchy model state. */
    HierarchyModel &operator=(const HierarchyModel &other);
    /** Move hierarchy model state. */
    HierarchyModel &operator=(HierarchyModel &&other) noexcept;
    /** Destroy hierarchy model state. */
    ~HierarchyModel();

    /** Return a module definition by name, if present. */
    [[nodiscard]] std::optional<ModuleDefId>
    module_definition_by_name(const ModuleName &name) const;

    /** Return a module instance by name, if present. */
    [[nodiscard]] std::optional<ModuleInstanceId>
    module_instance_by_name(const ModuleInstanceName &name) const;

    /** Return a template net by module-local name, if present. */
    [[nodiscard]] std::optional<TemplateNetDefId> template_net_by_name(ModuleDefId module,
                                                                       const NetName &name) const;

    /** Return a port definition by module-local name, if present. */
    [[nodiscard]] std::optional<PortDefId> port_by_name(ModuleDefId module,
                                                        const PortName &name) const;

    /** Return a module component template by reference designator, if present. */
    [[nodiscard]] std::optional<ModuleComponentId>
    module_component_by_reference(ModuleDefId module, const ReferenceDesignator &reference) const;

    /** Return the template net connected to a module component pin, if present. */
    [[nodiscard]] std::optional<TemplateNetDefId>
    template_net_for(ModuleDefId module, ModuleComponentId component, PinDefId pin) const;

    /** Return all module pin template connections for a module definition. */
    [[nodiscard]] std::vector<ModulePinConnection> module_pin_connections(ModuleDefId module) const;

    /** Return the binding for an instance port, if present. */
    [[nodiscard]] std::optional<PortBindingId> port_binding_for(ModuleInstanceId instance,
                                                                PortDefId port) const;

    /** Return all port bindings for a module instance in deterministic order. */
    [[nodiscard]] std::vector<PortBindingId> port_bindings_for(ModuleInstanceId instance) const;

    /** Return the concrete component for a module component template, if present. */
    [[nodiscard]] std::optional<ComponentId>
    concrete_component_for(ModuleInstanceId instance, ModuleComponentId component) const;

    /** Return the concrete net for a module template net, if present. */
    [[nodiscard]] std::optional<NetId> concrete_net_for(ModuleInstanceId instance,
                                                        TemplateNetDefId template_net) const;

    /** Return concrete net origins for a module instance. */
    [[nodiscard]] std::vector<std::pair<TemplateNetDefId, NetId>>
    module_net_origins(ModuleInstanceId instance) const;

    /** Return concrete component origins for a module instance. */
    [[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>>
    module_component_origins(ModuleInstanceId instance) const;

    /** Return whether a concrete net originated from a module instantiation. */
    [[nodiscard]] bool is_module_origin_net(NetId net) const;

    /** Return whether a concrete component originated from a module instantiation. */
    [[nodiscard]] bool is_module_origin_component(ComponentId component) const;

    /** Return a module definition by stable ID. */
    [[nodiscard]] const ModuleDefinition &module_definition(ModuleDefId id) const;

    /** Return a template net definition by stable ID. */
    [[nodiscard]] const TemplateNetDefinition &template_net_definition(TemplateNetDefId id) const;

    /** Return a port definition by stable ID. */
    [[nodiscard]] const PortDefinition &port_definition(PortDefId id) const;

    /** Return a module component template by stable ID. */
    [[nodiscard]] const ModuleComponentTemplate &
    module_component_template(ModuleComponentId id) const;

    /** Return a module instance by stable ID. */
    [[nodiscard]] const ModuleInstance &module_instance(ModuleInstanceId id) const;

    /** Return a port binding by stable ID. */
    [[nodiscard]] const PortBinding &port_binding(PortBindingId id) const;

    /** Return the number of module definitions. */
    [[nodiscard]] std::size_t module_definition_count() const noexcept;

    /** Return the number of template net definitions. */
    [[nodiscard]] std::size_t template_net_definition_count() const noexcept;

    /** Return the number of port definitions. */
    [[nodiscard]] std::size_t port_definition_count() const noexcept;

    /** Return the number of module component templates. */
    [[nodiscard]] std::size_t module_component_count() const noexcept;

    /** Return the number of module pin template connections. */
    [[nodiscard]] std::size_t module_pin_connection_count() const noexcept;

    /** Return the number of module instances. */
    [[nodiscard]] std::size_t module_instance_count() const noexcept;

    /** Return the number of port bindings. */
    [[nodiscard]] std::size_t port_binding_count() const noexcept;

    /** Require that a module definition ID belongs to this model. */
    void require_module_definition(ModuleDefId module) const;

    /** Require that a template net ID belongs to this model. */
    void require_template_net(TemplateNetDefId net) const;

    /** Require that a port definition ID belongs to this model. */
    void require_port(PortDefId port) const;

    /** Require that a module component template ID belongs to this model. */
    void require_module_component(ModuleComponentId component) const;

    /** Require that a module instance ID belongs to this model. */
    void require_module_instance(ModuleInstanceId instance) const;

    /** Require that a template net belongs to the given module definition. */
    void require_template_net_in_module(ModuleDefId module, TemplateNetDefId net) const;

    /** Require that a port definition belongs to the given module definition. */
    void require_port_in_module(ModuleDefId module, PortDefId port) const;

    /** Require that a component template belongs to the given module definition. */
    void require_module_component_in_module(ModuleDefId module, ModuleComponentId component) const;

    /** Return whether a template net belongs to the given module definition. */
    [[nodiscard]] bool template_net_belongs_to_module(ModuleDefId module,
                                                      TemplateNetDefId net) const;

    /** Return whether a component template belongs to the given module definition. */
    [[nodiscard]] bool module_component_belongs_to_module(ModuleDefId module,
                                                          ModuleComponentId component) const;

  protected:
    /** Construct a read-only facade over owner-private storage. */
    explicit HierarchyModel(std::shared_ptr<const detail::HierarchyState> state);

  private:
    [[nodiscard]] const detail::HierarchyState &state() const noexcept;

    std::shared_ptr<const detail::HierarchyState> state_;
};

} // namespace volt
