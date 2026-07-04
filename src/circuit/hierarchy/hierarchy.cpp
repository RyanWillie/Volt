#include <volt/circuit/hierarchy/hierarchy.hpp>

#include <volt/core/errors.hpp>

#include "../circuit_storage.hpp"

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

ModuleDefinition::ModuleDefinition(ModuleName name)
    : ModuleDefinition{std::make_shared<detail::ModuleDefinitionState>(std::move(name))} {}

ModuleDefinition::ModuleDefinition(std::shared_ptr<const detail::ModuleDefinitionState> state)
    : state_{std::move(state)} {}

ModuleDefinition::ModuleDefinition(const ModuleDefinition &other)
    : ModuleDefinition{std::make_shared<detail::ModuleDefinitionState>(other.state())} {}

ModuleDefinition::ModuleDefinition(ModuleDefinition &&other) noexcept = default;

ModuleDefinition &ModuleDefinition::operator=(const ModuleDefinition &other) {
    if (this != &other) {
        state_ = std::make_shared<detail::ModuleDefinitionState>(other.state());
    }
    return *this;
}

ModuleDefinition &ModuleDefinition::operator=(ModuleDefinition &&other) noexcept = default;

ModuleDefinition::~ModuleDefinition() = default;

[[nodiscard]] const ModuleName &ModuleDefinition::name() const noexcept { return state().name; }

[[nodiscard]] const std::vector<TemplateNetDefId> &
ModuleDefinition::template_nets() const noexcept {
    return state().template_nets;
}

[[nodiscard]] const std::vector<PortDefId> &ModuleDefinition::ports() const noexcept {
    return state().ports;
}

[[nodiscard]] const std::vector<ModuleComponentId> &ModuleDefinition::components() const noexcept {
    return state().components;
}

[[nodiscard]] const detail::ModuleDefinitionState &ModuleDefinition::state() const noexcept {
    return *state_;
}

namespace detail {

ModuleDefinitionStorage::ModuleDefinitionStorage(ModuleName name)
    : ModuleDefinitionStorage{std::make_shared<ModuleDefinitionState>(std::move(name))} {}

ModuleDefinitionStorage::ModuleDefinitionStorage(std::shared_ptr<ModuleDefinitionState> state)
    : ModuleDefinition{state}, state_{std::move(state)} {}

ModuleDefinitionStorage::ModuleDefinitionStorage(const ModuleDefinitionStorage &other)
    : ModuleDefinitionStorage{std::make_shared<ModuleDefinitionState>(other.state())} {}

ModuleDefinitionStorage &ModuleDefinitionStorage::operator=(const ModuleDefinitionStorage &other) {
    if (this != &other) {
        auto replacement =
            ModuleDefinitionStorage{std::make_shared<ModuleDefinitionState>(other.state())};
        *this = std::move(replacement);
    }
    return *this;
}

ModuleDefinitionStorage::ModuleDefinitionStorage(ModuleDefinition definition)
    : ModuleDefinitionStorage{std::make_shared<ModuleDefinitionState>(definition.name())} {
    state_->template_nets = definition.template_nets();
    state_->ports = definition.ports();
    state_->components = definition.components();
}

} // namespace detail

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
