#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/circuit/definitions.hpp>
#include <volt/circuit/hierarchy.hpp>
#include <volt/circuit/instances.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/circuit/parts.hpp>
#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>

namespace volt {

class CircuitDesignIntent;
class CircuitElectrical;
class CircuitHierarchy;
class CircuitView;

/** Owning database for the canonical logical circuit model. */
class Circuit {
  public:
    /** Store a reusable pin definition and return its stable ID. */
    [[nodiscard]] PinDefId add_pin_definition(PinDefinition definition);

    /** Store a reusable component definition and return its stable ID. */
    [[nodiscard]] ComponentDefId add_component_definition(ComponentDefinition definition);

    /** Store a component instance and return its stable ID. */
    [[nodiscard]] ComponentId add_component(ComponentInstance component);

    /** Store a concrete pin instance and return its stable ID. */
    [[nodiscard]] PinId add_pin(PinInstance pin);

    /** Store a canonical net and return its stable ID. */
    [[nodiscard]] NetId add_net(Net net);

    /**
     * Instantiate a component definition and create concrete pins for each ordered pin
     * definition.
     */
    [[nodiscard]] ComponentId instantiate_component(ComponentDefId definition,
                                                    ReferenceDesignator reference,
                                                    PropertyMap properties = {});

    /** Connect an existing pin to an existing net; returns true when the circuit changed. */
    bool connect(NetId net, PinId pin);

    /** Disconnect an existing pin from its current net; returns true when the circuit changed. */
    bool disconnect(PinId pin);

    /** Return a read-only view over this circuit. */
    [[nodiscard]] CircuitView view() const noexcept;

    /** Convert to a read-only view when calling read-only APIs. */
    [[nodiscard]] operator CircuitView() const noexcept;

  private:
    friend class CircuitDesignIntent;
    friend class CircuitElectrical;
    friend class CircuitHierarchy;
    friend class CircuitView;

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

    /** Instantiate a module at the root and create concrete nets for its template-local nets. */
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

    /** Set or replace a metadata property on an existing component instance. */
    void set_component_property(ComponentId component, PropertyKey key, PropertyValue value);

    /** Set or replace a typed electrical attribute on an existing component instance. */
    void set_component_electrical_attribute(ComponentId component,
                                            const ElectricalAttributeSpec &spec,
                                            ElectricalAttributeValue value);

    /** Set or replace a typed electrical attribute on an existing reusable pin definition. */
    void set_pin_definition_electrical_attribute(PinDefId pin_definition,
                                                 const ElectricalAttributeSpec &spec,
                                                 ElectricalAttributeValue value);

    /** Assign a selected physical implementation to an existing component instance. */
    void select_physical_part(ComponentId component, PhysicalPart physical_part);

    /** Set or replace a typed electrical attribute on a component's selected physical part. */
    void set_selected_part_electrical_attribute(ComponentId component,
                                                const ElectricalAttributeSpec &spec,
                                                ElectricalAttributeValue value);

    /** Set or replace a typed electrical attribute on an existing net. */
    void set_net_electrical_attribute(NetId net, const ElectricalAttributeSpec &spec,
                                      ElectricalAttributeValue value);

    /** Record that an otherwise empty or single-ended named net is intentionally exported. */
    bool mark_intentional_stub_net(NetId net);

    /** Record that an otherwise connectable concrete pin is intentionally left open. */
    bool mark_intentional_no_connect_pin(PinId pin);

    /** Return the selected physical implementation for a component, if one has been assigned. */
    [[nodiscard]] const std::optional<PhysicalPart> &
    selected_physical_part(ComponentId component) const;

    /** Return the net currently connected to the pin, if any. */
    [[nodiscard]] std::optional<NetId> net_of(PinId pin) const;

    /** Return the component with this reference designator, if it exists. */
    [[nodiscard]] std::optional<ComponentId>
    component_by_reference(const ReferenceDesignator &reference) const;

    /** Return the module definition with this name, if it exists. */
    [[nodiscard]] std::optional<ModuleDefId>
    module_definition_by_name(const ModuleName &name) const;

    /** Return the root-level module instance with this name, if it exists. */
    [[nodiscard]] std::optional<ModuleInstanceId>
    module_instance_by_name(const ModuleInstanceName &name) const;

    /** Return a template-local net in a module definition by name, if it exists. */
    [[nodiscard]] std::optional<TemplateNetDefId> template_net_by_name(ModuleDefId module,
                                                                       const NetName &name) const;

