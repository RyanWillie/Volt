#include <volt/circuit/circuit.hpp>

#include <volt/core/errors.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace volt {

Circuit::HierarchyState::HierarchyState(HierarchyState &&other) noexcept
    : module_definitions{std::move(other.module_definitions)},
      template_net_definitions{std::move(other.template_net_definitions)},
      module_component_templates{std::move(other.module_component_templates)},
      port_definitions{std::move(other.port_definitions)},
      module_instances{std::move(other.module_instances)},
      port_bindings{std::move(other.port_bindings)},
      module_instances_by_name{std::move(other.module_instances_by_name)} {
    other.reset();
}

Circuit::HierarchyState &Circuit::HierarchyState::operator=(HierarchyState &&other) noexcept {
    if (this == &other) {
        return *this;
    }

    module_definitions = std::move(other.module_definitions);
    template_net_definitions = std::move(other.template_net_definitions);
    module_component_templates = std::move(other.module_component_templates);
    port_definitions = std::move(other.port_definitions);
    module_instances = std::move(other.module_instances);
    port_bindings = std::move(other.port_bindings);
    module_instances_by_name = std::move(other.module_instances_by_name);
    other.reset();
    return *this;
}

void Circuit::HierarchyState::reset() noexcept {
    module_definitions = {};
    template_net_definitions = {};
    module_component_templates = {};
    port_definitions = {};
    module_instances = {};
    port_bindings = {};
    module_instances_by_name = {};
}

[[nodiscard]] ModuleDefId
Circuit::HierarchyState::add_module_definition(ModuleDefinition definition) {
    if (module_definition_by_name(definition.name()).has_value()) {
        throw KernelLogicError{ErrorCode::DuplicateName, "Module definition name already exists"};
    }

    return module_definitions.insert(std::move(definition));
}

[[nodiscard]] TemplateNetDefId
Circuit::HierarchyState::add_template_net(ModuleDefId module, TemplateNetDefinition net) {
    require_module_definition(module);
    if (template_net_by_name(module, net.name()).has_value()) {
        throw KernelLogicError{ErrorCode::DuplicateName,
                               "Template net name already exists in module definition"};
    }

    const auto id = template_net_definitions.insert(std::move(net));
    auto &definition = module_definitions.get(module);
    definition = std::move(definition).with_template_net(id);
    return id;
}

[[nodiscard]] PortDefId Circuit::HierarchyState::add_port_definition(ModuleDefId module,
                                                                     PortDefinition port) {
    require_template_net_in_module(module, port.internal_net());
    if (port_by_name(module, port.name()).has_value()) {
        throw KernelLogicError{ErrorCode::DuplicateName,
                               "Port name already exists in module definition"};
    }

    const auto id = port_definitions.insert(std::move(port));
    auto &definition = module_definitions.get(module);
    definition = std::move(definition).with_port(id);
    return id;
}

[[nodiscard]] ModuleComponentId
Circuit::HierarchyState::add_module_component(ModuleDefId module,
                                              ModuleComponentTemplate component) {
    require_module_definition(module);
    if (module_component_by_reference(module, component.reference()).has_value()) {
        throw KernelLogicError{ErrorCode::DuplicateName,
                               "Module component reference designator already exists"};
    }

    const auto id = module_component_templates.insert(std::move(component));
    auto &definition = module_definitions.get(module);
    definition = std::move(definition).with_component(id);
    return id;
}

bool Circuit::HierarchyState::connect_module_pin(ModuleDefId module, TemplateNetDefId net,
                                                 ModuleComponentId component, PinDefId pin) {
    require_template_net_in_module(module, net);
    require_module_component_in_module(module, component);

    const auto existing = template_net_for(module, component, pin);
    if (existing.has_value()) {
        if (existing.value() == net) {
            return false;
        }

        throw KernelLogicError{ErrorCode::InvalidState,
                               "Module component pin is already connected"};
    }

    auto &definition = module_definitions.get(module);
    definition = std::move(definition).with_connection(ModulePinConnection{net, component, pin});
    return true;
}

[[nodiscard]] ModuleInstanceId
Circuit::HierarchyState::add_module_instance(ModuleInstance instance) {
    require_module_definition(instance.definition());
    if (module_instance_by_name(instance.name()).has_value()) {
        throw KernelLogicError{ErrorCode::DuplicateName, "Module instance name already exists"};
    }

    auto instance_name = instance.name().value();
    const auto id = module_instances.insert(std::move(instance));
    module_instances_by_name.emplace(std::move(instance_name), id);
    return id;
}

[[nodiscard]] PortBindingId Circuit::HierarchyState::bind_port(ModuleInstanceId instance,
                                                               PortDefId port, NetId internal_net,
                                                               NetId parent_net) {
    require_module_instance(instance);
    require_port_in_module(module_instances.get(instance).definition(), port);
    if (port_binding_for(instance, port).has_value()) {
        throw KernelLogicError{ErrorCode::InvalidState, "Module instance port is already bound"};
    }
    if (is_module_origin_net(parent_net)) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Module instance port parent net must be outside module origins"};
    }

    const auto expected_internal =
        concrete_net_for(instance, port_definitions.get(port).internal_net());
    if (!expected_internal.has_value() || expected_internal.value() != internal_net) {
        throw KernelLogicError{ErrorCode::InvalidState,
                               "Port internal net has no concrete module instance net"};
    }
    if (internal_net == parent_net) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Module instance port cannot bind to its own internal net"};
    }

    return port_bindings.insert(PortBinding{instance, port, internal_net, parent_net});
}

