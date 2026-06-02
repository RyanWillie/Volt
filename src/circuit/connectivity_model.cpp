#include <volt/circuit/connectivity_model.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace volt {

[[nodiscard]] PinDefId ConnectivityModel::add_pin_definition(PinDefinition definition) {
    return pin_definitions_.insert(std::move(definition));
}

[[nodiscard]] ComponentDefId
ConnectivityModel::add_component_definition(ComponentDefinition definition) {
    for (const auto pin : definition.pins()) {
        require_pin_definition(pin);
    }

    return component_definitions_.insert(std::move(definition));
}

[[nodiscard]] ComponentId ConnectivityModel::add_component(ComponentInstance component) {
    require_component_definition(component.definition());
    if (component_by_reference(component.reference()).has_value()) {
        throw std::logic_error{"Component reference designator already exists"};
    }

    return components_.insert(std::move(component));
}

[[nodiscard]] PinId ConnectivityModel::add_pin(PinInstance pin) {
    require_component(pin.component());
    require_pin_definition(pin.definition());
    const auto &component_definition =
        component_definitions_.get(components_.get(pin.component()).definition());
    const auto &definition_pins = component_definition.pins();
    if (std::find(definition_pins.begin(), definition_pins.end(), pin.definition()) ==
        definition_pins.end()) {
        throw std::logic_error{"Pin definition does not belong to component definition"};
    }

    return pins_.insert(pin);
}

[[nodiscard]] NetId ConnectivityModel::add_net(Net net) {
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

[[nodiscard]] ComponentId ConnectivityModel::instantiate_component(ComponentDefId definition,
                                                                   ReferenceDesignator reference,
                                                                   PropertyMap properties) {
    require_component_definition(definition);

    const auto component =
        add_component(ComponentInstance{definition, std::move(reference), std::move(properties)});
    for (const auto pin_definition_id : component_definition(definition).pins()) {
        [[maybe_unused]] const auto pin = add_pin(PinInstance{component, pin_definition_id});
    }

    return component;
}

bool ConnectivityModel::connect(NetId net, PinId pin) {
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

bool ConnectivityModel::disconnect(PinId pin) {
    require_pin(pin);

    const auto existing_net = net_of_existing_pin(pin);
    if (!existing_net.has_value()) {
        return false;
    }

    return nets_.get(existing_net.value()).disconnect(pin);
}

void ConnectivityModel::set_component_property(ComponentId component, PropertyKey key,
                                               PropertyValue value) {
    require_component(component);
    components_.get(component).set_property(std::move(key), std::move(value));
}

[[nodiscard]] std::optional<NetId> ConnectivityModel::net_of(PinId pin) const {
    require_pin(pin);
    return net_of_existing_pin(pin);
}

[[nodiscard]] std::optional<ComponentId>
ConnectivityModel::component_by_reference(const ReferenceDesignator &reference) const {
    for (std::size_t index = 0; index < components_.size(); ++index) {
        const auto component_id = ComponentId{index};
        if (components_.get(component_id).reference() == reference) {
            return component_id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<NetId> ConnectivityModel::net_by_name(const NetName &name) const {
    for (std::size_t index = 0; index < nets_.size(); ++index) {
        const auto net_id = NetId{index};
        if (nets_.get(net_id).name() == name) {
            return net_id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::vector<PinId> ConnectivityModel::pins_for(ComponentId component) const {
    require_component(component);

    auto result = std::vector<PinId>{};
    for (std::size_t index = 0; index < pins_.size(); ++index) {
        const auto pin_id = PinId{index};
        if (pins_.get(pin_id).component() == component) {
            result.push_back(pin_id);
        }
    }

    return result;
}

[[nodiscard]] std::optional<PinId> ConnectivityModel::pin_by_name(ComponentId component,
                                                                  std::string_view name) const {
    for (const auto pin_id : pins_for(component)) {
        const auto definition = pins_.get(pin_id).definition();
        if (pin_definitions_.get(definition).name() == name) {
            return pin_id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<PinId> ConnectivityModel::pin_by_definition(ComponentId component,
                                                                        PinDefId definition) const {
    for (const auto pin_id : pins_for(component)) {
        if (pins_.get(pin_id).definition() == definition) {
            return pin_id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<PinId> ConnectivityModel::pin_by_number(ComponentId component,
                                                                    std::string_view number) const {
    for (const auto pin_id : pins_for(component)) {
        const auto definition = pins_.get(pin_id).definition();
        if (pin_definitions_.get(definition).number() == number) {
            return pin_id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] const PinDefinition &ConnectivityModel::pin_definition(PinDefId id) const {
    return pin_definitions_.get(id);
}

[[nodiscard]] const ComponentDefinition &
ConnectivityModel::component_definition(ComponentDefId id) const {
    return component_definitions_.get(id);
}

[[nodiscard]] const ComponentInstance &ConnectivityModel::component(ComponentId id) const {
    return components_.get(id);
}

[[nodiscard]] const PinInstance &ConnectivityModel::pin(PinId id) const { return pins_.get(id); }

[[nodiscard]] const Net &ConnectivityModel::net(NetId id) const { return nets_.get(id); }

[[nodiscard]] std::size_t ConnectivityModel::pin_definition_count() const noexcept {
    return pin_definitions_.size();
}

[[nodiscard]] std::size_t ConnectivityModel::component_definition_count() const noexcept {
    return component_definitions_.size();
}

[[nodiscard]] std::size_t ConnectivityModel::component_count() const noexcept {
    return components_.size();
}

[[nodiscard]] std::size_t ConnectivityModel::pin_count() const noexcept { return pins_.size(); }

[[nodiscard]] std::size_t ConnectivityModel::net_count() const noexcept { return nets_.size(); }

void ConnectivityModel::require_pin_definition(PinDefId pin_definition) const {
    if (!pin_definitions_.contains(pin_definition)) {
        throw std::out_of_range{"Pin definition ID does not belong to this circuit"};
    }
}

void ConnectivityModel::require_component_definition(ComponentDefId component_definition) const {
    if (!component_definitions_.contains(component_definition)) {
        throw std::out_of_range{"Component definition ID does not belong to this circuit"};
    }
}

void ConnectivityModel::require_component(ComponentId component) const {
    if (!components_.contains(component)) {
        throw std::out_of_range{"Component ID does not belong to this circuit"};
    }
}

void ConnectivityModel::require_pin(PinId pin) const {
    if (!pins_.contains(pin)) {
        throw std::out_of_range{"Pin ID does not belong to this circuit"};
    }
}

void ConnectivityModel::require_net(NetId net) const {
    if (!nets_.contains(net)) {
        throw std::out_of_range{"Net ID does not belong to this circuit"};
    }
}

[[nodiscard]] std::optional<NetId> ConnectivityModel::net_of_existing_pin(PinId pin) const {
    for (std::size_t index = 0; index < nets_.size(); ++index) {
        const auto net = NetId{index};
        if (nets_.get(net).contains(pin)) {
            return net;
        }
    }

    return std::nullopt;
}

} // namespace volt
