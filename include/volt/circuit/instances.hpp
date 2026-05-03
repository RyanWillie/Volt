#pragma once

#include <stdexcept>
#include <string>
#include <utility>

#include <volt/core/ids.hpp>

namespace volt {

/** Human-facing reference designator for a component instance, such as R1 or U1. */
class ReferenceDesignator {
  public:
    /** Construct a non-empty reference designator. */
    explicit ReferenceDesignator(std::string value) : value_{std::move(value)} {
        if (value_.empty()) {
            throw std::invalid_argument{"Reference designator must not be empty"};
        }
    }

    /** Return the stored reference designator string. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Return whether two reference designators carry the same value. */
    [[nodiscard]] friend bool operator==(const ReferenceDesignator &lhs,
                                         const ReferenceDesignator &rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

  private:
    std::string value_;
};

/** Concrete component occurrence in a design. */
class ComponentInstance {
  public:
    /** Construct an instance from a reusable component definition and reference. */
    ComponentInstance(ComponentDefId definition, ReferenceDesignator reference)
        : definition_{definition}, reference_{std::move(reference)} {}

    /** Return the reusable component definition used by this instance. */
    [[nodiscard]] ComponentDefId definition() const noexcept { return definition_; }

    /** Return this component's human-facing reference designator. */
    [[nodiscard]] const ReferenceDesignator &reference() const noexcept { return reference_; }

  private:
    ComponentDefId definition_;
    ReferenceDesignator reference_;
};

/** Concrete pin occurrence belonging to a component instance. */
class PinInstance {
  public:
    /** Construct a pin instance from its owning component and reusable pin definition. */
    PinInstance(ComponentId component, PinDefId definition)
        : component_{component}, definition_{definition} {}

    /** Return the component instance that owns this pin. */
    [[nodiscard]] ComponentId component() const noexcept { return component_; }

    /** Return the reusable pin definition used by this pin instance. */
    [[nodiscard]] PinDefId definition() const noexcept { return definition_; }

  private:
    ComponentId component_;
    PinDefId definition_;
};

} // namespace volt
