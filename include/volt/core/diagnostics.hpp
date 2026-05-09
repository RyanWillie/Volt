#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/core/ids.hpp>

namespace volt {

/** Severity level for a diagnostic emitted by the kernel. */
enum class Severity {
    Info,
    Warning,
    Error,
};

/** Machine-readable diagnostic code. */
class DiagnosticCode {
  public:
    /** Construct a non-empty diagnostic code. */
    explicit DiagnosticCode(std::string value) : value_{std::move(value)} {
        if (value_.empty()) {
            throw std::invalid_argument{"Diagnostic code must not be empty"};
        }
    }

    /** Return the stored diagnostic code string. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Return whether two diagnostic codes carry the same value. */
    [[nodiscard]] friend bool operator==(const DiagnosticCode &lhs,
                                         const DiagnosticCode &rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

    /** Order diagnostic codes lexicographically by value. */
    [[nodiscard]] friend bool operator<(const DiagnosticCode &lhs,
                                        const DiagnosticCode &rhs) noexcept {
        return lhs.value_ < rhs.value_;
    }

  private:
    std::string value_;
};

/** Kind of entity referenced by a diagnostic. */
enum class EntityKind {
    ComponentDef,
    Component,
    PinDef,
    Pin,
    Net,
    ModuleDef,
    ModuleInstance,
    PortDef,
};

/**
 * Reference to an entity involved in a diagnostic.
 *
 * EntityRef is a reporting type, not the core storage model. It deliberately stores kind
 * plus index so diagnostics can refer to different entity families uniformly.
 */
class EntityRef {
  public:
    /** Create a reference to a component definition. */
    [[nodiscard]] static EntityRef component_def(ComponentDefId id) noexcept {
        return EntityRef{EntityKind::ComponentDef, id.index()};
    }

    /** Create a reference to a component instance. */
    [[nodiscard]] static EntityRef component(ComponentId id) noexcept {
        return EntityRef{EntityKind::Component, id.index()};
    }

    /** Create a reference to a pin definition. */
    [[nodiscard]] static EntityRef pin_def(PinDefId id) noexcept {
        return EntityRef{EntityKind::PinDef, id.index()};
    }

    /** Create a reference to a pin instance. */
    [[nodiscard]] static EntityRef pin(PinId id) noexcept {
        return EntityRef{EntityKind::Pin, id.index()};
    }

    /** Create a reference to a net. */
    [[nodiscard]] static EntityRef net(NetId id) noexcept {
        return EntityRef{EntityKind::Net, id.index()};
    }

    /** Create a reference to a module definition. */
    [[nodiscard]] static EntityRef module_def(ModuleDefId id) noexcept {
        return EntityRef{EntityKind::ModuleDef, id.index()};
    }

    /** Create a reference to a module instance. */
    [[nodiscard]] static EntityRef module_instance(ModuleInstanceId id) noexcept {
        return EntityRef{EntityKind::ModuleInstance, id.index()};
    }

    /** Create a reference to a module port definition. */
    [[nodiscard]] static EntityRef port_def(PortDefId id) noexcept {
        return EntityRef{EntityKind::PortDef, id.index()};
    }

    /** Return the kind of entity referenced. */
    [[nodiscard]] EntityKind kind() const noexcept { return kind_; }

    /** Return the entity table index referenced. */
    [[nodiscard]] std::size_t index() const noexcept { return index_; }

    /** Return whether two entity references point at the same entity kind and index. */
    [[nodiscard]] friend bool operator==(EntityRef lhs, EntityRef rhs) noexcept = default;

  private:
    constexpr EntityRef(EntityKind kind, std::size_t index) noexcept : kind_{kind}, index_{index} {}

    EntityKind kind_;
    std::size_t index_;
};

/** Human- and machine-readable diagnostic emitted by kernel checks. */
class Diagnostic {
  public:
    /** Construct a diagnostic with optional related entities. */
    Diagnostic(Severity severity, DiagnosticCode code, std::string message,
               std::vector<EntityRef> entities = {})
        : severity_{severity}, code_{std::move(code)}, message_{std::move(message)},
          entities_{std::move(entities)} {}

    /** Return the diagnostic severity. */
    [[nodiscard]] Severity severity() const noexcept { return severity_; }

    /** Return the machine-readable code. */
    [[nodiscard]] const DiagnosticCode &code() const noexcept { return code_; }

    /** Return the human-readable message. */
    [[nodiscard]] const std::string &message() const noexcept { return message_; }

    /** Return entities related to this diagnostic. */
    [[nodiscard]] const std::vector<EntityRef> &entities() const noexcept { return entities_; }

  private:
    Severity severity_;
    DiagnosticCode code_;
    std::string message_;
    std::vector<EntityRef> entities_;
};

/** Ordered collection of diagnostics from one or more kernel checks. */
class DiagnosticReport {
  public:
    /** Append a diagnostic while preserving insertion order. */
    void add(Diagnostic diagnostic) { diagnostics_.push_back(std::move(diagnostic)); }

    /** Return all diagnostics in insertion order. */
    [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const noexcept {
        return diagnostics_;
    }

    /** Return whether the report has no diagnostics. */
    [[nodiscard]] bool empty() const noexcept { return diagnostics_.empty(); }

    /** Return whether any diagnostic has error severity. */
    [[nodiscard]] bool has_errors() const noexcept { return count(Severity::Error) > 0; }

    /** Return the total diagnostic count. */
    [[nodiscard]] std::size_t count() const noexcept { return diagnostics_.size(); }

    /** Return the number of diagnostics with the requested severity. */
    [[nodiscard]] std::size_t count(Severity severity) const noexcept {
        return static_cast<std::size_t>(std::count_if(diagnostics_.begin(), diagnostics_.end(),
                                                      [severity](const Diagnostic &diagnostic) {
                                                          return diagnostic.severity() == severity;
                                                      }));
    }

  private:
    std::vector<Diagnostic> diagnostics_;
};

} // namespace volt
