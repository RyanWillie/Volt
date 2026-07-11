#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <volt/circuit/connectivity/connectivity_model.hpp>
#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/circuit/connectivity/instances.hpp>
#include <volt/circuit/connectivity/nets.hpp>
#include <volt/circuit/constraints/net_classes.hpp>
#include <volt/circuit/detail/subsystem_storage.hpp>
#include <volt/circuit/electrical/electrical_model.hpp>
#include <volt/circuit/hierarchy/hierarchy.hpp>
#include <volt/circuit/hierarchy/hierarchy_model.hpp>
#include <volt/circuit/intent/design_intent.hpp>
#include <volt/circuit/parts/parts.hpp>
#include <volt/core/ids.hpp>

namespace volt {

class Circuit;

namespace queries {
[[nodiscard]] std::optional<ComponentId>
component_by_reference(const Circuit &circuit, const ReferenceDesignator &reference);
[[nodiscard]] std::optional<ModuleDefId> module_definition_by_name(const Circuit &circuit,
                                                                   const ModuleName &name);
[[nodiscard]] std::optional<ModuleInstanceId>
module_instance_by_name(const Circuit &circuit, const ModuleInstanceName &name);
[[nodiscard]] std::optional<TemplateNetDefId>
template_net_by_name(const Circuit &circuit, ModuleDefId module, const NetName &name);
[[nodiscard]] std::optional<PortDefId> port_by_name(const Circuit &circuit, ModuleDefId module,
                                                    const PortName &name);
[[nodiscard]] std::optional<ModuleComponentId>
module_component_by_reference(const Circuit &circuit, ModuleDefId module,
                              const ReferenceDesignator &reference);
[[nodiscard]] std::optional<TemplateNetDefId> template_net_for(const Circuit &circuit,
                                                               ModuleDefId module,
                                                               ModuleComponentId component,
                                                               PinDefId pin);
[[nodiscard]] std::optional<PortBindingId>
port_binding_for(const Circuit &circuit, ModuleInstanceId instance, PortDefId port);
[[nodiscard]] std::vector<PortBindingId> port_bindings_for(const Circuit &circuit,
                                                           ModuleInstanceId instance);
[[nodiscard]] std::optional<ComponentId> concrete_component_for(const Circuit &circuit,
                                                                ModuleInstanceId instance,
                                                                ModuleComponentId component);
[[nodiscard]] std::optional<NetId>
concrete_net_for(const Circuit &circuit, ModuleInstanceId instance, TemplateNetDefId template_net);
[[nodiscard]] std::vector<std::pair<TemplateNetDefId, NetId>>
module_net_origins(const Circuit &circuit, ModuleInstanceId instance);
[[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>>
module_component_origins(const Circuit &circuit, ModuleInstanceId instance);
[[nodiscard]] bool is_module_origin_net(const Circuit &circuit, NetId net);
[[nodiscard]] bool is_module_origin_component(const Circuit &circuit, ComponentId component);
[[nodiscard]] std::optional<NetId> net_by_name(const Circuit &circuit, const NetName &name);
[[nodiscard]] std::vector<PinId> pins_for(const Circuit &circuit, ComponentId component);
[[nodiscard]] std::optional<PinId> pin_by_name(const Circuit &circuit, ComponentId component,
                                               std::string_view name);
[[nodiscard]] std::optional<PinId> pin_by_definition(const Circuit &circuit, ComponentId component,
                                                     PinDefId definition);
[[nodiscard]] std::optional<PinId> pin_by_number(const Circuit &circuit, ComponentId component,
                                                 std::string_view number);
} // namespace queries

/** Map a Circuit-owned stable ID to its canonical entity type. */
template <typename Id> struct CircuitEntityTraits;

/// @cond
template <> struct CircuitEntityTraits<PinDefId> {
    using type = PinDefinition;
};

template <> struct CircuitEntityTraits<ComponentDefId> {
    using type = ComponentDefinition;
};

template <> struct CircuitEntityTraits<ComponentId> {
    using type = ComponentInstance;
};

template <> struct CircuitEntityTraits<PinId> {
    using type = PinInstance;
};

template <> struct CircuitEntityTraits<NetId> {
    using type = Net;
};

template <> struct CircuitEntityTraits<ModuleDefId> {
    using type = ModuleDefinition;
};

template <> struct CircuitEntityTraits<TemplateNetDefId> {
    using type = TemplateNetDefinition;
};

template <> struct CircuitEntityTraits<PortDefId> {
    using type = PortDefinition;
};

template <> struct CircuitEntityTraits<ModuleComponentId> {
    using type = ModuleComponentTemplate;
};

template <> struct CircuitEntityTraits<ModuleInstanceId> {
    using type = ModuleInstance;
};

template <> struct CircuitEntityTraits<PortBindingId> {
    using type = PortBinding;
};

template <> struct CircuitEntityTraits<NetClassId> {
    using type = NetClass;
};

/// @endcond

/** True when an ID names one of Circuit's canonical entity tables. */
template <typename Id>
concept CircuitEntityId = requires { typename CircuitEntityTraits<Id>::type; };

/** Canonical entity type selected by a Circuit-owned stable ID. */
template <CircuitEntityId Id> using entity_type_t = typename CircuitEntityTraits<Id>::type;

template <CircuitEntityId Id> class CircuitEntityRange;

/** Borrowed deterministic range selected by a Circuit-owned stable ID. */
template <CircuitEntityId Id> using entity_range_t = CircuitEntityRange<Id>;

/// @cond
namespace io::detail {
struct ConnectivityRestoration;
struct HierarchyDefinitionRestoration;
struct ModuleInstanceRestoration;
void restore_logical_connectivity(Circuit &circuit, ConnectivityRestoration restoration);
void restore_logical_hierarchy(Circuit &circuit, HierarchyDefinitionRestoration restoration);
[[nodiscard]] ModuleInstanceId
restore_logical_module_instance(Circuit &circuit, ModuleInstanceRestoration restoration);
} // namespace io::detail

/// @endcond

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
 *   docs/superpowers/specs/2026-06-02-volt-kernel-architecture-design.md and
 *   docs/design/adr-append-only-kernel.md.
 */
class Circuit {
  private:
    struct MutatorKey {
      public:
        [[nodiscard]] static MutatorKey make() noexcept { return MutatorKey{}; }

      private:
        MutatorKey() = default;
    };

  public:
    /**
     * Borrow-only facade for connectivity-owned mutations.
     *
     * A non-copyable scoped handle tied to a live lvalue Circuit. Root-owned or cross-subsystem
     * mutations stay on Circuit.
     */
    class ConnectivityMutator {
      public:
        /** Constructed only by Circuit::connectivity(); callers obtain this facade by value. */
        ConnectivityMutator(Circuit &circuit, MutatorKey) noexcept;

        ConnectivityMutator(const ConnectivityMutator &) = delete;
        ConnectivityMutator(ConnectivityMutator &&) = delete;
        ConnectivityMutator &operator=(const ConnectivityMutator &) = delete;
        ConnectivityMutator &operator=(ConnectivityMutator &&) = delete;

        /** Store a reusable pin definition and return its stable ID. */
        [[nodiscard]] PinDefId add_pin_definition(PinDefinition definition);

        /** Store a reusable component definition and return its stable ID. */
        [[nodiscard]] ComponentDefId add_component_definition(ComponentDefinition definition);

        /** Instantiate a component and all ordered definition pins, returning its stable ID. */
        [[nodiscard]] ComponentId add_component(ComponentInstance component);

        /** Store a concrete pin instance and return its stable ID. */
        [[nodiscard]] PinId add_pin(PinInstance pin);

        /** Store a canonical net and return its stable ID. */
        [[nodiscard]] NetId add_net(Net net);

        /** Set or replace a metadata property on an existing component instance. */
        void set_component_property(ComponentId component, PropertyKey key, PropertyValue value);

      private:
        Circuit &circuit_;
    };

    /**
     * Borrow-only facade for hierarchy-owned mutations.
     *
     * A non-copyable scoped handle tied to a live lvalue Circuit. Root-owned or cross-subsystem
     * mutations stay on Circuit.
     */
    class HierarchyMutator {
      public:
        /** Constructed only by Circuit::hierarchy(); callers obtain this facade by value. */
        HierarchyMutator(Circuit &circuit, MutatorKey) noexcept;

        HierarchyMutator(const HierarchyMutator &) = delete;
        HierarchyMutator(HierarchyMutator &&) = delete;
        HierarchyMutator &operator=(const HierarchyMutator &) = delete;
        HierarchyMutator &operator=(HierarchyMutator &&) = delete;

        /** Store a reusable logical module definition and return its stable ID. */
        [[nodiscard]] ModuleDefId add_module_definition(ModuleDefinition definition);

        /** Add a template-local net to a reusable module definition. */
        [[nodiscard]] TemplateNetDefId add_template_net(ModuleDefId module,
                                                        TemplateNetDefinition net);

        /** Add a boundary port to a reusable module definition. */
        [[nodiscard]] PortDefId add_port_definition(ModuleDefId module, PortDefinition port);

        /** Add a component occurrence to a reusable module definition. */
        [[nodiscard]] ModuleComponentId add_module_component(ModuleDefId module,
                                                             ModuleComponentTemplate component);

        /** Connect a module component template pin to a template-local net. */
        bool connect_module_pin(ModuleDefId module, TemplateNetDefId net,
                                ModuleComponentId component, PinDefId pin);

      private:
        Circuit &circuit_;
    };

    /**
     * Borrow-only facade for electrical-owned mutations.
     *
     * A non-copyable scoped handle tied to a live lvalue Circuit. Root-owned or cross-subsystem
     * mutations stay on Circuit.
     */
    class ElectricalMutator {
      public:
        /** Constructed only by Circuit::electrical(); callers obtain this facade by value. */
        ElectricalMutator(Circuit &circuit, MutatorKey) noexcept;

        ElectricalMutator(const ElectricalMutator &) = delete;
        ElectricalMutator(ElectricalMutator &&) = delete;
        ElectricalMutator &operator=(const ElectricalMutator &) = delete;
        ElectricalMutator &operator=(ElectricalMutator &&) = delete;

        /** Set or replace a typed electrical attribute on an existing component instance. */
        void set_component_electrical_attribute(ComponentId component,
                                                const ElectricalAttributeSpec &spec,
                                                ElectricalAttributeValue value);

        /** Set a typed attribute before a pin definition is committed to a component. */
        void set_pin_definition_electrical_attribute(PinDefId pin_definition,
                                                     const ElectricalAttributeSpec &spec,
                                                     ElectricalAttributeValue value);

        /** Set or replace a typed electrical attribute on an existing net. */
        void set_net_electrical_attribute(NetId net, const ElectricalAttributeSpec &spec,
                                          ElectricalAttributeValue value);

        /** Assign a selected physical implementation to an existing component instance. */
        void select_physical_part(ComponentId component, PhysicalPart physical_part);

        /** Set or replace a typed electrical attribute on a component's selected physical part. */
        void set_selected_part_electrical_attribute(ComponentId component,
                                                    const ElectricalAttributeSpec &spec,
                                                    ElectricalAttributeValue value);

      private:
        Circuit &circuit_;
    };

    /**
     * Borrow-only facade for design-intent mutations.
     *
     * A non-copyable scoped handle tied to a live lvalue Circuit. Root-owned or cross-subsystem
     * mutations stay on Circuit.
     */
    class IntentMutator {
      public:
        /** Constructed only by Circuit::intent(); callers obtain this facade by value. */
        IntentMutator(Circuit &circuit, MutatorKey) noexcept;

        IntentMutator(const IntentMutator &) = delete;
        IntentMutator(IntentMutator &&) = delete;
        IntentMutator &operator=(const IntentMutator &) = delete;
        IntentMutator &operator=(IntentMutator &&) = delete;

        /** Record that an otherwise empty or single-ended named net is intentionally exported. */
        bool mark_intentional_stub_net(NetId net);

        /** Record that an otherwise connectable concrete pin is intentionally left open. */
        bool mark_intentional_no_connect_pin(PinId pin);

        /** Set explicit do-not-populate assembly intent for an existing component. */
        void set_component_dnp(ComponentId component, bool dnp);

        /** Set or clear selected-part override assembly intent for an existing component. */
        void set_component_selection_override(ComponentId component, bool override);

      private:
        Circuit &circuit_;
    };

    /**
     * Borrow-only facade for net-class mutations.
     *
     * A non-copyable scoped handle tied to a live lvalue Circuit. Root-owned or cross-subsystem
     * mutations stay on Circuit.
     */
    class NetClassMutator {
      public:
        /** Constructed only by Circuit::net_classes(); callers obtain this facade by value. */
        NetClassMutator(Circuit &circuit, MutatorKey) noexcept;

        NetClassMutator(const NetClassMutator &) = delete;
        NetClassMutator(NetClassMutator &&) = delete;
        NetClassMutator &operator=(const NetClassMutator &) = delete;
        NetClassMutator &operator=(NetClassMutator &&) = delete;

        /** Store a reusable net class intent definition. */
        [[nodiscard]] NetClassId add_net_class(NetClass net_class);

        /** Assign an existing net class to an existing logical net. */
        bool assign_net_class(NetId net, NetClassId net_class);

      private:
        Circuit &circuit_;
    };

    /** Return the borrow-only connectivity mutation facade. */
    [[nodiscard]] ConnectivityMutator connectivity() & noexcept;
    [[nodiscard]] ConnectivityMutator connectivity() && = delete;

    /** Return the borrow-only hierarchy mutation facade. */
    [[nodiscard]] HierarchyMutator hierarchy() & noexcept;
    [[nodiscard]] HierarchyMutator hierarchy() && = delete;

    /** Return the borrow-only electrical mutation facade. */
    [[nodiscard]] ElectricalMutator electrical() & noexcept;
    [[nodiscard]] ElectricalMutator electrical() && = delete;

    /** Return the borrow-only design-intent mutation facade. */
    [[nodiscard]] IntentMutator intent() & noexcept;
    [[nodiscard]] IntentMutator intent() && = delete;

    /** Return the borrow-only net-class mutation facade. */
    [[nodiscard]] NetClassMutator net_classes() & noexcept;
    [[nodiscard]] NetClassMutator net_classes() && = delete;

    /** Atomically commit one complete reusable component definition. */
    [[nodiscard]] ComponentDefId define_component(ComponentSpec spec);

    /** Atomically commit one complete reusable module definition. */
    [[nodiscard]] ModuleDefId define_module(ModuleSpec spec);

    /** Atomically instantiate a component and every ordered pin in its definition. */
    [[nodiscard]] ComponentId instantiate_component(ComponentDefId definition,
                                                    ComponentInstanceSpec spec);

    /** Atomically add an unconnected canonical net from a complete typed input. */
    [[nodiscard]] NetId add_net(NetSpec spec);

    /** Instantiate a module at the root and create concrete nets for its template-local nets. */
    [[nodiscard]] ModuleInstanceId instantiate_root_module(ModuleDefId definition,
                                                           ModuleInstanceName name);

    /** Record an explicit connectivity edge from an instance-local port net to a parent net. */
    [[nodiscard]] PortBindingId bind_port(ModuleInstanceId instance, PortDefId port,
                                          NetId parent_net);

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

    /** Return a canonical entity selected by its strongly typed stable ID. */
    template <CircuitEntityId Id> [[nodiscard]] const entity_type_t<Id> &get(Id id) const;

    /**
     * Return a borrowed deterministic range over one canonical entity family.
     *
     * The range remains valid while this Circuit remains alive and is not structurally mutated.
     */
    template <CircuitEntityId Id> [[nodiscard]] entity_range_t<Id> all() const &;
    template <CircuitEntityId Id> [[nodiscard]] entity_range_t<Id> all() const && = delete;

    /** Return the net containing a valid concrete pin, or nullopt when it is disconnected. */
    [[nodiscard]] std::optional<NetId> net_of(PinId pin) const;

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
    friend std::optional<ComponentId> queries::component_by_reference(const Circuit &,
                                                                      const ReferenceDesignator &);
    friend std::optional<ModuleDefId> queries::module_definition_by_name(const Circuit &,
                                                                         const ModuleName &);
    friend std::optional<ModuleInstanceId>
    queries::module_instance_by_name(const Circuit &, const ModuleInstanceName &);
    friend std::optional<TemplateNetDefId>
    queries::template_net_by_name(const Circuit &, ModuleDefId, const NetName &);
    friend std::optional<PortDefId> queries::port_by_name(const Circuit &, ModuleDefId,
                                                          const PortName &);
    friend std::optional<ModuleComponentId>
    queries::module_component_by_reference(const Circuit &, ModuleDefId,
                                           const ReferenceDesignator &);
    friend std::optional<TemplateNetDefId> queries::template_net_for(const Circuit &, ModuleDefId,
                                                                     ModuleComponentId, PinDefId);
    friend std::optional<PortBindingId> queries::port_binding_for(const Circuit &, ModuleInstanceId,
                                                                  PortDefId);
    friend std::vector<PortBindingId> queries::port_bindings_for(const Circuit &, ModuleInstanceId);
    friend std::optional<ComponentId>
    queries::concrete_component_for(const Circuit &, ModuleInstanceId, ModuleComponentId);
    friend std::optional<NetId> queries::concrete_net_for(const Circuit &, ModuleInstanceId,
                                                          TemplateNetDefId);
    friend std::vector<std::pair<TemplateNetDefId, NetId>>
    queries::module_net_origins(const Circuit &, ModuleInstanceId);
    friend std::vector<std::pair<ModuleComponentId, ComponentId>>
    queries::module_component_origins(const Circuit &, ModuleInstanceId);
    friend bool queries::is_module_origin_net(const Circuit &, NetId);
    friend bool queries::is_module_origin_component(const Circuit &, ComponentId);
    friend std::optional<NetId> queries::net_by_name(const Circuit &, const NetName &);
    friend std::vector<PinId> queries::pins_for(const Circuit &, ComponentId);
    friend std::optional<PinId> queries::pin_by_name(const Circuit &, ComponentId,
                                                     std::string_view);
    friend std::optional<PinId> queries::pin_by_definition(const Circuit &, ComponentId, PinDefId);
    friend std::optional<PinId> queries::pin_by_number(const Circuit &, ComponentId,
                                                       std::string_view);

    friend void
    io::detail::restore_logical_connectivity(Circuit &circuit,
                                             io::detail::ConnectivityRestoration restoration);
    friend void
    io::detail::restore_logical_hierarchy(Circuit &circuit,
                                          io::detail::HierarchyDefinitionRestoration restoration);
    friend ModuleInstanceId
    io::detail::restore_logical_module_instance(Circuit &circuit,
                                                io::detail::ModuleInstanceRestoration restoration);

    struct ConnectivityStorage
        : detail::SubsystemStorage<ConnectivityModel, detail::ConnectivityState> {
        [[nodiscard]] PinDefId add_pin_definition(PinDefinition definition);
        [[nodiscard]] ComponentDefId add_component_definition(ComponentDefinition definition);
        [[nodiscard]] bool pin_definition_is_owned(PinDefId pin_definition) const;
        [[nodiscard]] ComponentId add_component(ComponentInstance component);
        [[nodiscard]] PinId add_pin(PinInstance pin);
        [[nodiscard]] NetId add_net(Net net);
        [[nodiscard]] ComponentId instantiate_component(ComponentDefId definition,
                                                        ReferenceDesignator reference,
                                                        PropertyMap properties = {});
        bool connect(NetId net, PinId pin);
        bool disconnect(PinId pin);
        void set_component_property(ComponentId component, PropertyKey key, PropertyValue value);
    };

    struct HierarchyStorage : detail::SubsystemStorage<HierarchyModel, detail::HierarchyState> {
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
    };

    struct ElectricalStorage : detail::SubsystemStorage<ElectricalModel, detail::ElectricalState> {
        [[nodiscard]] static ElectricalAttributeMap
        preflight_attributes(const std::vector<ElectricalAttributeAssignment> &assignments,
                             ElectricalAttributeOwner owner);
        void restore_component_attributes(ComponentId component, ElectricalAttributeMap attributes);
        void restore_pin_definition_attributes(PinDefId pin_definition,
                                               ElectricalAttributeMap attributes);
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
    };

    struct DesignIntentStorage : detail::SubsystemStorage<DesignIntent, detail::DesignIntentState> {
        bool mark_intentional_stub_net(NetId net);
        bool mark_intentional_no_connect_pin(PinId pin);
        void set_component_dnp(ComponentId component, bool dnp);
        void set_component_selection_override(ComponentId component, bool override);
    };

    struct NetClassStorage : detail::SubsystemStorage<NetClasses, detail::NetClassesState> {
        [[nodiscard]] NetClassId add_net_class(NetClass net_class);
        [[nodiscard]] bool assign_net_class(NetId net, NetClassId net_class);
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

    [[nodiscard]] ModuleInstanceId restore_root_module_instance(
        ModuleDefId definition, ModuleInstanceName name,
        const std::vector<std::pair<TemplateNetDefId, NetId>> &origins,
        const std::vector<std::pair<ModuleComponentId, ComponentId>> &component_origins);

    ConnectivityStorage connectivity_;
    HierarchyStorage hierarchy_;
    ElectricalStorage electrical_;
    DesignIntentStorage intent_;
    NetClassStorage net_classes_;
};

/**
 * Non-owning forward range over one Circuit entity family.
 *
 * Iterators keep a pointer to the Circuit, so destroying or structurally mutating the Circuit
 * invalidates the range and its iterators. Creating a range from a temporary Circuit is deleted.
 */
template <CircuitEntityId Id> class CircuitEntityRange {
  public:
    /** Forward iterator yielding const references to canonical entities. */
    /// @cond
    class iterator {
      public:
        using value_type = entity_type_t<Id>;
        using difference_type = std::ptrdiff_t;
        using reference = const value_type &;
        using pointer = const value_type *;
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;

        /** Construct a singular iterator. */
        iterator() = default;

        /** Return the canonical entity at the current stable-ID index. */
        [[nodiscard]] reference operator*() const { return circuit_->get(Id{index_}); }

        /** Return the canonical entity pointer at the current stable-ID index. */
        [[nodiscard]] pointer operator->() const { return &**this; }

        /** Advance to the next stable-ID index. */
        iterator &operator++() {
            ++index_;
            return *this;
        }

        /** Return the current iterator, then advance to the next stable-ID index. */
        iterator operator++(int) {
            auto previous = *this;
            ++*this;
            return previous;
        }

        /** Compare borrowed iterators by Circuit identity and stable-ID index. */
        friend bool operator==(const iterator &, const iterator &) = default;

        /** Construct an iterator at one deterministic entity-table index. */
        iterator(const Circuit &circuit, std::size_t index) noexcept
            : circuit_{&circuit}, index_{index} {}

        /** Prevent an iterator from borrowing a temporary Circuit. */
        iterator(const Circuit &&, std::size_t) = delete;

      private:
        const Circuit *circuit_ = nullptr;
        std::size_t index_ = 0;
    };

    /// @endcond

    /** Return an iterator at the first stable-ID index. */
    [[nodiscard]] iterator begin() const noexcept { return iterator{*circuit_, 0}; }

    /** Return the past-the-end iterator for the captured entity count. */
    [[nodiscard]] iterator end() const noexcept { return iterator{*circuit_, size_}; }

    /** Return the captured number of entities in this family. */
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    /** Construct a correctly sized range borrowing a live Circuit lvalue. */
    explicit CircuitEntityRange(const Circuit &circuit) noexcept : circuit_{&circuit} {
        if constexpr (std::same_as<Id, PinDefId>) {
            size_ = circuit.pin_definition_count();
        } else if constexpr (std::same_as<Id, ComponentDefId>) {
            size_ = circuit.component_definition_count();
        } else if constexpr (std::same_as<Id, ComponentId>) {
            size_ = circuit.component_count();
        } else if constexpr (std::same_as<Id, PinId>) {
            size_ = circuit.pin_count();
        } else if constexpr (std::same_as<Id, NetId>) {
            size_ = circuit.net_count();
        } else if constexpr (std::same_as<Id, ModuleDefId>) {
            size_ = circuit.module_definition_count();
        } else if constexpr (std::same_as<Id, TemplateNetDefId>) {
            size_ = circuit.template_net_definition_count();
        } else if constexpr (std::same_as<Id, PortDefId>) {
            size_ = circuit.port_definition_count();
        } else if constexpr (std::same_as<Id, ModuleComponentId>) {
            size_ = circuit.module_component_count();
        } else if constexpr (std::same_as<Id, ModuleInstanceId>) {
            size_ = circuit.module_instance_count();
        } else if constexpr (std::same_as<Id, PortBindingId>) {
            size_ = circuit.port_binding_count();
        } else {
            static_assert(std::same_as<Id, NetClassId>);
            size_ = circuit.net_class_count();
        }
    }

    /** Prevent a range from borrowing a temporary Circuit. */
    CircuitEntityRange(const Circuit &&) = delete;

  private:
    const Circuit *circuit_;
    std::size_t size_ = 0;
};

template <CircuitEntityId Id> [[nodiscard]] const entity_type_t<Id> &Circuit::get(Id id) const {
    if constexpr (std::same_as<Id, PinDefId>) {
        return pin_definition(id);
    } else if constexpr (std::same_as<Id, ComponentDefId>) {
        return component_definition(id);
    } else if constexpr (std::same_as<Id, ComponentId>) {
        return component(id);
    } else if constexpr (std::same_as<Id, PinId>) {
        return pin(id);
    } else if constexpr (std::same_as<Id, NetId>) {
        return net(id);
    } else if constexpr (std::same_as<Id, ModuleDefId>) {
        return module_definition(id);
    } else if constexpr (std::same_as<Id, TemplateNetDefId>) {
        return template_net_definition(id);
    } else if constexpr (std::same_as<Id, PortDefId>) {
        return port_definition(id);
    } else if constexpr (std::same_as<Id, ModuleComponentId>) {
        return module_component_template(id);
    } else if constexpr (std::same_as<Id, ModuleInstanceId>) {
        return module_instance(id);
    } else if constexpr (std::same_as<Id, PortBindingId>) {
        return port_binding(id);
    } else {
        static_assert(std::same_as<Id, NetClassId>);
        return net_class(id);
    }
}

template <CircuitEntityId Id> [[nodiscard]] entity_range_t<Id> Circuit::all() const & {
    return entity_range_t<Id>{*this};
}

} // namespace volt