    /** Return a port in a module definition by name, if it exists. */
    [[nodiscard]] std::optional<PortDefId> port_by_name(ModuleDefId module,
                                                        const PortName &name) const;

    /** Return a module component by local reference designator, if it exists. */
    [[nodiscard]] std::optional<ModuleComponentId>
    module_component_by_reference(ModuleDefId module, const ReferenceDesignator &reference) const;

    /** Return the template net connected to a module component pin, if any. */
    [[nodiscard]] std::optional<TemplateNetDefId>
    template_net_for(ModuleDefId module, ModuleComponentId component, PinDefId pin) const;

    /** Return module-local pin connections for one module definition. */
    [[nodiscard]] std::vector<ModulePinConnection> module_pin_connections(ModuleDefId module) const;

    /** Return the explicit binding for a module instance port, if it exists. */
    [[nodiscard]] std::optional<PortBindingId> port_binding_for(ModuleInstanceId instance,
                                                                PortDefId port) const;

    /** Return explicit port binding IDs for one module instance in module port order. */
    [[nodiscard]] std::vector<PortBindingId> port_bindings_for(ModuleInstanceId instance) const;

    /** Return the concrete component created for a module instance component template, if any. */
    [[nodiscard]] std::optional<ComponentId>
    concrete_component_for(ModuleInstanceId instance, ModuleComponentId component) const;

    /** Return concrete net origins for one module instance in module template-net order. */
    [[nodiscard]] std::vector<std::pair<TemplateNetDefId, NetId>>
    module_net_origins(ModuleInstanceId instance) const;

