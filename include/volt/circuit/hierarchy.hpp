#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/instances.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>

namespace volt {

/** Human-facing reusable module name. */
class ModuleName {
  public:
    /** Construct a non-empty reusable module name. */
    explicit ModuleName(std::string value) : value_{std::move(value)} {
        if (value_.empty()) {
            throw std::invalid_argument{"Module name must not be empty"};
        }
    }

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
    explicit ModuleInstanceName(std::string value) : value_{std::move(value)} {
        if (value_.empty()) {
            throw std::invalid_argument{"Module instance name must not be empty"};
        }
    }

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
    explicit PortName(std::string value) : value_{std::move(value)} {
        if (value_.empty()) {
            throw std::invalid_argument{"Port name must not be empty"};
        }
    }

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
    TemplateNetDefinition(NetName name, NetKind kind) : name_{std::move(name)}, kind_{kind} {}

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
                   bool required = true)
        : name_{std::move(name)}, internal_net_{internal_net}, role_{role}, required_{required} {}

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
                            PropertyMap properties = {})
        : definition_{definition}, reference_{std::move(reference)},
          properties_{std::move(properties)} {}

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
    ModulePinConnection(TemplateNetDefId net, ModuleComponentId component, PinDefId pin)
        : net_{net}, component_{component}, pin_{pin} {}

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

/** Reusable logical module template. */
class ModuleDefinition {
  public:
    /** Construct an empty reusable logical module template. */
    explicit ModuleDefinition(ModuleName name) : name_{std::move(name)} {}

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

  private:
    friend class Circuit;

    void add_template_net(TemplateNetDefId net) { template_nets_.push_back(net); }
    void add_port(PortDefId port) { ports_.push_back(port); }
    void add_component(ModuleComponentId component) { components_.push_back(component); }

    ModuleName name_;
    std::vector<TemplateNetDefId> template_nets_;
    std::vector<PortDefId> ports_;
    std::vector<ModuleComponentId> components_;
};

/** Root-level occurrence of a reusable module definition. */
class ModuleInstance {
  public:
    /** Construct a root-level occurrence of a reusable module definition. */
    ModuleInstance(ModuleDefId definition, ModuleInstanceName name)
        : definition_{definition}, name_{std::move(name)} {}

    /** Return the reusable module definition instantiated here. */
    [[nodiscard]] ModuleDefId definition() const noexcept { return definition_; }
    /** Return the root-level instance name. */
    [[nodiscard]] const ModuleInstanceName &name() const noexcept { return name_; }

  private:
    ModuleDefId definition_;
    ModuleInstanceName name_;
};

/** Concrete net created from a module template net. */
class ModuleNetOrigin {
  public:
    /** Construct origin metadata for a concrete net copied from a template-local net. */
    ModuleNetOrigin(ModuleInstanceId instance, TemplateNetDefId template_net)
        : instance_{instance}, template_net_{template_net} {}

    /** Return the module instance that owns the concrete net. */
    [[nodiscard]] ModuleInstanceId instance() const noexcept { return instance_; }
    /** Return the template-local net copied into the concrete net. */
    [[nodiscard]] TemplateNetDefId template_net() const noexcept { return template_net_; }

  private:
    ModuleInstanceId instance_;
    TemplateNetDefId template_net_;
};

/** Concrete component created from a module component template. */
class ModuleComponentOrigin {
  public:
    /** Construct origin metadata for a concrete component copied from a module template. */
    ModuleComponentOrigin(ModuleInstanceId instance, ModuleComponentId component)
        : instance_{instance}, component_{component} {}

    /** Return the module instance that owns the concrete component. */
    [[nodiscard]] ModuleInstanceId instance() const noexcept { return instance_; }
    /** Return the module component template copied into the concrete component. */
    [[nodiscard]] ModuleComponentId component() const noexcept { return component_; }

  private:
    ModuleInstanceId instance_;
    ModuleComponentId component_;
};

/** Explicit edge from an instance-local concrete port net to a parent concrete net. */
class PortBinding {
  public:
    /** Construct an explicit connectivity edge from an instance port net to a parent net. */
    PortBinding(ModuleInstanceId instance, PortDefId port, NetId internal_net, NetId parent_net)
        : instance_{instance}, port_{port}, internal_net_{internal_net}, parent_net_{parent_net} {}

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
