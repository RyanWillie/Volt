#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/circuit/connectivity/connectivity_model.hpp>
#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/circuit/connectivity/instances.hpp>
#include <volt/circuit/connectivity/nets.hpp>
#include <volt/circuit/constraints/net_classes.hpp>
#include <volt/circuit/electrical/electrical_model.hpp>
#include <volt/circuit/hierarchy/hierarchy.hpp>
#include <volt/circuit/hierarchy/hierarchy_model.hpp>
#include <volt/circuit/intent/design_intent.hpp>
#include <volt/circuit/parts/parts.hpp>
#include <volt/core/ids.hpp>

namespace volt {

/**
 * Canonical logical circuit model and aggregate root of the kernel.
 *
 * Responsibility: composes and coordinates the connectivity, hierarchy, electrical,
 *   design-intent, and net-class subsystems; owns only the structural primitives and the
 *   cross-subsystem invariants no single subsystem can enforce alone.
 * Invariants: cross-subsystem references are pre-flighted before any subsystem mutates, so a
 *   partial operation cannot leave invalid kernel state; structural violations throw.
 * Collaborators: composes the *Model subsystems; consumed read-only (const Circuit&) by
 *   validation/ERC, IO, and the Schematic/Board projections. See
 *   docs/superpowers/specs/2026-06-02-volt-kernel-architecture-design.md.
 */
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

    /** Set explicit do-not-populate assembly intent for an existing component. */
    void set_component_dnp(ComponentId component, bool dnp);

    /** Set or clear selected-part override assembly intent for an existing component. */
    void set_component_selection_override(ComponentId component, bool override);

    /** Store a reusable net class intent definition. */
    [[nodiscard]] NetClassId add_net_class(NetClass net_class);

    /** Assign an existing net class to an existing logical net. */
    bool assign_net_class(NetId net, NetClassId net_class);

    /** Return the selected physical implementation for a component, if one has been assigned. */
    [[nodiscard]] const std::optional<PhysicalPart> &
    selected_physical_part(ComponentId component) const;

    /** Return typed electrical attributes for an existing component instance. */
    [[nodiscard]] const ElectricalAttributeMap &
    component_electrical_attributes(ComponentId component) const;

    /** Return typed electrical attributes for an existing reusable pin definition. */
    [[nodiscard]] const ElectricalAttributeMap &
    pin_definition_electrical_attributes(PinDefId pin_definition) const;

    /** Return typed electrical attributes for an existing net. */
    [[nodiscard]] const ElectricalAttributeMap &net_electrical_attributes(NetId net) const;

    /** Return read-only access to connectivity-owned query primitives. */
    [[nodiscard]] const ConnectivityModel &connectivity_model() const noexcept {
        return connectivity_;
    }

    /** Return read-only access to hierarchy-owned query primitives. */
    [[nodiscard]] const HierarchyModel &hierarchy_model() const noexcept { return hierarchy_; }

    /** Return module-local pin connections for one module definition. */
    [[nodiscard]] std::vector<ModulePinConnection> module_pin_connections(ModuleDefId module) const;

    /** Return concrete net origins for one module instance in module template-net order. */
    [[nodiscard]] std::vector<std::pair<TemplateNetDefId, NetId>>
    module_net_origins(ModuleInstanceId instance) const;

