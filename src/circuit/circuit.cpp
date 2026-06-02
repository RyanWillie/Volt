#include <volt/circuit/circuit.hpp>

#include <volt/circuit/queries.hpp>

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
    return connectivity_.add_pin_definition(std::move(definition));
}

[[nodiscard]] ComponentDefId Circuit::add_component_definition(ComponentDefinition definition) {
    return connectivity_.add_component_definition(std::move(definition));
}

[[nodiscard]] ComponentId Circuit::add_component(ComponentInstance component) {
    return connectivity_.add_component(std::move(component));
}

[[nodiscard]] PinId Circuit::add_pin(PinInstance pin) { return connectivity_.add_pin(pin); }

[[nodiscard]] NetId Circuit::add_net(Net net) { return connectivity_.add_net(std::move(net)); }

[[nodiscard]] ModuleDefId Circuit::add_module_definition(ModuleDefinition definition) {
    return hierarchy_.add_module_definition(std::move(definition));
}

[[nodiscard]] TemplateNetDefId Circuit::add_template_net(ModuleDefId module,
                                                         TemplateNetDefinition net) {
    return hierarchy_.add_template_net(module, std::move(net));
}

[[nodiscard]] PortDefId Circuit::add_port_definition(ModuleDefId module, PortDefinition port) {
    return hierarchy_.add_port_definition(module, std::move(port));
}

[[nodiscard]] ModuleComponentId Circuit::add_module_component(ModuleDefId module,
                                                              ModuleComponentTemplate component) {
    require_component_definition(component.definition());
    return hierarchy_.add_module_component(module, std::move(component));
}

bool Circuit::connect_module_pin(ModuleDefId module, TemplateNetDefId net,
                                 ModuleComponentId component, PinDefId pin) {
    require_pin_in_module_component(component, pin);
    return hierarchy_.connect_module_pin(module, net, component, pin);
}

[[nodiscard]] ModuleInstanceId Circuit::instantiate_root_module(ModuleDefId definition,
                                                                ModuleInstanceName name) {
    require_module_definition(definition);
    if (queries::module_instance_by_name(*this, name).has_value()) {
        throw std::logic_error{"Module instance name already exists"};
    }

    std::vector<Net> concrete_nets;
    for (const auto template_net : hierarchy_.module_definition(definition).template_nets()) {
        const auto &template_net_definition = hierarchy_.template_net_definition(template_net);
        auto concrete_name = NetName{name.value() + "/" + template_net_definition.name().value()};
        if (queries::net_by_name(*this, concrete_name).has_value()) {
            throw std::logic_error{"Module instance concrete net name already exists"};
        }
        concrete_nets.emplace_back(std::move(concrete_name), template_net_definition.kind());
    }

    std::vector<ComponentInstance> concrete_components;
    for (const auto component : hierarchy_.module_definition(definition).components()) {
        const auto &component_template = hierarchy_.module_component_template(component);
        auto concrete_reference =
            ReferenceDesignator{name.value() + "/" + component_template.reference().value()};
        if (queries::component_by_reference(*this, concrete_reference).has_value()) {
            throw std::logic_error{"Module instance concrete component reference exists"};
        }
        concrete_components.emplace_back(component_template.definition(),
                                         std::move(concrete_reference),
                                         component_template.properties());
    }

    const auto instance = hierarchy_.instantiate_root_module(definition, std::move(name));
    const auto &module_components = hierarchy_.module_definition(definition).components();
    for (std::size_t index = 0; index < module_components.size(); ++index) {
        const auto component = instantiate_component(concrete_components.at(index).definition(),
                                                     concrete_components.at(index).reference(),
                                                     concrete_components.at(index).properties());
        hierarchy_.record_module_component_origin(instance, module_components.at(index), component);
    }

    const auto &template_nets = hierarchy_.module_definition(definition).template_nets();
    for (std::size_t index = 0; index < template_nets.size(); ++index) {
        const auto net = add_net(std::move(concrete_nets.at(index)));
        hierarchy_.record_module_net_origin(instance, template_nets.at(index), net);
    }

    for (const auto &connection : hierarchy_.module_pin_connections(definition)) {
        if (!require_template_net_in_module_if_present(definition, connection.net()) ||
            !require_module_component_in_module_if_present(definition, connection.component())) {
            continue;
        }
        const auto concrete_component =
            queries::concrete_component_for(*this, instance, connection.component());
        const auto concrete_net = queries::concrete_net_for(*this, instance, connection.net());
        if (!concrete_component.has_value() || !concrete_net.has_value()) {
            throw std::logic_error{"Module instance origin metadata is incomplete"};
        }
        const auto concrete_pin =
            queries::pin_by_definition(*this, concrete_component.value(), connection.pin());
        if (!concrete_pin.has_value()) {
            throw std::logic_error{"Concrete module component pin is missing"};
        }
        [[maybe_unused]] const auto changed = connect(concrete_net.value(), concrete_pin.value());
    }

    return instance;
}

