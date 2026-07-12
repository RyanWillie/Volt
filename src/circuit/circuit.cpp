#include <volt/circuit/circuit.hpp>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/errors.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace volt {

Circuit::ConnectivityMutator::ConnectivityMutator(Circuit &circuit, MutatorKey) noexcept
    : circuit_{circuit} {}

Circuit::HierarchyMutator::HierarchyMutator(Circuit &circuit, MutatorKey) noexcept
    : circuit_{circuit} {}

Circuit::ElectricalMutator::ElectricalMutator(Circuit &circuit, MutatorKey) noexcept
    : circuit_{circuit} {}

Circuit::IntentMutator::IntentMutator(Circuit &circuit, MutatorKey) noexcept : circuit_{circuit} {}

Circuit::NetClassMutator::NetClassMutator(Circuit &circuit, MutatorKey) noexcept
    : circuit_{circuit} {}

[[nodiscard]] Circuit::ConnectivityMutator Circuit::connectivity() & noexcept {
    return ConnectivityMutator{*this, MutatorKey::make()};
}

[[nodiscard]] Circuit::HierarchyMutator Circuit::hierarchy() & noexcept {
    return HierarchyMutator{*this, MutatorKey::make()};
}

[[nodiscard]] Circuit::ElectricalMutator Circuit::electrical() & noexcept {
    return ElectricalMutator{*this, MutatorKey::make()};
}

[[nodiscard]] Circuit::IntentMutator Circuit::intent() & noexcept {
    return IntentMutator{*this, MutatorKey::make()};
}

[[nodiscard]] Circuit::NetClassMutator Circuit::net_classes() & noexcept {
    return NetClassMutator{*this, MutatorKey::make()};
}

[[nodiscard]] PinDefId Circuit::ConnectivityMutator::add_pin_definition(PinDefinition definition) {
    return circuit_.connectivity_.add_pin_definition(std::move(definition));
}

[[nodiscard]] ComponentDefId
Circuit::ConnectivityMutator::add_component_definition(ComponentDefinition definition) {
    return circuit_.connectivity_.add_component_definition(std::move(definition));
}

[[nodiscard]] ComponentId Circuit::ConnectivityMutator::add_component(ComponentInstance component) {
    return circuit_.instantiate_component(
        component.definition(),
        ComponentInstanceSpec{component.reference(), component.properties()});
}

[[nodiscard]] PinId Circuit::ConnectivityMutator::add_pin(PinInstance pin) {
    return circuit_.connectivity_.add_pin(pin);
}

[[nodiscard]] NetId Circuit::ConnectivityMutator::add_net(Net net) {
    return circuit_.connectivity_.add_net(std::move(net));
}

void Circuit::ConnectivityMutator::set_component_property(ComponentId component, PropertyKey key,
                                                          PropertyValue value) {
    circuit_.connectivity_.set_component_property(component, std::move(key), std::move(value));
}

[[nodiscard]] ModuleDefId
Circuit::HierarchyMutator::add_module_definition(ModuleDefinition definition) {
    return circuit_.hierarchy_.add_module_definition(std::move(definition));
}

[[nodiscard]] TemplateNetDefId
Circuit::HierarchyMutator::add_template_net(ModuleDefId module, TemplateNetDefinition net) {
    return circuit_.hierarchy_.add_template_net(module, std::move(net));
}

[[nodiscard]] PortDefId Circuit::HierarchyMutator::add_port_definition(ModuleDefId module,
                                                                       PortDefinition port) {
    return circuit_.hierarchy_.add_port_definition(module, std::move(port));
}

[[nodiscard]] ModuleComponentId
Circuit::HierarchyMutator::add_module_component(ModuleDefId module,
                                                ModuleComponentTemplate component) {
    circuit_.require_component_definition(component.definition());
    return circuit_.hierarchy_.add_module_component(module, std::move(component));
}

bool Circuit::HierarchyMutator::connect_module_pin(ModuleDefId module, TemplateNetDefId net,
                                                   ModuleComponentId component, PinDefId pin) {
    circuit_.require_pin_in_module_component(component, pin);
    return circuit_.hierarchy_.connect_module_pin(module, net, component, pin);
}

