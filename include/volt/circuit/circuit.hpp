#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/circuit/definitions.hpp>
#include <volt/circuit/instances.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/circuit/parts.hpp>
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
        for (const auto pin : definition.pins()) {
            require_pin_definition(pin);
        }

        return component_definitions_.insert(std::move(definition));
    }

    /** Store a component instance and return its stable ID. */
    [[nodiscard]] ComponentId add_component(ComponentInstance component) {
        require_component_definition(component.definition());
        if (component_by_reference(component.reference()).has_value()) {
            throw std::logic_error{"Component reference designator already exists"};
        }

        return components_.insert(std::move(component));
    }

    /** Store a concrete pin instance and return its stable ID. */
    [[nodiscard]] PinId add_pin(PinInstance pin) {
        if (!components_.contains(pin.component())) {
            throw std::out_of_range{"Pin instance references a missing component"};
        }
        if (!pin_definitions_.contains(pin.definition())) {
            throw std::out_of_range{"Pin instance references a missing pin definition"};
        }

        return pins_.insert(std::move(pin));
    }

    /** Store a canonical net and return its stable ID. */
    [[nodiscard]] NetId add_net(Net net) {
        if (net_by_name(net.name()).has_value()) {
            throw std::logic_error{"Net name already exists"};
        }

        for (const auto pin : net.pins()) {
            require_pin(pin);
            if (net_of_existing_pin(pin).has_value()) {
                throw std::logic_error{"Pin is already connected to another net"};
            }
        }

        return nets_.insert(std::move(net));
    }

    /**
     * Instantiate a component definition and create concrete pins for each ordered pin
     * definition.
     */
    [[nodiscard]] ComponentId instantiate_component(ComponentDefId definition,
                                                    ReferenceDesignator reference,
                                                    PropertyMap properties = {}) {
        require_component_definition(definition);

        const auto component = add_component(
            ComponentInstance{definition, std::move(reference), std::move(properties)});
        for (const auto pin_definition_id : component_definition(definition).pins()) {
            [[maybe_unused]] const auto pin = add_pin(PinInstance{component, pin_definition_id});
        }

        return component;
    }

    /** Connect an existing pin to an existing net; returns true when the circuit changed. */
    bool connect(NetId net, PinId pin) {
        require_net(net);
        require_pin(pin);

        const auto existing_net = net_of_existing_pin(pin);
        if (existing_net.has_value()) {
            if (existing_net.value() == net) {
                return false;
            }

            throw std::logic_error{"Pin is already connected to another net"};
        }

        return nets_.get(net).connect(pin);
    }

    /** Disconnect an existing pin from its current net; returns true when the circuit changed. */
    bool disconnect(PinId pin) {
        require_pin(pin);

        const auto existing_net = net_of_existing_pin(pin);
        if (!existing_net.has_value()) {
            return false;
        }

        return nets_.get(existing_net.value()).disconnect(pin);
    }

    /** Set or replace a metadata property on an existing component instance. */
    void set_component_property(ComponentId component, PropertyKey key, PropertyValue value) {
        require_component(component);
        components_.get(component).set_property(std::move(key), std::move(value));
    }

    /** Assign a selected physical implementation to an existing component instance. */
    void select_physical_part(ComponentId component, PhysicalPart physical_part) {
        require_component(component);
        require_physical_part_matches_component_definition(components_.get(component).definition(),
                                                           physical_part);

        components_.get(component).select_physical_part(std::move(physical_part));
    }

    /** Return the selected physical implementation for a component, if one has been assigned. */
    [[nodiscard]] const std::optional<PhysicalPart> &
    selected_physical_part(ComponentId component) const {
        require_component(component);
        return components_.get(component).selected_physical_part();
    }

    /** Return the net currently connected to the pin, if any. */
    [[nodiscard]] std::optional<NetId> net_of(PinId pin) const {
        require_pin(pin);
        return net_of_existing_pin(pin);
    }

    /** Return the component with this reference designator, if it exists. */
    [[nodiscard]] std::optional<ComponentId>
    component_by_reference(const ReferenceDesignator &reference) const {
        for (std::size_t index = 0; index < components_.size(); ++index) {
            const auto component_id = ComponentId{index};
            if (components_.get(component_id).reference() == reference) {
                return component_id;
            }
        }

        return std::nullopt;
    }

    /** Return the net with this name, if it exists. */
    [[nodiscard]] std::optional<NetId> net_by_name(const NetName &name) const {
        for (std::size_t index = 0; index < nets_.size(); ++index) {
            const auto net_id = NetId{index};
            if (nets_.get(net_id).name() == name) {
                return net_id;
            }
        }

        return std::nullopt;
    }

    /** Return concrete pins belonging to a component in deterministic creation order. */
    [[nodiscard]] std::vector<PinId> pins_for(ComponentId component) const {
        require_component(component);

        std::vector<PinId> result;
        for (std::size_t index = 0; index < pins_.size(); ++index) {
            const auto pin_id = PinId{index};
            if (pins_.get(pin_id).component() == component) {
                result.push_back(pin_id);
            }
        }

        return result;
    }

    /** Return a component pin by reusable pin definition name, if it exists. */
    [[nodiscard]] std::optional<PinId> pin_by_name(ComponentId component,
                                                   std::string_view name) const {
        for (const auto pin_id : pins_for(component)) {
            const auto definition = pins_.get(pin_id).definition();
            if (pin_definitions_.get(definition).name() == name) {
                return pin_id;
            }
        }

        return std::nullopt;
    }

    /** Return a component pin by reusable pin definition number, if it exists. */
    [[nodiscard]] std::optional<PinId> pin_by_number(ComponentId component,
                                                     std::string_view number) const {
        for (const auto pin_id : pins_for(component)) {
            const auto definition = pins_.get(pin_id).definition();
            if (pin_definitions_.get(definition).number() == number) {
                return pin_id;
            }
        }

        return std::nullopt;
    }

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
    void require_pin_definition(PinDefId pin_definition) const {
        if (!pin_definitions_.contains(pin_definition)) {
            throw std::out_of_range{"Pin definition ID does not belong to this circuit"};
        }
    }

    void require_component_definition(ComponentDefId component_definition) const {
        if (!component_definitions_.contains(component_definition)) {
            throw std::out_of_range{"Component definition ID does not belong to this circuit"};
        }
    }

    void require_component(ComponentId component) const {
        if (!components_.contains(component)) {
            throw std::out_of_range{"Component ID does not belong to this circuit"};
        }
    }

    void require_pin(PinId pin) const {
        if (!pins_.contains(pin)) {
            throw std::out_of_range{"Pin ID does not belong to this circuit"};
        }
    }

    void require_net(NetId net) const {
        if (!nets_.contains(net)) {
            throw std::out_of_range{"Net ID does not belong to this circuit"};
        }
    }

    void
    require_physical_part_matches_component_definition(ComponentDefId component_definition,
                                                       const PhysicalPart &physical_part) const {
        const auto &definition_pins = component_definitions_.get(component_definition).pins();
        for (const auto &mapping : physical_part.pin_pad_mappings()) {
            if (std::find(definition_pins.begin(), definition_pins.end(), mapping.pin()) ==
                definition_pins.end()) {
                throw std::logic_error{"Physical part maps a pin outside the component definition"};
            }
        }

        for (const auto pin : definition_pins) {
            const auto mapped = std::any_of(
                physical_part.pin_pad_mappings().begin(), physical_part.pin_pad_mappings().end(),
                [pin](const PinPadMapping &mapping) { return mapping.pin() == pin; });
            if (!mapped) {
                throw std::logic_error{
                    "Physical part must map every pin in the component definition"};
            }
        }
    }

    [[nodiscard]] std::optional<NetId> net_of_existing_pin(PinId pin) const {
        for (std::size_t index = 0; index < nets_.size(); ++index) {
            const auto net = NetId{index};
            if (nets_.get(net).contains(pin)) {
                return net;
            }
        }

        return std::nullopt;
    }

    EntityTable<PinDefinition, PinDefId> pin_definitions_;
    EntityTable<ComponentDefinition, ComponentDefId> component_definitions_;
    EntityTable<ComponentInstance, ComponentId> components_;
    EntityTable<PinInstance, PinId> pins_;
    EntityTable<Net, NetId> nets_;
};

} // namespace volt
