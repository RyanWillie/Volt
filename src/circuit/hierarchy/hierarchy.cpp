#include <volt/circuit/hierarchy/hierarchy.hpp>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace volt {

ModuleName::ModuleName(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw std::invalid_argument{"Module name must not be empty"};
    }
}

ModuleInstanceName::ModuleInstanceName(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw std::invalid_argument{"Module instance name must not be empty"};
    }
}

PortName::PortName(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw std::invalid_argument{"Port name must not be empty"};
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

ModuleDefinition::ModuleDefinition(ModuleName name) : name_{std::move(name)} {}

[[nodiscard]] const std::vector<ModuleComponentId> &ModuleDefinition::components() const noexcept {
    return components_;
}

void ModuleDefinition::add_template_net(detail::KernelMutationAccess, TemplateNetDefId net) {
    template_nets_.push_back(net);
}

void ModuleDefinition::add_port(detail::KernelMutationAccess, PortDefId port) {
    ports_.push_back(port);
}

void ModuleDefinition::add_component(detail::KernelMutationAccess, ModuleComponentId component) {
    components_.push_back(component);
}

ModuleInstance::ModuleInstance(ModuleDefId definition, ModuleInstanceName name)
    : definition_{definition}, name_{std::move(name)} {}

ModuleNetOrigin::ModuleNetOrigin(ModuleInstanceId instance, TemplateNetDefId template_net)
    : instance_{instance}, template_net_{template_net} {}

ModuleComponentOrigin::ModuleComponentOrigin(ModuleInstanceId instance, ModuleComponentId component)
    : instance_{instance}, component_{component} {}

PortBinding::PortBinding(ModuleInstanceId instance, PortDefId port, NetId internal_net,
                         NetId parent_net)
    : instance_{instance}, port_{port}, internal_net_{internal_net}, parent_net_{parent_net} {}

} // namespace volt
