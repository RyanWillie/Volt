#include <volt/circuit/circuit.hpp>

#include <volt/core/errors.hpp>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace volt {

Circuit::ConnectivityState::ConnectivityState(ConnectivityState &&other) noexcept
    : pin_definitions{std::exchange(other.pin_definitions, {})},
      component_definitions{std::exchange(other.component_definitions, {})},
      components{std::exchange(other.components, {})}, pins{std::exchange(other.pins, {})},
      nets{std::exchange(other.nets, {})},
      components_by_reference{std::exchange(other.components_by_reference, {})},
      nets_by_name{std::exchange(other.nets_by_name, {})},
      component_definition_by_pin{std::exchange(other.component_definition_by_pin, {})},
      pins_by_component{std::exchange(other.pins_by_component, {})},
      net_by_pin{std::exchange(other.net_by_pin, {})},
      next_stub_order{std::exchange(other.next_stub_order, 0)},
      next_no_connect_order{std::exchange(other.next_no_connect_order, 0)},
      next_assembly_intent_order{std::exchange(other.next_assembly_intent_order, 0)},
      next_net_class_assignment_order{std::exchange(other.next_net_class_assignment_order, 0)} {}

Circuit::ConnectivityState &
Circuit::ConnectivityState::operator=(ConnectivityState &&other) noexcept {
    if (this != &other) {
        pin_definitions = std::exchange(other.pin_definitions, {});
        component_definitions = std::exchange(other.component_definitions, {});
        components = std::exchange(other.components, {});
        pins = std::exchange(other.pins, {});
        nets = std::exchange(other.nets, {});
        components_by_reference = std::exchange(other.components_by_reference, {});
        nets_by_name = std::exchange(other.nets_by_name, {});
        component_definition_by_pin = std::exchange(other.component_definition_by_pin, {});
        pins_by_component = std::exchange(other.pins_by_component, {});
        net_by_pin = std::exchange(other.net_by_pin, {});
        next_stub_order = std::exchange(other.next_stub_order, 0);
        next_no_connect_order = std::exchange(other.next_no_connect_order, 0);
        next_assembly_intent_order = std::exchange(other.next_assembly_intent_order, 0);
        next_net_class_assignment_order = std::exchange(other.next_net_class_assignment_order, 0);
    }
    return *this;
}

[[nodiscard]] PinDefId Circuit::ConnectivityState::add_pin_definition(PinDefinition definition) {
    const auto id = pin_definitions.insert(std::move(definition));
    component_definition_by_pin.emplace_back();
    return id;
}

[[nodiscard]] ComponentDefId
Circuit::ConnectivityState::add_component_definition(ComponentDefinition definition) {
    auto seen_pins = std::vector<PinDefId>{};
    for (const auto pin : definition.pins()) {
        require_pin_definition(pin);
        if (std::find(seen_pins.begin(), seen_pins.end(), pin) != seen_pins.end()) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Component definition contains a duplicate pin definition"};
        }
        if (component_definition_by_pin[pin.index()].has_value()) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Pin definition already belongs to a component definition",
                                   EntityRef::pin_def(pin)};
        }
        seen_pins.push_back(pin);
    }

    const auto id = component_definitions.insert(std::move(definition));
    for (const auto pin : seen_pins) {
        component_definition_by_pin[pin.index()] = id;
    }
    return id;
}

[[nodiscard]] ComponentId Circuit::ConnectivityState::add_component(ComponentInstance component) {
    require_component_definition(component.definition());
    if (component_by_reference(component.reference()).has_value()) {
        throw KernelLogicError{ErrorCode::DuplicateName,
                               "Component reference designator already exists"};
    }

    auto reference = component.reference().value();
    const auto id = components.insert(std::move(component));
    components_by_reference.emplace(std::move(reference), id);
    pins_by_component.emplace_back();
    return id;
}