[[nodiscard]] ComponentDefId Circuit::define_component(ComponentSpec spec) {
    auto pin_definitions = std::vector<PinDefinition>{};
    auto pin_attributes = std::vector<ElectricalAttributeMap>{};
    auto pin_ids = std::vector<PinDefId>{};
    pin_definitions.reserve(spec.pins.size());
    pin_attributes.reserve(spec.pins.size());
    pin_ids.reserve(spec.pins.size());

    const auto first_pin_index = connectivity_.pin_definition_count();
    for (std::size_t index = 0; index < spec.pins.size(); ++index) {
        const auto &pin = spec.pins[index];
        pin_definitions.emplace_back(pin.name, pin.number, pin.requirement, pin.terminal_kind,
                                     pin.direction, pin.signal_domain, pin.drive_kind,
                                     pin.polarity);
        pin_attributes.push_back(ElectricalStorage::preflight_attributes(
            pin.electrical_attributes, ElectricalAttributeOwner::PinSpec));
        pin_ids.emplace_back(first_pin_index + index);
    }

    auto definition =
        ComponentDefinition{std::move(spec.name), pin_ids, std::move(spec.properties),
                            std::move(spec.source), std::move(spec.schematic_symbols)};
    for (std::size_t index = 0; index < pin_definitions.size(); ++index) {
        const auto pin = connectivity_.add_pin_definition(std::move(pin_definitions[index]));
        electrical_.restore_pin_definition_attributes(pin, std::move(pin_attributes[index]));
    }
    return connectivity_.add_component_definition(std::move(definition));
}

[[nodiscard]] ModuleDefId Circuit::define_module(ModuleSpec spec) {
    if (hierarchy_.module_definition_by_name(spec.name).has_value()) {
        throw KernelLogicError{ErrorCode::DuplicateName, "Module definition name already exists"};
    }

    for (std::size_t index = 0; index < spec.template_nets.size(); ++index) {
        const auto duplicate =
            std::find_if(spec.template_nets.begin(),
                         spec.template_nets.begin() + static_cast<std::ptrdiff_t>(index),
                         [&spec, index](const auto &candidate) {
                             return candidate.name() == spec.template_nets[index].name();
                         });
        if (duplicate != spec.template_nets.begin() + static_cast<std::ptrdiff_t>(index)) {
            throw KernelLogicError{ErrorCode::DuplicateName,
                                   "Template net name already exists in module definition"};
        }
    }

    for (std::size_t index = 0; index < spec.components.size(); ++index) {
        require_component_definition(spec.components[index].definition());
        const auto duplicate = std::find_if(
            spec.components.begin(), spec.components.begin() + static_cast<std::ptrdiff_t>(index),
            [&spec, index](const auto &candidate) {
                return candidate.reference() == spec.components[index].reference();
            });
        if (duplicate != spec.components.begin() + static_cast<std::ptrdiff_t>(index)) {
            throw KernelLogicError{ErrorCode::DuplicateName,
                                   "Module component reference designator already exists"};
        }
    }

    auto resolved_connections =
        std::vector<std::pair<std::size_t, std::pair<std::size_t, PinDefId>>>{};
    resolved_connections.reserve(spec.connections.size());
    for (const auto &connection : spec.connections) {
        const auto net = std::find_if(
            spec.template_nets.begin(), spec.template_nets.end(),
            [&connection](const auto &candidate) { return candidate.name() == connection.net; });
        if (net == spec.template_nets.end()) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Module connection net does not exist in module spec"};
        }
        const auto component = std::find_if(
            spec.components.begin(), spec.components.end(), [&connection](const auto &candidate) {
                return candidate.reference() == connection.component;
            });
        if (component == spec.components.end()) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Module connection component does not exist in module spec"};
        }
        require_pin_definition(connection.pin);
        const auto &component_pins = component_definition(component->definition()).pins();
        if (std::find(component_pins.begin(), component_pins.end(), connection.pin) ==
            component_pins.end()) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Pin definition does not belong to module component definition"};
        }

        const auto net_index = static_cast<std::size_t>(net - spec.template_nets.begin());
        const auto component_index = static_cast<std::size_t>(component - spec.components.begin());
        const auto already_connected =
            std::any_of(resolved_connections.begin(), resolved_connections.end(),
                        [component_index, &connection](const auto &resolved) {
                            return resolved.second.first == component_index &&
                                   resolved.second.second == connection.pin;
                        });
        if (already_connected) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Module component pin is already connected"};
        }
        resolved_connections.emplace_back(net_index, std::pair{component_index, connection.pin});
    }

    auto resolved_ports = std::vector<PortDefinition>{};
    resolved_ports.reserve(spec.ports.size());
    const auto first_template_net_index = hierarchy_.template_net_definition_count();
    for (std::size_t index = 0; index < spec.ports.size(); ++index) {
        const auto &port = spec.ports[index];
        const auto duplicate = std::find_if(
            spec.ports.begin(), spec.ports.begin() + static_cast<std::ptrdiff_t>(index),
            [&port](const auto &candidate) { return candidate.name == port.name; });
        if (duplicate != spec.ports.begin() + static_cast<std::ptrdiff_t>(index)) {
            throw KernelLogicError{ErrorCode::DuplicateName,
                                   "Port name already exists in module definition"};
        }
        const auto internal_net = std::find_if(
            spec.template_nets.begin(), spec.template_nets.end(),
            [&port](const auto &candidate) { return candidate.name() == port.internal_net; });
        if (internal_net == spec.template_nets.end()) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Module port internal net does not exist in module spec"};
        }
        const auto net_index = static_cast<std::size_t>(internal_net - spec.template_nets.begin());
        resolved_ports.emplace_back(port.name,
                                    TemplateNetDefId{first_template_net_index + net_index},
                                    port.role, port.required);
    }

    const auto module = hierarchy_.add_module_definition(ModuleDefinition{std::move(spec.name)});
    auto template_nets = std::vector<TemplateNetDefId>{};
    template_nets.reserve(spec.template_nets.size());
    for (auto &net : spec.template_nets) {
        template_nets.push_back(hierarchy_.add_template_net(module, std::move(net)));
    }
    auto components = std::vector<ModuleComponentId>{};
    components.reserve(spec.components.size());
    for (auto &component : spec.components) {
        components.push_back(hierarchy_.add_module_component(module, std::move(component)));
    }
    for (const auto &[net_index, component_and_pin] : resolved_connections) {
        [[maybe_unused]] const auto changed = hierarchy_.connect_module_pin(
            module, template_nets[net_index], components[component_and_pin.first],
            component_and_pin.second);
    }
    for (auto &port : resolved_ports) {
        [[maybe_unused]] const auto id = hierarchy_.add_port_definition(module, std::move(port));
    }

    return module;
}

