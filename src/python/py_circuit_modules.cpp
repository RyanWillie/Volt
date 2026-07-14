#include "py_circuit.hpp"

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/errors.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace volt::python {

PyCircuit::ModuleDraft &PyCircuit::module_draft(std::size_t module) {
    const auto found = std::find_if(module_drafts_.begin(), module_drafts_.end(),
                                    [module](const auto &draft) { return draft.handle == module; });
    if (found == module_drafts_.end()) {
        throw volt::KernelRangeError{volt::ErrorCode::UnknownEntity,
                                     "Module definition ID does not belong to this circuit",
                                     volt::EntityRef::module_def(volt::ModuleDefId{module})};
    }
    return *found;
}

const PyCircuit::ModuleDraft &PyCircuit::module_draft(std::size_t module) const {
    const auto found = std::find_if(module_drafts_.begin(), module_drafts_.end(),
                                    [module](const auto &draft) { return draft.handle == module; });
    if (found == module_drafts_.end()) {
        throw volt::KernelRangeError{volt::ErrorCode::UnknownEntity,
                                     "Module definition ID does not belong to this circuit",
                                     volt::EntityRef::module_def(volt::ModuleDefId{module})};
    }
    return *found;
}

std::pair<const PyCircuit::ModuleDraft *, std::size_t>
PyCircuit::template_net_draft(std::size_t net) const {
    for (const auto &draft : module_drafts_) {
        const auto found =
            std::find(draft.template_net_handles.begin(), draft.template_net_handles.end(), net);
        if (found != draft.template_net_handles.end()) {
            return {&draft, static_cast<std::size_t>(found - draft.template_net_handles.begin())};
        }
    }
    throw volt::KernelRangeError{volt::ErrorCode::UnknownEntity,
                                 "Template net definition ID does not belong to this circuit",
                                 volt::EntityRef::template_net_def(volt::TemplateNetDefId{net})};
}

std::pair<const PyCircuit::ModuleDraft *, std::size_t>
PyCircuit::module_component_draft(std::size_t component) const {
    for (const auto &draft : module_drafts_) {
        const auto found =
            std::find(draft.component_handles.begin(), draft.component_handles.end(), component);
        if (found != draft.component_handles.end()) {
            return {&draft, static_cast<std::size_t>(found - draft.component_handles.begin())};
        }
    }
    throw volt::KernelRangeError{
        volt::ErrorCode::UnknownEntity, "Module component ID does not belong to this circuit",
        volt::EntityRef::module_component(volt::ModuleComponentId{component})};
}

std::pair<const PyCircuit::ModuleDraft *, std::size_t>
PyCircuit::port_draft(std::size_t port) const {
    for (const auto &draft : module_drafts_) {
        const auto found = std::find(draft.port_handles.begin(), draft.port_handles.end(), port);
        if (found != draft.port_handles.end()) {
            return {&draft, static_cast<std::size_t>(found - draft.port_handles.begin())};
        }
    }
    throw volt::KernelRangeError{volt::ErrorCode::UnknownEntity,
                                 "Port definition ID does not belong to this circuit",
                                 volt::EntityRef::port_def(volt::PortDefId{port})};
}

void PyCircuit::preflight_module_drafts(std::size_t module,
                                        const volt::ModuleSpec &candidate) const {
    auto preflight = circuit_;
    for (const auto &draft : module_drafts_) {
        if (draft.committed_id.has_value()) {
            continue;
        }
        [[maybe_unused]] const auto defined =
            preflight.define_module(draft.handle == module ? candidate : draft.spec);
    }
}

volt::Circuit PyCircuit::materialized_circuit() const {
    auto materialized = circuit_;
    for (const auto &draft : module_drafts_) {
        if (!draft.committed_id.has_value()) {
            [[maybe_unused]] const auto defined = materialized.define_module(draft.spec);
        }
    }
    return materialized;
}

volt::PortDefId PyCircuit::resolved_port_id(std::size_t port) const {
    const auto [draft, local_index] = port_draft(port);
    if (!draft->committed_id.has_value()) {
        throw volt::KernelLogicError{volt::ErrorCode::InvalidState,
                                     "Module definition has not been committed"};
    }
    return circuit_.get(draft->committed_id.value()).ports().at(local_index);
}

volt::ModuleComponentId PyCircuit::resolved_module_component_id(std::size_t component) const {
    const auto [draft, local_index] = module_component_draft(component);
    if (!draft->committed_id.has_value()) {
        throw volt::KernelLogicError{volt::ErrorCode::InvalidState,
                                     "Module definition has not been committed"};
    }
    return circuit_.get(draft->committed_id.value()).components().at(local_index);
}

std::size_t PyCircuit::public_template_net_index(volt::TemplateNetDefId net) const {
    for (const auto &draft : module_drafts_) {
        if (!draft.committed_id.has_value()) {
            continue;
        }
        const auto &actual = circuit_.get(draft.committed_id.value()).template_nets();
        const auto found = std::find(actual.begin(), actual.end(), net);
        if (found != actual.end()) {
            return draft.template_net_handles.at(static_cast<std::size_t>(found - actual.begin()));
        }
    }
    return net.index();
}

std::size_t PyCircuit::public_port_index(volt::PortDefId port) const {
    for (const auto &draft : module_drafts_) {
        if (!draft.committed_id.has_value()) {
            continue;
        }
        const auto &actual = circuit_.get(draft.committed_id.value()).ports();
        const auto found = std::find(actual.begin(), actual.end(), port);
        if (found != actual.end()) {
            return draft.port_handles.at(static_cast<std::size_t>(found - actual.begin()));
        }
    }
    return port.index();
}

std::size_t PyCircuit::public_module_component_index(volt::ModuleComponentId component) const {
    for (const auto &draft : module_drafts_) {
        if (!draft.committed_id.has_value()) {
            continue;
        }
        const auto &actual = circuit_.get(draft.committed_id.value()).components();
        const auto found = std::find(actual.begin(), actual.end(), component);
        if (found != actual.end()) {
            return draft.component_handles.at(static_cast<std::size_t>(found - actual.begin()));
        }
    }
    return component.index();
}

std::size_t PyCircuit::define_module(const std::string &name) {
    auto preflight = materialized_circuit();
    auto spec = volt::ModuleSpec{.name = volt::ModuleName{name}};
    [[maybe_unused]] const auto defined = preflight.define_module(spec);

    const auto handle = module_drafts_.size();
    module_drafts_.push_back(ModuleDraft{.handle = handle,
                                         .spec = std::move(spec),
                                         .template_net_handles = {},
                                         .port_handles = {},
                                         .component_handles = {},
                                         .committed_id = std::nullopt});
    return handle;
}

std::size_t PyCircuit::add_template_net(std::size_t module, const std::string &name,
                                        const std::string &kind) {
    auto &draft = module_draft(module);
    if (draft.committed_id.has_value()) {
        throw volt::KernelLogicError{volt::ErrorCode::InvalidState,
                                     "Committed module definitions are immutable"};
    }
    auto candidate = draft.spec;
    candidate.template_nets.emplace_back(volt::NetName{name}, parse_net_kind(kind));
    preflight_module_drafts(module, candidate);

    const auto handle = next_template_net_handle_++;
    draft.spec = std::move(candidate);
    draft.template_net_handles.push_back(handle);
    return handle;
}

std::pair<std::size_t, std::size_t>
PyCircuit::add_module_port(std::size_t module, const std::string &name, const std::string &kind,
                           const std::string &role, bool required) {
    auto &draft = module_draft(module);
    if (draft.committed_id.has_value()) {
        throw volt::KernelLogicError{volt::ErrorCode::InvalidState,
                                     "Committed module definitions are immutable"};
    }

    auto candidate = draft.spec;
    candidate.template_nets.emplace_back(volt::NetName{name}, parse_net_kind(kind));
    preflight_module_drafts(module, candidate);
    candidate.ports.push_back(volt::ModulePortSpec{volt::PortName{name}, volt::NetName{name},
                                                   parse_port_role(role), required});
    preflight_module_drafts(module, candidate);

    const auto net_handle = next_template_net_handle_++;
    const auto port_handle = next_port_handle_++;
    draft.spec = std::move(candidate);
    draft.template_net_handles.push_back(net_handle);
    draft.port_handles.push_back(port_handle);
    return {net_handle, port_handle};
}

std::size_t PyCircuit::add_module_component(std::size_t module, std::size_t definition,
                                            const std::string &reference,
                                            const py::dict &properties) {
    auto &draft = module_draft(module);
    if (draft.committed_id.has_value()) {
        throw volt::KernelLogicError{volt::ErrorCode::InvalidState,
                                     "Committed module definitions are immutable"};
    }
    auto candidate = draft.spec;
    candidate.components.emplace_back(component_def_id(definition),
                                      volt::ReferenceDesignator{reference},
                                      properties_from_dict(properties));
    preflight_module_drafts(module, candidate);

    const auto handle = next_module_component_handle_++;
    draft.spec = std::move(candidate);
    draft.component_handles.push_back(handle);
    return handle;
}

std::size_t PyCircuit::module_component_pin_by_name(std::size_t component,
                                                    const std::string &name) const {
    const auto matches = module_component_pins_by_name(component, name);
    if (matches.empty()) {
        throw std::out_of_range{"Module component has no pin with that name"};
    }
    if (matches.size() > 1) {
        throw std::invalid_argument{"Module component pin name is ambiguous"};
    }

    return matches.front().index();
}

std::size_t PyCircuit::module_component_pin_by_number(std::size_t component,
                                                      const std::string &number) const {
    const auto [draft, local_index] = module_component_draft(component);
    const auto &component_template = draft->spec.components.at(local_index);
    const auto &definition = circuit_.get(component_template.definition());
    for (const auto pin : definition.pins()) {
        if (circuit_.get(pin).number() == number) {
            return pin.index();
        }
    }

    throw std::out_of_range{"Module component has no pin with that number"};
}

py::list PyCircuit::module_component_pin_refs(std::size_t component) const {
    auto result = py::list{};
    const auto [draft, local_index] = module_component_draft(component);
    const auto &component_template = draft->spec.components.at(local_index);
    const auto &definition = circuit_.get(component_template.definition());
    for (const auto pin : definition.pins()) {
        const auto &pin_definition = circuit_.get(pin);
        auto item = py::dict{};
        item["index"] = pin.index();
        item["name"] = pin_definition.name();
        item["number"] = pin_definition.number();
        result.append(std::move(item));
    }
    return result;
}

void PyCircuit::connect_module_pins(
    std::size_t module, std::size_t net,
    const std::vector<std::pair<std::size_t, std::size_t>> &component_pins) {
    auto &draft = module_draft(module);
    if (draft.committed_id.has_value()) {
        throw volt::KernelLogicError{volt::ErrorCode::InvalidState,
                                     "Committed module definitions are immutable"};
    }
    const auto [net_draft, net_index] = template_net_draft(net);
    if (net_draft->handle != module) {
        throw volt::KernelLogicError{
            volt::ErrorCode::CrossReferenceViolation,
            "Template net does not belong to module definition",
            volt::EntityRef::template_net_def(volt::TemplateNetDefId{net})};
    }

    const auto &target_net = draft.spec.template_nets.at(net_index).name();
    auto candidate = draft.spec;
    for (const auto &[component, pin] : component_pins) {
        const auto [component_draft, component_index] = module_component_draft(component);
        if (component_draft->handle != module) {
            throw volt::KernelLogicError{
                volt::ErrorCode::CrossReferenceViolation,
                "Module component does not belong to module definition",
                volt::EntityRef::module_component(volt::ModuleComponentId{component})};
        }

        const auto &target_component = draft.spec.components.at(component_index).reference();
        const auto pin_definition = volt::PinDefId{pin};
        const auto existing = std::find_if(
            candidate.connections.begin(), candidate.connections.end(),
            [&target_component, pin_definition](const auto &connection) {
                return connection.component == target_component && connection.pin == pin_definition;
            });
        if (existing != candidate.connections.end() && existing->net == target_net) {
            continue;
        }

        candidate.connections.push_back(
            volt::ModulePinConnectionSpec{target_net, target_component, pin_definition});
    }
    preflight_module_drafts(module, candidate);
    draft.spec = std::move(candidate);
}

std::size_t PyCircuit::instantiate_root_module(std::size_t definition, const std::string &name) {
    auto &requested = module_draft(definition);
    if (requested.committed_id.has_value()) {
        return circuit_
            .instantiate_module(requested.committed_id.value(),
                                volt::ModuleInstanceSpec{.name = volt::ModuleInstanceName{name}})
            .index();
    }

    auto candidate = circuit_;
    const auto candidate_definition = candidate.define_module(requested.spec);
    const auto instance = candidate.instantiate_module(
        candidate_definition, volt::ModuleInstanceSpec{.name = volt::ModuleInstanceName{name}});

    circuit_ = std::move(candidate);
    requested.committed_id = candidate_definition;
    return instance.index();
}

std::size_t PyCircuit::concrete_component_for(std::size_t instance, std::size_t component) const {
    const auto concrete = queries::concrete_component_for(circuit_, module_instance_id(instance),
                                                          resolved_module_component_id(component));
    if (!concrete.has_value()) {
        throw std::out_of_range{"Module instance has no concrete component for template"};
    }
    return concrete.value().index();
}

void PyCircuit::bind_port(std::size_t instance, std::size_t port, std::size_t parent_net) {
    [[maybe_unused]] const auto binding = circuit_.bind_port(
        module_instance_id(instance), resolved_port_id(port), net_id(parent_net));
}

void PyCircuit::connect_endpoints(std::size_t net,
                                  const std::vector<PyConnectivityEndpoint> &endpoints) {
    const auto target_net = net_id(net);
    static_cast<void>(circuit_.get(target_net));

    auto seen_ports = std::vector<std::pair<volt::ModuleInstanceId, volt::PortDefId>>{};
    for (const auto &endpoint : endpoints) {
        if (const auto *pin_index = std::get_if<std::size_t>(&endpoint)) {
            const auto pin = pin_id(*pin_index);
            const auto current_net = circuit_.net_of(pin);
            if (current_net.has_value() && current_net.value() != target_net) {
                throw volt::KernelLogicError{volt::ErrorCode::InvalidState,
                                             "Pin is already connected to another net"};
            }
            continue;
        }

        const auto &[instance_index, port_index] =
            std::get<std::pair<std::size_t, std::size_t>>(endpoint);
        const auto instance = module_instance_id(instance_index);
        const auto port = resolved_port_id(port_index);
        if (volt::queries::is_module_origin_net(circuit_, target_net)) {
            throw volt::KernelLogicError{
                volt::ErrorCode::CrossReferenceViolation,
                "Module instance port parent net must be outside module origins"};
        }
        const auto internal_net =
            volt::queries::concrete_net_for(circuit_, instance, circuit_.get(port).internal_net());
        if (!internal_net.has_value()) {
            throw volt::KernelLogicError{volt::ErrorCode::InvalidState,
                                         "Port internal net has no concrete module instance net"};
        }

        const auto endpoint_key = std::pair{instance, port};
        if (volt::queries::port_binding_for(circuit_, instance, port).has_value() ||
            std::find(seen_ports.begin(), seen_ports.end(), endpoint_key) != seen_ports.end()) {
            throw volt::KernelLogicError{volt::ErrorCode::InvalidState,
                                         "Module instance port is already bound"};
        }
        seen_ports.push_back(endpoint_key);
    }

    for (const auto &endpoint : endpoints) {
        if (const auto *port = std::get_if<std::pair<std::size_t, std::size_t>>(&endpoint)) {
            [[maybe_unused]] const auto binding = circuit_.bind_port(
                module_instance_id(port->first), resolved_port_id(port->second), target_net);
        } else {
            static_cast<void>(
                circuit_.connect(target_net, pin_id(std::get<std::size_t>(endpoint))));
        }
    }
}

py::list PyCircuit::template_nets(std::size_t module) const {
    auto result = py::list{};
    const auto &draft = module_draft(module);
    for (std::size_t index = 0; index < draft.spec.template_nets.size(); ++index) {
        const auto &net = draft.spec.template_nets[index];
        auto item = py::dict{};
        item["index"] = draft.template_net_handles[index];
        item["name"] = net.name().value();
        item["kind"] = net_kind_name(net.kind());
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::module_ports(std::size_t module) const {
    auto result = py::list{};
    const auto &draft = module_draft(module);
    for (std::size_t index = 0; index < draft.spec.ports.size(); ++index) {
        const auto &port = draft.spec.ports[index];
        const auto net = std::find_if(
            draft.spec.template_nets.begin(), draft.spec.template_nets.end(),
            [&port](const auto &candidate) { return candidate.name() == port.internal_net; });
        auto item = py::dict{};
        item["index"] = draft.port_handles[index];
        item["name"] = port.name.value();
        item["internal_net"] = draft.template_net_handles.at(
            static_cast<std::size_t>(net - draft.spec.template_nets.begin()));
        item["role"] = port_role_name(port.role);
        item["required"] = port.required;
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::module_components(std::size_t module) const {
    auto result = py::list{};
    const auto &draft = module_draft(module);
    for (std::size_t index = 0; index < draft.spec.components.size(); ++index) {
        const auto &component = draft.spec.components[index];
        auto item = py::dict{};
        item["index"] = draft.component_handles[index];
        item["definition"] = component.definition().index();
        item["reference"] = component.reference().value();
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::module_connections(std::size_t module) const {
    auto result = py::list{};
    const auto &draft = module_draft(module);
    for (const auto &connection : draft.spec.connections) {
        const auto net = std::find_if(
            draft.spec.template_nets.begin(), draft.spec.template_nets.end(),
            [&connection](const auto &candidate) { return candidate.name() == connection.net; });
        const auto component =
            std::find_if(draft.spec.components.begin(), draft.spec.components.end(),
                         [&connection](const auto &candidate) {
                             return candidate.reference() == connection.component;
                         });
        auto item = py::dict{};
        item["net"] = draft.template_net_handles.at(
            static_cast<std::size_t>(net - draft.spec.template_nets.begin()));
        item["component"] = draft.component_handles.at(
            static_cast<std::size_t>(component - draft.spec.components.begin()));
        item["pin_definition"] = connection.pin.index();
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::module_net_origins(std::size_t instance) const {
    auto result = py::list{};
    for (const auto &[template_net, concrete_net] :
         volt::queries::module_net_origins(circuit_, module_instance_id(instance))) {
        auto item = py::dict{};
        item["template_net"] = public_template_net_index(template_net);
        item["net"] = concrete_net.index();
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::module_component_origins(std::size_t instance) const {
    auto result = py::list{};
    for (const auto &[module_component, concrete_component] :
         volt::queries::module_component_origins(circuit_, module_instance_id(instance))) {
        auto item = py::dict{};
        item["module_component"] = public_module_component_index(module_component);
        item["component"] = concrete_component.index();
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::port_bindings(std::size_t instance) const {
    auto result = py::list{};
    for (const auto binding_id :
         queries::port_bindings_for(circuit_, module_instance_id(instance))) {
        const auto &binding = circuit_.get(binding_id);
        auto item = py::dict{};
        item["port"] = public_port_index(binding.port());
        item["internal_net"] = binding.internal_net().index();
        item["parent_net"] = binding.parent_net().index();
        result.append(std::move(item));
    }
    return result;
}

std::vector<volt::PinDefId>
PyCircuit::module_component_pins_by_name(std::size_t component, const std::string &name) const {
    auto result = std::vector<volt::PinDefId>{};
    const auto [draft, local_index] = module_component_draft(component);
    const auto &component_template = draft->spec.components.at(local_index);
    const auto &definition = circuit_.get(component_template.definition());
    for (const auto pin : definition.pins()) {
        if (circuit_.get(pin).name() == name) {
            result.push_back(pin);
        }
    }
    return result;
}

} // namespace volt::python
