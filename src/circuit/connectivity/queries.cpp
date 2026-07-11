#include <volt/circuit/connectivity/queries.hpp>

#include <algorithm>

#include <volt/core/errors.hpp>

namespace {

template <typename Id, typename Predicate>
[[nodiscard]] std::optional<Id> find_entity(const volt::Circuit &circuit, Predicate matches) {
    std::size_t index = 0;
    for (const auto &entity : circuit.all<Id>()) {
        if (matches(entity)) {
            return Id{index};
        }
        ++index;
    }
    return std::nullopt;
}

void require_component(const volt::Circuit &circuit, volt::ComponentId component) {
    if (component.index() >= circuit.all<volt::ComponentId>().size()) {
        throw volt::KernelRangeError{volt::ErrorCode::UnknownEntity,
                                     "Component ID does not belong to this circuit",
                                     volt::EntityRef::component(component)};
    }
}

void require_module_instance(const volt::Circuit &circuit, volt::ModuleInstanceId instance) {
    if (instance.index() >= circuit.all<volt::ModuleInstanceId>().size()) {
        throw volt::KernelRangeError{volt::ErrorCode::UnknownEntity,
                                     "Module instance ID does not belong to this circuit",
                                     volt::EntityRef::module_instance(instance)};
    }
}

void require_module_component_in_module(const volt::Circuit &circuit, volt::ModuleDefId module,
                                        volt::ModuleComponentId component) {
    const auto &components = circuit.get(module).components();
    static_cast<void>(circuit.get(component));
    if (std::find(components.begin(), components.end(), component) == components.end()) {
        throw volt::KernelLogicError{volt::ErrorCode::CrossReferenceViolation,
                                     "Module component does not belong to module definition",
                                     volt::EntityRef::module_component(component)};
    }
}

void require_template_net_in_module(const volt::Circuit &circuit, volt::ModuleDefId module,
                                    volt::TemplateNetDefId net) {
    const auto &nets = circuit.get(module).template_nets();
    static_cast<void>(circuit.get(net));
    if (std::find(nets.begin(), nets.end(), net) == nets.end()) {
        throw volt::KernelLogicError{volt::ErrorCode::CrossReferenceViolation,
                                     "Template net does not belong to module definition",
                                     volt::EntityRef::template_net_def(net)};
    }
}

void require_port_in_module(const volt::Circuit &circuit, volt::ModuleDefId module,
                            volt::PortDefId port) {
    const auto &ports = circuit.get(module).ports();
    static_cast<void>(circuit.get(port));
    if (std::find(ports.begin(), ports.end(), port) == ports.end()) {
        throw volt::KernelLogicError{volt::ErrorCode::CrossReferenceViolation,
                                     "Port does not belong to module definition",
                                     volt::EntityRef::port_def(port)};
    }
}

} // namespace

