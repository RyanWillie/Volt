#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <volt/circuit/parts.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>

namespace volt {

class Circuit;

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
    /** Construct an instance from a reusable component definition, reference, and properties. */
    ComponentInstance(ComponentDefId definition, ReferenceDesignator reference,
                      PropertyMap properties = {})
        : definition_{definition}, reference_{std::move(reference)},
          properties_{std::move(properties)} {}

    /** Return the reusable component definition used by this instance. */
    [[nodiscard]] ComponentDefId definition() const noexcept { return definition_; }

    /** Return this component's human-facing reference designator. */
    [[nodiscard]] const ReferenceDesignator &reference() const noexcept { return reference_; }

    /** Return extensible metadata properties for this component instance. */
    [[nodiscard]] const PropertyMap &properties() const noexcept { return properties_; }

    /** Return the selected physical implementation for this component, if assigned. */
    [[nodiscard]] const std::optional<PhysicalPart> &selected_physical_part() const noexcept {
        return selected_physical_part_;
    }

  private:
    friend class Circuit;

    void set_property(PropertyKey key, PropertyValue value) {
        properties_.set(std::move(key), std::move(value));
    }

    void select_physical_part(PhysicalPart physical_part) {
        selected_physical_part_ = std::move(physical_part);
    }

    ComponentDefId definition_;
    ReferenceDesignator reference_;
    PropertyMap properties_;
    std::optional<PhysicalPart> selected_physical_part_;
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
