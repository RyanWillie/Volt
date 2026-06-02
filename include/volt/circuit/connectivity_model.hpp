#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include <volt/circuit/definitions.hpp>
#include <volt/circuit/instances.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/circuit/parts.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>

namespace volt {

/** Owns logical connectivity storage: component definitions, component instances, pins, and nets.
 */
class ConnectivityModel {
  public:
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

    [[nodiscard]] std::optional<NetId> net_of(PinId pin) const;

    [[nodiscard]] std::optional<ComponentId>
    component_by_reference(const ReferenceDesignator &reference) const;

    [[nodiscard]] std::optional<NetId> net_by_name(const NetName &name) const;

    [[nodiscard]] std::vector<PinId> pins_for(ComponentId component) const;

    [[nodiscard]] std::optional<PinId> pin_by_name(ComponentId component,
                                                   std::string_view name) const;

    [[nodiscard]] std::optional<PinId> pin_by_definition(ComponentId component,
                                                         PinDefId definition) const;

    [[nodiscard]] std::optional<PinId> pin_by_number(ComponentId component,
                                                     std::string_view number) const;

    [[nodiscard]] const PinDefinition &pin_definition(PinDefId id) const;

    [[nodiscard]] const ComponentDefinition &component_definition(ComponentDefId id) const;

    [[nodiscard]] const ComponentInstance &component(ComponentId id) const;

    [[nodiscard]] const PinInstance &pin(PinId id) const;

    [[nodiscard]] const Net &net(NetId id) const;

    [[nodiscard]] std::size_t pin_definition_count() const noexcept;

    [[nodiscard]] std::size_t component_definition_count() const noexcept;

    [[nodiscard]] std::size_t component_count() const noexcept;

    [[nodiscard]] std::size_t pin_count() const noexcept;

    [[nodiscard]] std::size_t net_count() const noexcept;

    void require_pin_definition(PinDefId pin_definition) const;

    void require_component_definition(ComponentDefId component_definition) const;

    void require_component(ComponentId component) const;

    void require_pin(PinId pin) const;

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