    /** Return concrete component origins for one module instance in module component order. */
    [[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>>
    module_component_origins(ModuleInstanceId instance) const;

    /** Return whether this net has explicit author intent as a named/exported stub. */
    [[nodiscard]] bool is_intentional_stub_net(NetId net) const;

    /** Return whether this concrete pin has explicit no-connect author intent. */
    [[nodiscard]] bool is_intentional_no_connect_pin(PinId pin) const;

    /** Return explicit component DNP intent, if one has been authored. */
    [[nodiscard]] std::optional<bool> component_dnp(ComponentId component) const;

    /** Return whether this component has selected-part override intent. */
    [[nodiscard]] bool is_component_selection_override(ComponentId component) const;

    /** Return intentional stub-net assertions in deterministic insertion order. */
    [[nodiscard]] const std::vector<NetId> &intentional_stub_nets() const noexcept;

    /** Return intentional no-connect pin assertions in deterministic insertion order. */
    [[nodiscard]] const std::vector<PinId> &intentional_no_connect_pins() const noexcept;

    /** Return component assembly intent in deterministic insertion order. */
    [[nodiscard]] const std::vector<ComponentAssemblyIntent> &
    component_assembly_intents() const noexcept;

    /** Return a reusable net class intent definition by ID. */
    [[nodiscard]] const NetClass &net_class(NetClassId id) const;

    /** Return a net class by stable name, if one exists. */
    [[nodiscard]] std::optional<NetClassId> net_class_by_name(const NetClassName &name) const;

    /** Return the assigned net class for a net, if one exists. */
    [[nodiscard]] std::optional<NetClassId> net_class_for_net(NetId net) const;

    /** Return net-class net assignments in deterministic insertion order. */
    [[nodiscard]] const std::vector<std::pair<NetId, NetClassId>> &
    net_class_assignments() const noexcept;

    /** Return a reusable pin definition by ID. */
    [[nodiscard]] const PinDefinition &pin_definition(PinDefId id) const;

    /** Return a reusable component definition by ID. */
    [[nodiscard]] const ComponentDefinition &component_definition(ComponentDefId id) const;

    /** Return a component instance by ID. */
    [[nodiscard]] const ComponentInstance &component(ComponentId id) const;

    /** Return a concrete pin instance by ID. */
    [[nodiscard]] const PinInstance &pin(PinId id) const { return connectivity_.pin(id); }

    /** Return a canonical net by ID. */
    [[nodiscard]] const Net &net(NetId id) const { return connectivity_.net(id); }

    /** Return a reusable module definition by ID. */
    [[nodiscard]] const ModuleDefinition &module_definition(ModuleDefId id) const;

    /** Return a template-local net definition by ID. */
    [[nodiscard]] const TemplateNetDefinition &template_net_definition(TemplateNetDefId id) const {
        return hierarchy_.template_net_definition(id);
    }

    /** Return a module port definition by ID. */
    [[nodiscard]] const PortDefinition &port_definition(PortDefId id) const;

    /** Return a module component template by ID. */
    [[nodiscard]] const ModuleComponentTemplate &
    module_component_template(ModuleComponentId id) const {
        return hierarchy_.module_component_template(id);
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
    [[nodiscard]] std::size_t component_count() const noexcept {
        return connectivity_.component_count();
    }

    /** Return the number of concrete pin instances. */
    [[nodiscard]] std::size_t pin_count() const noexcept { return connectivity_.pin_count(); }

    /** Return the number of canonical nets. */
    [[nodiscard]] std::size_t net_count() const noexcept { return connectivity_.net_count(); }

    /** Return the number of reusable module definitions. */
    [[nodiscard]] std::size_t module_definition_count() const noexcept;

    /** Return the number of template-local net definitions. */
    [[nodiscard]] std::size_t template_net_definition_count() const noexcept {
        return hierarchy_.template_net_definition_count();
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
    [[nodiscard]] std::size_t port_binding_count() const noexcept {
        return hierarchy_.port_binding_count();
    }

    /** Return the number of reusable net class intent definitions. */
    [[nodiscard]] std::size_t net_class_count() const noexcept {
        return net_classes_.net_class_count();
    }

  private:
    struct ConnectivityStorage : ConnectivityModel {
        ConnectivityStorage();
        ConnectivityStorage(const ConnectivityStorage &other);
        ConnectivityStorage(ConnectivityStorage &&other) noexcept = default;
        ConnectivityStorage &operator=(const ConnectivityStorage &other);
        ConnectivityStorage &operator=(ConnectivityStorage &&other) noexcept = default;

        [[nodiscard]] PinDefId add_pin_definition(PinDefinition definition);
        [[nodiscard]] ComponentDefId add_component_definition(ComponentDefinition definition);
        [[nodiscard]] ComponentId add_component(ComponentInstance component);
        [[nodiscard]] PinId add_pin(PinInstance pin);
        [[nodiscard]] NetId add_net(Net net);
        [[nodiscard]] ComponentId instantiate_component(ComponentDefId definition,
                                                        ReferenceDesignator reference,
                                                        PropertyMap properties = {});
        bool connect(NetId net, PinId pin);
        bool disconnect(PinId pin);
        void set_component_property(ComponentId component, PropertyKey key, PropertyValue value);

      private:
        explicit ConnectivityStorage(std::shared_ptr<detail::ConnectivityState> state);
        [[nodiscard]] detail::ConnectivityState &mutable_state() noexcept;
        [[nodiscard]] const detail::ConnectivityState &state() const noexcept;

        std::shared_ptr<detail::ConnectivityState> state_;
    };

    struct HierarchyStorage : HierarchyModel {
        HierarchyStorage();
        HierarchyStorage(const HierarchyStorage &other);
        HierarchyStorage(HierarchyStorage &&other) noexcept = default;
        HierarchyStorage &operator=(const HierarchyStorage &other);
        HierarchyStorage &operator=(HierarchyStorage &&other) noexcept = default;

        [[nodiscard]] ModuleDefId add_module_definition(ModuleDefinition definition);
        [[nodiscard]] TemplateNetDefId add_template_net(ModuleDefId module,
                                                        TemplateNetDefinition net);
        [[nodiscard]] PortDefId add_port_definition(ModuleDefId module, PortDefinition port);
        [[nodiscard]] ModuleComponentId add_module_component(ModuleDefId module,
                                                             ModuleComponentTemplate component);
        bool connect_module_pin(ModuleDefId module, TemplateNetDefId net,
                                ModuleComponentId component, PinDefId pin);
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

      private:
        explicit HierarchyStorage(std::shared_ptr<detail::HierarchyState> state);
        [[nodiscard]] detail::HierarchyState &mutable_state() noexcept;
        [[nodiscard]] const detail::HierarchyState &state() const noexcept;

        std::shared_ptr<detail::HierarchyState> state_;
    };

    struct ElectricalStorage : ElectricalModel {
        ElectricalStorage();
        ElectricalStorage(const ElectricalStorage &other);
        ElectricalStorage(ElectricalStorage &&other) noexcept = default;
        ElectricalStorage &operator=(const ElectricalStorage &other);
        ElectricalStorage &operator=(ElectricalStorage &&other) noexcept = default;

        void set_component_attribute(ComponentId component, const ElectricalAttributeSpec &spec,
                                     ElectricalAttributeValue value);
        void set_pin_definition_attribute(PinDefId pin_definition,
                                          const ElectricalAttributeSpec &spec,
                                          ElectricalAttributeValue value);
        void set_net_attribute(NetId net, const ElectricalAttributeSpec &spec,
                               ElectricalAttributeValue value);
        void select_physical_part(ComponentId component, PhysicalPart physical_part,
                                  const std::vector<PinDefId> &component_pins);
        void set_selected_part_attribute(ComponentId component, const ElectricalAttributeSpec &spec,
                                         ElectricalAttributeValue value);

      private:
        explicit ElectricalStorage(std::shared_ptr<detail::ElectricalState> state);
        [[nodiscard]] detail::ElectricalState &mutable_state() noexcept;
        [[nodiscard]] const detail::ElectricalState &state() const noexcept;

        std::shared_ptr<detail::ElectricalState> state_;
    };

    struct DesignIntentStorage : DesignIntent {
        DesignIntentStorage();
        DesignIntentStorage(const DesignIntentStorage &other);
        DesignIntentStorage(DesignIntentStorage &&other) noexcept = default;
        DesignIntentStorage &operator=(const DesignIntentStorage &other);
        DesignIntentStorage &operator=(DesignIntentStorage &&other) noexcept = default;

        bool mark_intentional_stub_net(NetId net);
        bool mark_intentional_no_connect_pin(PinId pin);
        void set_component_dnp(ComponentId component, bool dnp);
        void set_component_selection_override(ComponentId component, bool override);

      private:
        explicit DesignIntentStorage(std::shared_ptr<detail::DesignIntentState> state);
        [[nodiscard]] detail::DesignIntentState &mutable_state() noexcept;
        [[nodiscard]] const detail::DesignIntentState &state() const noexcept;

        std::shared_ptr<detail::DesignIntentState> state_;
    };

    struct NetClassStorage : NetClasses {
        NetClassStorage();
        NetClassStorage(const NetClassStorage &other);
        NetClassStorage(NetClassStorage &&other) noexcept = default;
        NetClassStorage &operator=(const NetClassStorage &other);
        NetClassStorage &operator=(NetClassStorage &&other) noexcept = default;

        [[nodiscard]] NetClassId add_net_class(NetClass net_class);
        [[nodiscard]] bool assign_net_class(NetId net, NetClassId net_class);

      private:
        explicit NetClassStorage(std::shared_ptr<detail::NetClassesState> state);
        [[nodiscard]] detail::NetClassesState &mutable_state() noexcept;
        [[nodiscard]] const detail::NetClassesState &state() const noexcept;

        std::shared_ptr<detail::NetClassesState> state_;
    };

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
        return hierarchy_.template_net_belongs_to_module(module, net);
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

    void require_net_class(NetClassId net_class) const;

    [[nodiscard]] std::optional<NetId> net_of_existing_pin(PinId pin) const;

    ConnectivityStorage connectivity_;
    HierarchyStorage hierarchy_;
    ElectricalStorage electrical_;
    DesignIntentStorage intent_;
    NetClassStorage net_classes_;
};

} // namespace volt
