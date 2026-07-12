#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>
#include <volt/io/detail/typed_id.hpp>

#include "logical_circuit_parser.hpp"

namespace volt::io::detail {

void LogicalCircuitParser::read_module_definitions() {
    const auto modules = optional_array_field(document_, "module_definitions");
    if (modules == nullptr) {
        return;
    }

    struct ParsedModuleDefinition {
        std::string id;
        ModuleDefinition definition;
    };

    struct ParsedTemplateNetDefinition {
        std::string id;
        std::string module;
        TemplateNetDefinition definition;
    };

    struct ParsedModuleComponent {
        std::string id;
        std::string module;
        ModuleComponentTemplate component;
    };

    struct ParsedConnection {
        std::string module;
        std::string net;
        std::string component;
        PinDefId pin;
    };

    struct ParsedPortDefinition {
        std::string id;
        std::string module;
        PortName name;
        std::string internal_net;
        PortRole role;
        bool required;
    };

    auto seen = std::set<std::string>{};
    auto seen_template_nets = std::set<std::string>{};
    auto seen_module_components = std::set<std::string>{};
    auto seen_ports = std::set<std::string>{};
    auto parsed_modules = std::vector<ParsedModuleDefinition>{};
    auto parsed_template_nets = std::vector<ParsedTemplateNetDefinition>{};
    auto parsed_components = std::vector<ParsedModuleComponent>{};
    auto parsed_connections = std::vector<ParsedConnection>{};
    auto parsed_ports = std::vector<ParsedPortDefinition>{};
    for (const auto &module_object : *modules) {
        const auto id = local_id<ModuleDefId>(module_object, seen);
        const auto module = ModuleDefId{parsed_modules.size()};
        auto module_name = ModuleName{string_field(module_object, "name")};
        auto local_template_nets = std::set<std::string>{};
        auto local_components = std::set<std::string>{};

        for (const auto &net_object : array_field(module_object, "local_nets")) {
            const auto net_id = local_id<TemplateNetDefId>(net_object, seen_template_nets);
            auto definition = TemplateNetDefinition{
                NetName{string_field(net_object, "name")},
                net_kind(string_field(net_object, "kind")),
            };
            local_template_nets.insert(net_id);
            parsed_template_nets.push_back(
                ParsedTemplateNetDefinition{net_id, id, std::move(definition)});
        }

        if (const auto components = optional_array_field(module_object, "components")) {
            for (const auto &component_object : *components) {
                const auto component_id =
                    local_id<ModuleComponentId>(component_object, seen_module_components);
                auto component = ModuleComponentTemplate{
                    resolve(component_def_ids_, string_field(component_object, "definition")),
                    ReferenceDesignator{string_field(component_object, "reference")},
                    properties(field(component_object, "properties")),
                };
                local_components.insert(component_id);
                parsed_components.push_back(
                    ParsedModuleComponent{component_id, id, std::move(component)});
            }
        }

        if (const auto connections = optional_array_field(module_object, "connections")) {
            for (const auto &connection_object : *connections) {
                const auto net_id = string_field(connection_object, "net");
                const auto component_id = string_field(connection_object, "component");
                require(local_template_nets.contains(net_id),
                        "Template net does not belong to module definition");
                require(local_components.contains(component_id),
                        "Module component does not belong to module definition");
                const auto pin = resolve(pin_def_ids_, string_field(connection_object, "pin"));
                parsed_connections.push_back(ParsedConnection{id, net_id, component_id, pin});
            }
        }

        for (const auto &port_object : array_field(module_object, "ports")) {
            const auto port_id = local_id<PortDefId>(port_object, seen_ports);
            const auto internal_net_id = string_field(port_object, "internal_net");
            require(local_template_nets.contains(internal_net_id),
                    "Template net does not belong to module definition");
            const auto required_it = port_object.find("required");
            auto required = true;
            if (required_it != port_object.end()) {
                require(required_it->is_boolean(), "Expected boolean field: required");
                required = required_it->get<bool>();
            }
            auto name = PortName{string_field(port_object, "name")};
            const auto role = port_role(optional_string_field(port_object, "role", "Passive"));
            parsed_ports.push_back(ParsedPortDefinition{port_id, id, std::move(name),
                                                        internal_net_id, role, required});
        }

        parsed_modules.push_back(
            ParsedModuleDefinition{id, ModuleDefinition{std::move(module_name)}});
        module_def_ids_.emplace(id, module);
    }

    std::stable_sort(parsed_template_nets.begin(), parsed_template_nets.end(),
                     [](const auto &lhs, const auto &rhs) {
                         return decode_local_id<TemplateNetDefId>(lhs.id).index() <
                                decode_local_id<TemplateNetDefId>(rhs.id).index();
                     });
    std::stable_sort(parsed_components.begin(), parsed_components.end(),
                     [](const auto &lhs, const auto &rhs) {
                         return decode_local_id<ModuleComponentId>(lhs.id).index() <
                                decode_local_id<ModuleComponentId>(rhs.id).index();
                     });
    std::stable_sort(parsed_ports.begin(), parsed_ports.end(),
                     [](const auto &lhs, const auto &rhs) {
                         return decode_local_id<PortDefId>(lhs.id).index() <
                                decode_local_id<PortDefId>(rhs.id).index();
                     });

    auto &restoration = plan_.hierarchy;
    for (auto &module : parsed_modules) {
        restoration.module_definitions.push_back(RestoredModuleDefinition{
            resolve(module_def_ids_, module.id), std::move(module.definition)});
    }
    for (std::size_t index = 0; index < parsed_template_nets.size(); ++index) {
        auto &net = parsed_template_nets[index];
        const auto id = TemplateNetDefId{index};
        template_net_ids_.emplace(net.id, id);
        restoration.template_nets.push_back(RestoredTemplateNetDefinition{
            id, resolve(module_def_ids_, net.module), std::move(net.definition)});
    }
    for (std::size_t index = 0; index < parsed_components.size(); ++index) {
        auto &component = parsed_components[index];
        const auto id = ModuleComponentId{index};
        module_component_ids_.emplace(component.id, id);
        restoration.components.push_back(RestoredModuleComponent{
            id, resolve(module_def_ids_, component.module), std::move(component.component)});
    }
    for (const auto &connection : parsed_connections) {
        restoration.connections.push_back(RestoredModulePinConnection{
            resolve(module_def_ids_, connection.module), resolve(template_net_ids_, connection.net),
            resolve(module_component_ids_, connection.component), connection.pin});
    }
    for (std::size_t index = 0; index < parsed_ports.size(); ++index) {
        auto &port = parsed_ports[index];
        const auto id = PortDefId{index};
        port_def_ids_.emplace(port.id, id);
        restoration.ports.push_back(RestoredPortDefinition{
            id, resolve(module_def_ids_, port.module),
            PortDefinition{std::move(port.name), resolve(template_net_ids_, port.internal_net),
                           port.role, port.required}});
    }
}

void LogicalCircuitParser::read_module_instances() {
    const auto modules = optional_array_field(document_, "module_instances");
    if (modules == nullptr) {
        return;
    }

    auto seen = std::set<std::string>{};
    for (const auto &instance_object : *modules) {
        static_cast<void>(local_id<ModuleInstanceId>(instance_object, seen));
        const auto definition =
            resolve(module_def_ids_, string_field(instance_object, "definition"));
        auto name = ModuleInstanceName{string_field(instance_object, "name")};
        auto origins = std::vector<std::pair<TemplateNetDefId, NetId>>{};
        for (const auto &origin_object : array_field(instance_object, "net_origins")) {
            origins.emplace_back(
                resolve(template_net_ids_, string_field(origin_object, "template_net")),
                resolve(net_ids_, string_field(origin_object, "net")));
        }
        auto component_origins = std::vector<std::pair<ModuleComponentId, ComponentId>>{};
        if (const auto components = optional_array_field(instance_object, "component_origins")) {
            for (const auto &origin_object : *components) {
                component_origins.emplace_back(
                    resolve(module_component_ids_,
                            string_field(origin_object, "template_component")),
                    resolve(component_ids_, string_field(origin_object, "component")));
            }
        } else {
            component_origins = infer_component_origins(definition, name);
        }

        auto bindings = std::vector<RestoredPortBinding>{};
        for (const auto &binding_object : array_field(instance_object, "port_bindings")) {
            bindings.push_back(RestoredPortBinding{
                resolve(port_def_ids_, string_field(binding_object, "port")),
                resolve(net_ids_, string_field(binding_object, "parent_net")),
            });
        }

        const auto instance = ModuleInstanceId{plan_.module_instances.size()};
        plan_.module_instances.push_back(RestoredModuleInstance{
            instance,
            ModuleInstanceRestoration{definition, std::move(name), std::move(origins),
                                      std::move(component_origins)},
            std::move(bindings),
        });
    }
}

[[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>>
LogicalCircuitParser::infer_component_origins(ModuleDefId definition,
                                              const ModuleInstanceName &name) const {
    auto component_origins = std::vector<std::pair<ModuleComponentId, ComponentId>>{};
    for (const auto &restored : plan_.hierarchy.components) {
        if (restored.module != definition) {
            continue;
        }
        const auto concrete_reference =
            ReferenceDesignator{name.value() + "/" + restored.component.reference().value()};
        const auto concrete_component = component_reference_ids_.find(concrete_reference.value());
        require(concrete_component != component_reference_ids_.end(),
                "Missing module instance concrete component for inferred component origin");
        component_origins.emplace_back(restored.id, concrete_component->second);
    }
    return component_origins;
}

} // namespace volt::io::detail