[[nodiscard]] ComponentId Circuit::instantiate_component(ComponentDefId definition,
                                                         ComponentInstanceSpec spec) {
    require_component_definition(definition);
    if (queries::component_by_reference(*this, spec.reference).has_value()) {
        throw KernelLogicError{ErrorCode::DuplicateName,
                               "Component reference designator already exists"};
    }

    return connectivity_.instantiate_component(definition, std::move(spec.reference),
                                               std::move(spec.properties));
}

[[nodiscard]] NetId Circuit::add_net(NetSpec spec) {
    if (queries::net_by_name(*this, spec.name).has_value()) {
        throw KernelLogicError{ErrorCode::DuplicateName, "Net name already exists"};
    }
    auto net = Net{std::move(spec.name), spec.kind};
    return connectivity_.add_net(std::move(net));
}

[[nodiscard]] NetClassId Circuit::define_net_class(NetClassSpec spec) {
    return net_classes_.add_net_class(std::move(spec.net_class));
}

[[nodiscard]] ModuleInstanceId Circuit::instantiate_root_module(ModuleDefId definition,
                                                                ModuleInstanceName name) {
    require_module_definition(definition);
    if (queries::module_instance_by_name(*this, name).has_value()) {
        throw KernelLogicError{ErrorCode::DuplicateName, "Module instance name already exists"};
    }

    std::vector<Net> concrete_nets;
    for (const auto template_net : hierarchy_.module_definition(definition).template_nets()) {
        const auto &template_net_definition = hierarchy_.template_net_definition(template_net);
        auto concrete_name = NetName{name.value() + "/" + template_net_definition.name().value()};
        if (queries::net_by_name(*this, concrete_name).has_value()) {
            throw KernelLogicError{ErrorCode::DuplicateName,
                                   "Module instance concrete net name already exists"};
        }
        concrete_nets.emplace_back(std::move(concrete_name), template_net_definition.kind());
    }

    std::vector<ComponentInstance> concrete_components;
    for (const auto component : hierarchy_.module_definition(definition).components()) {
        const auto &component_template = hierarchy_.module_component_template(component);
        auto concrete_reference =
            ReferenceDesignator{name.value() + "/" + component_template.reference().value()};
        if (queries::component_by_reference(*this, concrete_reference).has_value()) {
            throw KernelLogicError{ErrorCode::DuplicateName,
                                   "Module instance concrete component reference exists"};
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
        const auto net = connectivity().add_net(std::move(concrete_nets.at(index)));
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
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Module instance origin metadata is incomplete"};
        }
        const auto concrete_pin =
            queries::pin_by_definition(*this, concrete_component.value(), connection.pin());
        if (!concrete_pin.has_value()) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Concrete module component pin is missing"};
        }
        [[maybe_unused]] const auto changed = connect(concrete_net.value(), concrete_pin.value());
    }

    return instance;
}

