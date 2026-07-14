#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/connectivity/instances.hpp>
#include <volt/circuit/connectivity/nets.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>

namespace volt {

/** Human-facing reusable module name. */
class ModuleName {
  public:
    /** Construct a non-empty reusable module name. */
    explicit ModuleName(std::string value);

    /** Return the stored module name string. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Return whether two module names carry the same string. */
    [[nodiscard]] friend bool operator==(const ModuleName &lhs, const ModuleName &rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

  private:
    std::string value_;
};

/** Human-facing module instance name. */
class ModuleInstanceName {
  public:
    /** Construct a non-empty root-level module instance name. */
    explicit ModuleInstanceName(std::string value);

    /** Return the stored module instance name string. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Return whether two module instance names carry the same string. */
    [[nodiscard]] friend bool operator==(const ModuleInstanceName &lhs,
                                         const ModuleInstanceName &rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

  private:
    std::string value_;
};

/** Human-facing module port name. */
class PortName {
  public:
    /** Construct a non-empty module port name. */
    explicit PortName(std::string value);

    /** Return the stored module port name string. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Return whether two port names carry the same string. */
    [[nodiscard]] friend bool operator==(const PortName &lhs, const PortName &rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

  private:
    std::string value_;
};

/** Broad electrical role for a module boundary port. */
enum class PortRole {
    Passive,
    Input,
    Output,
    Bidirectional,
    PowerInput,
    PowerOutput,
    Ground,
};

/** Template-local net declared inside a reusable module definition. */
class TemplateNetDefinition {
  public:
    /** Construct a template-local net definition with its scoped name and kind. */
    TemplateNetDefinition(NetName name, NetKind kind);

    /** Return the template-local net name. */
    [[nodiscard]] const NetName &name() const noexcept { return name_; }

    /** Return the net kind copied to concrete nets at module instantiation. */
    [[nodiscard]] NetKind kind() const noexcept { return kind_; }

  private:
    NetName name_;
    NetKind kind_;
};

/** Boundary port declared by a reusable module definition. */
class PortDefinition {
  public:
    /** Construct a boundary port that maps to exactly one template-local internal net. */
    PortDefinition(PortName name, TemplateNetDefId internal_net, PortRole role = PortRole::Passive,
                   bool required = true);

    /** Return the module-local port name. */
    [[nodiscard]] const PortName &name() const noexcept { return name_; }

    /** Return the template-local net exposed by this port. */
    [[nodiscard]] TemplateNetDefId internal_net() const noexcept { return internal_net_; }

    /** Return the broad electrical role for this module boundary. */
    [[nodiscard]] PortRole role() const noexcept { return role_; }

    /** Return whether each module instance must bind this port. */
    [[nodiscard]] bool required() const noexcept { return required_; }

  private:
    PortName name_;
    TemplateNetDefId internal_net_;
    PortRole role_;
    bool required_;
};

/** Component occurrence declared inside a reusable module definition. */
class ModuleComponentTemplate {
  public:
    /** Construct a module-local component occurrence from a reusable component definition. */
    ModuleComponentTemplate(ComponentDefId definition, ReferenceDesignator reference,
                            PropertyMap properties = {});

    /** Return the reusable component definition instantiated by this template. */
    [[nodiscard]] ComponentDefId definition() const noexcept { return definition_; }

    /** Return the module-local reference designator. */
    [[nodiscard]] const ReferenceDesignator &reference() const noexcept { return reference_; }

    /** Return module-local instance properties copied to concrete instances. */
    [[nodiscard]] const PropertyMap &properties() const noexcept { return properties_; }

  private:
    ComponentDefId definition_;
    ReferenceDesignator reference_;
    PropertyMap properties_;
};

/** Template-local connection from one module component pin to one module template net. */
class ModulePinConnection {
  public:
    /** Construct a module-local pin connection. */
    ModulePinConnection(TemplateNetDefId net, ModuleComponentId component, PinDefId pin);

    /** Return the template net connected to the pin. */
    [[nodiscard]] TemplateNetDefId net() const noexcept { return net_; }

    /** Return the module component template that owns the pin. */
    [[nodiscard]] ModuleComponentId component() const noexcept { return component_; }

    /** Return the reusable pin definition connected on that component template. */
    [[nodiscard]] PinDefId pin() const noexcept { return pin_; }

