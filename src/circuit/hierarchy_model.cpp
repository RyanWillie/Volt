#include <volt/circuit/hierarchy_model.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace volt {

[[nodiscard]] ModuleDefId HierarchyModel::add_module_definition(ModuleDefinition definition) {
    if (module_definition_by_name(definition.name()).has_value()) {
        throw std::logic_error{"Module definition name already exists"};
    }

    return module_definitions_.insert(std::move(definition));
}

[[nodiscard]] TemplateNetDefId HierarchyModel::add_template_net(ModuleDefId module,
                                                                TemplateNetDefinition net) {
    require_module_definition(module);
    if (template_net_by_name(module, net.name()).has_value()) {
        throw std::logic_error{"Template net name already exists in module definition"};
    }

    const auto id = template_net_definitions_.insert(std::move(net));
    module_definitions_.get(module).add_template_net(id);
    return id;
}

[[nodiscard]] PortDefId HierarchyModel::add_port_definition(ModuleDefId module,
                                                            PortDefinition port) {
    require_template_net_in_module(module, port.internal_net());
    if (port_by_name(module, port.name()).has_value()) {
        throw std::logic_error{"Port name already exists in module definition"};
    }

    const auto id = port_definitions_.insert(std::move(port));
    module_definitions_.get(module).add_port(id);
    return id;
}

[[nodiscard]] ModuleComponentId
HierarchyModel::add_module_component(ModuleDefId module, ModuleComponentTemplate component) {
    require_module_definition(module);
    if (module_component_by_reference(module, component.reference()).has_value()) {
        throw std::logic_error{"Module component reference designator already exists"};
    }

    const auto id = module_component_templates_.insert(std::move(component));
    module_definitions_.get(module).add_component(id);
    return id;
}

bool HierarchyModel::connect_module_pin(ModuleDefId module, TemplateNetDefId net,
                                        ModuleComponentId component, PinDefId pin) {
    require_template_net_in_module(module, net);
    require_module_component_in_module(module, component);

    const auto existing = template_net_for(module, component, pin);
    if (existing.has_value()) {
        if (existing.value() == net) {
            return false;
        }

        throw std::logic_error{"Module component pin is already connected"};
    }

    module_pin_connections_.push_back(ModulePinConnection{net, component, pin});
    return true;
}

[[nodiscard]] ModuleInstanceId HierarchyModel::instantiate_root_module(ModuleDefId definition,
                                                                       ModuleInstanceName name) {
    require_module_definition(definition);
    if (module_instance_by_name(name).has_value()) {
        throw std::logic_error{"Module instance name already exists"};
    }

    return module_instances_.insert(ModuleInstance{definition, std::move(name)});
}

[[nodiscard]] ModuleInstanceId HierarchyModel::restore_root_module_instance(
    ModuleDefId definition, ModuleInstanceName name,
    const std::vector<std::pair<TemplateNetDefId, NetId>> &origins,
    const std::vector<std::pair<ModuleComponentId, ComponentId>> &component_origins) {
    require_module_definition(definition);
    if (module_instance_by_name(name).has_value()) {
        throw std::logic_error{"Module instance name already exists"};
    }

    const auto &template_nets = module_definitions_.get(definition).template_nets();
    if (origins.size() != template_nets.size()) {
        throw std::logic_error{"Module instance origin net count does not match definition"};
    }

    auto seen_template_nets = std::vector<TemplateNetDefId>{};
    auto seen_concrete_nets = std::vector<NetId>{};
    for (const auto &[template_net, concrete_net] : origins) {
        require_template_net_in_module(definition, template_net);
        if (std::find(seen_template_nets.begin(), seen_template_nets.end(), template_net) !=
            seen_template_nets.end()) {
            throw std::logic_error{"Duplicate module instance template-net origin"};
        }
        if (std::find(seen_concrete_nets.begin(), seen_concrete_nets.end(), concrete_net) !=
            seen_concrete_nets.end()) {
            throw std::logic_error{"Duplicate module instance concrete-net origin"};
        }
        if (is_module_origin_net(concrete_net)) {
            throw std::logic_error{"Concrete net already has module origin metadata"};
        }
        seen_template_nets.push_back(template_net);
        seen_concrete_nets.push_back(concrete_net);
    }

    for (const auto template_net : template_nets) {
        if (std::find(seen_template_nets.begin(), seen_template_nets.end(), template_net) ==
            seen_template_nets.end()) {
            throw std::logic_error{"Missing module instance template-net origin"};
        }
    }

    const auto &module_components = module_definitions_.get(definition).components();
    if (component_origins.size() != module_components.size()) {
        throw std::logic_error{"Module instance component origin count does not match definition"};
    }

    auto seen_template_components = std::vector<ModuleComponentId>{};
    auto seen_concrete_components = std::vector<ComponentId>{};
    for (const auto &[template_component, concrete_component] : component_origins) {
        require_module_component_in_module(definition, template_component);
        if (std::find(seen_template_components.begin(), seen_template_components.end(),
                      template_component) != seen_template_components.end()) {
            throw std::logic_error{"Duplicate module instance template-component origin"};
        }
        if (std::find(seen_concrete_components.begin(), seen_concrete_components.end(),
                      concrete_component) != seen_concrete_components.end()) {
            throw std::logic_error{"Duplicate module instance concrete-component origin"};
        }
        if (is_module_origin_component(concrete_component)) {
            throw std::logic_error{"Concrete component already has module origin metadata"};
        }
        seen_template_components.push_back(template_component);
        seen_concrete_components.push_back(concrete_component);
    }

    for (const auto template_component : module_components) {
        if (std::find(seen_template_components.begin(), seen_template_components.end(),
                      template_component) == seen_template_components.end()) {
            throw std::logic_error{"Missing module instance template-component origin"};
        }
    }

    const auto instance = instantiate_root_module(definition, std::move(name));
    for (const auto &[template_net, concrete_net] : origins) {
        record_module_net_origin(instance, template_net, concrete_net);
    }
    for (const auto &[template_component, concrete_component] : component_origins) {
        record_module_component_origin(instance, template_component, concrete_component);
    }
    return instance;
}

