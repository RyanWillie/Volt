#include <volt/circuit/hierarchy/hierarchy_model.hpp>

#include "../circuit_storage.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace volt {

HierarchyModel::HierarchyModel() : HierarchyModel{std::make_shared<detail::HierarchyState>()} {}

HierarchyModel::HierarchyModel(std::shared_ptr<const detail::HierarchyState> state)
    : state_{std::move(state)} {}

HierarchyModel::HierarchyModel(const HierarchyModel &other)
    : HierarchyModel{std::make_shared<detail::HierarchyState>(other.state())} {}

HierarchyModel::HierarchyModel(HierarchyModel &&other) noexcept = default;

HierarchyModel &HierarchyModel::operator=(const HierarchyModel &other) {
    if (this != &other) {
        state_ = std::make_shared<detail::HierarchyState>(other.state());
    }
    return *this;
}

HierarchyModel &HierarchyModel::operator=(HierarchyModel &&other) noexcept = default;

HierarchyModel::~HierarchyModel() = default;

Circuit::HierarchyStorage::HierarchyStorage()
    : HierarchyStorage{std::make_shared<detail::HierarchyState>()} {}

Circuit::HierarchyStorage::HierarchyStorage(std::shared_ptr<detail::HierarchyState> state)
    : HierarchyModel{state}, state_{std::move(state)} {}

Circuit::HierarchyStorage::HierarchyStorage(const HierarchyStorage &other)
    : HierarchyStorage{std::make_shared<detail::HierarchyState>(other.state())} {}

Circuit::HierarchyStorage &Circuit::HierarchyStorage::operator=(const HierarchyStorage &other) {
    if (this != &other) {
        auto replacement =
            HierarchyStorage{std::make_shared<detail::HierarchyState>(other.state())};
        *this = std::move(replacement);
    }
    return *this;
}

[[nodiscard]] detail::HierarchyState &Circuit::HierarchyStorage::mutable_state() noexcept {
    return *state_;
}

[[nodiscard]] const detail::HierarchyState &Circuit::HierarchyStorage::state() const noexcept {
    return *state_;
}

[[nodiscard]] ModuleDefId
Circuit::HierarchyStorage::add_module_definition(ModuleDefinition definition) {
    if (module_definition_by_name(definition.name()).has_value()) {
        throw std::logic_error{"Module definition name already exists"};
    }

    return mutable_state().module_definitions.insert(
        detail::ModuleDefinitionStorage{std::move(definition)});
}

[[nodiscard]] TemplateNetDefId
Circuit::HierarchyStorage::add_template_net(ModuleDefId module, TemplateNetDefinition net) {
    require_module_definition(module);
    if (template_net_by_name(module, net.name()).has_value()) {
        throw std::logic_error{"Template net name already exists in module definition"};
    }

    const auto id = mutable_state().template_net_definitions.insert(std::move(net));
    mutable_state().module_definitions.get(module).add_template_net(id);
    return id;
}

[[nodiscard]] PortDefId Circuit::HierarchyStorage::add_port_definition(ModuleDefId module,
                                                                       PortDefinition port) {
    require_template_net_in_module(module, port.internal_net());
    if (port_by_name(module, port.name()).has_value()) {
        throw std::logic_error{"Port name already exists in module definition"};
    }

    const auto id = mutable_state().port_definitions.insert(std::move(port));
    mutable_state().module_definitions.get(module).add_port(id);
    return id;
}

[[nodiscard]] ModuleComponentId
Circuit::HierarchyStorage::add_module_component(ModuleDefId module,
                                                ModuleComponentTemplate component) {
    require_module_definition(module);
    if (module_component_by_reference(module, component.reference()).has_value()) {
        throw std::logic_error{"Module component reference designator already exists"};
    }

    const auto id = mutable_state().module_component_templates.insert(std::move(component));
    mutable_state().module_definitions.get(module).add_component(id);
    return id;
}

bool Circuit::HierarchyStorage::connect_module_pin(ModuleDefId module, TemplateNetDefId net,
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

    mutable_state().module_pin_connections.push_back(ModulePinConnection{net, component, pin});
    return true;
}

[[nodiscard]] ModuleInstanceId
Circuit::HierarchyStorage::instantiate_root_module(ModuleDefId definition,
                                                   ModuleInstanceName name) {
    require_module_definition(definition);
    if (module_instance_by_name(name).has_value()) {
        throw std::logic_error{"Module instance name already exists"};
    }

    return mutable_state().module_instances.insert(ModuleInstance{definition, std::move(name)});
}

