#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/circuit/definitions.hpp>
#include <volt/circuit/hierarchy.hpp>
#include <volt/circuit/instances.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/circuit/parts.hpp>
#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>

namespace volt {

/** Owning database for the canonical logical circuit model. */
class Circuit {
  public:
    /** Store a reusable pin definition and return its stable ID. */
    [[nodiscard]] PinDefId add_pin_definition(PinDefinition definition) {
        return pin_definitions_.insert(std::move(definition));
    }

    /** Store a reusable component definition and return its stable ID. */
    [[nodiscard]] ComponentDefId add_component_definition(ComponentDefinition definition) {
        for (const auto pin : definition.pins()) {
            require_pin_definition(pin);
        }

        return component_definitions_.insert(std::move(definition));
    }

    /** Store a component instance and return its stable ID. */
    [[nodiscard]] ComponentId add_component(ComponentInstance component) {
        require_component_definition(component.definition());
        if (component_by_reference(component.reference()).has_value()) {
            throw std::logic_error{"Component reference designator already exists"};
        }

        return components_.insert(std::move(component));
    }

    /** Store a concrete pin instance and return its stable ID. */
    [[nodiscard]] PinId add_pin(PinInstance pin) {
        if (!components_.contains(pin.component())) {
            throw std::out_of_range{"Pin instance references a missing component"};
        }
        if (!pin_definitions_.contains(pin.definition())) {
            throw std::out_of_range{"Pin instance references a missing pin definition"};
        }

        return pins_.insert(std::move(pin));
    }

