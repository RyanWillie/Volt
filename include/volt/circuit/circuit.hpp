#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/circuit/connectivity/instances.hpp>
#include <volt/circuit/connectivity/nets.hpp>
#include <volt/circuit/constraints/net_classes.hpp>
#include <volt/circuit/hierarchy/hierarchy.hpp>
#include <volt/circuit/intent/design_intent.hpp>
#include <volt/circuit/parts/parts.hpp>
#include <volt/circuit/updates.hpp>
#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>

namespace volt {

class Circuit;

/// @cond
namespace detail {

template <typename Id> struct CircuitEntityDescriptor;
template <typename Id> class CircuitEntityRange;

template <> struct CircuitEntityDescriptor<PinDefId> {
    using type = PinDefinition;
};

template <> struct CircuitEntityDescriptor<ComponentDefId> {
    using type = ComponentDefinition;
};

template <> struct CircuitEntityDescriptor<ComponentId> {
    using type = ComponentInstance;
};

template <> struct CircuitEntityDescriptor<PinId> {
    using type = PinInstance;
};

template <> struct CircuitEntityDescriptor<NetId> {
    using type = Net;
};

template <> struct CircuitEntityDescriptor<ModuleDefId> {
    using type = ModuleDefinition;
};

template <> struct CircuitEntityDescriptor<TemplateNetDefId> {
    using type = TemplateNetDefinition;
};

template <> struct CircuitEntityDescriptor<PortDefId> {
    using type = PortDefinition;
};

template <> struct CircuitEntityDescriptor<ModuleComponentId> {
    using type = ModuleComponentTemplate;
};

template <> struct CircuitEntityDescriptor<ModuleInstanceId> {
    using type = ModuleInstance;
};

template <> struct CircuitEntityDescriptor<PortBindingId> {
    using type = PortBinding;
};

template <> struct CircuitEntityDescriptor<NetClassId> {
    using type = NetClass;
};

} // namespace detail

/// @endcond

/** True when an ID names one of Circuit's canonical entity tables. */
template <typename Id>
concept CircuitEntityId =
    std::same_as<Id, PinDefId> || std::same_as<Id, ComponentDefId> ||
    std::same_as<Id, ComponentId> || std::same_as<Id, PinId> || std::same_as<Id, NetId> ||
    std::same_as<Id, ModuleDefId> || std::same_as<Id, TemplateNetDefId> ||
    std::same_as<Id, PortDefId> || std::same_as<Id, ModuleComponentId> ||
    std::same_as<Id, ModuleInstanceId> || std::same_as<Id, PortBindingId> ||
    std::same_as<Id, NetClassId>;

/** Canonical entity type selected by a Circuit-owned stable ID. */
template <CircuitEntityId Id>
using entity_type_t = typename detail::CircuitEntityDescriptor<Id>::type;

/** Borrowed deterministic range selected by a Circuit-owned stable ID. */
template <CircuitEntityId Id> using entity_range_t = detail::CircuitEntityRange<Id>;

/// @cond
namespace io::detail {
struct LogicalCircuitRestorationPlan;
[[nodiscard]] Circuit restore_logical_circuit(LogicalCircuitRestorationPlan plan);
} // namespace io::detail

/// @endcond

/**
 * Canonical logical circuit model and aggregate root of the kernel.
 *
 * Responsibility: owns canonical logical entities, relationships, typed electrical meaning,
 *   hierarchy, design intent, and net-class assignments.
 * Invariants: cross-entity references are pre-flighted before mutation, so a partial operation
 *   cannot leave invalid kernel state; structural violations throw.
 * Collaborators: consumed read-only by validation, IO, and schematic/PCB projections. See
 *   docs/design/adr-circuit-aggregate-api.md and docs/design/adr-append-only-kernel.md.
 */
class Circuit final {
  public:
    /** Atomically commit one complete reusable component definition. */
    [[nodiscard]] ComponentDefId define_component(ComponentSpec spec);

    /** Atomically commit one complete reusable module definition. */
    [[nodiscard]] ModuleDefId define_module(ModuleSpec spec);

    /** Atomically commit one complete reusable net-class definition. */
    [[nodiscard]] NetClassId define_net_class(NetClassSpec spec);

    /** Atomically instantiate a component and every ordered pin in its definition. */
    [[nodiscard]] ComponentId instantiate_component(ComponentDefId definition,
                                                    ComponentInstanceSpec spec);

    /** Atomically instantiate a root module and all of its concrete entities. */
    [[nodiscard]] ModuleInstanceId instantiate_module(ModuleDefId definition,
                                                      ModuleInstanceSpec spec);

    /** Atomically add an unconnected canonical net from a complete typed input. */
    [[nodiscard]] NetId add_net(NetSpec spec);

    /** Connect an existing pin to an existing net; returns true when the circuit changed. */
    bool connect(NetId net, PinId pin);