[[nodiscard]] ModuleInstanceId Circuit::HierarchyStorage::restore_root_module_instance(
    ModuleDefId definition, ModuleInstanceName name,
    const std::vector<std::pair<TemplateNetDefId, NetId>> &origins,
    const std::vector<std::pair<ModuleComponentId, ComponentId>> &component_origins) {
    require_module_definition(definition);
    if (module_instance_by_name(name).has_value()) {
        throw std::logic_error{"Module instance name already exists"};
    }

    const auto &template_nets = state().module_definitions.get(definition).template_nets();
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

    const auto &module_components = state().module_definitions.get(definition).components();
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

void Circuit::HierarchyStorage::record_module_net_origin(ModuleInstanceId instance,
                                                         TemplateNetDefId template_net,
                                                         NetId concrete_net) {
    require_module_instance(instance);
    require_template_net_in_module(state().module_instances.get(instance).definition(),
                                   template_net);
    if (is_module_origin_net(concrete_net)) {
        throw std::logic_error{"Concrete net already has module origin metadata"};
    }
    if (concrete_net_for(instance, template_net).has_value()) {
        throw std::logic_error{"Module instance template net already has origin metadata"};
    }

    mutable_state().module_net_origins.push_back(ModuleNetOrigin{instance, template_net});
    mutable_state().module_origin_nets.push_back(concrete_net);
}

void Circuit::HierarchyStorage::record_module_component_origin(ModuleInstanceId instance,
                                                               ModuleComponentId component,
                                                               ComponentId concrete_component) {
    require_module_instance(instance);
    require_module_component_in_module(state().module_instances.get(instance).definition(),
                                       component);
    if (is_module_origin_component(concrete_component)) {
        throw std::logic_error{"Concrete component already has module origin metadata"};
    }
    if (concrete_component_for(instance, component).has_value()) {
        throw std::logic_error{"Module instance component already has origin metadata"};
    }

    mutable_state().module_component_origins.push_back(ModuleComponentOrigin{instance, component});
    mutable_state().module_origin_components.push_back(concrete_component);
}

[[nodiscard]] PortBindingId Circuit::HierarchyStorage::bind_port(ModuleInstanceId instance,
                                                                 PortDefId port, NetId internal_net,
                                                                 NetId parent_net) {
    require_module_instance(instance);
    require_port_in_module(state().module_instances.get(instance).definition(), port);
    if (port_binding_for(instance, port).has_value()) {
        throw std::logic_error{"Module instance port is already bound"};
    }
    if (is_module_origin_net(parent_net)) {
        throw std::logic_error{"Module instance port parent net must be outside module origins"};
    }

    const auto expected_internal =
        concrete_net_for(instance, state().port_definitions.get(port).internal_net());
    if (!expected_internal.has_value() || expected_internal.value() != internal_net) {
        throw std::logic_error{"Port internal net has no concrete module instance net"};
    }
    if (internal_net == parent_net) {
        throw std::logic_error{"Module instance port cannot bind to its own internal net"};
    }

    return mutable_state().port_bindings.insert(
        PortBinding{instance, port, internal_net, parent_net});
}

[[nodiscard]] std::optional<ModuleDefId>
HierarchyModel::module_definition_by_name(const ModuleName &name) const {
    for (std::size_t index = 0; index < state().module_definitions.size(); ++index) {
        const auto id = ModuleDefId{index};
        if (state().module_definitions.get(id).name() == name) {
            return id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<ModuleInstanceId>
HierarchyModel::module_instance_by_name(const ModuleInstanceName &name) const {
    for (std::size_t index = 0; index < state().module_instances.size(); ++index) {
        const auto id = ModuleInstanceId{index};
        if (state().module_instances.get(id).name() == name) {
            return id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<TemplateNetDefId>
HierarchyModel::template_net_by_name(ModuleDefId module, const NetName &name) const {
    require_module_definition(module);
    for (const auto net : state().module_definitions.get(module).template_nets()) {
        if (state().template_net_definitions.get(net).name() == name) {
            return net;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<PortDefId> HierarchyModel::port_by_name(ModuleDefId module,
                                                                    const PortName &name) const {
    require_module_definition(module);
    for (const auto port : state().module_definitions.get(module).ports()) {
        if (state().port_definitions.get(port).name() == name) {
            return port;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<ModuleComponentId>
HierarchyModel::module_component_by_reference(ModuleDefId module,
                                              const ReferenceDesignator &reference) const {
    require_module_definition(module);
    for (const auto component : state().module_definitions.get(module).components()) {
        if (state().module_component_templates.get(component).reference() == reference) {
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
    for (const auto &connection : state().module_pin_connections) {
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
    for (const auto &connection : state().module_pin_connections) {
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
    require_port_in_module(state().module_instances.get(instance).definition(), port);
    for (std::size_t index = 0; index < state().port_bindings.size(); ++index) {
        const auto id = PortBindingId{index};
        const auto &binding = state().port_bindings.get(id);
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
         state()
             .module_definitions.get(state().module_instances.get(instance).definition())
             .ports()) {
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
    require_module_component_in_module(state().module_instances.get(instance).definition(),
                                       component);
    for (std::size_t index = 0; index < state().module_component_origins.size(); ++index) {
        const auto &origin = state().module_component_origins.at(index);
        if (origin.instance() == instance && origin.component() == component) {
            return state().module_origin_components.at(index);
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<NetId>
HierarchyModel::concrete_net_for(ModuleInstanceId instance, TemplateNetDefId template_net) const {
    require_module_instance(instance);
    require_template_net_in_module(state().module_instances.get(instance).definition(),
                                   template_net);
    for (std::size_t index = 0; index < state().module_net_origins.size(); ++index) {
        const auto &origin = state().module_net_origins.at(index);
        if (origin.instance() == instance && origin.template_net() == template_net) {
            return state().module_origin_nets.at(index);
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::vector<std::pair<TemplateNetDefId, NetId>>
HierarchyModel::module_net_origins(ModuleInstanceId instance) const {
    require_module_instance(instance);
    auto result = std::vector<std::pair<TemplateNetDefId, NetId>>{};
    for (const auto template_net :
         state()
             .module_definitions.get(state().module_instances.get(instance).definition())
             .template_nets()) {
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
         state()
             .module_definitions.get(state().module_instances.get(instance).definition())
             .components()) {
        const auto concrete_component = concrete_component_for(instance, component);
        if (concrete_component.has_value()) {
            result.emplace_back(component, concrete_component.value());
        }
    }
    return result;
}

[[nodiscard]] bool HierarchyModel::is_module_origin_net(NetId net) const {
    return std::find(state().module_origin_nets.begin(), state().module_origin_nets.end(), net) !=
           state().module_origin_nets.end();
}

[[nodiscard]] bool HierarchyModel::is_module_origin_component(ComponentId component) const {
    return std::find(state().module_origin_components.begin(),
                     state().module_origin_components.end(),
                     component) != state().module_origin_components.end();
}

[[nodiscard]] const ModuleDefinition &HierarchyModel::module_definition(ModuleDefId id) const {
    return state().module_definitions.get(id);
}

[[nodiscard]] const TemplateNetDefinition &
HierarchyModel::template_net_definition(TemplateNetDefId id) const {
    return state().template_net_definitions.get(id);
}

[[nodiscard]] const PortDefinition &HierarchyModel::port_definition(PortDefId id) const {
    return state().port_definitions.get(id);
}

[[nodiscard]] const ModuleComponentTemplate &
HierarchyModel::module_component_template(ModuleComponentId id) const {
    return state().module_component_templates.get(id);
}

[[nodiscard]] const ModuleInstance &HierarchyModel::module_instance(ModuleInstanceId id) const {
    return state().module_instances.get(id);
}

[[nodiscard]] const PortBinding &HierarchyModel::port_binding(PortBindingId id) const {
    return state().port_bindings.get(id);
}

[[nodiscard]] std::size_t HierarchyModel::module_definition_count() const noexcept {
    return state().module_definitions.size();
}

[[nodiscard]] std::size_t HierarchyModel::template_net_definition_count() const noexcept {
    return state().template_net_definitions.size();
}

[[nodiscard]] std::size_t HierarchyModel::port_definition_count() const noexcept {
    return state().port_definitions.size();
}

[[nodiscard]] std::size_t HierarchyModel::module_component_count() const noexcept {
    return state().module_component_templates.size();
}

[[nodiscard]] std::size_t HierarchyModel::module_pin_connection_count() const noexcept {
    return state().module_pin_connections.size();
}

[[nodiscard]] std::size_t HierarchyModel::module_instance_count() const noexcept {
    return state().module_instances.size();
}

[[nodiscard]] std::size_t HierarchyModel::port_binding_count() const noexcept {
    return state().port_bindings.size();
}

void HierarchyModel::require_module_definition(ModuleDefId module) const {
    if (!state().module_definitions.contains(module)) {
        throw std::out_of_range{"Module definition ID does not belong to this circuit"};
    }
}

void HierarchyModel::require_template_net(TemplateNetDefId net) const {
    if (!state().template_net_definitions.contains(net)) {
        throw std::out_of_range{"Template net definition ID does not belong to this circuit"};
    }
}

void HierarchyModel::require_port(PortDefId port) const {
    if (!state().port_definitions.contains(port)) {
        throw std::out_of_range{"Port definition ID does not belong to this circuit"};
    }
}

void HierarchyModel::require_module_component(ModuleComponentId component) const {
    if (!state().module_component_templates.contains(component)) {
        throw std::out_of_range{"Module component ID does not belong to this circuit"};
    }
}

void HierarchyModel::require_module_instance(ModuleInstanceId instance) const {
    if (!state().module_instances.contains(instance)) {
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
    const auto &ports = state().module_definitions.get(module).ports();
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
    const auto &nets = state().module_definitions.get(module).template_nets();
    return std::find(nets.begin(), nets.end(), net) != nets.end();
}

[[nodiscard]] bool
HierarchyModel::module_component_belongs_to_module(ModuleDefId module,
                                                   ModuleComponentId component) const {
    require_module_definition(module);
    require_module_component(component);
    const auto &components = state().module_definitions.get(module).components();
    return std::find(components.begin(), components.end(), component) != components.end();
}

[[nodiscard]] const detail::HierarchyState &HierarchyModel::state() const noexcept {
    return *state_;
}

} // namespace volt