[[nodiscard]] PinId Circuit::ConnectivityState::add_pin(PinInstance pin) {
    require_component(pin.component());
    require_pin_definition(pin.definition());
    const auto &component_definition =
        component_definitions.get(components.get(pin.component()).definition());
    const auto &definition_pins = component_definition.pins();
    if (std::find(definition_pins.begin(), definition_pins.end(), pin.definition()) ==
        definition_pins.end()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Pin definition does not belong to component definition"};
    }
    if (pin_by_definition(pin.component(), pin.definition()).has_value()) {
        throw KernelLogicError{ErrorCode::InvalidState,
                               "Component pin definition is already materialized"};
    }

    const auto id = pins.insert(pin);
    pins_by_component[pin.component().index()].push_back(id);
    net_by_pin.emplace_back();
    return id;
}

[[nodiscard]] NetId Circuit::ConnectivityState::add_net(Net net) {
    if (net_by_name(net.name()).has_value()) {
        throw KernelLogicError{ErrorCode::DuplicateName, "Net name already exists"};
    }

    for (const auto pin : net.pins()) {
        require_pin(pin);
        if (net_of_existing_pin(pin).has_value()) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Pin is already connected to another net"};
        }
    }

    auto name = net.name().value();
    const auto id = nets.insert(std::move(net));
    nets_by_name.emplace(std::move(name), id);
    for (const auto pin : nets.get(id).pins()) {
        net_by_pin[pin.index()] = id;
    }
    return id;
}

[[nodiscard]] ComponentId
Circuit::ConnectivityState::instantiate_component(ComponentDefId definition,
                                                  ComponentInstanceSpec spec) {
    require_component_definition(definition);

    const auto component = add_component(
        ComponentInstance{definition, std::move(spec.reference), std::move(spec.properties)});
    for (const auto pin_definition_id : component_definitions.get(definition).pins()) {
        [[maybe_unused]] const auto pin = add_pin(PinInstance{component, pin_definition_id});
    }

    return component;
}

bool Circuit::ConnectivityState::connect(NetId net, PinId pin) {
    require_net(net);
    require_pin(pin);

    const auto existing_net = net_of_existing_pin(pin);
    if (existing_net.has_value()) {
        if (existing_net.value() == net) {
            return false;
        }

        throw KernelLogicError{ErrorCode::InvalidState, "Pin is already connected to another net"};
    }

    const auto changed = nets.get(net).connect(pin);
    if (changed) {
        net_by_pin[pin.index()] = net;
    }
    return changed;
}

bool Circuit::ConnectivityState::disconnect(PinId pin) {
    require_pin(pin);

    const auto existing_net = net_of_existing_pin(pin);
    if (!existing_net.has_value()) {
        return false;
    }

    const auto changed = nets.get(existing_net.value()).disconnect(pin);
    if (changed) {
        net_by_pin[pin.index()] = std::nullopt;
    }
    return changed;
}

bool Circuit::ConnectivityState::mark_intentional_stub(NetId net) {
    const auto &stored = nets.get(net);
    if (stored.intentional_stub()) {
        return false;
    }
    replace_net(net, stored.with_intentional_stub(next_stub_order));
    ++next_stub_order;
    return true;
}

void Circuit::ConnectivityState::mark_intentional_no_connect(PinId pin) {
    const auto &stored = pins.get(pin);
    if (stored.intentional_no_connect()) {
        return;
    }
    replace_pin(pin, stored.with_intentional_no_connect(next_no_connect_order));
    ++next_no_connect_order;
}

void Circuit::ConnectivityState::set_component_assembly_intent(
    ComponentId component, std::optional<bool> dnp, std::optional<bool> selection_override) {
    const auto &stored = components.get(component);
    const auto had_intent = stored.assembly_intent_order().has_value();
    auto updated = stored.with_assembly_intent(dnp, selection_override, next_assembly_intent_order);
    const auto added_intent = !had_intent && updated.assembly_intent_order().has_value();
    replace_component(component, std::move(updated));
    if (added_intent) {
        ++next_assembly_intent_order;
    }
}