    /** Disconnect an existing pin from its current net; returns true when the circuit changed. */
    bool disconnect(PinId pin);

    /** Record an explicit connectivity edge from an instance-local port net to a parent net. */
    [[nodiscard]] PortBindingId bind_port(ModuleInstanceId instance, PortDefId port,
                                          NetId parent_net);

    /** Apply one closed typed progressive update to a component instance. */
    void update(ComponentId component, ComponentUpdate change);

    /** Apply one closed typed progressive update to a logical net. */
    void update(NetId net, NetUpdate change);

    /** Mark an existing concrete pin as intentionally unconnected. */
    void mark_no_connect(PinId pin);

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

  private:
    friend Circuit
    io::detail::restore_logical_circuit(io::detail::LogicalCircuitRestorationPlan plan);

    struct ConnectivityState {
        ConnectivityState() = default;
        ConnectivityState(const ConnectivityState &) = default;
        ConnectivityState &operator=(const ConnectivityState &) = default;
        ConnectivityState(ConnectivityState &&other) noexcept;
        ConnectivityState &operator=(ConnectivityState &&other) noexcept;

        EntityTable<PinDefinition, PinDefId> pin_definitions;
        EntityTable<ComponentDefinition, ComponentDefId> component_definitions;
        EntityTable<ComponentInstance, ComponentId> components;
        EntityTable<PinInstance, PinId> pins;
        EntityTable<Net, NetId> nets;
        std::unordered_map<std::string, ComponentId> components_by_reference;
        std::unordered_map<std::string, NetId> nets_by_name;
        std::vector<std::optional<ComponentDefId>> component_definition_by_pin;
        std::vector<std::vector<PinId>> pins_by_component;
        std::vector<std::optional<NetId>> net_by_pin;
        std::size_t next_stub_order = 0;
        std::size_t next_no_connect_order = 0;
        std::size_t next_assembly_intent_order = 0;
        std::size_t next_net_class_assignment_order = 0;

        [[nodiscard]] PinDefId add_pin_definition(PinDefinition definition);
        [[nodiscard]] ComponentDefId add_component_definition(ComponentDefinition definition);
        [[nodiscard]] ComponentId add_component(ComponentInstance component);
        [[nodiscard]] PinId add_pin(PinInstance pin);
        [[nodiscard]] NetId add_net(Net net);
        [[nodiscard]] ComponentId instantiate_component(ComponentDefId definition,
                                                        ComponentInstanceSpec spec);
        bool connect(NetId net, PinId pin);
        bool disconnect(PinId pin);
        bool mark_intentional_stub(NetId net);
        void mark_intentional_no_connect(PinId pin);
        void set_component_assembly_intent(ComponentId component, std::optional<bool> dnp,
                                           std::optional<bool> selection_override);
        bool assign_net_class(NetId net, NetClassId net_class);
        void set_component_property(ComponentId component, PropertyKey key, PropertyValue value);
        void replace_pin_definition(PinDefId id, PinDefinition definition);
        void replace_component(ComponentId id, ComponentInstance component);
        void replace_pin(PinId id, PinInstance pin);
        void replace_net(NetId id, Net net);

        [[nodiscard]] std::optional<ComponentId>
        component_by_reference(const ReferenceDesignator &reference) const;
        [[nodiscard]] std::optional<NetId> net_by_name(const NetName &name) const;
        [[nodiscard]] std::vector<PinId> pins_for(ComponentId component) const;
        [[nodiscard]] std::optional<PinId> pin_by_definition(ComponentId component,
                                                             PinDefId definition) const;
        void require_pin_definition(PinDefId id) const;
        void require_component_definition(ComponentDefId id) const;
        void require_component(ComponentId id) const;
        void require_pin(PinId id) const;
        void require_net(NetId id) const;
        [[nodiscard]] std::optional<NetId> net_of_existing_pin(PinId pin) const;
    };

    struct HierarchyState {
        HierarchyState() = default;
        HierarchyState(const HierarchyState &) = default;
        HierarchyState &operator=(const HierarchyState &) = default;
        HierarchyState(HierarchyState &&other) noexcept;
        HierarchyState &operator=(HierarchyState &&other) noexcept;

        EntityTable<ModuleDefinition, ModuleDefId> module_definitions;
        EntityTable<TemplateNetDefinition, TemplateNetDefId> template_net_definitions;
        EntityTable<ModuleComponentTemplate, ModuleComponentId> module_component_templates;
        EntityTable<PortDefinition, PortDefId> port_definitions;
        EntityTable<ModuleInstance, ModuleInstanceId> module_instances;
        EntityTable<PortBinding, PortBindingId> port_bindings;
        std::unordered_map<std::string, ModuleInstanceId> module_instances_by_name;

