#pragma once

#include <cstddef>
#include <utility>

#include <volt/circuit/definitions.hpp>
#include <volt/circuit/instances.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>

namespace volt {

/** Owning database for the canonical logical circuit model. */
class Circuit {
  public:
    /** Store a reusable pin definition and return its stable ID. */
    [[nodiscard]] PinDefId add_pin_definition(PinDefinition definition) {
        return pin_definitions_.insert(std::move(definition));
    }

    /** Store a reusable component definition and return its stable ID. */
    [[nodiscard]] ComponentDefId add_component_definition(ComponentDefinition definition) {
        return component_definitions_.insert(std::move(definition));
    }

    /** Store a component instance and return its stable ID. */
    [[nodiscard]] ComponentId add_component(ComponentInstance component) {
        return components_.insert(std::move(component));
    }

    /** Store a concrete pin instance and return its stable ID. */
    [[nodiscard]] PinId add_pin(PinInstance pin) { return pins_.insert(std::move(pin)); }

    /** Store a canonical net and return its stable ID. */
    [[nodiscard]] NetId add_net(Net net) { return nets_.insert(std::move(net)); }

    /** Return a reusable pin definition by ID. */
    [[nodiscard]] const PinDefinition &pin_definition(PinDefId id) const {
        return pin_definitions_.get(id);
    }

    /** Return a reusable component definition by ID. */
    [[nodiscard]] const ComponentDefinition &component_definition(ComponentDefId id) const {
        return component_definitions_.get(id);
    }

    /** Return a component instance by ID. */
    [[nodiscard]] const ComponentInstance &component(ComponentId id) const {
        return components_.get(id);
    }

    /** Return a concrete pin instance by ID. */
    [[nodiscard]] const PinInstance &pin(PinId id) const { return pins_.get(id); }

    /** Return a canonical net by ID. */
    [[nodiscard]] const Net &net(NetId id) const { return nets_.get(id); }

    /** Return the number of reusable pin definitions. */
    [[nodiscard]] std::size_t pin_definition_count() const noexcept {
        return pin_definitions_.size();
    }

    /** Return the number of reusable component definitions. */
    [[nodiscard]] std::size_t component_definition_count() const noexcept {
        return component_definitions_.size();
    }

    /** Return the number of component instances. */
    [[nodiscard]] std::size_t component_count() const noexcept { return components_.size(); }

    /** Return the number of concrete pin instances. */
    [[nodiscard]] std::size_t pin_count() const noexcept { return pins_.size(); }

    /** Return the number of canonical nets. */
    [[nodiscard]] std::size_t net_count() const noexcept { return nets_.size(); }

  private:
    EntityTable<PinDefinition, PinDefId> pin_definitions_;
    EntityTable<ComponentDefinition, ComponentDefId> component_definitions_;
    EntityTable<ComponentInstance, ComponentId> components_;
    EntityTable<PinInstance, PinId> pins_;
    EntityTable<Net, NetId> nets_;
};

} // namespace volt
