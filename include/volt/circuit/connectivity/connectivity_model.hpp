#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/circuit/connectivity/instances.hpp>
#include <volt/circuit/connectivity/nets.hpp>
#include <volt/circuit/parts/parts.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>

namespace volt {

/**
 * Owns logical connectivity storage: component definitions, instances, pins, and nets.
 *
 * Responsibility: the source of truth for logical "what connects to what"; holds the entity
 *   tables and the connect/disconnect primitives.
 * Invariants: a pin connects to at most one net; nets and pins reference only existing
 *   entities. Violations throw at the mutation boundary.
 * Collaborators: composed by Circuit; never references Circuit back (acyclic).
 */
class ConnectivityModel {
  public:
    /** Add a reusable pin definition and return its stable ID. */
    [[nodiscard]] PinDefId add_pin_definition(PinDefinition definition);

    /** Add a reusable component definition and return its stable ID. */
    [[nodiscard]] ComponentDefId add_component_definition(ComponentDefinition definition);

    /** Add a concrete component instance and return its stable ID. */
    [[nodiscard]] ComponentId add_component(ComponentInstance component);

    /** Add a concrete pin instance and return its stable ID. */
    [[nodiscard]] PinId add_pin(PinInstance pin);

    /** Add a logical net and return its stable ID. */
    [[nodiscard]] NetId add_net(Net net);

    /** Instantiate a component definition and its concrete pins. */
    [[nodiscard]] ComponentId instantiate_component(ComponentDefId definition,
                                                    ReferenceDesignator reference,
                                                    PropertyMap properties = {});

    /** Connect a concrete pin to a logical net. */
    bool connect(NetId net, PinId pin);

    /** Disconnect a concrete pin from its current logical net. */
    bool disconnect(PinId pin);

    /** Set a property on a concrete component. */
    void set_component_property(ComponentId component, PropertyKey key, PropertyValue value);

    /** Return the logical net currently containing a concrete pin, if any. */
    [[nodiscard]] std::optional<NetId> net_of(PinId pin) const;

    /** Return a component by reference designator, if present. */
    [[nodiscard]] std::optional<ComponentId>
    component_by_reference(const ReferenceDesignator &reference) const;

    /** Return a net by name, if present. */
    [[nodiscard]] std::optional<NetId> net_by_name(const NetName &name) const;

    /** Return concrete pins owned by a component in deterministic order. */
    [[nodiscard]] std::vector<PinId> pins_for(ComponentId component) const;

    /** Return a component pin by reusable pin name, if present. */
    [[nodiscard]] std::optional<PinId> pin_by_name(ComponentId component,
                                                   std::string_view name) const;

    /** Return a component pin by reusable pin definition, if present. */
    [[nodiscard]] std::optional<PinId> pin_by_definition(ComponentId component,
                                                         PinDefId definition) const;

    /** Return a component pin by reusable pin number, if present. */
    [[nodiscard]] std::optional<PinId> pin_by_number(ComponentId component,
                                                     std::string_view number) const;

    /** Return a reusable pin definition by stable ID. */
    [[nodiscard]] const PinDefinition &pin_definition(PinDefId id) const;

    /** Return a reusable component definition by stable ID. */
    [[nodiscard]] const ComponentDefinition &component_definition(ComponentDefId id) const;

    /** Return a concrete component by stable ID. */
    [[nodiscard]] const ComponentInstance &component(ComponentId id) const;

    /** Return a concrete pin by stable ID. */
    [[nodiscard]] const PinInstance &pin(PinId id) const;

    /** Return a logical net by stable ID. */
    [[nodiscard]] const Net &net(NetId id) const;

    /** Return the number of reusable pin definitions. */
    [[nodiscard]] std::size_t pin_definition_count() const noexcept;

    /** Return the number of reusable component definitions. */
    [[nodiscard]] std::size_t component_definition_count() const noexcept;

    /** Return the number of concrete components. */
    [[nodiscard]] std::size_t component_count() const noexcept;

    /** Return the number of concrete pins. */
    [[nodiscard]] std::size_t pin_count() const noexcept;

    /** Return the number of logical nets. */
    [[nodiscard]] std::size_t net_count() const noexcept;

    /** Require that a pin definition ID belongs to this model. */
    void require_pin_definition(PinDefId pin_definition) const;

    /** Require that a component definition ID belongs to this model. */
    void require_component_definition(ComponentDefId component_definition) const;

    /** Require that a concrete component ID belongs to this model. */
    void require_component(ComponentId component) const;

    /** Require that a concrete pin ID belongs to this model. */
    void require_pin(PinId pin) const;

    /** Require that a logical net ID belongs to this model. */
    void require_net(NetId net) const;

  private:
    [[nodiscard]] std::optional<NetId> net_of_existing_pin(PinId pin) const;

    EntityTable<PinDefinition, PinDefId> pin_definitions_;
    EntityTable<ComponentDefinition, ComponentDefId> component_definitions_;
    EntityTable<ComponentInstance, ComponentId> components_;
    EntityTable<PinInstance, PinId> pins_;
    EntityTable<Net, NetId> nets_;
};

} // namespace volt