        [[nodiscard]] ModuleDefId add_module_definition(ModuleDefinition definition);
        [[nodiscard]] TemplateNetDefId add_template_net(ModuleDefId module,
                                                        TemplateNetDefinition net);
        [[nodiscard]] PortDefId add_port_definition(ModuleDefId module, PortDefinition port);
        [[nodiscard]] ModuleComponentId add_module_component(ModuleDefId module,
                                                             ModuleComponentTemplate component);
        bool connect_module_pin(ModuleDefId module, TemplateNetDefId net,
                                ModuleComponentId component, PinDefId pin);
        [[nodiscard]] ModuleInstanceId add_module_instance(ModuleInstance instance);
        [[nodiscard]] PortBindingId bind_port(ModuleInstanceId instance, PortDefId port,
                                              NetId internal_net, NetId parent_net);

        [[nodiscard]] std::optional<ModuleDefId>
        module_definition_by_name(const ModuleName &name) const;
        [[nodiscard]] std::optional<ModuleInstanceId>
        module_instance_by_name(const ModuleInstanceName &name) const;
        [[nodiscard]] std::optional<TemplateNetDefId>
        template_net_by_name(ModuleDefId module, const NetName &name) const;
        [[nodiscard]] std::optional<PortDefId> port_by_name(ModuleDefId module,
                                                            const PortName &name) const;
        [[nodiscard]] std::optional<ModuleComponentId>
        module_component_by_reference(ModuleDefId module,
                                      const ReferenceDesignator &reference) const;
        [[nodiscard]] std::optional<TemplateNetDefId>
        template_net_for(ModuleDefId module, ModuleComponentId component, PinDefId pin) const;
        [[nodiscard]] std::optional<PortBindingId> port_binding_for(ModuleInstanceId instance,
                                                                    PortDefId port) const;
        [[nodiscard]] std::optional<NetId> concrete_net_for(ModuleInstanceId instance,
                                                            TemplateNetDefId template_net) const;
        [[nodiscard]] bool is_module_origin_net(NetId net) const;
        void require_module_definition(ModuleDefId id) const;
        void require_template_net(TemplateNetDefId id) const;
        void require_port(PortDefId id) const;
        void require_module_component(ModuleComponentId id) const;
        void require_module_instance(ModuleInstanceId id) const;
        void require_template_net_in_module(ModuleDefId module, TemplateNetDefId net) const;
        void require_port_in_module(ModuleDefId module, PortDefId port) const;
        void require_module_component_in_module(ModuleDefId module,
                                                ModuleComponentId component) const;
        void reset() noexcept;
    };

    struct NetClassState {
        NetClassState() = default;
        NetClassState(const NetClassState &) = default;
        NetClassState &operator=(const NetClassState &) = default;
        NetClassState(NetClassState &&other) noexcept;
        NetClassState &operator=(NetClassState &&other) noexcept;

        EntityTable<NetClass, NetClassId> net_classes;
        [[nodiscard]] NetClassId add_net_class(NetClass net_class);
        [[nodiscard]] std::optional<NetClassId> net_class_by_name(const NetClassName &name) const;
        void require_net_class(NetClassId id) const;
    };

    [[nodiscard]] static ElectricalAttributeMap
    preflight_attributes(const std::vector<ElectricalAttributeAssignment> &assignments,
                         ElectricalAttributeOwner owner);
    static void require_attribute_owner(const ElectricalAttributeSpec &spec,
                                        ElectricalAttributeOwner expected);
    static void
    require_physical_part_matches_component_definition(const std::vector<PinDefId> &component_pins,
                                                       const PhysicalPart &physical_part);
    void restore_component_attributes(ComponentId component, ElectricalAttributeMap attributes);
    void restore_pin_definition_attributes(PinDefId pin_definition,
                                           ElectricalAttributeMap attributes);
    void set_component_attribute(ComponentId component, const ElectricalAttributeSpec &spec,
                                 ElectricalAttributeValue value);
    void set_net_attribute(NetId net, const ElectricalAttributeSpec &spec,
                           ElectricalAttributeValue value);
    void select_physical_part(ComponentId component, PhysicalPart physical_part,
                              const std::vector<PinDefId> &component_pins);
    void set_selected_part_attribute(ComponentId component, const ElectricalAttributeSpec &spec,
                                     ElectricalAttributeValue value);
    void require_pin_definition(PinDefId pin_definition) const;

    void require_component_definition(ComponentDefId component_definition) const;

    void require_component(ComponentId component) const;

    void require_module_definition(ModuleDefId module) const;

    void require_template_net_in_module(ModuleDefId module, TemplateNetDefId net) const;

    void require_module_component_in_module(ModuleDefId module, ModuleComponentId component) const;

