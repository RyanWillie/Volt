#include <volt/circuit/queries.hpp>

#include <algorithm>
#include <cstddef>

namespace volt::queries {

[[nodiscard]] std::optional<NetId> net_of(const Circuit &circuit, PinId pin) {
    static_cast<void>(circuit.pin(pin));
    for (std::size_t index = 0; index < circuit.net_count(); ++index) {
        const auto net_id = NetId{index};
        if (circuit.net(net_id).contains(pin)) {
            return net_id;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ComponentId>
component_by_reference(const Circuit &circuit, const ReferenceDesignator &reference) {
    for (std::size_t index = 0; index < circuit.component_count(); ++index) {
        const auto component_id = ComponentId{index};
        if (circuit.component(component_id).reference() == reference) {
            return component_id;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ModuleDefId> module_definition_by_name(const Circuit &circuit,
                                                                   const ModuleName &name) {
    for (std::size_t index = 0; index < circuit.module_definition_count(); ++index) {
        const auto id = ModuleDefId{index};
        if (circuit.module_definition(id).name() == name) {
            return id;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ModuleInstanceId>
module_instance_by_name(const Circuit &circuit, const ModuleInstanceName &name) {
    for (std::size_t index = 0; index < circuit.module_instance_count(); ++index) {
        const auto id = ModuleInstanceId{index};
        if (circuit.module_instance(id).name() == name) {
            return id;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<TemplateNetDefId>
template_net_by_name(const Circuit &circuit, ModuleDefId module, const NetName &name) {
    for (const auto net : circuit.module_definition(module).template_nets()) {
        if (circuit.template_net_definition(net).name() == name) {
            return net;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<PortDefId> port_by_name(const Circuit &circuit, ModuleDefId module,
                                                    const PortName &name) {
    for (const auto port : circuit.module_definition(module).ports()) {
        if (circuit.port_definition(port).name() == name) {
            return port;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ModuleComponentId>
module_component_by_reference(const Circuit &circuit, ModuleDefId module,
                              const ReferenceDesignator &reference) {
    for (const auto component : circuit.module_definition(module).components()) {
        if (circuit.module_component_template(component).reference() == reference) {
            return component;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<TemplateNetDefId> template_net_for(const Circuit &circuit,
                                                               ModuleDefId module,
                                                               ModuleComponentId component,
                                                               PinDefId pin) {
    for (const auto &connection : circuit.module_pin_connections(module)) {
        if (connection.component() == component && connection.pin() == pin) {
            return connection.net();
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<PortBindingId>
port_binding_for(const Circuit &circuit, ModuleInstanceId instance, PortDefId port) {
    static_cast<void>(circuit.module_instance(instance));
    for (std::size_t index = 0; index < circuit.port_binding_count(); ++index) {
        const auto id = PortBindingId{index};
        const auto &binding = circuit.port_binding(id);
        if (binding.instance() == instance && binding.port() == port) {
            return id;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<PortBindingId> port_bindings_for(const Circuit &circuit,
                                                           ModuleInstanceId instance) {
    const auto &module = circuit.module_definition(circuit.module_instance(instance).definition());
    auto result = std::vector<PortBindingId>{};
    for (const auto port : module.ports()) {
        const auto binding = port_binding_for(circuit, instance, port);
        if (binding.has_value()) {
            result.push_back(binding.value());
        }
    }
    return result;
}

[[nodiscard]] std::optional<ComponentId> concrete_component_for(const Circuit &circuit,
                                                                ModuleInstanceId instance,
                                                                ModuleComponentId component) {
    for (const auto &[template_component, concrete_component] :
         circuit.module_component_origins(instance)) {
        if (template_component == component) {
            return concrete_component;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<NetId>
concrete_net_for(const Circuit &circuit, ModuleInstanceId instance, TemplateNetDefId template_net) {
    for (const auto &[origin_template_net, concrete_net] : circuit.module_net_origins(instance)) {
        if (origin_template_net == template_net) {
            return concrete_net;
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
    static_cast<void>(circuit.net(net));
    for (std::size_t index = 0; index < circuit.module_instance_count(); ++index) {
        const auto origins = circuit.module_net_origins(ModuleInstanceId{index});
        const auto found = std::find_if(origins.begin(), origins.end(),
                                        [net](const std::pair<TemplateNetDefId, NetId> &origin) {
                                            return origin.second == net;
                                        });
        if (found != origins.end()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool is_module_origin_component(const Circuit &circuit, ComponentId component) {
    static_cast<void>(circuit.component(component));
    for (std::size_t index = 0; index < circuit.module_instance_count(); ++index) {
        const auto origins = circuit.module_component_origins(ModuleInstanceId{index});
        const auto found =
            std::find_if(origins.begin(), origins.end(),
                         [component](const std::pair<ModuleComponentId, ComponentId> &origin) {
                             return origin.second == component;
                         });
        if (found != origins.end()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::optional<NetId> net_by_name(const Circuit &circuit, const NetName &name) {
    for (std::size_t index = 0; index < circuit.net_count(); ++index) {
        const auto net_id = NetId{index};
        if (circuit.net(net_id).name() == name) {
            return net_id;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<PinId> pins_for(const Circuit &circuit, ComponentId component) {
    static_cast<void>(circuit.component(component));
    auto result = std::vector<PinId>{};
    for (std::size_t index = 0; index < circuit.pin_count(); ++index) {
        const auto pin_id = PinId{index};
        if (circuit.pin(pin_id).component() == component) {
            result.push_back(pin_id);
        }
    }
    return result;
}

[[nodiscard]] std::optional<PinId> pin_by_name(const Circuit &circuit, ComponentId component,
                                               std::string_view name) {
    for (const auto pin_id : pins_for(circuit, component)) {
        const auto &definition = circuit.pin_definition(circuit.pin(pin_id).definition());
        if (definition.name() == name) {
            return pin_id;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<PinId> pin_by_definition(const Circuit &circuit, ComponentId component,
                                                     PinDefId definition) {
    for (const auto pin_id : pins_for(circuit, component)) {
        if (circuit.pin(pin_id).definition() == definition) {
            return pin_id;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<PinId> pin_by_number(const Circuit &circuit, ComponentId component,
                                                 std::string_view number) {
    for (const auto pin_id : pins_for(circuit, component)) {
        const auto &definition = circuit.pin_definition(circuit.pin(pin_id).definition());
        if (definition.number() == number) {
            return pin_id;
        }
    }
    return std::nullopt;
}

} // namespace volt::queries
