#include <volt/circuit/circuit.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace volt {

[[nodiscard]] PinDefId Circuit::add_pin_definition(PinDefinition definition) {
    return pin_definitions_.insert(std::move(definition));
}
[[nodiscard]] ComponentDefId Circuit::add_component_definition(ComponentDefinition definition) {
    for (const auto pin : definition.pins()) {
        require_pin_definition(pin);
    }

    return component_definitions_.insert(std::move(definition));
}
[[nodiscard]] ComponentId Circuit::add_component(ComponentInstance component) {
    require_component_definition(component.definition());
    if (component_by_reference(component.reference()).has_value()) {
        throw std::logic_error{"Component reference designator already exists"};
    }

    return components_.insert(std::move(component));
}
[[nodiscard]] PinId Circuit::add_pin(PinInstance pin) {
    if (!components_.contains(pin.component())) {
        throw std::out_of_range{"Pin instance references a missing component"};
    }
    if (!pin_definitions_.contains(pin.definition())) {
        throw std::out_of_range{"Pin instance references a missing pin definition"};
    }

    return pins_.insert(std::move(pin));
}
[[nodiscard]] NetId Circuit::add_net(Net net) {
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
[[nodiscard]] ModuleDefId Circuit::add_module_definition(ModuleDefinition definition) {
    if (module_definition_by_name(definition.name()).has_value()) {
        throw std::logic_error{"Module definition name already exists"};
    }

    return module_definitions_.insert(std::move(definition));
}
[[nodiscard]] TemplateNetDefId Circuit::add_template_net(ModuleDefId module,
                                                         TemplateNetDefinition net) {
    require_module_definition(module);
    if (template_net_by_name(module, net.name()).has_value()) {
        throw std::logic_error{"Template net name already exists in module definition"};
    }

    const auto id = template_net_definitions_.insert(std::move(net));
    module_definitions_.get(module).add_template_net(id);
    return id;
}
[[nodiscard]] PortDefId Circuit::add_port_definition(ModuleDefId module, PortDefinition port) {
    require_module_definition(module);
    require_template_net_in_module(module, port.internal_net());
    if (port_by_name(module, port.name()).has_value()) {
        throw std::logic_error{"Port name already exists in module definition"};
    }

    const auto id = port_definitions_.insert(std::move(port));
    module_definitions_.get(module).add_port(id);
    return id;
}
[[nodiscard]] ModuleComponentId Circuit::add_module_component(ModuleDefId module,
                                                              ModuleComponentTemplate component) {
    require_module_definition(module);
    require_component_definition(component.definition());
    if (module_component_by_reference(module, component.reference()).has_value()) {
        throw std::logic_error{"Module component reference designator already exists"};
    }

    const auto id = module_component_templates_.insert(std::move(component));
    module_definitions_.get(module).add_component(id);
    return id;
}
bool Circuit::connect_module_pin(ModuleDefId module, TemplateNetDefId net,
                                 ModuleComponentId component, PinDefId pin) {
    require_template_net_in_module(module, net);
    require_module_component_in_module(module, component);
    require_pin_in_module_component(component, pin);

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
[[nodiscard]] ModuleInstanceId Circuit::instantiate_root_module(ModuleDefId definition,
                                                                ModuleInstanceName name) {
    require_module_definition(definition);
    if (module_instance_by_name(name).has_value()) {
        throw std::logic_error{"Module instance name already exists"};
    }

    std::vector<Net> concrete_nets;
    for (const auto template_net : module_definitions_.get(definition).template_nets()) {
        const auto &template_net_definition = template_net_definitions_.get(template_net);
        auto concrete_name = NetName{name.value() + "/" + template_net_definition.name().value()};
        if (net_by_name(concrete_name).has_value()) {
            throw std::logic_error{"Module instance concrete net name already exists"};
        }
        concrete_nets.emplace_back(std::move(concrete_name), template_net_definition.kind());
    }

    std::vector<ComponentInstance> concrete_components;
    for (const auto component : module_definitions_.get(definition).components()) {
        const auto &component_template = module_component_templates_.get(component);
        auto concrete_reference =
            ReferenceDesignator{name.value() + "/" + component_template.reference().value()};
        if (component_by_reference(concrete_reference).has_value()) {
            throw std::logic_error{"Module instance concrete component reference exists"};
        }
        concrete_components.emplace_back(component_template.definition(),
                                         std::move(concrete_reference),
                                         component_template.properties());
    }

    const auto instance = module_instances_.insert(ModuleInstance{definition, std::move(name)});
    const auto &module_components = module_definitions_.get(definition).components();
    for (std::size_t index = 0; index < module_components.size(); ++index) {
        const auto component = instantiate_component(concrete_components.at(index).definition(),
                                                     concrete_components.at(index).reference(),
                                                     concrete_components.at(index).properties());
        module_component_origins_.push_back(
            ModuleComponentOrigin{instance, module_components.at(index)});
        module_origin_components_.push_back(component);
    }

    const auto &template_nets = module_definitions_.get(definition).template_nets();
    for (std::size_t index = 0; index < template_nets.size(); ++index) {
        const auto net = add_net(std::move(concrete_nets.at(index)));
        module_net_origins_.push_back(ModuleNetOrigin{instance, template_nets.at(index)});
        module_origin_nets_.push_back(net);
    }

    for (const auto &connection : module_pin_connections_) {
        if (!require_template_net_in_module_if_present(definition, connection.net()) ||
            !require_module_component_in_module_if_present(definition, connection.component())) {
            continue;
        }
        const auto concrete_component = concrete_component_for(instance, connection.component());
        const auto concrete_net = concrete_net_for(instance, connection.net());
        if (!concrete_component.has_value() || !concrete_net.has_value()) {
            throw std::logic_error{"Module instance origin metadata is incomplete"};
        }
        const auto concrete_pin = pin_by_definition(concrete_component.value(), connection.pin());
        if (!concrete_pin.has_value()) {
            throw std::logic_error{"Concrete module component pin is missing"};
        }
        [[maybe_unused]] const auto changed = connect(concrete_net.value(), concrete_pin.value());
    }

    return instance;
}
[[nodiscard]] PortBindingId Circuit::bind_port(ModuleInstanceId instance, PortDefId port,
                                               NetId parent_net) {
    require_module_instance(instance);
    require_port_in_module(module_instances_.get(instance).definition(), port);
    require_net(parent_net);
    if (is_module_origin_net(parent_net)) {
        throw std::logic_error{"Module instance port parent net must be outside module origins"};
    }

    if (port_binding_for(instance, port).has_value()) {
        throw std::logic_error{"Module instance port is already bound"};
    }

    const auto internal_net =
        concrete_net_for(instance, port_definitions_.get(port).internal_net());
    if (!internal_net.has_value()) {
        throw std::logic_error{"Port internal net has no concrete module instance net"};
    }
    if (internal_net.value() == parent_net) {
        throw std::logic_error{"Module instance port cannot bind to its own internal net"};
    }

    return port_bindings_.insert(PortBinding{instance, port, internal_net.value(), parent_net});
}
[[nodiscard]] ModuleInstanceId Circuit::restore_root_module_instance(
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
        require_net(concrete_net);
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
        require_component(concrete_component);
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
        if (components_.get(concrete_component).definition() !=
            module_component_templates_.get(template_component).definition()) {
            throw std::logic_error{
                "Module instance concrete component definition does not match template"};
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
    require_restored_module_connectivity_matches_template(definition, origins, component_origins);

    const auto instance = module_instances_.insert(ModuleInstance{definition, std::move(name)});
    for (const auto &[template_net, concrete_net] : origins) {
        module_net_origins_.push_back(ModuleNetOrigin{instance, template_net});
        module_origin_nets_.push_back(concrete_net);
    }
    for (const auto &[template_component, concrete_component] : component_origins) {
        module_component_origins_.push_back(ModuleComponentOrigin{instance, template_component});
        module_origin_components_.push_back(concrete_component);
    }
    return instance;
}
[[nodiscard]] ComponentId Circuit::instantiate_component(ComponentDefId definition,
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
bool Circuit::connect(NetId net, PinId pin) {
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
bool Circuit::disconnect(PinId pin) {
    require_pin(pin);

    const auto existing_net = net_of_existing_pin(pin);
    if (!existing_net.has_value()) {
        return false;
    }

    return nets_.get(existing_net.value()).disconnect(pin);
}
void Circuit::set_component_property(ComponentId component, PropertyKey key, PropertyValue value) {
    require_component(component);
    components_.get(component).set_property(std::move(key), std::move(value));
}
void Circuit::set_component_electrical_attribute(ComponentId component,
                                                 const ElectricalAttributeSpec &spec,
                                                 ElectricalAttributeValue value) {
    require_component(component);
    require_attribute_owner(spec, ElectricalAttributeOwner::ComponentInstance);
    components_.get(component).set_electrical_attribute(spec, std::move(value));
}
void Circuit::set_pin_definition_electrical_attribute(PinDefId pin_definition,
                                                      const ElectricalAttributeSpec &spec,
                                                      ElectricalAttributeValue value) {
    require_pin_definition(pin_definition);
    require_attribute_owner(spec, ElectricalAttributeOwner::PinSpec);
    pin_definitions_.get(pin_definition).set_electrical_attribute(spec, std::move(value));
}
void Circuit::select_physical_part(ComponentId component, PhysicalPart physical_part) {
    require_component(component);
    require_physical_part_matches_component_definition(components_.get(component).definition(),
                                                       physical_part);

    components_.get(component).select_physical_part(std::move(physical_part));
}
void Circuit::set_selected_part_electrical_attribute(ComponentId component,
                                                     const ElectricalAttributeSpec &spec,
                                                     ElectricalAttributeValue value) {
    require_component(component);
    require_attribute_owner(spec, ElectricalAttributeOwner::SelectedPart);
    components_.get(component).set_selected_part_electrical_attribute(spec, std::move(value));
}
void Circuit::set_net_electrical_attribute(NetId net, const ElectricalAttributeSpec &spec,
                                           ElectricalAttributeValue value) {
    require_net(net);
    require_attribute_owner(spec, ElectricalAttributeOwner::Net);
    nets_.get(net).set_electrical_attribute(spec, std::move(value));
}
bool Circuit::mark_intentional_stub_net(NetId net) {
    require_net(net);
    if (is_intentional_stub_net(net)) {
        return false;
    }

    intentional_stub_nets_.push_back(net);
    return true;
}
bool Circuit::mark_intentional_no_connect_pin(PinId pin) {
    require_pin(pin);
    if (is_intentional_no_connect_pin(pin)) {
        return false;
    }

    intentional_no_connect_pins_.push_back(pin);
    return true;
}
[[nodiscard]] const std::optional<PhysicalPart> &
Circuit::selected_physical_part(ComponentId component) const {
    require_component(component);
    return components_.get(component).selected_physical_part();
}
[[nodiscard]] std::optional<NetId> Circuit::net_of(PinId pin) const {
    require_pin(pin);
    return net_of_existing_pin(pin);
}
[[nodiscard]] std::optional<ComponentId>
Circuit::component_by_reference(const ReferenceDesignator &reference) const {
    for (std::size_t index = 0; index < components_.size(); ++index) {
        const auto component_id = ComponentId{index};
        if (components_.get(component_id).reference() == reference) {
            return component_id;
        }
    }

    return std::nullopt;
}
[[nodiscard]] std::optional<ModuleDefId>
Circuit::module_definition_by_name(const ModuleName &name) const {
    for (std::size_t index = 0; index < module_definitions_.size(); ++index) {
        const auto id = ModuleDefId{index};
        if (module_definitions_.get(id).name() == name) {
            return id;
        }
    }

    return std::nullopt;
}
[[nodiscard]] std::optional<ModuleInstanceId>
Circuit::module_instance_by_name(const ModuleInstanceName &name) const {
    for (std::size_t index = 0; index < module_instances_.size(); ++index) {
        const auto id = ModuleInstanceId{index};
        if (module_instances_.get(id).name() == name) {
            return id;
        }
    }

    return std::nullopt;
}
[[nodiscard]] std::optional<TemplateNetDefId>
Circuit::template_net_by_name(ModuleDefId module, const NetName &name) const {
    require_module_definition(module);
    for (const auto net : module_definitions_.get(module).template_nets()) {
        if (template_net_definitions_.get(net).name() == name) {
            return net;
        }
    }

    return std::nullopt;
}
[[nodiscard]] std::optional<PortDefId> Circuit::port_by_name(ModuleDefId module,
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
Circuit::module_component_by_reference(ModuleDefId module,
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
Circuit::template_net_for(ModuleDefId module, ModuleComponentId component, PinDefId pin) const {
    require_module_definition(module);
    require_module_component_in_module(module, component);
    require_pin_in_module_component(component, pin);
    for (const auto &connection : module_pin_connections_) {
        if (connection.component() == component && connection.pin() == pin) {
            return connection.net();
        }
    }

    return std::nullopt;
}
[[nodiscard]] std::vector<ModulePinConnection>
Circuit::module_pin_connections(ModuleDefId module) const {
    require_module_definition(module);
    auto result = std::vector<ModulePinConnection>{};
    for (const auto &connection : module_pin_connections_) {
        if (require_template_net_in_module_if_present(module, connection.net()) &&
            require_module_component_in_module_if_present(module, connection.component())) {
            result.push_back(connection);
        }
    }
    return result;
}
[[nodiscard]] std::optional<PortBindingId> Circuit::port_binding_for(ModuleInstanceId instance,
                                                                     PortDefId port) const {
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
Circuit::port_bindings_for(ModuleInstanceId instance) const {
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
Circuit::concrete_component_for(ModuleInstanceId instance, ModuleComponentId component) const {
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
[[nodiscard]] std::optional<NetId> Circuit::concrete_net_for(ModuleInstanceId instance,
                                                             TemplateNetDefId template_net) const {
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
Circuit::module_net_origins(ModuleInstanceId instance) const {
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
Circuit::module_component_origins(ModuleInstanceId instance) const {
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
[[nodiscard]] bool Circuit::is_module_origin_net(NetId net) const {
    require_net(net);
    return std::find(module_origin_nets_.begin(), module_origin_nets_.end(), net) !=
           module_origin_nets_.end();
}
[[nodiscard]] bool Circuit::is_intentional_stub_net(NetId net) const {
    require_net(net);
    return std::find(intentional_stub_nets_.begin(), intentional_stub_nets_.end(), net) !=
           intentional_stub_nets_.end();
}
[[nodiscard]] bool Circuit::is_intentional_no_connect_pin(PinId pin) const {
    require_pin(pin);
    return std::find(intentional_no_connect_pins_.begin(), intentional_no_connect_pins_.end(),
                     pin) != intentional_no_connect_pins_.end();
}
[[nodiscard]] const std::vector<NetId> &Circuit::intentional_stub_nets() const noexcept {
    return intentional_stub_nets_;
}
[[nodiscard]] const std::vector<PinId> &Circuit::intentional_no_connect_pins() const noexcept {
    return intentional_no_connect_pins_;
}
[[nodiscard]] bool Circuit::is_module_origin_component(ComponentId component) const {
    require_component(component);
    return std::find(module_origin_components_.begin(), module_origin_components_.end(),
                     component) != module_origin_components_.end();
}
[[nodiscard]] std::optional<NetId> Circuit::net_by_name(const NetName &name) const {
    for (std::size_t index = 0; index < nets_.size(); ++index) {
        const auto net_id = NetId{index};
        if (nets_.get(net_id).name() == name) {
            return net_id;
        }
    }

    return std::nullopt;
}
[[nodiscard]] std::vector<PinId> Circuit::pins_for(ComponentId component) const {
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
[[nodiscard]] std::optional<PinId> Circuit::pin_by_name(ComponentId component,
                                                        std::string_view name) const {
    for (const auto pin_id : pins_for(component)) {
        const auto definition = pins_.get(pin_id).definition();
        if (pin_definitions_.get(definition).name() == name) {
            return pin_id;
        }
    }

    return std::nullopt;
}
[[nodiscard]] std::optional<PinId> Circuit::pin_by_definition(ComponentId component,
                                                              PinDefId definition) const {
    for (const auto pin_id : pins_for(component)) {
        if (pins_.get(pin_id).definition() == definition) {
            return pin_id;
        }
    }

    return std::nullopt;
}
[[nodiscard]] std::optional<PinId> Circuit::pin_by_number(ComponentId component,
                                                          std::string_view number) const {
    for (const auto pin_id : pins_for(component)) {
        const auto definition = pins_.get(pin_id).definition();
        if (pin_definitions_.get(definition).number() == number) {
            return pin_id;
        }
    }

    return std::nullopt;
}
[[nodiscard]] const PinDefinition &Circuit::pin_definition(PinDefId id) const {
    return pin_definitions_.get(id);
}
[[nodiscard]] const ComponentDefinition &Circuit::component_definition(ComponentDefId id) const {
    return component_definitions_.get(id);
}
[[nodiscard]] const ComponentInstance &Circuit::component(ComponentId id) const {
    return components_.get(id);
}
[[nodiscard]] const ModuleDefinition &Circuit::module_definition(ModuleDefId id) const {
    return module_definitions_.get(id);
}
[[nodiscard]] const PortDefinition &Circuit::port_definition(PortDefId id) const {
    return port_definitions_.get(id);
}
[[nodiscard]] const ModuleInstance &Circuit::module_instance(ModuleInstanceId id) const {
    return module_instances_.get(id);
}
[[nodiscard]] const PortBinding &Circuit::port_binding(PortBindingId id) const {
    return port_bindings_.get(id);
}
[[nodiscard]] std::size_t Circuit::pin_definition_count() const noexcept {
    return pin_definitions_.size();
}
[[nodiscard]] std::size_t Circuit::component_definition_count() const noexcept {
    return component_definitions_.size();
}
[[nodiscard]] std::size_t Circuit::module_definition_count() const noexcept {
    return module_definitions_.size();
}
[[nodiscard]] std::size_t Circuit::port_definition_count() const noexcept {
    return port_definitions_.size();
}
[[nodiscard]] std::size_t Circuit::module_component_count() const noexcept {
    return module_component_templates_.size();
}
[[nodiscard]] std::size_t Circuit::module_pin_connection_count() const noexcept {
    return module_pin_connections_.size();
}
[[nodiscard]] std::size_t Circuit::module_instance_count() const noexcept {
    return module_instances_.size();
}
void Circuit::require_pin_definition(PinDefId pin_definition) const {
    if (!pin_definitions_.contains(pin_definition)) {
        throw std::out_of_range{"Pin definition ID does not belong to this circuit"};
    }
}
void Circuit::require_component_definition(ComponentDefId component_definition) const {
    if (!component_definitions_.contains(component_definition)) {
        throw std::out_of_range{"Component definition ID does not belong to this circuit"};
    }
}
void Circuit::require_component(ComponentId component) const {
    if (!components_.contains(component)) {
        throw std::out_of_range{"Component ID does not belong to this circuit"};
    }
}
void Circuit::require_module_definition(ModuleDefId module) const {
    if (!module_definitions_.contains(module)) {
        throw std::out_of_range{"Module definition ID does not belong to this circuit"};
    }
}
void Circuit::require_template_net(TemplateNetDefId net) const {
    if (!template_net_definitions_.contains(net)) {
        throw std::out_of_range{"Template net definition ID does not belong to this circuit"};
    }
}
void Circuit::require_port(PortDefId port) const {
    if (!port_definitions_.contains(port)) {
        throw std::out_of_range{"Port definition ID does not belong to this circuit"};
    }
}
void Circuit::require_module_component(ModuleComponentId component) const {
    if (!module_component_templates_.contains(component)) {
        throw std::out_of_range{"Module component ID does not belong to this circuit"};
    }
}
void Circuit::require_module_instance(ModuleInstanceId instance) const {
    if (!module_instances_.contains(instance)) {
        throw std::out_of_range{"Module instance ID does not belong to this circuit"};
    }
}
void Circuit::require_template_net_in_module(ModuleDefId module, TemplateNetDefId net) const {
    require_module_definition(module);
    require_template_net(net);
    const auto &nets = module_definitions_.get(module).template_nets();
    if (std::find(nets.begin(), nets.end(), net) == nets.end()) {
        throw std::logic_error{"Template net does not belong to module definition"};
    }
}
void Circuit::require_port_in_module(ModuleDefId module, PortDefId port) const {
    require_module_definition(module);
    require_port(port);
    const auto &ports = module_definitions_.get(module).ports();
    if (std::find(ports.begin(), ports.end(), port) == ports.end()) {
        throw std::logic_error{"Port does not belong to module definition"};
    }
}
void Circuit::require_module_component_in_module(ModuleDefId module,
                                                 ModuleComponentId component) const {
    require_module_definition(module);
    require_module_component(component);
    const auto &components = module_definitions_.get(module).components();
    if (std::find(components.begin(), components.end(), component) == components.end()) {
        throw std::logic_error{"Module component does not belong to module definition"};
    }
}
[[nodiscard]] bool
Circuit::require_module_component_in_module_if_present(ModuleDefId module,
                                                       ModuleComponentId component) const {
    require_module_definition(module);
    require_module_component(component);
    const auto &components = module_definitions_.get(module).components();
    return std::find(components.begin(), components.end(), component) != components.end();
}
void Circuit::require_pin_in_module_component(ModuleComponentId component, PinDefId pin) const {
    require_module_component(component);
    require_pin_definition(pin);
    const auto &definition =
        component_definitions_.get(module_component_templates_.get(component).definition());
    const auto &pins = definition.pins();
    if (std::find(pins.begin(), pins.end(), pin) == pins.end()) {
        throw std::logic_error{"Pin definition does not belong to module component definition"};
    }
}
void Circuit::require_restored_module_connectivity_matches_template(
    ModuleDefId definition, const std::vector<std::pair<TemplateNetDefId, NetId>> &origins,
    const std::vector<std::pair<ModuleComponentId, ComponentId>> &component_origins) const {
    require_module_definition(definition);
    for (const auto &connection : module_pin_connections(definition)) {
        const auto concrete_component = std::find_if(
            component_origins.begin(), component_origins.end(),
            [&connection](const auto &origin) { return origin.first == connection.component(); });
        const auto concrete_net =
            std::find_if(origins.begin(), origins.end(), [&connection](const auto &origin) {
                return origin.first == connection.net();
            });
        if (concrete_component == component_origins.end() || concrete_net == origins.end()) {
            throw std::logic_error{"Module instance origin metadata is incomplete"};
        }

        const auto concrete_pin = pin_by_definition(concrete_component->second, connection.pin());
        if (!concrete_pin.has_value()) {
            throw std::logic_error{"Concrete module component pin is missing"};
        }

        if (net_of_existing_pin(concrete_pin.value()) != concrete_net->second) {
            throw std::logic_error{"Module instance concrete connectivity does not match template"};
        }
    }
}
void Circuit::require_pin(PinId pin) const {
    if (!pins_.contains(pin)) {
        throw std::out_of_range{"Pin ID does not belong to this circuit"};
    }
}
void Circuit::require_net(NetId net) const {
    if (!nets_.contains(net)) {
        throw std::out_of_range{"Net ID does not belong to this circuit"};
    }
}
void Circuit::require_attribute_owner(const ElectricalAttributeSpec &spec,
                                      ElectricalAttributeOwner expected) {
    if (spec.owner() != expected) {
        throw std::logic_error{"Electrical attribute spec owner is not valid here"};
    }
}
void Circuit::require_physical_part_matches_component_definition(
    ComponentDefId component_definition, const PhysicalPart &physical_part) const {
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
            throw std::logic_error{"Physical part must map every pin in the component definition"};
        }
    }
}
[[nodiscard]] std::optional<NetId> Circuit::net_of_existing_pin(PinId pin) const {
    for (std::size_t index = 0; index < nets_.size(); ++index) {
        const auto net = NetId{index};
        if (nets_.get(net).contains(pin)) {
            return net;
        }
    }

    return std::nullopt;
}

} // namespace volt