  private:
    TemplateNetDefId net_;
    ModuleComponentId component_;
    PinDefId pin_;
};

/** Complete module-local pin connection input using domain labels and canonical PinDefId. */
struct ModulePinConnectionSpec {
    /** Target template-local net name. */
    NetName net;
    /** Target module-local component reference. */
    ReferenceDesignator component;
    /** Canonical pin definition owned by the component definition. */
    PinDefId pin;
};

/** Complete module boundary-port input resolved against the ModuleSpec's local nets. */
struct ModulePortSpec {
    /** Unique module-local port name. */
    PortName name;
    /** Template-local net exposed by the port. */
    NetName internal_net;
    /** Electrical direction or power role at the module boundary. */
    PortRole role = PortRole::Passive;
    /** Whether normal design intent expects the port to be bound. */
    bool required = true;
};

/** Complete reusable module input lowered atomically by Circuit. */
struct ModuleSpec {
    /** Unique reusable module name. */
    ModuleName name;
    /** Ordered template-local net definitions. */
    std::vector<TemplateNetDefinition> template_nets = {};
    /** Ordered module-local component occurrences. */
    std::vector<ModuleComponentTemplate> components = {};
    /** Ordered internal component-pin connections. */
    std::vector<ModulePinConnectionSpec> connections = {};
    /** Ordered module boundary ports. */
    std::vector<ModulePortSpec> ports = {};
};

/** Reusable logical module template. */
class ModuleDefinition {
  public:
    /** Construct a reusable logical module template from its complete owned relationships. */
    explicit ModuleDefinition(ModuleName name, std::vector<TemplateNetDefId> template_nets = {},
                              std::vector<PortDefId> ports = {},
                              std::vector<ModuleComponentId> components = {},
                              std::vector<ModulePinConnection> connections = {});

    /** Return the reusable module name. */
    [[nodiscard]] const ModuleName &name() const noexcept { return name_; }

    /** Return template-local nets in deterministic insertion order. */
    [[nodiscard]] const std::vector<TemplateNetDefId> &template_nets() const noexcept {
        return template_nets_;
    }

    /** Return module ports in deterministic insertion order. */
    [[nodiscard]] const std::vector<PortDefId> &ports() const noexcept { return ports_; }

    /** Return component templates in deterministic insertion order. */
    [[nodiscard]] const std::vector<ModuleComponentId> &components() const noexcept {
        return components_;
    }

    /** Return module-local pin connections in deterministic insertion order. */
    [[nodiscard]] const std::vector<ModulePinConnection> &connections() const noexcept {
        return connections_;
    }

    /** Append one owned template net while consuming this temporary value. */
    [[nodiscard]] ModuleDefinition with_template_net(TemplateNetDefId net) &&;

    /** Append one owned port while consuming this temporary value. */
    [[nodiscard]] ModuleDefinition with_port(PortDefId port) &&;

    /** Append one owned component template while consuming this temporary value. */
    [[nodiscard]] ModuleDefinition with_component(ModuleComponentId component) &&;

    /** Append one module-local pin connection while consuming this temporary value. */
    [[nodiscard]] ModuleDefinition with_connection(ModulePinConnection connection) &&;

  private:
    ModuleName name_;
    std::vector<TemplateNetDefId> template_nets_;
    std::vector<PortDefId> ports_;
    std::vector<ModuleComponentId> components_;
    std::vector<ModulePinConnection> connections_;
};

/** Complete root module-instance input lowered atomically by Circuit. */
struct ModuleInstanceSpec {
    /** Unique root-level module instance name. */
    ModuleInstanceName name;
};

/** Root-level occurrence of a reusable module definition. */
class ModuleInstance {
  public:
    /** Construct a root-level occurrence of a reusable module definition. */
    ModuleInstance(ModuleDefId definition, ModuleInstanceName name,
                   std::vector<std::pair<TemplateNetDefId, NetId>> net_origins = {},
                   std::vector<std::pair<ModuleComponentId, ComponentId>> component_origins = {});

    /** Return the reusable module definition instantiated here. */
    [[nodiscard]] ModuleDefId definition() const noexcept { return definition_; }

    /** Return the root-level instance name. */
    [[nodiscard]] const ModuleInstanceName &name() const noexcept { return name_; }

    /** Return concrete net origins in module template-net order. */
    [[nodiscard]] const std::vector<std::pair<TemplateNetDefId, NetId>> &
    net_origins() const noexcept {
        return net_origins_;
    }

    /** Return concrete component origins in module component order. */
    [[nodiscard]] const std::vector<std::pair<ModuleComponentId, ComponentId>> &
    component_origins() const noexcept {
        return component_origins_;
    }

  private:
    ModuleDefId definition_;
    ModuleInstanceName name_;
    std::vector<std::pair<TemplateNetDefId, NetId>> net_origins_;
    std::vector<std::pair<ModuleComponentId, ComponentId>> component_origins_;
};

/** Explicit edge from an instance-local concrete port net to a parent concrete net. */
class PortBinding {
  public:
    /** Construct an explicit connectivity edge from an instance port net to a parent net. */
    PortBinding(ModuleInstanceId instance, PortDefId port, NetId internal_net, NetId parent_net);

    /** Return the module instance whose port is bound. */
    [[nodiscard]] ModuleInstanceId instance() const noexcept { return instance_; }

    /** Return the module port definition being bound. */
    [[nodiscard]] PortDefId port() const noexcept { return port_; }

    /** Return the instance-local concrete port net. */
    [[nodiscard]] NetId internal_net() const noexcept { return internal_net_; }

    /** Return the parent/root concrete net connected by this binding edge. */
    [[nodiscard]] NetId parent_net() const noexcept { return parent_net_; }

  private:
    ModuleInstanceId instance_;
    PortDefId port_;
    NetId internal_net_;
    NetId parent_net_;
};

} // namespace volt
