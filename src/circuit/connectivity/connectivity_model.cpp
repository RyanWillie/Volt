#include <volt/circuit/connectivity/connectivity_model.hpp>

#include <volt/core/errors.hpp>

#include "../circuit_storage.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace volt {

ConnectivityModel::ConnectivityModel()
    : ConnectivityModel{std::make_shared<detail::ConnectivityState>()} {}

ConnectivityModel::ConnectivityModel(std::shared_ptr<const detail::ConnectivityState> state)
    : state_{std::move(state)} {}

ConnectivityModel::ConnectivityModel(const ConnectivityModel &other)
    : ConnectivityModel{std::make_shared<detail::ConnectivityState>(other.state())} {}

ConnectivityModel::ConnectivityModel(ConnectivityModel &&other) noexcept = default;

ConnectivityModel &ConnectivityModel::operator=(const ConnectivityModel &other) {
    if (this != &other) {
        state_ = std::make_shared<detail::ConnectivityState>(other.state());
    }
    return *this;
}

ConnectivityModel &ConnectivityModel::operator=(ConnectivityModel &&other) noexcept = default;

ConnectivityModel::~ConnectivityModel() = default;

[[nodiscard]] PinDefId Circuit::ConnectivityStorage::add_pin_definition(PinDefinition definition) {
    const auto id = mutable_state().pin_definitions.insert(std::move(definition));
    mutable_state().component_definition_by_pin.emplace_back();
    return id;
}

[[nodiscard]] ComponentDefId
Circuit::ConnectivityStorage::add_component_definition(ComponentDefinition definition) {
    auto seen_pins = std::vector<PinDefId>{};
    for (const auto pin : definition.pins()) {
        require_pin_definition(pin);
        if (std::find(seen_pins.begin(), seen_pins.end(), pin) != seen_pins.end()) {
            throw KernelArgumentError{ErrorCode::InvalidArgument,
                                      "Component definition contains a duplicate pin definition"};
        }
        if (state().component_definition_by_pin[pin.index()].has_value()) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Pin definition already belongs to a component definition",
                                   EntityRef::pin_def(pin)};
        }
        seen_pins.push_back(pin);
    }

    const auto id = mutable_state().component_definitions.insert(std::move(definition));
    for (const auto pin : seen_pins) {
        mutable_state().component_definition_by_pin[pin.index()] = id;
    }
    return id;
}

[[nodiscard]] bool
Circuit::ConnectivityStorage::pin_definition_is_owned(PinDefId pin_definition) const {
    require_pin_definition(pin_definition);
    return state().component_definition_by_pin[pin_definition.index()].has_value();
}

[[nodiscard]] ComponentId Circuit::ConnectivityStorage::add_component(ComponentInstance component) {
    require_component_definition(component.definition());
    if (component_by_reference(component.reference()).has_value()) {
        throw KernelLogicError{ErrorCode::DuplicateName,
                               "Component reference designator already exists"};
    }

    auto reference = component.reference().value();
    const auto id = mutable_state().components.insert(std::move(component));
    mutable_state().components_by_reference.emplace(std::move(reference), id);
    mutable_state().pins_by_component.emplace_back();
    return id;
}

[[nodiscard]] PinId Circuit::ConnectivityStorage::add_pin(PinInstance pin) {
    require_component(pin.component());
    require_pin_definition(pin.definition());
    const auto &component_definition =
        state().component_definitions.get(state().components.get(pin.component()).definition());
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

    const auto id = mutable_state().pins.insert(pin);
    mutable_state().pins_by_component[pin.component().index()].push_back(id);
    mutable_state().net_by_pin.emplace_back();
    return id;
}

[[nodiscard]] NetId Circuit::ConnectivityStorage::add_net(Net net) {
    if (net_by_name(net.name()).has_value()) {
        throw KernelLogicError{ErrorCode::DuplicateName, "Net name already exists"};
    }

    for (const auto pin : net.pins()) {
        require_pin(pin);
        if (net_of(pin).has_value()) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Pin is already connected to another net"};
        }
    }

    auto name = net.name().value();
    const auto id = mutable_state().nets.insert(std::move(net));
    mutable_state().nets_by_name.emplace(std::move(name), id);
    for (const auto pin : mutable_state().nets.get(id).pins()) {
        mutable_state().net_by_pin[pin.index()] = id;
    }
    return id;
}

[[nodiscard]] ComponentId Circuit::ConnectivityStorage::instantiate_component(
    ComponentDefId definition, ReferenceDesignator reference, PropertyMap properties) {
    require_component_definition(definition);

    const auto component =
        add_component(ComponentInstance{definition, std::move(reference), std::move(properties)});
    for (const auto pin_definition_id : component_definition(definition).pins()) {
        [[maybe_unused]] const auto pin = add_pin(PinInstance{component, pin_definition_id});
    }

    return component;
}

bool Circuit::ConnectivityStorage::connect(NetId net, PinId pin) {
    require_net(net);
    require_pin(pin);

    const auto existing_net = net_of(pin);
    if (existing_net.has_value()) {
        if (existing_net.value() == net) {
            return false;
        }

        throw KernelLogicError{ErrorCode::InvalidState, "Pin is already connected to another net"};
    }

    const auto changed = mutable_state().nets.get(net).connect(pin);
    if (changed) {
        mutable_state().net_by_pin[pin.index()] = net;
    }
    return changed;
}