[[nodiscard]] PortBindingId Circuit::bind_port(ModuleInstanceId instance, PortDefId port,
                                               NetId parent_net) {
    require_net(parent_net);
    if (queries::is_module_origin_net(*this, parent_net)) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Module instance port parent net must be outside module origins"};
    }

    const auto internal_net =
        queries::concrete_net_for(*this, instance, hierarchy_.port_definition(port).internal_net());
    if (!internal_net.has_value()) {
        throw KernelLogicError{ErrorCode::InvalidState,
                               "Port internal net has no concrete module instance net"};
    }

    return hierarchy_.bind_port(instance, port, internal_net.value(), parent_net);
}

[[nodiscard]] ModuleInstanceId Circuit::restore_root_module_instance(
    ModuleDefId definition, ModuleInstanceName name,
    const std::vector<std::pair<TemplateNetDefId, NetId>> &origins,
    const std::vector<std::pair<ModuleComponentId, ComponentId>> &component_origins) {
    require_module_definition(definition);
    if (queries::module_instance_by_name(*this, name).has_value()) {
        throw KernelLogicError{ErrorCode::DuplicateName, "Module instance name already exists"};
    }

    const auto &template_nets = hierarchy_.module_definition(definition).template_nets();
    if (origins.size() != template_nets.size()) {
        throw KernelLogicError{ErrorCode::InvalidArgument,
                               "Module instance origin net count does not match definition"};
    }

    auto seen_template_nets = std::vector<TemplateNetDefId>{};
    auto seen_concrete_nets = std::vector<NetId>{};
    for (const auto &[template_net, concrete_net] : origins) {
        require_template_net_in_module(definition, template_net);
        require_net(concrete_net);
        if (std::find(seen_template_nets.begin(), seen_template_nets.end(), template_net) !=
            seen_template_nets.end()) {
            throw KernelLogicError{ErrorCode::InvalidArgument,
                                   "Duplicate module instance template-net origin"};
        }
        if (std::find(seen_concrete_nets.begin(), seen_concrete_nets.end(), concrete_net) !=
            seen_concrete_nets.end()) {
            throw KernelLogicError{ErrorCode::InvalidArgument,
                                   "Duplicate module instance concrete-net origin"};
        }
        if (queries::is_module_origin_net(*this, concrete_net)) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Concrete net already has module origin metadata"};
        }
        seen_template_nets.push_back(template_net);
        seen_concrete_nets.push_back(concrete_net);
    }

    for (const auto template_net : template_nets) {
        if (std::find(seen_template_nets.begin(), seen_template_nets.end(), template_net) ==
            seen_template_nets.end()) {
            throw KernelLogicError{ErrorCode::InvalidArgument,
                                   "Missing module instance template-net origin"};
        }
    }

    const auto &module_components = hierarchy_.module_definition(definition).components();
    if (component_origins.size() != module_components.size()) {
        throw KernelLogicError{ErrorCode::InvalidArgument,
                               "Module instance component origin count does not match definition"};
    }

    auto seen_template_components = std::vector<ModuleComponentId>{};
    auto seen_concrete_components = std::vector<ComponentId>{};
    for (const auto &[template_component, concrete_component] : component_origins) {
        require_module_component_in_module(definition, template_component);
        require_component(concrete_component);
        if (std::find(seen_template_components.begin(), seen_template_components.end(),
                      template_component) != seen_template_components.end()) {
            throw KernelLogicError{ErrorCode::InvalidArgument,
                                   "Duplicate module instance template-component origin"};
        }
        if (std::find(seen_concrete_components.begin(), seen_concrete_components.end(),
                      concrete_component) != seen_concrete_components.end()) {
            throw KernelLogicError{ErrorCode::InvalidArgument,
                                   "Duplicate module instance concrete-component origin"};
        }
        if (queries::is_module_origin_component(*this, concrete_component)) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Concrete component already has module origin metadata"};
        }
        if (connectivity_.component(concrete_component).definition() !=
            hierarchy_.module_component_template(template_component).definition()) {
            throw KernelLogicError{
                ErrorCode::CrossReferenceViolation,
                "Module instance concrete component definition does not match template"};
        }
        seen_template_components.push_back(template_component);
        seen_concrete_components.push_back(concrete_component);
    }

    for (const auto template_component : module_components) {
        if (std::find(seen_template_components.begin(), seen_template_components.end(),
                      template_component) == seen_template_components.end()) {
            throw KernelLogicError{ErrorCode::InvalidArgument,
                                   "Missing module instance template-component origin"};
        }
    }
    require_restored_module_connectivity_matches_template(definition, origins, component_origins);

    return hierarchy_.restore_root_module_instance(definition, std::move(name), origins,
                                                   component_origins);
}