    /** Store a canonical net and return its stable ID. */
    [[nodiscard]] NetId add_net(Net net) {
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

    /** Store a reusable logical module definition and return its stable ID. */
    [[nodiscard]] ModuleDefId add_module_definition(ModuleDefinition definition) {
        if (module_definition_by_name(definition.name()).has_value()) {
            throw std::logic_error{"Module definition name already exists"};
        }

        return module_definitions_.insert(std::move(definition));
    }

    /** Add a template-local net to a reusable module definition. */
    [[nodiscard]] TemplateNetDefId add_template_net(ModuleDefId module, TemplateNetDefinition net) {
        require_module_definition(module);
        if (template_net_by_name(module, net.name()).has_value()) {
            throw std::logic_error{"Template net name already exists in module definition"};
        }

        const auto id = template_net_definitions_.insert(std::move(net));
        module_definitions_.get(module).add_template_net(id);
        return id;
    }

    /** Add a boundary port to a reusable module definition. */
    [[nodiscard]] PortDefId add_port_definition(ModuleDefId module, PortDefinition port) {
        require_module_definition(module);
        require_template_net_in_module(module, port.internal_net());
        if (port_by_name(module, port.name()).has_value()) {
            throw std::logic_error{"Port name already exists in module definition"};
        }

        const auto id = port_definitions_.insert(std::move(port));
        module_definitions_.get(module).add_port(id);
        return id;
    }

    /** Add a component occurrence to a reusable module definition. */
    [[nodiscard]] ModuleComponentId add_module_component(ModuleDefId module,
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

    /** Connect a module component template pin to a template-local net. */
    bool connect_module_pin(ModuleDefId module, TemplateNetDefId net, ModuleComponentId component,
                            PinDefId pin) {
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

    /** Instantiate a module at the root and create concrete nets for its template-local nets. */
    [[nodiscard]] ModuleInstanceId instantiate_root_module(ModuleDefId definition,
                                                           ModuleInstanceName name) {
        require_module_definition(definition);
        if (module_instance_by_name(name).has_value()) {
            throw std::logic_error{"Module instance name already exists"};
        }

        std::vector<Net> concrete_nets;
        for (const auto template_net : module_definitions_.get(definition).template_nets()) {
            const auto &template_net_definition = template_net_definitions_.get(template_net);
            auto concrete_name =
                NetName{name.value() + "/" + template_net_definition.name().value()};
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
            const auto component =
                instantiate_component(concrete_components.at(index).definition(),
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
                !require_module_component_in_module_if_present(definition,
                                                               connection.component())) {
                continue;
            }
            const auto concrete_component =
                concrete_component_for(instance, connection.component());
            const auto concrete_net = concrete_net_for(instance, connection.net());
            if (!concrete_component.has_value() || !concrete_net.has_value()) {
                throw std::logic_error{"Module instance origin metadata is incomplete"};
            }
            const auto concrete_pin =
                pin_by_definition(concrete_component.value(), connection.pin());
            if (!concrete_pin.has_value()) {
                throw std::logic_error{"Concrete module component pin is missing"};
            }
            [[maybe_unused]] const auto changed =
                connect(concrete_net.value(), concrete_pin.value());
        }

        return instance;
    }

    /** Record an explicit connectivity edge from an instance-local port net to a parent net. */
    [[nodiscard]] PortBindingId bind_port(ModuleInstanceId instance, PortDefId port,
                                          NetId parent_net) {
        require_module_instance(instance);
        require_port_in_module(module_instances_.get(instance).definition(), port);
        require_net(parent_net);
        if (is_module_origin_net(parent_net)) {
            throw std::logic_error{
                "Module instance port parent net must be outside module origins"};
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

    /** Restore a root module instance over existing concrete nets while loading JSON. */
    [[nodiscard]] ModuleInstanceId restore_root_module_instance(
        ModuleDefId definition, ModuleInstanceName name,
        const std::vector<std::pair<TemplateNetDefId, NetId>> &origins,
        const std::vector<std::pair<ModuleComponentId, ComponentId>> &component_origins = {}) {
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
            throw std::logic_error{
                "Module instance component origin count does not match definition"};
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

        const auto instance = module_instances_.insert(ModuleInstance{definition, std::move(name)});
        for (const auto &[template_net, concrete_net] : origins) {
            module_net_origins_.push_back(ModuleNetOrigin{instance, template_net});
            module_origin_nets_.push_back(concrete_net);
        }
        for (const auto &[template_component, concrete_component] : component_origins) {
            module_component_origins_.push_back(
                ModuleComponentOrigin{instance, template_component});
            module_origin_components_.push_back(concrete_component);
        }
        return instance;
    }

    /**
     * Instantiate a component definition and create concrete pins for each ordered pin
     * definition.
     */
    [[nodiscard]] ComponentId instantiate_component(ComponentDefId definition,
                                                    ReferenceDesignator reference,
                                                    PropertyMap properties = {}) {
        require_component_definition(definition);

        const auto component = add_component(
            ComponentInstance{definition, std::move(reference), std::move(properties)});
        for (const auto pin_definition_id : component_definition(definition).pins()) {
            [[maybe_unused]] const auto pin = add_pin(PinInstance{component, pin_definition_id});
        }

        return component;
    }

    /** Connect an existing pin to an existing net; returns true when the circuit changed. */
    bool connect(NetId net, PinId pin) {
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

    /** Disconnect an existing pin from its current net; returns true when the circuit changed. */
    bool disconnect(PinId pin) {
        require_pin(pin);

        const auto existing_net = net_of_existing_pin(pin);
        if (!existing_net.has_value()) {
            return false;
        }

        return nets_.get(existing_net.value()).disconnect(pin);
    }

    /** Set or replace a metadata property on an existing component instance. */
    void set_component_property(ComponentId component, PropertyKey key, PropertyValue value) {
        require_component(component);
        components_.get(component).set_property(std::move(key), std::move(value));
    }

    /** Set or replace a typed electrical attribute on an existing component instance. */
    void set_component_electrical_attribute(ComponentId component,
                                            const ElectricalAttributeSpec &spec,
                                            ElectricalAttributeValue value) {
        require_component(component);
        require_attribute_owner(spec, ElectricalAttributeOwner::ComponentInstance);
        components_.get(component).set_electrical_attribute(spec, std::move(value));
    }

    /** Set or replace a typed electrical attribute on an existing reusable pin definition. */
    void set_pin_definition_electrical_attribute(PinDefId pin_definition,
                                                 const ElectricalAttributeSpec &spec,
                                                 ElectricalAttributeValue value) {
        require_pin_definition(pin_definition);
        require_attribute_owner(spec, ElectricalAttributeOwner::PinSpec);
        pin_definitions_.get(pin_definition).set_electrical_attribute(spec, std::move(value));
    }

    /** Assign a selected physical implementation to an existing component instance. */
    void select_physical_part(ComponentId component, PhysicalPart physical_part) {
        require_component(component);
        require_physical_part_matches_component_definition(components_.get(component).definition(),
                                                           physical_part);

        components_.get(component).select_physical_part(std::move(physical_part));
    }

    /** Set or replace a typed electrical attribute on a component's selected physical part. */
    void set_selected_part_electrical_attribute(ComponentId component,
                                                const ElectricalAttributeSpec &spec,
                                                ElectricalAttributeValue value) {
        require_component(component);
        require_attribute_owner(spec, ElectricalAttributeOwner::SelectedPart);
        components_.get(component).set_selected_part_electrical_attribute(spec, std::move(value));
    }

    /** Set or replace a typed electrical attribute on an existing net. */
    void set_net_electrical_attribute(NetId net, const ElectricalAttributeSpec &spec,
                                      ElectricalAttributeValue value) {
        require_net(net);
        require_attribute_owner(spec, ElectricalAttributeOwner::Net);
        nets_.get(net).set_electrical_attribute(spec, std::move(value));
    }

    /** Return the selected physical implementation for a component, if one has been assigned. */
    [[nodiscard]] const std::optional<PhysicalPart> &
    selected_physical_part(ComponentId component) const {
        require_component(component);
        return components_.get(component).selected_physical_part();
    }

    /** Return the net currently connected to the pin, if any. */
    [[nodiscard]] std::optional<NetId> net_of(PinId pin) const {
        require_pin(pin);
        return net_of_existing_pin(pin);
    }

    /** Return the component with this reference designator, if it exists. */
    [[nodiscard]] std::optional<ComponentId>
    component_by_reference(const ReferenceDesignator &reference) const {
        for (std::size_t index = 0; index < components_.size(); ++index) {
            const auto component_id = ComponentId{index};
            if (components_.get(component_id).reference() == reference) {
                return component_id;
            }
        }

        return std::nullopt;
    }

    /** Return the module definition with this name, if it exists. */
    [[nodiscard]] std::optional<ModuleDefId>
    module_definition_by_name(const ModuleName &name) const {
        for (std::size_t index = 0; index < module_definitions_.size(); ++index) {
            const auto id = ModuleDefId{index};
            if (module_definitions_.get(id).name() == name) {
                return id;
            }
        }

        return std::nullopt;
    }

    /** Return the root-level module instance with this name, if it exists. */
    [[nodiscard]] std::optional<ModuleInstanceId>
    module_instance_by_name(const ModuleInstanceName &name) const {
        for (std::size_t index = 0; index < module_instances_.size(); ++index) {
            const auto id = ModuleInstanceId{index};
            if (module_instances_.get(id).name() == name) {
                return id;
            }
        }

        return std::nullopt;
    }

    /** Return a template-local net in a module definition by name, if it exists. */
    [[nodiscard]] std::optional<TemplateNetDefId> template_net_by_name(ModuleDefId module,
                                                                       const NetName &name) const {
        require_module_definition(module);
        for (const auto net : module_definitions_.get(module).template_nets()) {
            if (template_net_definitions_.get(net).name() == name) {
                return net;
            }
        }

        return std::nullopt;
    }

    /** Return a port in a module definition by name, if it exists. */
    [[nodiscard]] std::optional<PortDefId> port_by_name(ModuleDefId module,
                                                        const PortName &name) const {
        require_module_definition(module);
        for (const auto port : module_definitions_.get(module).ports()) {
            if (port_definitions_.get(port).name() == name) {
                return port;
            }
        }

        return std::nullopt;
    }

    /** Return a module component by local reference designator, if it exists. */
    [[nodiscard]] std::optional<ModuleComponentId>
    module_component_by_reference(ModuleDefId module, const ReferenceDesignator &reference) const {
        require_module_definition(module);
        for (const auto component : module_definitions_.get(module).components()) {
            if (module_component_templates_.get(component).reference() == reference) {
                return component;
            }
        }

        return std::nullopt;
    }

    /** Return the template net connected to a module component pin, if any. */
    [[nodiscard]] std::optional<TemplateNetDefId>
    template_net_for(ModuleDefId module, ModuleComponentId component, PinDefId pin) const {
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

    /** Return the explicit binding for a module instance port, if it exists. */
    [[nodiscard]] std::optional<PortBindingId> port_binding_for(ModuleInstanceId instance,
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

    /** Return the concrete component created for a module instance component template, if any. */
    [[nodiscard]] std::optional<ComponentId>
    concrete_component_for(ModuleInstanceId instance, ModuleComponentId component) const {
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

    /** Return whether a net is concrete module-origin net. */
    [[nodiscard]] bool is_module_origin_net(NetId net) const {
        require_net(net);
        return std::find(module_origin_nets_.begin(), module_origin_nets_.end(), net) !=
               module_origin_nets_.end();
    }

    /** Return whether a component is a concrete module-origin component. */
    [[nodiscard]] bool is_module_origin_component(ComponentId component) const {
        require_component(component);
        return std::find(module_origin_components_.begin(), module_origin_components_.end(),
                         component) != module_origin_components_.end();
    }

    /** Return the concrete net created for a module instance template-local net, if any. */
    [[nodiscard]] std::optional<NetId> concrete_net_for(ModuleInstanceId instance,
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

    /** Return the net with this name, if it exists. */
    [[nodiscard]] std::optional<NetId> net_by_name(const NetName &name) const {
        for (std::size_t index = 0; index < nets_.size(); ++index) {
            const auto net_id = NetId{index};
            if (nets_.get(net_id).name() == name) {
                return net_id;
            }
        }

        return std::nullopt;
    }

    /** Return concrete pins belonging to a component in deterministic creation order. */
    [[nodiscard]] std::vector<PinId> pins_for(ComponentId component) const {
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

    /** Return a component pin by reusable pin definition name, if it exists. */
    [[nodiscard]] std::optional<PinId> pin_by_name(ComponentId component,
                                                   std::string_view name) const {
        for (const auto pin_id : pins_for(component)) {
            const auto definition = pins_.get(pin_id).definition();
            if (pin_definitions_.get(definition).name() == name) {
                return pin_id;
            }
        }

        return std::nullopt;
    }

    /** Return a component pin by reusable pin definition, if it exists. */
    [[nodiscard]] std::optional<PinId> pin_by_definition(ComponentId component,
                                                         PinDefId definition) const {
        for (const auto pin_id : pins_for(component)) {
            if (pins_.get(pin_id).definition() == definition) {
                return pin_id;
            }
        }

        return std::nullopt;
    }

    /** Return a component pin by reusable pin definition number, if it exists. */
    [[nodiscard]] std::optional<PinId> pin_by_number(ComponentId component,
                                                     std::string_view number) const {
        for (const auto pin_id : pins_for(component)) {
            const auto definition = pins_.get(pin_id).definition();
            if (pin_definitions_.get(definition).number() == number) {
                return pin_id;
            }
        }

        return std::nullopt;
    }

    /** Return a reusable pin definition by ID. */
    [[nodiscard]] const PinDefinition &pin_definition(PinDefId id) const {
        return pin_definitions_.get(id);
    }

    /** Return a reusable component definition by ID. */
    [[nodiscard]] const ComponentDefinition &component_definition(ComponentDefId id) const {
        return component_definitions_.get(id);
    }

    /** Return a component instance by ID. */
    [[nodiscard]] const ComponentInstance &component(ComponentId id) const {
        return components_.get(id);
    }

    /** Return a concrete pin instance by ID. */
    [[nodiscard]] const PinInstance &pin(PinId id) const { return pins_.get(id); }

    /** Return a canonical net by ID. */
    [[nodiscard]] const Net &net(NetId id) const { return nets_.get(id); }

    /** Return a reusable module definition by ID. */
    [[nodiscard]] const ModuleDefinition &module_definition(ModuleDefId id) const {
        return module_definitions_.get(id);
    }

    /** Return a template-local net definition by ID. */
    [[nodiscard]] const TemplateNetDefinition &template_net_definition(TemplateNetDefId id) const {
        return template_net_definitions_.get(id);
    }

    /** Return a module port definition by ID. */
    [[nodiscard]] const PortDefinition &port_definition(PortDefId id) const {
        return port_definitions_.get(id);
    }

    /** Return a module component template by ID. */
    [[nodiscard]] const ModuleComponentTemplate &
    module_component_template(ModuleComponentId id) const {
        return module_component_templates_.get(id);
    }

    /** Return a root-level module instance by ID. */
    [[nodiscard]] const ModuleInstance &module_instance(ModuleInstanceId id) const {
        return module_instances_.get(id);
    }

    /** Return an explicit module port binding by ID. */
    [[nodiscard]] const PortBinding &port_binding(PortBindingId id) const {
        return port_bindings_.get(id);
    }

    /** Return the number of reusable pin definitions. */
    [[nodiscard]] std::size_t pin_definition_count() const noexcept {
        return pin_definitions_.size();
    }

    /** Return the number of reusable component definitions. */
    [[nodiscard]] std::size_t component_definition_count() const noexcept {
        return component_definitions_.size();
    }

    /** Return the number of component instances. */
    [[nodiscard]] std::size_t component_count() const noexcept { return components_.size(); }

    /** Return the number of concrete pin instances. */
    [[nodiscard]] std::size_t pin_count() const noexcept { return pins_.size(); }

    /** Return the number of canonical nets. */
    [[nodiscard]] std::size_t net_count() const noexcept { return nets_.size(); }

    /** Return the number of reusable module definitions. */
    [[nodiscard]] std::size_t module_definition_count() const noexcept {
        return module_definitions_.size();
    }

    /** Return the number of template-local net definitions. */
    [[nodiscard]] std::size_t template_net_definition_count() const noexcept {
        return template_net_definitions_.size();
    }

    /** Return the number of module port definitions. */
    [[nodiscard]] std::size_t port_definition_count() const noexcept {
        return port_definitions_.size();
    }

    /** Return the number of module component templates. */
    [[nodiscard]] std::size_t module_component_count() const noexcept {
        return module_component_templates_.size();
    }

    /** Return the number of module pin template connections. */
    [[nodiscard]] std::size_t module_pin_connection_count() const noexcept {
        return module_pin_connections_.size();
    }

    /** Return the number of root-level module instances. */
    [[nodiscard]] std::size_t module_instance_count() const noexcept {
        return module_instances_.size();
    }

    /** Return the number of explicit module port bindings. */
    [[nodiscard]] std::size_t port_binding_count() const noexcept { return port_bindings_.size(); }

  private:
    void require_pin_definition(PinDefId pin_definition) const {
        if (!pin_definitions_.contains(pin_definition)) {
            throw std::out_of_range{"Pin definition ID does not belong to this circuit"};
        }
    }

    void require_component_definition(ComponentDefId component_definition) const {
        if (!component_definitions_.contains(component_definition)) {
            throw std::out_of_range{"Component definition ID does not belong to this circuit"};
        }
    }

    void require_component(ComponentId component) const {
        if (!components_.contains(component)) {
            throw std::out_of_range{"Component ID does not belong to this circuit"};
        }
    }

    void require_module_definition(ModuleDefId module) const {
        if (!module_definitions_.contains(module)) {
            throw std::out_of_range{"Module definition ID does not belong to this circuit"};
        }
    }

    void require_template_net(TemplateNetDefId net) const {
        if (!template_net_definitions_.contains(net)) {
            throw std::out_of_range{"Template net definition ID does not belong to this circuit"};
        }
    }

    void require_port(PortDefId port) const {
        if (!port_definitions_.contains(port)) {
            throw std::out_of_range{"Port definition ID does not belong to this circuit"};
        }
    }

    void require_module_component(ModuleComponentId component) const {
        if (!module_component_templates_.contains(component)) {
            throw std::out_of_range{"Module component ID does not belong to this circuit"};
        }
    }

    void require_module_instance(ModuleInstanceId instance) const {
        if (!module_instances_.contains(instance)) {
            throw std::out_of_range{"Module instance ID does not belong to this circuit"};
        }
    }

    void require_template_net_in_module(ModuleDefId module, TemplateNetDefId net) const {
        require_module_definition(module);
        require_template_net(net);
        const auto &nets = module_definitions_.get(module).template_nets();
        if (std::find(nets.begin(), nets.end(), net) == nets.end()) {
            throw std::logic_error{"Template net does not belong to module definition"};
        }
    }

    void require_port_in_module(ModuleDefId module, PortDefId port) const {
        require_module_definition(module);
        require_port(port);
        const auto &ports = module_definitions_.get(module).ports();
        if (std::find(ports.begin(), ports.end(), port) == ports.end()) {
            throw std::logic_error{"Port does not belong to module definition"};
        }
    }

    void require_module_component_in_module(ModuleDefId module, ModuleComponentId component) const {
        require_module_definition(module);
        require_module_component(component);
        const auto &components = module_definitions_.get(module).components();
        if (std::find(components.begin(), components.end(), component) == components.end()) {
            throw std::logic_error{"Module component does not belong to module definition"};
        }
    }

    [[nodiscard]] bool require_template_net_in_module_if_present(ModuleDefId module,
                                                                 TemplateNetDefId net) const {
        require_module_definition(module);
        require_template_net(net);
        const auto &nets = module_definitions_.get(module).template_nets();
        return std::find(nets.begin(), nets.end(), net) != nets.end();
    }

    [[nodiscard]] bool
    require_module_component_in_module_if_present(ModuleDefId module,
                                                  ModuleComponentId component) const {
        require_module_definition(module);
        require_module_component(component);
        const auto &components = module_definitions_.get(module).components();
        return std::find(components.begin(), components.end(), component) != components.end();
    }

    void require_pin_in_module_component(ModuleComponentId component, PinDefId pin) const {
        require_module_component(component);
        require_pin_definition(pin);
        const auto &definition =
            component_definitions_.get(module_component_templates_.get(component).definition());
        const auto &pins = definition.pins();
        if (std::find(pins.begin(), pins.end(), pin) == pins.end()) {
            throw std::logic_error{"Pin definition does not belong to module component definition"};
        }
    }

    void require_pin(PinId pin) const {
        if (!pins_.contains(pin)) {
            throw std::out_of_range{"Pin ID does not belong to this circuit"};
        }
    }

    void require_net(NetId net) const {
        if (!nets_.contains(net)) {
            throw std::out_of_range{"Net ID does not belong to this circuit"};
        }
    }

    static void require_attribute_owner(const ElectricalAttributeSpec &spec,
                                        ElectricalAttributeOwner expected) {
        if (spec.owner() != expected) {
            throw std::logic_error{"Electrical attribute spec owner is not valid here"};
        }
    }

    void
    require_physical_part_matches_component_definition(ComponentDefId component_definition,
                                                       const PhysicalPart &physical_part) const {
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
                throw std::logic_error{
                    "Physical part must map every pin in the component definition"};
            }
        }
    }

    [[nodiscard]] std::optional<NetId> net_of_existing_pin(PinId pin) const {
        for (std::size_t index = 0; index < nets_.size(); ++index) {
            const auto net = NetId{index};
            if (nets_.get(net).contains(pin)) {
                return net;
            }
        }

        return std::nullopt;
    }

    EntityTable<PinDefinition, PinDefId> pin_definitions_;
    EntityTable<ComponentDefinition, ComponentDefId> component_definitions_;
    EntityTable<ComponentInstance, ComponentId> components_;
    EntityTable<PinInstance, PinId> pins_;
    EntityTable<Net, NetId> nets_;
    EntityTable<ModuleDefinition, ModuleDefId> module_definitions_;
    EntityTable<TemplateNetDefinition, TemplateNetDefId> template_net_definitions_;
    EntityTable<ModuleComponentTemplate, ModuleComponentId> module_component_templates_;
    EntityTable<PortDefinition, PortDefId> port_definitions_;
    EntityTable<ModuleInstance, ModuleInstanceId> module_instances_;
    EntityTable<PortBinding, PortBindingId> port_bindings_;
    std::vector<ModulePinConnection> module_pin_connections_;
    std::vector<ModuleNetOrigin> module_net_origins_;
    std::vector<NetId> module_origin_nets_;
    std::vector<ModuleComponentOrigin> module_component_origins_;
    std::vector<ComponentId> module_origin_components_;
};

} // namespace volt