bool Circuit::ConnectivityState::assign_net_class(NetId net, NetClassId net_class) {
    const auto &stored = nets.get(net);
    if (stored.net_class() == net_class) {
        return false;
    }
    const auto first_assignment = !stored.net_class_assignment_order().has_value();
    replace_net(net, stored.with_net_class(net_class, next_net_class_assignment_order));
    if (first_assignment) {
        ++next_net_class_assignment_order;
    }
    return true;
}

void Circuit::ConnectivityState::set_component_property(ComponentId component, PropertyKey key,
                                                        PropertyValue value) {
    require_component(component);
    auto &stored = components.get(component);
    stored = stored.with_property(std::move(key), std::move(value));
}

void Circuit::ConnectivityState::replace_pin_definition(PinDefId id, PinDefinition definition) {
    require_pin_definition(id);
    pin_definitions.get(id) = std::move(definition);
}

void Circuit::ConnectivityState::replace_component(ComponentId id, ComponentInstance component) {
    require_component(id);
    components.get(id) = std::move(component);
}

void Circuit::ConnectivityState::replace_pin(PinId id, PinInstance pin) {
    require_pin(id);
    pins.get(id) = pin;
}

void Circuit::ConnectivityState::replace_net(NetId id, Net net) {
    require_net(id);
    nets.get(id) = std::move(net);
}

[[nodiscard]] std::optional<ComponentId>
Circuit::ConnectivityState::component_by_reference(const ReferenceDesignator &reference) const {
    const auto found = components_by_reference.find(reference.value());
    if (found == components_by_reference.end()) {
        return std::nullopt;
    }

    return found->second;
}

[[nodiscard]] std::optional<NetId>
Circuit::ConnectivityState::net_by_name(const NetName &name) const {
    const auto found = nets_by_name.find(name.value());
    if (found == nets_by_name.end()) {
        return std::nullopt;
    }

    return found->second;
}

[[nodiscard]] std::vector<PinId> Circuit::ConnectivityState::pins_for(ComponentId component) const {
    require_component(component);

    return pins_by_component[component.index()];
}

[[nodiscard]] std::optional<PinId>
Circuit::ConnectivityState::pin_by_definition(ComponentId component, PinDefId definition) const {
    for (const auto pin_id : pins_for(component)) {
        if (pins.get(pin_id).definition() == definition) {
            return pin_id;
        }
    }

    return std::nullopt;
}

void Circuit::ConnectivityState::require_pin_definition(PinDefId pin_definition) const {
    if (!pin_definitions.contains(pin_definition)) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Pin definition ID does not belong to this circuit",
                               EntityRef::pin_def(pin_definition)};
    }
}

void Circuit::ConnectivityState::require_component_definition(
    ComponentDefId component_definition) const {
    if (!component_definitions.contains(component_definition)) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Component definition ID does not belong to this circuit",
                               EntityRef::component_def(component_definition)};
    }
}

void Circuit::ConnectivityState::require_component(ComponentId component) const {
    if (!components.contains(component)) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Component ID does not belong to this circuit",
                               EntityRef::component(component)};
    }
}

void Circuit::ConnectivityState::require_pin(PinId pin) const {
    if (!pins.contains(pin)) {
        throw KernelRangeError{ErrorCode::UnknownEntity, "Pin ID does not belong to this circuit",
                               EntityRef::pin(pin)};
    }
}

void Circuit::ConnectivityState::require_net(NetId net) const {
    if (!nets.contains(net)) {
        throw KernelRangeError{ErrorCode::UnknownEntity, "Net ID does not belong to this circuit",
                               EntityRef::net(net)};
    }
}

[[nodiscard]] std::optional<NetId>
Circuit::ConnectivityState::net_of_existing_pin(PinId pin) const {
    return net_by_pin[pin.index()];
}

} // namespace volt