[[nodiscard]] ComponentId Circuit::instantiate_component(ComponentDefId definition,
                                                         ReferenceDesignator reference,
                                                         PropertyMap properties) {
    return instantiate_component(
        definition, ComponentInstanceSpec{std::move(reference), std::move(properties)});
}

bool Circuit::connect(NetId net, PinId pin) { return connectivity_.connect(net, pin); }

bool Circuit::disconnect(PinId pin) { return connectivity_.disconnect(pin); }

void Circuit::update(ComponentId component, ComponentUpdate change) {
    require_component(component);
    std::visit(
        [this, component](auto update) {
            using Update = decltype(update);
            if constexpr (std::same_as<Update, SetComponentProperty>) {
                connectivity_.set_component_property(component, std::move(update.key),
                                                     std::move(update.value));
            } else if constexpr (std::same_as<Update, SetComponentElectricalAttribute>) {
                electrical_.set_component_attribute(component, update.spec,
                                                    std::move(update.value));
            } else if constexpr (std::same_as<Update, SelectPhysicalPart>) {
                electrical_.select_physical_part(
                    component, std::move(update.physical_part),
                    component_definition(this->component(component).definition()).pins());
            } else if constexpr (std::same_as<Update, SetSelectedPartElectricalAttribute>) {
                electrical_.set_selected_part_attribute(component, update.spec,
                                                        std::move(update.value));
            } else if constexpr (std::same_as<Update, SetAssemblyIntent>) {
                if (!update.dnp.has_value() && !update.selection_override.has_value()) {
                    throw KernelArgumentError{
                        ErrorCode::InvalidArgument,
                        "Assembly intent update must set DNP or selection override intent"};
                }
                if (update.dnp.has_value()) {
                    intent_.set_component_dnp(component, update.dnp.value());
                }
                if (update.selection_override.has_value()) {
                    intent_.set_component_selection_override(component,
                                                             update.selection_override.value());
                }
            }
        },
        std::move(change));
}

void Circuit::update(NetId net, NetUpdate change) {
    require_net(net);
    std::visit(
        [this, net](auto update) {
            using Update = decltype(update);
            if constexpr (std::same_as<Update, SetNetElectricalAttribute>) {
                electrical_.set_net_attribute(net, update.spec, std::move(update.value));
            } else if constexpr (std::same_as<Update, AssignNetClass>) {
                require_net_class(update.net_class);
                [[maybe_unused]] const auto changed =
                    net_classes_.assign_net_class(net, update.net_class);
            } else if constexpr (std::same_as<Update, MarkIntentionalStub>) {
                [[maybe_unused]] const auto changed = intent_.mark_intentional_stub_net(net);
            }
        },
        std::move(change));
}

void Circuit::mark_no_connect(PinId pin) {
    require_pin(pin);
    [[maybe_unused]] const auto changed = intent_.mark_intentional_no_connect_pin(pin);
}

[[nodiscard]] std::optional<NetId> Circuit::net_of(PinId pin) const {
    return connectivity_.net_of(pin);
}

void Circuit::ElectricalMutator::set_component_electrical_attribute(
    ComponentId component, const ElectricalAttributeSpec &spec, ElectricalAttributeValue value) {
    circuit_.require_component(component);
    circuit_.electrical_.set_component_attribute(component, spec, value);
}

void Circuit::ElectricalMutator::set_pin_definition_electrical_attribute(
    PinDefId pin_definition, const ElectricalAttributeSpec &spec, ElectricalAttributeValue value) {
    circuit_.require_pin_definition(pin_definition);
    if (circuit_.connectivity_.pin_definition_is_owned(pin_definition)) {
        throw KernelLogicError{ErrorCode::InvalidState,
                               "Committed pin definition electrical attributes are immutable",
                               EntityRef::pin_def(pin_definition)};
    }
    circuit_.electrical_.set_pin_definition_attribute(pin_definition, spec, value);
}