void HierarchyModel::record_module_net_origin(ModuleInstanceId instance,
                                              TemplateNetDefId template_net, NetId concrete_net) {
    require_module_instance(instance);
    require_template_net_in_module(module_instances_.get(instance).definition(), template_net);
    if (is_module_origin_net(concrete_net)) {
        throw std::logic_error{"Concrete net already has module origin metadata"};
    }
    if (concrete_net_for(instance, template_net).has_value()) {
        throw std::logic_error{"Module instance template net already has origin metadata"};
    }

    module_net_origins_.push_back(ModuleNetOrigin{instance, template_net});
    module_origin_nets_.push_back(concrete_net);
}

void HierarchyModel::record_module_component_origin(ModuleInstanceId instance,
                                                    ModuleComponentId component,
                                                    ComponentId concrete_component) {
    require_module_instance(instance);
    require_module_component_in_module(module_instances_.get(instance).definition(), component);
    if (is_module_origin_component(concrete_component)) {
        throw std::logic_error{"Concrete component already has module origin metadata"};
    }
    if (concrete_component_for(instance, component).has_value()) {
        throw std::logic_error{"Module instance component already has origin metadata"};
    }

    module_component_origins_.push_back(ModuleComponentOrigin{instance, component});
    module_origin_components_.push_back(concrete_component);
}

[[nodiscard]] PortBindingId HierarchyModel::bind_port(ModuleInstanceId instance, PortDefId port,
                                                      NetId internal_net, NetId parent_net) {
    require_module_instance(instance);
    require_port_in_module(module_instances_.get(instance).definition(), port);
    if (port_binding_for(instance, port).has_value()) {
        throw std::logic_error{"Module instance port is already bound"};
    }
    if (is_module_origin_net(parent_net)) {
        throw std::logic_error{"Module instance port parent net must be outside module origins"};
    }

    const auto expected_internal =
        concrete_net_for(instance, port_definitions_.get(port).internal_net());
    if (!expected_internal.has_value() || expected_internal.value() != internal_net) {
        throw std::logic_error{"Port internal net has no concrete module instance net"};
    }
    if (internal_net == parent_net) {
        throw std::logic_error{"Module instance port cannot bind to its own internal net"};
    }

    return port_bindings_.insert(PortBinding{instance, port, internal_net, parent_net});
}

