#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>

namespace volt {

/** Read-only view over the canonical logical circuit model. */
class CircuitView {
  public:
    /** Construct a view over an existing logical circuit. */
    explicit CircuitView(const Circuit &circuit) noexcept : circuit_{&circuit} {}

    /** Return whether two views reference the same logical circuit object. */
    [[nodiscard]] bool references_same_circuit(CircuitView other) const noexcept {
        return circuit_ == other.circuit_;
    }

    /** Return the selected physical implementation for a component, if assigned. */
    [[nodiscard]] const std::optional<PhysicalPart> &
    selected_physical_part(ComponentId component) const {
        return circuit_->selected_physical_part(component);
    }

    /** Return the net currently connected to the pin, if any. */
    [[nodiscard]] std::optional<NetId> net_of(PinId pin) const { return circuit_->net_of(pin); }

    /** Return the component with this reference designator, if it exists. */
    [[nodiscard]] std::optional<ComponentId>
    component_by_reference(const ReferenceDesignator &reference) const {
        return circuit_->component_by_reference(reference);
    }

    /** Return the module definition with this name, if it exists. */
    [[nodiscard]] std::optional<ModuleDefId>
    module_definition_by_name(const ModuleName &name) const {
        return circuit_->module_definition_by_name(name);
    }

    /** Return the root-level module instance with this name, if it exists. */
    [[nodiscard]] std::optional<ModuleInstanceId>
    module_instance_by_name(const ModuleInstanceName &name) const {
        return circuit_->module_instance_by_name(name);
    }

    /** Return a template-local net in a module definition by name, if it exists. */
    [[nodiscard]] std::optional<TemplateNetDefId> template_net_by_name(ModuleDefId module,
                                                                       const NetName &name) const {
        return circuit_->template_net_by_name(module, name);
    }

    /** Return a port in a module definition by name, if it exists. */
    [[nodiscard]] std::optional<PortDefId> port_by_name(ModuleDefId module,
                                                        const PortName &name) const {
        return circuit_->port_by_name(module, name);
    }

    /** Return a module component by local reference designator, if it exists. */
    [[nodiscard]] std::optional<ModuleComponentId>
    module_component_by_reference(ModuleDefId module, const ReferenceDesignator &reference) const {
        return circuit_->module_component_by_reference(module, reference);
    }

    /** Return the template net connected to a module component pin, if any. */
    [[nodiscard]] std::optional<TemplateNetDefId>
    template_net_for(ModuleDefId module, ModuleComponentId component, PinDefId pin) const {
        return circuit_->template_net_for(module, component, pin);
    }

    /** Return module-local pin connections for one module definition. */
    [[nodiscard]] std::vector<ModulePinConnection>
    module_pin_connections(ModuleDefId module) const {
        return circuit_->module_pin_connections(module);
    }

    /** Return the explicit binding for a module instance port, if it exists. */
    [[nodiscard]] std::optional<PortBindingId> port_binding_for(ModuleInstanceId instance,
                                                                PortDefId port) const {
        return circuit_->port_binding_for(instance, port);
    }

    /** Return explicit port binding IDs for one module instance in module port order. */
    [[nodiscard]] std::vector<PortBindingId> port_bindings_for(ModuleInstanceId instance) const {
        return circuit_->port_bindings_for(instance);
    }

    /** Return the concrete component created for a module component template, if any. */
    [[nodiscard]] std::optional<ComponentId>
    concrete_component_for(ModuleInstanceId instance, ModuleComponentId component) const {
        return circuit_->concrete_component_for(instance, component);
    }

    /** Return concrete net origins for one module instance in template-net order. */
    [[nodiscard]] std::vector<std::pair<TemplateNetDefId, NetId>>
    module_net_origins(ModuleInstanceId instance) const {
        return circuit_->module_net_origins(instance);
    }