    void require_restored_module_connectivity_matches_template(
        ModuleDefId definition, const std::vector<std::pair<TemplateNetDefId, NetId>> &origins,
        const std::vector<std::pair<ModuleComponentId, ComponentId>> &component_origins) const;

    void require_pin(PinId pin) const;

    void require_net(NetId net) const;

    void require_net_class(NetClassId net_class) const;

    [[nodiscard]] ModuleInstanceId restore_root_module_instance(
        ModuleDefId definition, ModuleInstanceName name,
        const std::vector<std::pair<TemplateNetDefId, NetId>> &origins,
        const std::vector<std::pair<ModuleComponentId, ComponentId>> &component_origins);

    ConnectivityState connectivity_;
    HierarchyState hierarchy_;
    NetClassState net_classes_;
};

/**
 * Non-owning forward range over one Circuit entity family.
 *
 * Iterators keep a pointer to the Circuit, so destroying or structurally mutating the Circuit
 * invalidates the range and its iterators. Creating a range from a temporary Circuit is deleted.
 */
/// @cond
namespace detail {

template <typename Id> class CircuitEntityRange {
  public:
    /** Forward iterator yielding const references to canonical entities. */
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

    /** Return an iterator at the first stable-ID index. */
    [[nodiscard]] iterator begin() const noexcept { return iterator{*circuit_, 0}; }

    /** Return the past-the-end iterator for the captured entity count. */
    [[nodiscard]] iterator end() const noexcept { return iterator{*circuit_, size_}; }

    /** Return the captured number of entities in this family. */
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

  private:
    friend entity_range_t<Id> Circuit::all<Id>() const &;

    CircuitEntityRange(const Circuit &circuit, std::size_t size) noexcept
        : circuit_{&circuit}, size_{size} {}

    const Circuit *circuit_;
    std::size_t size_ = 0;
};

} // namespace detail

/// @endcond

template <CircuitEntityId Id> [[nodiscard]] const entity_type_t<Id> &Circuit::get(Id id) const {
    if constexpr (std::same_as<Id, PinDefId>) {
        return connectivity_.pin_definitions.get(id);
    } else if constexpr (std::same_as<Id, ComponentDefId>) {
        return connectivity_.component_definitions.get(id);
    } else if constexpr (std::same_as<Id, ComponentId>) {
        return connectivity_.components.get(id);
    } else if constexpr (std::same_as<Id, PinId>) {
        return connectivity_.pins.get(id);
    } else if constexpr (std::same_as<Id, NetId>) {
        return connectivity_.nets.get(id);
    } else if constexpr (std::same_as<Id, ModuleDefId>) {
        return hierarchy_.module_definitions.get(id);
    } else if constexpr (std::same_as<Id, TemplateNetDefId>) {
        return hierarchy_.template_net_definitions.get(id);
    } else if constexpr (std::same_as<Id, PortDefId>) {
        return hierarchy_.port_definitions.get(id);
    } else if constexpr (std::same_as<Id, ModuleComponentId>) {
        return hierarchy_.module_component_templates.get(id);
    } else if constexpr (std::same_as<Id, ModuleInstanceId>) {
        return hierarchy_.module_instances.get(id);
    } else if constexpr (std::same_as<Id, PortBindingId>) {
        return hierarchy_.port_bindings.get(id);
    } else {
        return net_classes_.net_classes.get(id);
    }
}

template <CircuitEntityId Id> [[nodiscard]] entity_range_t<Id> Circuit::all() const & {
    std::size_t size = 0;
    if constexpr (std::same_as<Id, PinDefId>) {
        size = connectivity_.pin_definitions.size();
    } else if constexpr (std::same_as<Id, ComponentDefId>) {
        size = connectivity_.component_definitions.size();
    } else if constexpr (std::same_as<Id, ComponentId>) {
        size = connectivity_.components.size();
    } else if constexpr (std::same_as<Id, PinId>) {
        size = connectivity_.pins.size();
    } else if constexpr (std::same_as<Id, NetId>) {
        size = connectivity_.nets.size();
    } else if constexpr (std::same_as<Id, ModuleDefId>) {
        size = hierarchy_.module_definitions.size();
    } else if constexpr (std::same_as<Id, TemplateNetDefId>) {
        size = hierarchy_.template_net_definitions.size();
    } else if constexpr (std::same_as<Id, PortDefId>) {
        size = hierarchy_.port_definitions.size();
    } else if constexpr (std::same_as<Id, ModuleComponentId>) {
        size = hierarchy_.module_component_templates.size();
    } else if constexpr (std::same_as<Id, ModuleInstanceId>) {
        size = hierarchy_.module_instances.size();
    } else if constexpr (std::same_as<Id, PortBindingId>) {
        size = hierarchy_.port_bindings.size();
    } else {
        size = net_classes_.net_classes.size();
    }
    return entity_range_t<Id>{*this, size};
}

} // namespace volt
