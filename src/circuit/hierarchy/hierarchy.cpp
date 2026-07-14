#include <volt/circuit/hierarchy/hierarchy.hpp>

#include <volt/core/errors.hpp>

#include <string>
#include <utility>
#include <vector>

namespace volt {

ModuleName::ModuleName(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, "Module name must not be empty"};
    }
}

ModuleInstanceName::ModuleInstanceName(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Module instance name must not be empty"};
    }
}

PortName::PortName(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, "Port name must not be empty"};
    }
}

TemplateNetDefinition::TemplateNetDefinition(NetName name, NetKind kind)
    : name_{std::move(name)}, kind_{kind} {}

PortDefinition::PortDefinition(PortName name, TemplateNetDefId internal_net, PortRole role,
                               bool required)
    : name_{std::move(name)}, internal_net_{internal_net}, role_{role}, required_{required} {}

ModuleComponentTemplate::ModuleComponentTemplate(ComponentDefId definition,
                                                 ReferenceDesignator reference,
                                                 PropertyMap properties)
    : definition_{definition}, reference_{std::move(reference)},
      properties_{std::move(properties)} {}

ModulePinConnection::ModulePinConnection(TemplateNetDefId net, ModuleComponentId component,
                                         PinDefId pin)
    : net_{net}, component_{component}, pin_{pin} {}

ModuleDefinition::ModuleDefinition(ModuleName name, std::vector<TemplateNetDefId> template_nets,
                                   std::vector<PortDefId> ports,
                                   std::vector<ModuleComponentId> components,
                                   std::vector<ModulePinConnection> connections)
    : name_{std::move(name)}, template_nets_{std::move(template_nets)}, ports_{std::move(ports)},
      components_{std::move(components)}, connections_{std::move(connections)} {}

[[nodiscard]] ModuleDefinition ModuleDefinition::with_template_net(TemplateNetDefId net) && {
    template_nets_.push_back(net);
    return std::move(*this);
}

[[nodiscard]] ModuleDefinition ModuleDefinition::with_port(PortDefId port) && {
    ports_.push_back(port);
    return std::move(*this);
}

[[nodiscard]] ModuleDefinition ModuleDefinition::with_component(ModuleComponentId component) && {
    components_.push_back(component);
    return std::move(*this);
}

[[nodiscard]] ModuleDefinition
ModuleDefinition::with_connection(ModulePinConnection connection) && {
    connections_.push_back(connection);
    return std::move(*this);
}

ModuleInstance::ModuleInstance(
    ModuleDefId definition, ModuleInstanceName name,
    std::vector<std::pair<TemplateNetDefId, NetId>> net_origins,
    std::vector<std::pair<ModuleComponentId, ComponentId>> component_origins)
    : definition_{definition}, name_{std::move(name)}, net_origins_{std::move(net_origins)},
      component_origins_{std::move(component_origins)} {}

PortBinding::PortBinding(ModuleInstanceId instance, PortDefId port, NetId internal_net,
                         NetId parent_net)
    : instance_{instance}, port_{port}, internal_net_{internal_net}, parent_net_{parent_net} {}

} // namespace volt