void Circuit::ElectricalMutator::set_net_electrical_attribute(NetId net,
                                                              const ElectricalAttributeSpec &spec,
                                                              ElectricalAttributeValue value) {
    circuit_.require_net(net);
    circuit_.electrical_.set_net_attribute(net, spec, value);
}

void Circuit::ElectricalMutator::select_physical_part(ComponentId component,
                                                      PhysicalPart physical_part) {
    circuit_.require_component(component);
    circuit_.electrical_.select_physical_part(
        component, std::move(physical_part),
        circuit_.component_definition(circuit_.component(component).definition()).pins());
}

void Circuit::ElectricalMutator::set_selected_part_electrical_attribute(
    ComponentId component, const ElectricalAttributeSpec &spec, ElectricalAttributeValue value) {
    circuit_.require_component(component);
    circuit_.electrical_.set_selected_part_attribute(component, spec, value);
}

bool Circuit::IntentMutator::mark_intentional_stub_net(NetId net) {
    circuit_.require_net(net);
    return circuit_.intent_.mark_intentional_stub_net(net);
}

bool Circuit::IntentMutator::mark_intentional_no_connect_pin(PinId pin) {
    circuit_.require_pin(pin);
    return circuit_.intent_.mark_intentional_no_connect_pin(pin);
}

void Circuit::IntentMutator::set_component_dnp(ComponentId component, bool dnp) {
    circuit_.require_component(component);
    circuit_.intent_.set_component_dnp(component, dnp);
}

void Circuit::IntentMutator::set_component_selection_override(ComponentId component,
                                                              bool override) {
    circuit_.require_component(component);
    circuit_.intent_.set_component_selection_override(component, override);
}

[[nodiscard]] NetClassId Circuit::NetClassMutator::add_net_class(NetClass net_class) {
    return circuit_.net_classes_.add_net_class(std::move(net_class));
}

bool Circuit::NetClassMutator::assign_net_class(NetId net, NetClassId net_class) {
    circuit_.require_net(net);
    circuit_.require_net_class(net_class);
    return circuit_.net_classes_.assign_net_class(net, net_class);
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

[[nodiscard]] std::optional<bool> Circuit::component_dnp(ComponentId component) const {
    require_component(component);
    return intent_.component_dnp(component);
}

[[nodiscard]] bool Circuit::is_component_selection_override(ComponentId component) const {
    require_component(component);
    return intent_.is_component_selection_override(component);
}

[[nodiscard]] const std::vector<NetId> &Circuit::intentional_stub_nets() const noexcept {
    return intent_.intentional_stub_nets();
}

[[nodiscard]] const std::vector<PinId> &Circuit::intentional_no_connect_pins() const noexcept {
    return intent_.intentional_no_connect_pins();
}

[[nodiscard]] const std::vector<ComponentAssemblyIntent> &
Circuit::component_assembly_intents() const noexcept {
    return intent_.component_assembly_intents();
}

[[nodiscard]] const NetClass &Circuit::net_class(NetClassId id) const {
    return net_classes_.net_class(id);
}

[[nodiscard]] std::optional<NetClassId> Circuit::net_class_by_name(const NetClassName &name) const {
    return net_classes_.net_class_by_name(name);
}

[[nodiscard]] std::optional<NetClassId> Circuit::net_class_for_net(NetId net) const {
    require_net(net);
    return net_classes_.net_class_for_net(net);
}

[[nodiscard]] const std::vector<std::pair<NetId, NetClassId>> &
Circuit::net_class_assignments() const noexcept {
    return net_classes_.net_class_assignments();
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
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Pin definition does not belong to module component definition"};
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
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Module instance origin metadata is incomplete"};
        }

        const auto concrete_pin =
            queries::pin_by_definition(*this, concrete_component->second, connection.pin());
        if (!concrete_pin.has_value()) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Concrete module component pin is missing"};
        }

        if (connectivity_.net_of(concrete_pin.value()) != concrete_net->second) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Module instance concrete connectivity does not match template"};
        }
    }
}

void Circuit::require_pin(PinId pin) const { connectivity_.require_pin(pin); }

void Circuit::require_net(NetId net) const { connectivity_.require_net(net); }

void Circuit::require_net_class(NetClassId net_class) const {
    net_classes_.require_net_class(net_class);
}

[[nodiscard]] std::optional<NetId> Circuit::net_of_existing_pin(PinId pin) const {
    return connectivity_.net_of(pin);
}

} // namespace volt