    /** Return concrete component origins for one module instance in component order. */
    [[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>>
    module_component_origins(ModuleInstanceId instance) const {
        return circuit_->module_component_origins(instance);
    }

    /** Return whether a net is concrete module-origin net. */
    [[nodiscard]] bool is_module_origin_net(NetId net) const {
        return circuit_->is_module_origin_net(net);
    }

    /** Return whether this net has explicit author intent as a named/exported stub. */
    [[nodiscard]] bool is_intentional_stub_net(NetId net) const {
        return circuit_->is_intentional_stub_net(net);
    }

    /** Return whether this concrete pin has explicit no-connect author intent. */
    [[nodiscard]] bool is_intentional_no_connect_pin(PinId pin) const {
        return circuit_->is_intentional_no_connect_pin(pin);
    }

    /** Return intentional stub-net assertions in deterministic insertion order. */
    [[nodiscard]] const std::vector<NetId> &intentional_stub_nets() const noexcept {
        return circuit_->intentional_stub_nets();
    }

    /** Return intentional no-connect pin assertions in deterministic insertion order. */
    [[nodiscard]] const std::vector<PinId> &intentional_no_connect_pins() const noexcept {
        return circuit_->intentional_no_connect_pins();
    }

    /** Return whether a component is a concrete module-origin component. */
    [[nodiscard]] bool is_module_origin_component(ComponentId component) const {
        return circuit_->is_module_origin_component(component);
    }

    /** Return the concrete net created for a module instance template-local net, if any. */
    [[nodiscard]] std::optional<NetId> concrete_net_for(ModuleInstanceId instance,
                                                        TemplateNetDefId template_net) const {
        return circuit_->concrete_net_for(instance, template_net);
    }

    /** Return the net with this name, if it exists. */
    [[nodiscard]] std::optional<NetId> net_by_name(const NetName &name) const {
        return circuit_->net_by_name(name);
    }

    /** Return concrete pins belonging to a component in deterministic creation order. */
    [[nodiscard]] std::vector<PinId> pins_for(ComponentId component) const {
        return circuit_->pins_for(component);
    }

    /** Return a component pin by reusable pin definition name, if it exists. */
    [[nodiscard]] std::optional<PinId> pin_by_name(ComponentId component,
                                                   std::string_view name) const {
        return circuit_->pin_by_name(component, name);
    }

    /** Return a component pin by reusable pin definition, if it exists. */
    [[nodiscard]] std::optional<PinId> pin_by_definition(ComponentId component,
                                                         PinDefId definition) const {
        return circuit_->pin_by_definition(component, definition);
    }

    /** Return a component pin by reusable pin definition number, if it exists. */
    [[nodiscard]] std::optional<PinId> pin_by_number(ComponentId component,
                                                     std::string_view number) const {
        return circuit_->pin_by_number(component, number);
    }

    /** Return a reusable pin definition by ID. */
    [[nodiscard]] const PinDefinition &pin_definition(PinDefId id) const {
        return circuit_->pin_definition(id);
    }

    /** Return a reusable component definition by ID. */
    [[nodiscard]] const ComponentDefinition &component_definition(ComponentDefId id) const {
        return circuit_->component_definition(id);
    }

    /** Return a component instance by ID. */
    [[nodiscard]] const ComponentInstance &component(ComponentId id) const {
        return circuit_->component(id);
    }

    /** Return a concrete pin instance by ID. */
    [[nodiscard]] const PinInstance &pin(PinId id) const { return circuit_->pin(id); }

    /** Return a canonical net by ID. */
    [[nodiscard]] const Net &net(NetId id) const { return circuit_->net(id); }

    /** Return a reusable module definition by ID. */
    [[nodiscard]] const ModuleDefinition &module_definition(ModuleDefId id) const {
        return circuit_->module_definition(id);
    }

    /** Return a template-local net definition by ID. */
    [[nodiscard]] const TemplateNetDefinition &template_net_definition(TemplateNetDefId id) const {
        return circuit_->template_net_definition(id);
    }

    /** Return a module port definition by ID. */
    [[nodiscard]] const PortDefinition &port_definition(PortDefId id) const {
        return circuit_->port_definition(id);
    }

    /** Return a module component template by ID. */
    [[nodiscard]] const ModuleComponentTemplate &
    module_component_template(ModuleComponentId id) const {
        return circuit_->module_component_template(id);
    }

    /** Return a root-level module instance by ID. */
    [[nodiscard]] const ModuleInstance &module_instance(ModuleInstanceId id) const {
        return circuit_->module_instance(id);
    }

    /** Return an explicit module port binding by ID. */
    [[nodiscard]] const PortBinding &port_binding(PortBindingId id) const {
        return circuit_->port_binding(id);
    }

    /** Return the number of reusable pin definitions. */
    [[nodiscard]] std::size_t pin_definition_count() const noexcept {
        return circuit_->pin_definition_count();
    }

    /** Return the number of reusable component definitions. */
    [[nodiscard]] std::size_t component_definition_count() const noexcept {
        return circuit_->component_definition_count();
    }

    /** Return the number of component instances. */
    [[nodiscard]] std::size_t component_count() const noexcept {
        return circuit_->component_count();
    }

    /** Return the number of concrete pin instances. */
    [[nodiscard]] std::size_t pin_count() const noexcept { return circuit_->pin_count(); }

    /** Return the number of canonical nets. */
    [[nodiscard]] std::size_t net_count() const noexcept { return circuit_->net_count(); }

    /** Return the number of reusable module definitions. */
    [[nodiscard]] std::size_t module_definition_count() const noexcept {
        return circuit_->module_definition_count();
    }

    /** Return the number of template-local net definitions. */
    [[nodiscard]] std::size_t template_net_definition_count() const noexcept {
        return circuit_->template_net_definition_count();
    }

    /** Return the number of module port definitions. */
    [[nodiscard]] std::size_t port_definition_count() const noexcept {
        return circuit_->port_definition_count();
    }

    /** Return the number of module component templates. */
    [[nodiscard]] std::size_t module_component_count() const noexcept {
        return circuit_->module_component_count();
    }

    /** Return the number of module pin template connections. */
    [[nodiscard]] std::size_t module_pin_connection_count() const noexcept {
        return circuit_->module_pin_connection_count();
    }

    /** Return the number of root-level module instances. */
    [[nodiscard]] std::size_t module_instance_count() const noexcept {
        return circuit_->module_instance_count();
    }

    /** Return the number of explicit module port bindings. */
    [[nodiscard]] std::size_t port_binding_count() const noexcept {
        return circuit_->port_binding_count();
    }

  private:
    const Circuit *circuit_;
};

} // namespace volt
