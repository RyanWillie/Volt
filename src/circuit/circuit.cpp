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

[[nodiscard]] ComponentDefId Circuit::define_component(ComponentSpec spec) {
    auto pin_definitions = std::vector<PinDefinition>{};
    auto pin_ids = std::vector<PinDefId>{};
    pin_definitions.reserve(spec.pins.size());
    pin_ids.reserve(spec.pins.size());

    const auto first_pin_index = connectivity_.pin_definitions.size();
    for (std::size_t index = 0; index < spec.pins.size(); ++index) {
        const auto &pin = spec.pins[index];
        pin_definitions.emplace_back(
            pin.name, pin.number, pin.requirement, pin.terminal_kind, pin.direction,
            pin.signal_domain, pin.drive_kind, pin.polarity,
            preflight_attributes(pin.electrical_attributes, ElectricalAttributeOwner::PinSpec));
        pin_ids.emplace_back(first_pin_index + index);
    }

    auto definition = ComponentDefinition::make(
        std::move(spec.name), pin_definitions, pin_ids, std::move(spec.properties),
        std::move(spec.source), std::move(spec.schematic_symbols), std::move(spec.contract));
    for (auto &pin_definition : pin_definitions) {
        [[maybe_unused]] const auto pin =
            connectivity_.add_pin_definition(std::move(pin_definition));
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
        const auto &component_pins = get(component->definition()).pins();
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
    const auto first_template_net_index = hierarchy_.template_net_definitions.size();
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

    return connectivity_.instantiate_component(definition, std::move(spec));
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

[[nodiscard]] ModuleInstanceId Circuit::instantiate_module(ModuleDefId definition,
                                                           ModuleInstanceSpec spec) {
    auto name = std::move(spec.name);
    require_module_definition(definition);
    if (queries::module_instance_by_name(*this, name).has_value()) {
        throw KernelLogicError{ErrorCode::DuplicateName, "Module instance name already exists"};
    }

    std::vector<Net> concrete_nets;
    for (const auto template_net : get(definition).template_nets()) {
        const auto &template_net_definition = get(template_net);
        auto concrete_name = NetName{name.value() + "/" + template_net_definition.name().value()};
        if (queries::net_by_name(*this, concrete_name).has_value()) {
            throw KernelLogicError{ErrorCode::DuplicateName,
                                   "Module instance concrete net name already exists"};
        }
        concrete_nets.emplace_back(std::move(concrete_name), template_net_definition.kind());
    }

    std::vector<ComponentInstance> concrete_components;
    for (const auto component : get(definition).components()) {
        const auto &component_template = get(component);
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

    const auto &module_components = get(definition).components();
    auto component_origins = std::vector<std::pair<ModuleComponentId, ComponentId>>{};
    component_origins.reserve(module_components.size());
    for (std::size_t index = 0; index < module_components.size(); ++index) {
        auto &concrete_component = concrete_components.at(index);
        const auto component = instantiate_component(
            concrete_component.definition(),
            ComponentInstanceSpec{concrete_component.reference(), concrete_component.properties()});
        component_origins.emplace_back(module_components.at(index), component);
    }

    const auto &template_nets = get(definition).template_nets();
    auto net_origins = std::vector<std::pair<TemplateNetDefId, NetId>>{};
    net_origins.reserve(template_nets.size());
    for (std::size_t index = 0; index < template_nets.size(); ++index) {
        const auto net = connectivity_.add_net(std::move(concrete_nets.at(index)));
        net_origins.emplace_back(template_nets.at(index), net);
    }

    const auto instance = hierarchy_.add_module_instance(ModuleInstance{
        definition, std::move(name), std::move(net_origins), std::move(component_origins)});

    for (const auto &connection : get(definition).connections()) {
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

    const auto internal_net = queries::concrete_net_for(*this, instance, get(port).internal_net());
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

    const auto &template_nets = get(definition).template_nets();
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

    const auto &module_components = get(definition).components();
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
        if (get(concrete_component).definition() != get(template_component).definition()) {
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

    auto canonical_net_origins = std::vector<std::pair<TemplateNetDefId, NetId>>{};
    canonical_net_origins.reserve(template_nets.size());
    for (const auto template_net : template_nets) {
        const auto origin =
            std::find_if(origins.begin(), origins.end(),
                         [template_net](const auto &pair) { return pair.first == template_net; });
        canonical_net_origins.push_back(*origin);
    }

    auto canonical_component_origins = std::vector<std::pair<ModuleComponentId, ComponentId>>{};
    canonical_component_origins.reserve(module_components.size());
    for (const auto template_component : module_components) {
        const auto origin = std::find_if(
            component_origins.begin(), component_origins.end(),
            [template_component](const auto &pair) { return pair.first == template_component; });
        canonical_component_origins.push_back(*origin);
    }

    return hierarchy_.add_module_instance(ModuleInstance{definition, std::move(name),
                                                         std::move(canonical_net_origins),
                                                         std::move(canonical_component_origins)});
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
                set_component_attribute(component, update.spec, std::move(update.value));
            } else if constexpr (std::same_as<Update, SelectPhysicalPart>) {
                select_physical_part(component, std::move(update.physical_part),
                                     get(get(component).definition()).pins());
            } else if constexpr (std::same_as<Update, SetSelectedPartElectricalAttribute>) {
                set_selected_part_attribute(component, update.spec, std::move(update.value));
            } else if constexpr (std::same_as<Update, SetAssemblyIntent>) {
                if (!update.dnp.has_value() && !update.selection_override.has_value()) {
                    throw KernelArgumentError{
                        ErrorCode::InvalidArgument,
                        "Assembly intent update must set DNP or selection override intent"};
                }
                connectivity_.set_component_assembly_intent(component, update.dnp,
                                                            update.selection_override);
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
                set_net_attribute(net, update.spec, std::move(update.value));
            } else if constexpr (std::same_as<Update, AssignNetClass>) {
                require_net_class(update.net_class);
                [[maybe_unused]] const auto changed =
                    connectivity_.assign_net_class(net, update.net_class);
            } else if constexpr (std::same_as<Update, MarkIntentionalStub>) {
                [[maybe_unused]] const auto changed = connectivity_.mark_intentional_stub(net);
            }
        },
        std::move(change));
}

void Circuit::mark_no_connect(PinId pin) {
    require_pin(pin);
    connectivity_.mark_intentional_no_connect(pin);
}

[[nodiscard]] std::optional<NetId> Circuit::net_of(PinId pin) const {
    require_pin(pin);
    return connectivity_.net_of_existing_pin(pin);
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

void Circuit::require_template_net_in_module(ModuleDefId module, TemplateNetDefId net) const {
    hierarchy_.require_template_net_in_module(module, net);
}

void Circuit::require_module_component_in_module(ModuleDefId module,
                                                 ModuleComponentId component) const {
    hierarchy_.require_module_component_in_module(module, component);
}

void Circuit::require_restored_module_connectivity_matches_template(
    ModuleDefId definition, const std::vector<std::pair<TemplateNetDefId, NetId>> &origins,
    const std::vector<std::pair<ModuleComponentId, ComponentId>> &component_origins) const {
    require_module_definition(definition);
    for (const auto &connection : get(definition).connections()) {
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

        if (net_of(concrete_pin.value()) != concrete_net->second) {
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

} // namespace volt