[[nodiscard]] PortBindingId Circuit::bind_port(ModuleInstanceId instance, PortDefId port,
                                               NetId parent_net) {
    require_net(parent_net);
    if (queries::is_module_origin_net(*this, parent_net)) {
        throw std::logic_error{"Module instance port parent net must be outside module origins"};
    }

    const auto internal_net =
        queries::concrete_net_for(*this, instance, hierarchy_.port_definition(port).internal_net());
    if (!internal_net.has_value()) {
        throw std::logic_error{"Port internal net has no concrete module instance net"};
    }

    return hierarchy_.bind_port(instance, port, internal_net.value(), parent_net);
}

[[nodiscard]] ModuleInstanceId Circuit::restore_root_module_instance(
    ModuleDefId definition, ModuleInstanceName name,
    const std::vector<std::pair<TemplateNetDefId, NetId>> &origins,
    const std::vector<std::pair<ModuleComponentId, ComponentId>> &component_origins) {
    require_module_definition(definition);
    if (queries::module_instance_by_name(*this, name).has_value()) {
        throw std::logic_error{"Module instance name already exists"};
    }

    const auto &template_nets = hierarchy_.module_definition(definition).template_nets();
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
        if (queries::is_module_origin_net(*this, concrete_net)) {
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

    const auto &module_components = hierarchy_.module_definition(definition).components();
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
        if (queries::is_module_origin_component(*this, concrete_component)) {
            throw std::logic_error{"Concrete component already has module origin metadata"};
        }
        if (connectivity_.component(concrete_component).definition() !=
            hierarchy_.module_component_template(template_component).definition()) {
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

    return hierarchy_.restore_root_module_instance(definition, std::move(name), origins,
                                                   component_origins);
}

[[nodiscard]] ComponentId Circuit::instantiate_component(ComponentDefId definition,
                                                         ReferenceDesignator reference,
                                                         PropertyMap properties) {
    return connectivity_.instantiate_component(definition, std::move(reference),
                                               std::move(properties));
}

bool Circuit::connect(NetId net, PinId pin) { return connectivity_.connect(net, pin); }

bool Circuit::disconnect(PinId pin) { return connectivity_.disconnect(pin); }

void Circuit::set_component_property(ComponentId component, PropertyKey key, PropertyValue value) {
    connectivity_.set_component_property(component, std::move(key), std::move(value));
}

void Circuit::set_component_electrical_attribute(ComponentId component,
                                                 const ElectricalAttributeSpec &spec,
                                                 ElectricalAttributeValue value) {
    require_component(component);
    electrical_.set_component_attribute(component, spec, value);
}

void Circuit::set_pin_definition_electrical_attribute(PinDefId pin_definition,
                                                      const ElectricalAttributeSpec &spec,
                                                      ElectricalAttributeValue value) {
    require_pin_definition(pin_definition);
    electrical_.set_pin_definition_attribute(pin_definition, spec, value);
}

void Circuit::select_physical_part(ComponentId component, PhysicalPart physical_part) {
    require_component(component);
    electrical_.select_physical_part(
        component, std::move(physical_part),
        component_definition(this->component(component).definition()).pins());
}

void Circuit::set_selected_part_electrical_attribute(ComponentId component,
                                                     const ElectricalAttributeSpec &spec,
                                                     ElectricalAttributeValue value) {
    require_component(component);
    electrical_.set_selected_part_attribute(component, spec, value);
}

void Circuit::set_net_electrical_attribute(NetId net, const ElectricalAttributeSpec &spec,
                                           ElectricalAttributeValue value) {
    require_net(net);
    electrical_.set_net_attribute(net, spec, value);
}

bool Circuit::mark_intentional_stub_net(NetId net) {
    require_net(net);
    return intent_.mark_intentional_stub_net(net);
}

bool Circuit::mark_intentional_no_connect_pin(PinId pin) {
    require_pin(pin);
    return intent_.mark_intentional_no_connect_pin(pin);
}

[[nodiscard]] RuleClassId Circuit::add_rule_class(RuleClass rule_class) {
    return rule_classes_.add_rule_class(std::move(rule_class));
}

bool Circuit::assign_net_rule_class(NetId net, RuleClassId rule_class) {
    require_net(net);
    require_rule_class(rule_class);
    return rule_classes_.assign_net_rule_class(net, rule_class);
}

[[nodiscard]] const std::optional<PhysicalPart> &
Circuit::selected_physical_part(ComponentId component) const {
    require_component(component);
    return electrical_.selected_physical_part(component);
}

[[nodiscard]] const ElectricalAttributeMap &
Circuit::component_electrical_attributes(ComponentId component) const {
    require_component(component);
    return electrical_.component_attributes(component);
}

[[nodiscard]] const ElectricalAttributeMap &
Circuit::pin_definition_electrical_attributes(PinDefId pin_definition) const {
    require_pin_definition(pin_definition);
    return electrical_.pin_definition_attributes(pin_definition);
}

[[nodiscard]] const ElectricalAttributeMap &Circuit::net_electrical_attributes(NetId net) const {
    require_net(net);
    return electrical_.net_attributes(net);
}

[[nodiscard]] std::vector<ModulePinConnection>
Circuit::module_pin_connections(ModuleDefId module) const {
    return hierarchy_.module_pin_connections(module);
}

[[nodiscard]] std::vector<std::pair<TemplateNetDefId, NetId>>
Circuit::module_net_origins(ModuleInstanceId instance) const {
    return hierarchy_.module_net_origins(instance);
}

[[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>>
Circuit::module_component_origins(ModuleInstanceId instance) const {
    return hierarchy_.module_component_origins(instance);
}

[[nodiscard]] bool Circuit::is_intentional_stub_net(NetId net) const {
    require_net(net);
    return intent_.is_intentional_stub_net(net);
}

[[nodiscard]] bool Circuit::is_intentional_no_connect_pin(PinId pin) const {
    require_pin(pin);
    return intent_.is_intentional_no_connect_pin(pin);
}

[[nodiscard]] const std::vector<NetId> &Circuit::intentional_stub_nets() const noexcept {
    return intent_.intentional_stub_nets();
}

[[nodiscard]] const std::vector<PinId> &Circuit::intentional_no_connect_pins() const noexcept {
    return intent_.intentional_no_connect_pins();
}

[[nodiscard]] const RuleClass &Circuit::rule_class(RuleClassId id) const {
    return rule_classes_.rule_class(id);
}

[[nodiscard]] std::optional<RuleClassId>
Circuit::rule_class_by_name(const RuleClassName &name) const {
    return rule_classes_.rule_class_by_name(name);
}

[[nodiscard]] std::optional<RuleClassId> Circuit::rule_class_for_net(NetId net) const {
    require_net(net);
    return rule_classes_.rule_class_for_net(net);
}

[[nodiscard]] const std::vector<std::pair<NetId, RuleClassId>> &
Circuit::net_rule_class_assignments() const noexcept {
    return rule_classes_.net_rule_class_assignments();
}

[[nodiscard]] const PinDefinition &Circuit::pin_definition(PinDefId id) const {
    return connectivity_.pin_definition(id);
}

[[nodiscard]] const ComponentDefinition &Circuit::component_definition(ComponentDefId id) const {
    return connectivity_.component_definition(id);
}

[[nodiscard]] const ComponentInstance &Circuit::component(ComponentId id) const {
    return connectivity_.component(id);
}

[[nodiscard]] const ModuleDefinition &Circuit::module_definition(ModuleDefId id) const {
    return hierarchy_.module_definition(id);
}

[[nodiscard]] const PortDefinition &Circuit::port_definition(PortDefId id) const {
    return hierarchy_.port_definition(id);
}

[[nodiscard]] const ModuleInstance &Circuit::module_instance(ModuleInstanceId id) const {
    return hierarchy_.module_instance(id);
}

[[nodiscard]] const PortBinding &Circuit::port_binding(PortBindingId id) const {
    return hierarchy_.port_binding(id);
}

[[nodiscard]] std::size_t Circuit::pin_definition_count() const noexcept {
    return connectivity_.pin_definition_count();
}

[[nodiscard]] std::size_t Circuit::component_definition_count() const noexcept {
    return connectivity_.component_definition_count();
}

[[nodiscard]] std::size_t Circuit::module_definition_count() const noexcept {
    return hierarchy_.module_definition_count();
}

[[nodiscard]] std::size_t Circuit::port_definition_count() const noexcept {
    return hierarchy_.port_definition_count();
}

[[nodiscard]] std::size_t Circuit::module_component_count() const noexcept {
    return hierarchy_.module_component_count();
}

[[nodiscard]] std::size_t Circuit::module_pin_connection_count() const noexcept {
    return hierarchy_.module_pin_connection_count();
}

[[nodiscard]] std::size_t Circuit::module_instance_count() const noexcept {
    return hierarchy_.module_instance_count();
}

void Circuit::require_pin_definition(PinDefId pin_definition) const {
    connectivity_.require_pin_definition(pin_definition);
}

void Circuit::require_component_definition(ComponentDefId component_definition) const {
    connectivity_.require_component_definition(component_definition);
}

void Circuit::require_component(ComponentId component) const {
    connectivity_.require_component(component);
}

void Circuit::require_module_definition(ModuleDefId module) const {
    hierarchy_.require_module_definition(module);
}

void Circuit::require_template_net(TemplateNetDefId net) const {
    hierarchy_.require_template_net(net);
}

void Circuit::require_port(PortDefId port) const { hierarchy_.require_port(port); }

void Circuit::require_module_component(ModuleComponentId component) const {
    hierarchy_.require_module_component(component);
}

void Circuit::require_module_instance(ModuleInstanceId instance) const {
    hierarchy_.require_module_instance(instance);
}

void Circuit::require_template_net_in_module(ModuleDefId module, TemplateNetDefId net) const {
    hierarchy_.require_template_net_in_module(module, net);
}

void Circuit::require_port_in_module(ModuleDefId module, PortDefId port) const {
    hierarchy_.require_port_in_module(module, port);
}

void Circuit::require_module_component_in_module(ModuleDefId module,
                                                 ModuleComponentId component) const {
    hierarchy_.require_module_component_in_module(module, component);
}

[[nodiscard]] bool
Circuit::require_module_component_in_module_if_present(ModuleDefId module,
                                                       ModuleComponentId component) const {
    return hierarchy_.module_component_belongs_to_module(module, component);
}

void Circuit::require_pin_in_module_component(ModuleComponentId component, PinDefId pin) const {
    require_module_component(component);
    require_pin_definition(pin);
    const auto &definition = connectivity_.component_definition(
        hierarchy_.module_component_template(component).definition());
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

        const auto concrete_pin =
            queries::pin_by_definition(*this, concrete_component->second, connection.pin());
        if (!concrete_pin.has_value()) {
            throw std::logic_error{"Concrete module component pin is missing"};
        }

        if (connectivity_.net_of(concrete_pin.value()) != concrete_net->second) {
            throw std::logic_error{"Module instance concrete connectivity does not match template"};
        }
    }
}

void Circuit::require_pin(PinId pin) const { connectivity_.require_pin(pin); }

void Circuit::require_net(NetId net) const { connectivity_.require_net(net); }

void Circuit::require_rule_class(RuleClassId rule_class) const {
    rule_classes_.require_rule_class(rule_class);
}

[[nodiscard]] std::optional<NetId> Circuit::net_of_existing_pin(PinId pin) const {
    return connectivity_.net_of(pin);
}

} // namespace volt
