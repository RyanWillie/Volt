#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/core/ids.hpp>

namespace volt {

enum class Severity {
    Info,
    Warning,
    Error,
};

class DiagnosticCode {
  public:
    explicit DiagnosticCode(std::string value) : value_{std::move(value)} {
        if (value_.empty()) {
            throw std::invalid_argument{"Diagnostic code must not be empty"};
        }
    }

    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    [[nodiscard]] friend bool operator==(const DiagnosticCode &lhs,
                                         const DiagnosticCode &rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

    [[nodiscard]] friend bool operator<(const DiagnosticCode &lhs,
                                        const DiagnosticCode &rhs) noexcept {
        return lhs.value_ < rhs.value_;
    }

  private:
    std::string value_;
};

enum class EntityKind {
    ComponentDef,
    Component,
    PinDef,
    Pin,
    Net,
};

class EntityRef {
  public:
    [[nodiscard]] static EntityRef component_def(ComponentDefId id) noexcept {
        return EntityRef{EntityKind::ComponentDef, id.index()};
    }

    [[nodiscard]] static EntityRef component(ComponentId id) noexcept {
        return EntityRef{EntityKind::Component, id.index()};
    }

    [[nodiscard]] static EntityRef pin_def(PinDefId id) noexcept {
        return EntityRef{EntityKind::PinDef, id.index()};
    }

    [[nodiscard]] static EntityRef pin(PinId id) noexcept {
        return EntityRef{EntityKind::Pin, id.index()};
    }

    [[nodiscard]] static EntityRef net(NetId id) noexcept {
        return EntityRef{EntityKind::Net, id.index()};
    }

    [[nodiscard]] EntityKind kind() const noexcept { return kind_; }

    [[nodiscard]] std::size_t index() const noexcept { return index_; }

    [[nodiscard]] friend bool operator==(EntityRef lhs, EntityRef rhs) noexcept = default;

  private:
    constexpr EntityRef(EntityKind kind, std::size_t index) noexcept : kind_{kind}, index_{index} {}

    EntityKind kind_;
    std::size_t index_;
};

class Diagnostic {
  public:
    Diagnostic(Severity severity, DiagnosticCode code, std::string message,
               std::vector<EntityRef> entities = {})
        : severity_{severity}, code_{std::move(code)}, message_{std::move(message)},
          entities_{std::move(entities)} {}

    [[nodiscard]] Severity severity() const noexcept { return severity_; }

    [[nodiscard]] const DiagnosticCode &code() const noexcept { return code_; }

    [[nodiscard]] const std::string &message() const noexcept { return message_; }

    [[nodiscard]] const std::vector<EntityRef> &entities() const noexcept { return entities_; }

  private:
    Severity severity_;
    DiagnosticCode code_;
    std::string message_;
    std::vector<EntityRef> entities_;
};

class DiagnosticReport {
  public:
    void add(Diagnostic diagnostic) { diagnostics_.push_back(std::move(diagnostic)); }

    [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const noexcept {
        return diagnostics_;
    }

    [[nodiscard]] bool empty() const noexcept { return diagnostics_.empty(); }

    [[nodiscard]] bool has_errors() const noexcept { return count(Severity::Error) > 0; }

    [[nodiscard]] std::size_t count() const noexcept { return diagnostics_.size(); }

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
