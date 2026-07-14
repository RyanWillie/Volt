#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <volt/circuit/parts/parts.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>

namespace volt {

/** Human-facing reference designator for a component instance, such as R1 or U1. */
class ReferenceDesignator {
  public:
    /** Construct a non-empty reference designator. */
    explicit ReferenceDesignator(std::string value);

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
                      PropertyMap properties = {},
                      ElectricalAttributeMap electrical_attributes = {});

    /** Return the reusable component definition used by this instance. */
    [[nodiscard]] ComponentDefId definition() const noexcept { return definition_; }

    /** Return this component's human-facing reference designator. */
    [[nodiscard]] const ReferenceDesignator &reference() const noexcept { return reference_; }

    /** Return extensible metadata properties for this component instance. */
    [[nodiscard]] const PropertyMap &properties() const noexcept { return properties_; }

    /** Return typed electrical attributes owned by this component instance. */
    [[nodiscard]] const ElectricalAttributeMap &electrical_attributes() const noexcept {
        return electrical_attributes_;
    }

    /** Return the selected physical implementation, if one has been assigned. */
    [[nodiscard]] const std::optional<PhysicalPart> &selected_physical_part() const noexcept {
        return selected_physical_part_;
    }

    /** Return explicit do-not-populate intent, when authored. */
    [[nodiscard]] const std::optional<bool> &dnp() const noexcept { return dnp_; }

    /** Return whether the selected part is an intentional instance override. */
    [[nodiscard]] bool selection_override() const noexcept { return selection_override_; }

    /** Return the first-authored order of this component's active assembly intent. */
    [[nodiscard]] const std::optional<std::size_t> &assembly_intent_order() const noexcept {
        return assembly_intent_order_;
    }

    /** Return a copy with one metadata property set or replaced. */
    [[nodiscard]] ComponentInstance with_property(PropertyKey key, PropertyValue value) const;

    /** Return a copy with one component electrical attribute set or replaced. */
    [[nodiscard]] ComponentInstance with_electrical_attribute(const ElectricalAttributeSpec &spec,
                                                              ElectricalAttributeValue value) const;

    /** Return a copy with the selected physical implementation replaced. */
    [[nodiscard]] ComponentInstance with_selected_physical_part(PhysicalPart part) const;

    /** Return a copy with one selected-part electrical attribute set or replaced. */
    [[nodiscard]] ComponentInstance
    with_selected_part_electrical_attribute(const ElectricalAttributeSpec &spec,
                                            ElectricalAttributeValue value) const;

    /** Return a copy with the supplied assembly-intent fields updated. */
    [[nodiscard]] ComponentInstance with_assembly_intent(std::optional<bool> dnp,
                                                         std::optional<bool> selection_override,
                                                         std::size_t first_authored_order) const;

  private:
    ComponentDefId definition_;
    ReferenceDesignator reference_;
    PropertyMap properties_;
    ElectricalAttributeMap electrical_attributes_;
    std::optional<PhysicalPart> selected_physical_part_;
    std::optional<bool> dnp_;
    bool selection_override_;
    std::optional<std::size_t> assembly_intent_order_;
};

/** Complete component instance input lowered atomically with all definition pins. */
struct ComponentInstanceSpec {
    /** Unique human-facing reference designator. */
    ReferenceDesignator reference;
    /** Instance metadata properties. */
    PropertyMap properties = {};
};

/** Concrete pin occurrence belonging to a component instance. */
class PinInstance {
  public:
    /** Construct a pin instance from its owning component and reusable pin definition. */
    PinInstance(ComponentId component, PinDefId definition);

    /** Return the component instance that owns this pin. */
    [[nodiscard]] ComponentId component() const noexcept { return component_; }

    /** Return the reusable pin definition used by this pin instance. */
    [[nodiscard]] PinDefId definition() const noexcept { return definition_; }

    /** Return whether the author explicitly marked this pin as intentionally unconnected. */
    [[nodiscard]] bool intentional_no_connect() const noexcept { return intentional_no_connect_; }

    /** Return the first-authored order of this pin's intentional no-connect marker. */
    [[nodiscard]] const std::optional<std::size_t> &intentional_no_connect_order() const noexcept {
        return intentional_no_connect_order_;
    }

    /** Return a copy marked as intentionally unconnected. */
    [[nodiscard]] PinInstance with_intentional_no_connect(std::size_t first_authored_order) const;

  private:
    ComponentId component_;
    PinDefId definition_;
    bool intentional_no_connect_;
    std::optional<std::size_t> intentional_no_connect_order_;
};

} // namespace volt