[[nodiscard]] std::optional<ModuleDefId>
HierarchyModel::module_definition_by_name(const ModuleName &name) const {
    for (std::size_t index = 0; index < module_definitions_.size(); ++index) {
        const auto id = ModuleDefId{index};
        if (module_definitions_.get(id).name() == name) {
            return id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<ModuleInstanceId>
HierarchyModel::module_instance_by_name(const ModuleInstanceName &name) const {
    for (std::size_t index = 0; index < module_instances_.size(); ++index) {
        const auto id = ModuleInstanceId{index};
        if (module_instances_.get(id).name() == name) {
            return id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<TemplateNetDefId>
HierarchyModel::template_net_by_name(ModuleDefId module, const NetName &name) const {
    require_module_definition(module);
    for (const auto net : module_definitions_.get(module).template_nets()) {
        if (template_net_definitions_.get(net).name() == name) {
            return net;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<PortDefId> HierarchyModel::port_by_name(ModuleDefId module,
                                                                    const PortName &name) const {
    require_module_definition(module);
    for (const auto port : module_definitions_.get(module).ports()) {
        if (port_definitions_.get(port).name() == name) {
            return port;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<ModuleComponentId>
HierarchyModel::module_component_by_reference(ModuleDefId module,
                                              const ReferenceDesignator &reference) const {
    require_module_definition(module);
    for (const auto component : module_definitions_.get(module).components()) {
        if (module_component_templates_.get(component).reference() == reference) {
            return component;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<TemplateNetDefId>
HierarchyModel::template_net_for(ModuleDefId module, ModuleComponentId component,
                                 PinDefId pin) const {
    require_module_definition(module);
    require_module_component_in_module(module, component);
    for (const auto &connection : module_pin_connections_) {
        if (connection.component() == component && connection.pin() == pin) {
            return connection.net();
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::vector<ModulePinConnection>
HierarchyModel::module_pin_connections(ModuleDefId module) const {
    require_module_definition(module);
    auto result = std::vector<ModulePinConnection>{};
    for (const auto &connection : module_pin_connections_) {
        if (template_net_belongs_to_module(module, connection.net()) &&
            module_component_belongs_to_module(module, connection.component())) {
            result.push_back(connection);
        }
    }
    return result;
}

[[nodiscard]] std::optional<PortBindingId>
HierarchyModel::port_binding_for(ModuleInstanceId instance, PortDefId port) const {
    require_module_instance(instance);
    require_port_in_module(module_instances_.get(instance).definition(), port);
    for (std::size_t index = 0; index < port_bindings_.size(); ++index) {
        const auto id = PortBindingId{index};
        const auto &binding = port_bindings_.get(id);
        if (binding.instance() == instance && binding.port() == port) {
            return id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::vector<PortBindingId>
HierarchyModel::port_bindings_for(ModuleInstanceId instance) const {
    require_module_instance(instance);
    auto result = std::vector<PortBindingId>{};
    for (const auto port :
         module_definitions_.get(module_instances_.get(instance).definition()).ports()) {
        const auto binding = port_binding_for(instance, port);
        if (binding.has_value()) {
            result.push_back(binding.value());
        }
    }
    return result;
}

[[nodiscard]] std::optional<ComponentId>
HierarchyModel::concrete_component_for(ModuleInstanceId instance,
                                       ModuleComponentId component) const {
    require_module_instance(instance);
    require_module_component_in_module(module_instances_.get(instance).definition(), component);
    for (std::size_t index = 0; index < module_component_origins_.size(); ++index) {
        const auto &origin = module_component_origins_.at(index);
        if (origin.instance() == instance && origin.component() == component) {
            return module_origin_components_.at(index);
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<NetId>
HierarchyModel::concrete_net_for(ModuleInstanceId instance, TemplateNetDefId template_net) const {
    require_module_instance(instance);
    require_template_net_in_module(module_instances_.get(instance).definition(), template_net);
    for (std::size_t index = 0; index < module_net_origins_.size(); ++index) {
        const auto &origin = module_net_origins_.at(index);
        if (origin.instance() == instance && origin.template_net() == template_net) {
            return module_origin_nets_.at(index);
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::vector<std::pair<TemplateNetDefId, NetId>>
HierarchyModel::module_net_origins(ModuleInstanceId instance) const {
    require_module_instance(instance);
    auto result = std::vector<std::pair<TemplateNetDefId, NetId>>{};
    for (const auto template_net :
         module_definitions_.get(module_instances_.get(instance).definition()).template_nets()) {
        const auto concrete_net = concrete_net_for(instance, template_net);
        if (concrete_net.has_value()) {
            result.emplace_back(template_net, concrete_net.value());
        }
    }
    return result;
}

[[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>>
HierarchyModel::module_component_origins(ModuleInstanceId instance) const {
    require_module_instance(instance);
    auto result = std::vector<std::pair<ModuleComponentId, ComponentId>>{};
    for (const auto component :
         module_definitions_.get(module_instances_.get(instance).definition()).components()) {
        const auto concrete_component = concrete_component_for(instance, component);
        if (concrete_component.has_value()) {
            result.emplace_back(component, concrete_component.value());
        }
    }
    return result;
}

[[nodiscard]] bool HierarchyModel::is_module_origin_net(NetId net) const {
    return std::find(module_origin_nets_.begin(), module_origin_nets_.end(), net) !=
           module_origin_nets_.end();
}

[[nodiscard]] bool HierarchyModel::is_module_origin_component(ComponentId component) const {
    return std::find(module_origin_components_.begin(), module_origin_components_.end(),
                     component) != module_origin_components_.end();
}

[[nodiscard]] const ModuleDefinition &HierarchyModel::module_definition(ModuleDefId id) const {
    return module_definitions_.get(id);
}

[[nodiscard]] const TemplateNetDefinition &
HierarchyModel::template_net_definition(TemplateNetDefId id) const {
    return template_net_definitions_.get(id);
}

[[nodiscard]] const PortDefinition &HierarchyModel::port_definition(PortDefId id) const {
    return port_definitions_.get(id);
}

[[nodiscard]] const ModuleComponentTemplate &
HierarchyModel::module_component_template(ModuleComponentId id) const {
    return module_component_templates_.get(id);
}

[[nodiscard]] const ModuleInstance &HierarchyModel::module_instance(ModuleInstanceId id) const {
    return module_instances_.get(id);
}

[[nodiscard]] const PortBinding &HierarchyModel::port_binding(PortBindingId id) const {
    return port_bindings_.get(id);
}

[[nodiscard]] std::size_t HierarchyModel::module_definition_count() const noexcept {
    return module_definitions_.size();
}

[[nodiscard]] std::size_t HierarchyModel::template_net_definition_count() const noexcept {
    return template_net_definitions_.size();
}

[[nodiscard]] std::size_t HierarchyModel::port_definition_count() const noexcept {
    return port_definitions_.size();
}

[[nodiscard]] std::size_t HierarchyModel::module_component_count() const noexcept {
    return module_component_templates_.size();
}

[[nodiscard]] std::size_t HierarchyModel::module_pin_connection_count() const noexcept {
    return module_pin_connections_.size();
}

[[nodiscard]] std::size_t HierarchyModel::module_instance_count() const noexcept {
    return module_instances_.size();
}

[[nodiscard]] std::size_t HierarchyModel::port_binding_count() const noexcept {
    return port_bindings_.size();
}

void HierarchyModel::require_module_definition(ModuleDefId module) const {
    if (!module_definitions_.contains(module)) {
        throw std::out_of_range{"Module definition ID does not belong to this circuit"};
    }
}

void HierarchyModel::require_template_net(TemplateNetDefId net) const {
    if (!template_net_definitions_.contains(net)) {
        throw std::out_of_range{"Template net definition ID does not belong to this circuit"};
    }
}

void HierarchyModel::require_port(PortDefId port) const {
    if (!port_definitions_.contains(port)) {
        throw std::out_of_range{"Port definition ID does not belong to this circuit"};
    }
}

void HierarchyModel::require_module_component(ModuleComponentId component) const {
    if (!module_component_templates_.contains(component)) {
        throw std::out_of_range{"Module component ID does not belong to this circuit"};
    }
}

void HierarchyModel::require_module_instance(ModuleInstanceId instance) const {
    if (!module_instances_.contains(instance)) {
        throw std::out_of_range{"Module instance ID does not belong to this circuit"};
    }
}

void HierarchyModel::require_template_net_in_module(ModuleDefId module,
                                                    TemplateNetDefId net) const {
    require_module_definition(module);
    require_template_net(net);
    if (!template_net_belongs_to_module(module, net)) {
        throw std::logic_error{"Template net does not belong to module definition"};
    }
}

void HierarchyModel::require_port_in_module(ModuleDefId module, PortDefId port) const {
    require_module_definition(module);
    require_port(port);
    const auto &ports = module_definitions_.get(module).ports();
    if (std::find(ports.begin(), ports.end(), port) == ports.end()) {
        throw std::logic_error{"Port does not belong to module definition"};
    }
}

void HierarchyModel::require_module_component_in_module(ModuleDefId module,
                                                        ModuleComponentId component) const {
    require_module_definition(module);
    require_module_component(component);
    if (!module_component_belongs_to_module(module, component)) {
        throw std::logic_error{"Module component does not belong to module definition"};
    }
}

[[nodiscard]] bool HierarchyModel::template_net_belongs_to_module(ModuleDefId module,
                                                                  TemplateNetDefId net) const {
    require_module_definition(module);
    require_template_net(net);
    const auto &nets = module_definitions_.get(module).template_nets();
    return std::find(nets.begin(), nets.end(), net) != nets.end();
}

[[nodiscard]] bool
HierarchyModel::module_component_belongs_to_module(ModuleDefId module,
                                                   ModuleComponentId component) const {
    require_module_definition(module);
    require_module_component(component);
    const auto &components = module_definitions_.get(module).components();
    return std::find(components.begin(), components.end(), component) != components.end();
}

} // namespace volt
