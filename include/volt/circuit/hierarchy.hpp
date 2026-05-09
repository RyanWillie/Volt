#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/nets.hpp>
#include <volt/core/ids.hpp>

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

  private:
    friend class Circuit;

    void add_template_net(TemplateNetDefId net) { template_nets_.push_back(net); }
    void add_port(PortDefId port) { ports_.push_back(port); }

    ModuleName name_;
    std::vector<TemplateNetDefId> template_nets_;
    std::vector<PortDefId> ports_;
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