[[nodiscard]] std::optional<ModuleDefId>
Circuit::HierarchyState::module_definition_by_name(const ModuleName &name) const {
    for (std::size_t index = 0; index < module_definitions.size(); ++index) {
        const auto id = ModuleDefId{index};
        if (module_definitions.get(id).name() == name) {
            return id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<ModuleInstanceId>
Circuit::HierarchyState::module_instance_by_name(const ModuleInstanceName &name) const {
    const auto found = module_instances_by_name.find(name.value());
    if (found == module_instances_by_name.end()) {
        return std::nullopt;
    }

    return found->second;
}

[[nodiscard]] std::optional<TemplateNetDefId>
Circuit::HierarchyState::template_net_by_name(ModuleDefId module, const NetName &name) const {
    require_module_definition(module);
    for (const auto net : module_definitions.get(module).template_nets()) {
        if (template_net_definitions.get(net).name() == name) {
            return net;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<PortDefId>
Circuit::HierarchyState::port_by_name(ModuleDefId module, const PortName &name) const {
    require_module_definition(module);
    for (const auto port : module_definitions.get(module).ports()) {
        if (port_definitions.get(port).name() == name) {
            return port;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<ModuleComponentId>
Circuit::HierarchyState::module_component_by_reference(ModuleDefId module,
                                                       const ReferenceDesignator &reference) const {
    require_module_definition(module);
    for (const auto component : module_definitions.get(module).components()) {
        if (module_component_templates.get(component).reference() == reference) {
            return component;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<TemplateNetDefId>
Circuit::HierarchyState::template_net_for(ModuleDefId module, ModuleComponentId component,
                                          PinDefId pin) const {
    require_module_definition(module);
    require_module_component_in_module(module, component);
    for (const auto &connection : module_definitions.get(module).connections()) {
        if (connection.component() == component && connection.pin() == pin) {
            return connection.net();
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<PortBindingId>
Circuit::HierarchyState::port_binding_for(ModuleInstanceId instance, PortDefId port) const {
    require_module_instance(instance);
    require_port_in_module(module_instances.get(instance).definition(), port);
    for (std::size_t index = 0; index < port_bindings.size(); ++index) {
        const auto id = PortBindingId{index};
        const auto &binding = port_bindings.get(id);
        if (binding.instance() == instance && binding.port() == port) {
            return id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<NetId>
Circuit::HierarchyState::concrete_net_for(ModuleInstanceId instance,
                                          TemplateNetDefId template_net) const {
    require_module_instance(instance);
    require_template_net_in_module(module_instances.get(instance).definition(), template_net);
    for (const auto &[origin, concrete] : module_instances.get(instance).net_origins()) {
        if (origin == template_net) {
            return concrete;
        }
    }

    return std::nullopt;
}

[[nodiscard]] bool Circuit::HierarchyState::is_module_origin_net(NetId net) const {
    for (std::size_t index = 0; index < module_instances.size(); ++index) {
        const auto &origins = module_instances.get(ModuleInstanceId{index}).net_origins();
        if (std::ranges::any_of(origins,
                                [net](const auto &origin) { return origin.second == net; })) {
            return true;
        }
    }
    return false;
}

void Circuit::HierarchyState::require_module_definition(ModuleDefId module) const {
    if (!module_definitions.contains(module)) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Module definition ID does not belong to this circuit",
                               EntityRef::module_def(module)};
    }
}

void Circuit::HierarchyState::require_template_net(TemplateNetDefId net) const {
    if (!template_net_definitions.contains(net)) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Template net definition ID does not belong to this circuit",
                               EntityRef::template_net_def(net)};
    }
}

void Circuit::HierarchyState::require_port(PortDefId port) const {
    if (!port_definitions.contains(port)) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Port definition ID does not belong to this circuit",
                               EntityRef::port_def(port)};
    }
}

void Circuit::HierarchyState::require_module_component(ModuleComponentId component) const {
    if (!module_component_templates.contains(component)) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Module component ID does not belong to this circuit",
                               EntityRef::module_component(component)};
    }
}

void Circuit::HierarchyState::require_module_instance(ModuleInstanceId instance) const {
    if (!module_instances.contains(instance)) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Module instance ID does not belong to this circuit",
                               EntityRef::module_instance(instance)};
    }
}

void Circuit::HierarchyState::require_template_net_in_module(ModuleDefId module,
                                                             TemplateNetDefId net) const {
    require_module_definition(module);
    require_template_net(net);
    const auto &nets = module_definitions.get(module).template_nets();
    if (std::find(nets.begin(), nets.end(), net) == nets.end()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Template net does not belong to module definition",
                               EntityRef::template_net_def(net)};
    }
}

void Circuit::HierarchyState::require_port_in_module(ModuleDefId module, PortDefId port) const {
    require_module_definition(module);
    require_port(port);
    const auto &ports = module_definitions.get(module).ports();
    if (std::find(ports.begin(), ports.end(), port) == ports.end()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Port does not belong to module definition",
                               EntityRef::port_def(port)};
    }
}

void Circuit::HierarchyState::require_module_component_in_module(
    ModuleDefId module, ModuleComponentId component) const {
    require_module_definition(module);
    require_module_component(component);
    const auto &components = module_definitions.get(module).components();
    if (std::find(components.begin(), components.end(), component) == components.end()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Module component does not belong to module definition",
                               EntityRef::module_component(component)};
    }
}

} // namespace volt