namespace volt::queries {

[[nodiscard]] std::optional<NetId> net_of(const Circuit &circuit, PinId pin) {
    return circuit.net_of(pin);
}

[[nodiscard]] std::optional<ComponentId>
component_by_reference(const Circuit &circuit, const ReferenceDesignator &reference) {
    return find_entity<ComponentId>(circuit, [&reference](const auto &component) {
        return component.reference() == reference;
    });
}

[[nodiscard]] std::optional<ModuleDefId> module_definition_by_name(const Circuit &circuit,
                                                                   const ModuleName &name) {
    return find_entity<ModuleDefId>(circuit,
                                    [&name](const auto &module) { return module.name() == name; });
}

[[nodiscard]] std::optional<ModuleInstanceId>
module_instance_by_name(const Circuit &circuit, const ModuleInstanceName &name) {
    return find_entity<ModuleInstanceId>(
        circuit, [&name](const auto &instance) { return instance.name() == name; });
}

[[nodiscard]] std::optional<TemplateNetDefId>
template_net_by_name(const Circuit &circuit, ModuleDefId module, const NetName &name) {
    for (const auto net : circuit.get(module).template_nets()) {
        if (circuit.get(net).name() == name) {
            return net;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<PortDefId> port_by_name(const Circuit &circuit, ModuleDefId module,
                                                    const PortName &name) {
    for (const auto port : circuit.get(module).ports()) {
        if (circuit.get(port).name() == name) {
            return port;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ModuleComponentId>
module_component_by_reference(const Circuit &circuit, ModuleDefId module,
                              const ReferenceDesignator &reference) {
    for (const auto component : circuit.get(module).components()) {
        if (circuit.get(component).reference() == reference) {
            return component;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<TemplateNetDefId> template_net_for(const Circuit &circuit,
                                                               ModuleDefId module,
                                                               ModuleComponentId component,
                                                               PinDefId pin) {
    require_module_component_in_module(circuit, module, component);
    for (const auto &connection : circuit.module_pin_connections(module)) {
        if (connection.component() == component && connection.pin() == pin) {
            return connection.net();
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<PortBindingId>
port_binding_for(const Circuit &circuit, ModuleInstanceId instance, PortDefId port) {
    require_module_instance(circuit, instance);
    require_port_in_module(circuit, circuit.get(instance).definition(), port);
    return find_entity<PortBindingId>(circuit, [instance, port](const auto &binding) {
        return binding.instance() == instance && binding.port() == port;
    });
}

[[nodiscard]] std::vector<PortBindingId> port_bindings_for(const Circuit &circuit,
                                                           ModuleInstanceId instance) {
    require_module_instance(circuit, instance);
    auto result = std::vector<PortBindingId>{};
    for (const auto port : circuit.get(circuit.get(instance).definition()).ports()) {
        if (const auto binding = port_binding_for(circuit, instance, port); binding.has_value()) {
            result.push_back(binding.value());
        }
    }
    return result;
}

[[nodiscard]] std::optional<ComponentId> concrete_component_for(const Circuit &circuit,
                                                                ModuleInstanceId instance,
                                                                ModuleComponentId component) {
    require_module_instance(circuit, instance);
    require_module_component_in_module(circuit, circuit.get(instance).definition(), component);
    for (const auto &[origin, concrete] : circuit.module_component_origins(instance)) {
        if (origin == component) {
            return concrete;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<NetId>
concrete_net_for(const Circuit &circuit, ModuleInstanceId instance, TemplateNetDefId template_net) {
    require_module_instance(circuit, instance);
    require_template_net_in_module(circuit, circuit.get(instance).definition(), template_net);
    for (const auto &[origin, concrete] : circuit.module_net_origins(instance)) {
        if (origin == template_net) {
            return concrete;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<std::pair<TemplateNetDefId, NetId>>
module_net_origins(const Circuit &circuit, ModuleInstanceId instance) {
    return circuit.module_net_origins(instance);
}

[[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>>
module_component_origins(const Circuit &circuit, ModuleInstanceId instance) {
    return circuit.module_component_origins(instance);
}

[[nodiscard]] bool is_module_origin_net(const Circuit &circuit, NetId net) {
    static_cast<void>(circuit.get(net));
    std::size_t instance_index = 0;
    for ([[maybe_unused]] const auto &instance : circuit.all<ModuleInstanceId>()) {
        const auto origins = circuit.module_net_origins(ModuleInstanceId{instance_index});
        if (std::ranges::any_of(origins,
                                [net](const auto &origin) { return origin.second == net; })) {
            return true;
        }
        ++instance_index;
    }
    return false;
}

[[nodiscard]] bool is_module_origin_component(const Circuit &circuit, ComponentId component) {
    static_cast<void>(circuit.get(component));
    std::size_t instance_index = 0;
    for ([[maybe_unused]] const auto &instance : circuit.all<ModuleInstanceId>()) {
        const auto origins = circuit.module_component_origins(ModuleInstanceId{instance_index});
        if (std::ranges::any_of(
                origins, [component](const auto &origin) { return origin.second == component; })) {
            return true;
        }
        ++instance_index;
    }
    return false;
}

[[nodiscard]] std::optional<NetId> net_by_name(const Circuit &circuit, const NetName &name) {
    return find_entity<NetId>(circuit, [&name](const auto &net) { return net.name() == name; });
}

[[nodiscard]] std::vector<PinId> pins_for(const Circuit &circuit, ComponentId component) {
    require_component(circuit, component);
    auto result = std::vector<PinId>{};
    std::size_t pin_index = 0;
    for (const auto &pin : circuit.all<PinId>()) {
        if (pin.component() == component) {
            result.push_back(PinId{pin_index});
        }
        ++pin_index;
    }
    return result;
}

[[nodiscard]] std::optional<PinId> pin_by_name(const Circuit &circuit, ComponentId component,
                                               std::string_view name) {
    for (const auto pin : pins_for(circuit, component)) {
        if (circuit.get(circuit.get(pin).definition()).name() == name) {
            return pin;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<PinId> pin_by_definition(const Circuit &circuit, ComponentId component,
                                                     PinDefId definition) {
    for (const auto pin : pins_for(circuit, component)) {
        if (circuit.get(pin).definition() == definition) {
            return pin;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<PinId> pin_by_number(const Circuit &circuit, ComponentId component,
                                                 std::string_view number) {
    for (const auto pin : pins_for(circuit, component)) {
        if (circuit.get(circuit.get(pin).definition()).number() == number) {
            return pin;
        }
    }
    return std::nullopt;
}

} // namespace volt::queries