bool Circuit::ConnectivityStorage::disconnect(PinId pin) {
    require_pin(pin);

    const auto existing_net = net_of(pin);
    if (!existing_net.has_value()) {
        return false;
    }

    const auto changed = mutable_state().nets.get(existing_net.value()).disconnect(pin);
    if (changed) {
        mutable_state().net_by_pin[pin.index()] = std::nullopt;
    }
    return changed;
}

void Circuit::ConnectivityStorage::set_component_property(ComponentId component, PropertyKey key,
                                                          PropertyValue value) {
    require_component(component);
    auto &stored = mutable_state().components.get(component);
    stored = stored.with_property(std::move(key), std::move(value));
}

[[nodiscard]] std::optional<NetId> ConnectivityModel::net_of(PinId pin) const {
    require_pin(pin);
    return net_of_existing_pin(pin);
}

[[nodiscard]] std::optional<ComponentId>
ConnectivityModel::component_by_reference(const ReferenceDesignator &reference) const {
    const auto found = state().components_by_reference.find(reference.value());
    if (found == state().components_by_reference.end()) {
        return std::nullopt;
    }

    return found->second;
}

[[nodiscard]] std::optional<NetId> ConnectivityModel::net_by_name(const NetName &name) const {
    const auto found = state().nets_by_name.find(name.value());
    if (found == state().nets_by_name.end()) {
        return std::nullopt;
    }

    return found->second;
}

[[nodiscard]] std::vector<PinId> ConnectivityModel::pins_for(ComponentId component) const {
    require_component(component);

    return state().pins_by_component[component.index()];
}

[[nodiscard]] std::optional<PinId> ConnectivityModel::pin_by_name(ComponentId component,
                                                                  std::string_view name) const {
    for (const auto pin_id : pins_for(component)) {
        const auto definition = state().pins.get(pin_id).definition();
        if (state().pin_definitions.get(definition).name() == name) {
            return pin_id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<PinId> ConnectivityModel::pin_by_definition(ComponentId component,
                                                                        PinDefId definition) const {
    for (const auto pin_id : pins_for(component)) {
        if (state().pins.get(pin_id).definition() == definition) {
            return pin_id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<PinId> ConnectivityModel::pin_by_number(ComponentId component,
                                                                    std::string_view number) const {
    for (const auto pin_id : pins_for(component)) {
        const auto definition = state().pins.get(pin_id).definition();
        if (state().pin_definitions.get(definition).number() == number) {
            return pin_id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] const PinDefinition &ConnectivityModel::pin_definition(PinDefId id) const {
    return state().pin_definitions.get(id);
}

[[nodiscard]] const ComponentDefinition &
ConnectivityModel::component_definition(ComponentDefId id) const {
    return state().component_definitions.get(id);
}

[[nodiscard]] const ComponentInstance &ConnectivityModel::component(ComponentId id) const {
    return state().components.get(id);
}

[[nodiscard]] const PinInstance &ConnectivityModel::pin(PinId id) const {
    return state().pins.get(id);
}

[[nodiscard]] const Net &ConnectivityModel::net(NetId id) const { return state().nets.get(id); }

[[nodiscard]] std::size_t ConnectivityModel::pin_definition_count() const noexcept {
    return state().pin_definitions.size();
}

[[nodiscard]] std::size_t ConnectivityModel::component_definition_count() const noexcept {
    return state().component_definitions.size();
}

[[nodiscard]] std::size_t ConnectivityModel::component_count() const noexcept {
    return state().components.size();
}

[[nodiscard]] std::size_t ConnectivityModel::pin_count() const noexcept {
    return state().pins.size();
}

[[nodiscard]] std::size_t ConnectivityModel::net_count() const noexcept {
    return state().nets.size();
}

void ConnectivityModel::require_pin_definition(PinDefId pin_definition) const {
    if (!state().pin_definitions.contains(pin_definition)) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Pin definition ID does not belong to this circuit",
                               EntityRef::pin_def(pin_definition)};
    }
}

void ConnectivityModel::require_component_definition(ComponentDefId component_definition) const {
    if (!state().component_definitions.contains(component_definition)) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Component definition ID does not belong to this circuit",
                               EntityRef::component_def(component_definition)};
    }
}

void ConnectivityModel::require_component(ComponentId component) const {
    if (!state().components.contains(component)) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Component ID does not belong to this circuit",
                               EntityRef::component(component)};
    }
}

void ConnectivityModel::require_pin(PinId pin) const {
    if (!state().pins.contains(pin)) {
        throw KernelRangeError{ErrorCode::UnknownEntity, "Pin ID does not belong to this circuit",
                               EntityRef::pin(pin)};
    }
}

void ConnectivityModel::require_net(NetId net) const {
    if (!state().nets.contains(net)) {
        throw KernelRangeError{ErrorCode::UnknownEntity, "Net ID does not belong to this circuit",
                               EntityRef::net(net)};
    }
}

[[nodiscard]] const detail::ConnectivityState &ConnectivityModel::state() const noexcept {
    return *state_;
}

[[nodiscard]] std::optional<NetId> ConnectivityModel::net_of_existing_pin(PinId pin) const {
    return state().net_by_pin[pin.index()];
}

} // namespace volt
