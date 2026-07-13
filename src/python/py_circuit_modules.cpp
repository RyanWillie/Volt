#include "py_circuit.hpp"

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/errors.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace volt::python {

std::size_t PyCircuit::define_module(const std::string &name) {
    return circuit_.hierarchy()
        .add_module_definition(volt::ModuleDefinition{volt::ModuleName{name}})
        .index();
}

std::size_t PyCircuit::add_template_net(std::size_t module, const std::string &name,
                                        const std::string &kind) {
    return circuit_.hierarchy()
        .add_template_net(module_def_id(module),
                          volt::TemplateNetDefinition{volt::NetName{name}, parse_net_kind(kind)})
        .index();
}

std::size_t PyCircuit::add_port(std::size_t module, const std::string &name,
                                std::size_t internal_net, const std::string &role, bool required) {
    return circuit_.hierarchy()
        .add_port_definition(module_def_id(module),
                             volt::PortDefinition{volt::PortName{name},
                                                  template_net_def_id(internal_net),
                                                  parse_port_role(role), required})
        .index();
}

std::size_t PyCircuit::add_module_component(std::size_t module, std::size_t definition,
                                            const std::string &reference,
                                            const py::dict &properties) {
    return circuit_.hierarchy()
        .add_module_component(module_def_id(module),
                              volt::ModuleComponentTemplate{component_def_id(definition),
                                                            volt::ReferenceDesignator{reference},
                                                            properties_from_dict(properties)})
        .index();
}

std::size_t PyCircuit::module_component_pin_by_name(std::size_t component,
                                                    const std::string &name) const {
    const auto matches = module_component_pins_by_name(module_component_id(component), name);
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
    const auto component_handle = module_component_id(component);
    const auto &component_template = circuit_.module_component_template(component_handle);
    const auto &definition = circuit_.component_definition(component_template.definition());
    for (const auto pin : definition.pins()) {
        if (circuit_.pin_definition(pin).number() == number) {
            return pin.index();
        }
    }

    throw std::out_of_range{"Module component has no pin with that number"};
}

py::list PyCircuit::module_component_pin_refs(std::size_t component) const {
    auto result = py::list{};
    const auto component_handle = module_component_id(component);
    const auto &component_template = circuit_.module_component_template(component_handle);
    const auto &definition = circuit_.component_definition(component_template.definition());
    for (const auto pin : definition.pins()) {
        const auto &pin_definition = circuit_.pin_definition(pin);
        auto item = py::dict{};
        item["index"] = pin.index();
        item["name"] = pin_definition.name();
        item["number"] = pin_definition.number();
        result.append(std::move(item));
    }
    return result;
}

void PyCircuit::connect_module_pin(std::size_t module, std::size_t net, std::size_t component,
                                   std::size_t pin) {
    circuit_.hierarchy().connect_module_pin(module_def_id(module), template_net_def_id(net),
                                            module_component_id(component), volt::PinDefId{pin});
}

std::size_t PyCircuit::instantiate_root_module(std::size_t definition, const std::string &name) {
    return circuit_
        .instantiate_root_module(module_def_id(definition), volt::ModuleInstanceName{name})
        .index();
}

std::size_t PyCircuit::concrete_component_for(std::size_t instance, std::size_t component) const {
    const auto concrete = queries::concrete_component_for(circuit_, module_instance_id(instance),
                                                          module_component_id(component));
    if (!concrete.has_value()) {
        throw std::out_of_range{"Module instance has no concrete component for template"};
    }
    return concrete.value().index();
}

void PyCircuit::bind_port(std::size_t instance, std::size_t port, std::size_t parent_net) {
    [[maybe_unused]] const auto binding =
        circuit_.bind_port(module_instance_id(instance), port_def_id(port), net_id(parent_net));
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
        const auto port = port_def_id(port_index);
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
                module_instance_id(port->first), port_def_id(port->second), target_net);
        } else {
            static_cast<void>(
                circuit_.connect(target_net, pin_id(std::get<std::size_t>(endpoint))));
        }
    }
}

py::list PyCircuit::template_nets(std::size_t module) const {
    auto result = py::list{};
    const auto &definition = circuit_.module_definition(module_def_id(module));
    for (const auto net_id : definition.template_nets()) {
        const auto &net = circuit_.template_net_definition(net_id);
        auto item = py::dict{};
        item["index"] = net_id.index();
        item["name"] = net.name().value();
        item["kind"] = net_kind_name(net.kind());
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::module_ports(std::size_t module) const {
    auto result = py::list{};
    const auto &definition = circuit_.module_definition(module_def_id(module));
    for (const auto port_id : definition.ports()) {
        const auto &port = circuit_.port_definition(port_id);
        auto item = py::dict{};
        item["index"] = port_id.index();
        item["name"] = port.name().value();
        item["internal_net"] = port.internal_net().index();
        item["role"] = port_role_name(port.role());
        item["required"] = port.required();
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::module_components(std::size_t module) const {
    auto result = py::list{};
    const auto &definition = circuit_.module_definition(module_def_id(module));
    for (const auto component_id : definition.components()) {
        const auto &component = circuit_.module_component_template(component_id);
        auto item = py::dict{};
        item["index"] = component_id.index();
        item["definition"] = component.definition().index();
        item["reference"] = component.reference().value();
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::module_connections(std::size_t module) const {
    auto result = py::list{};
    for (const auto &connection : circuit_.module_pin_connections(module_def_id(module))) {
        auto item = py::dict{};
        item["net"] = connection.net().index();
        item["component"] = connection.component().index();
        item["pin_definition"] = connection.pin().index();
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::module_net_origins(std::size_t instance) const {
    auto result = py::list{};
    for (const auto &[template_net, concrete_net] :
         queries::module_net_origins(circuit_, module_instance_id(instance))) {
        auto item = py::dict{};
        item["template_net"] = template_net.index();
        item["net"] = concrete_net.index();
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::module_component_origins(std::size_t instance) const {
    auto result = py::list{};
    for (const auto &[module_component, concrete_component] :
         queries::module_component_origins(circuit_, module_instance_id(instance))) {
        auto item = py::dict{};
        item["module_component"] = module_component.index();
        item["component"] = concrete_component.index();
        result.append(std::move(item));
    }
    return result;
}

py::list PyCircuit::port_bindings(std::size_t instance) const {
    auto result = py::list{};
    for (const auto binding_id :
         queries::port_bindings_for(circuit_, module_instance_id(instance))) {
        const auto &binding = circuit_.port_binding(binding_id);
        auto item = py::dict{};
        item["port"] = binding.port().index();
        item["internal_net"] = binding.internal_net().index();
        item["parent_net"] = binding.parent_net().index();
        result.append(std::move(item));
    }
    return result;
}

std::vector<volt::PinDefId>
PyCircuit::module_component_pins_by_name(volt::ModuleComponentId component,
                                         const std::string &name) const {
    auto result = std::vector<volt::PinDefId>{};
    const auto &component_template = circuit_.module_component_template(component);
    const auto &definition = circuit_.component_definition(component_template.definition());
    for (const auto pin : definition.pins()) {
        if (circuit_.pin_definition(pin).name() == name) {
            result.push_back(pin);
        }
    }
    return result;
}

} // namespace volt::python