    /** Return concrete component origins for one module instance in module component order. */
    [[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>>
    module_component_origins(ModuleInstanceId instance) const;

    /** Return whether a net is concrete module-origin net. */
    [[nodiscard]] bool is_module_origin_net(NetId net) const;

    /** Return whether this net has explicit author intent as a named/exported stub. */
    [[nodiscard]] bool is_intentional_stub_net(NetId net) const;

    /** Return whether this concrete pin has explicit no-connect author intent. */
    [[nodiscard]] bool is_intentional_no_connect_pin(PinId pin) const;

    /** Return intentional stub-net assertions in deterministic insertion order. */
    [[nodiscard]] const std::vector<NetId> &intentional_stub_nets() const noexcept;

    /** Return intentional no-connect pin assertions in deterministic insertion order. */
    [[nodiscard]] const std::vector<PinId> &intentional_no_connect_pins() const noexcept;

    /** Return whether a component is a concrete module-origin component. */
    [[nodiscard]] bool is_module_origin_component(ComponentId component) const;

    /** Return the concrete net created for a module instance template-local net, if any. */
    [[nodiscard]] std::optional<NetId> concrete_net_for(ModuleInstanceId instance,
                                                        TemplateNetDefId template_net) const;

    /** Return the net with this name, if it exists. */
    [[nodiscard]] std::optional<NetId> net_by_name(const NetName &name) const;

    /** Return concrete pins belonging to a component in deterministic creation order. */
    [[nodiscard]] std::vector<PinId> pins_for(ComponentId component) const;

    /** Return a component pin by reusable pin definition name, if it exists. */
    [[nodiscard]] std::optional<PinId> pin_by_name(ComponentId component,
                                                   std::string_view name) const;

    /** Return a component pin by reusable pin definition, if it exists. */
    [[nodiscard]] std::optional<PinId> pin_by_definition(ComponentId component,
                                                         PinDefId definition) const;

    /** Return a component pin by reusable pin definition number, if it exists. */
    [[nodiscard]] std::optional<PinId> pin_by_number(ComponentId component,
                                                     std::string_view number) const;

    /** Return a reusable pin definition by ID. */
    [[nodiscard]] const PinDefinition &pin_definition(PinDefId id) const;

    /** Return a reusable component definition by ID. */
    [[nodiscard]] const ComponentDefinition &component_definition(ComponentDefId id) const;

    /** Return a component instance by ID. */
    [[nodiscard]] const ComponentInstance &component(ComponentId id) const;

    /** Return a concrete pin instance by ID. */
    [[nodiscard]] const PinInstance &pin(PinId id) const { return pins_.get(id); }

    /** Return a canonical net by ID. */
    [[nodiscard]] const Net &net(NetId id) const { return nets_.get(id); }

    /** Return a reusable module definition by ID. */
    [[nodiscard]] const ModuleDefinition &module_definition(ModuleDefId id) const;

    /** Return a template-local net definition by ID. */
    [[nodiscard]] const TemplateNetDefinition &template_net_definition(TemplateNetDefId id) const {
        return template_net_definitions_.get(id);
    }

    /** Return a module port definition by ID. */
    [[nodiscard]] const PortDefinition &port_definition(PortDefId id) const;

    /** Return a module component template by ID. */
    [[nodiscard]] const ModuleComponentTemplate &
    module_component_template(ModuleComponentId id) const {
        return module_component_templates_.get(id);
    }

    /** Return a root-level module instance by ID. */
    [[nodiscard]] const ModuleInstance &module_instance(ModuleInstanceId id) const;

    /** Return an explicit module port binding by ID. */
    [[nodiscard]] const PortBinding &port_binding(PortBindingId id) const;

    /** Return the number of reusable pin definitions. */
    [[nodiscard]] std::size_t pin_definition_count() const noexcept;

    /** Return the number of reusable component definitions. */
    [[nodiscard]] std::size_t component_definition_count() const noexcept;

    /** Return the number of component instances. */
    [[nodiscard]] std::size_t component_count() const noexcept { return components_.size(); }

    /** Return the number of concrete pin instances. */
    [[nodiscard]] std::size_t pin_count() const noexcept { return pins_.size(); }

    /** Return the number of canonical nets. */
    [[nodiscard]] std::size_t net_count() const noexcept { return nets_.size(); }

    /** Return the number of reusable module definitions. */
    [[nodiscard]] std::size_t module_definition_count() const noexcept;

    /** Return the number of template-local net definitions. */
    [[nodiscard]] std::size_t template_net_definition_count() const noexcept {
        return template_net_definitions_.size();
    }

    /** Return the number of module port definitions. */
    [[nodiscard]] std::size_t port_definition_count() const noexcept;

    /** Return the number of module component templates. */
    [[nodiscard]] std::size_t module_component_count() const noexcept;

    /** Return the number of module pin template connections. */
    [[nodiscard]] std::size_t module_pin_connection_count() const noexcept;

    /** Return the number of root-level module instances. */
    [[nodiscard]] std::size_t module_instance_count() const noexcept;

    /** Return the number of explicit module port bindings. */
    [[nodiscard]] std::size_t port_binding_count() const noexcept { return port_bindings_.size(); }

    void require_pin_definition(PinDefId pin_definition) const;

    void require_component_definition(ComponentDefId component_definition) const;

    void require_component(ComponentId component) const;

    void require_module_definition(ModuleDefId module) const;

    void require_template_net(TemplateNetDefId net) const;

    void require_port(PortDefId port) const;

    void require_module_component(ModuleComponentId component) const;

    void require_module_instance(ModuleInstanceId instance) const;

    void require_template_net_in_module(ModuleDefId module, TemplateNetDefId net) const;

    void require_port_in_module(ModuleDefId module, PortDefId port) const;

    void require_module_component_in_module(ModuleDefId module, ModuleComponentId component) const;

    [[nodiscard]] bool require_template_net_in_module_if_present(ModuleDefId module,
                                                                 TemplateNetDefId net) const {
        require_module_definition(module);
        require_template_net(net);
        const auto &nets = module_definitions_.get(module).template_nets();
        return std::find(nets.begin(), nets.end(), net) != nets.end();
    }

    [[nodiscard]] bool
    require_module_component_in_module_if_present(ModuleDefId module,
                                                  ModuleComponentId component) const;

    void require_pin_in_module_component(ModuleComponentId component, PinDefId pin) const;

    void require_restored_module_connectivity_matches_template(
        ModuleDefId definition, const std::vector<std::pair<TemplateNetDefId, NetId>> &origins,
        const std::vector<std::pair<ModuleComponentId, ComponentId>> &component_origins) const;

    void require_pin(PinId pin) const;

    void require_net(NetId net) const;

    static void require_attribute_owner(const ElectricalAttributeSpec &spec,
                                        ElectricalAttributeOwner expected);

    void
    require_physical_part_matches_component_definition(ComponentDefId component_definition,
                                                       const PhysicalPart &physical_part) const;

    [[nodiscard]] std::optional<NetId> net_of_existing_pin(PinId pin) const;

    EntityTable<PinDefinition, PinDefId> pin_definitions_;
    EntityTable<ComponentDefinition, ComponentDefId> component_definitions_;
    EntityTable<ComponentInstance, ComponentId> components_;
    EntityTable<PinInstance, PinId> pins_;
    EntityTable<Net, NetId> nets_;
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
    std::vector<NetId> intentional_stub_nets_;
    std::vector<PinId> intentional_no_connect_pins_;
};

} // namespace volt
