#pragma once

#include <optional>
#include <variant>

#include <volt/circuit/parts/parts.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>

namespace volt {

/** Set or replace one metadata property on a component instance. */
struct SetComponentProperty {
    /** Property key to set. */
    PropertyKey key;
    /** Property value to store. */
    PropertyValue value;
};

/** Set or replace one typed electrical attribute on a component instance. */
struct SetComponentElectricalAttribute {
    /** Component-instance-owned attribute specification. */
    ElectricalAttributeSpec spec;
    /** Typed attribute value to store. */
    ElectricalAttributeValue value;
};

/** Select one physical implementation for a component instance. */
struct SelectPhysicalPart {
    /** Complete selected physical part and pin-pad mapping. */
    PhysicalPart physical_part;
};

/** Set or replace one typed electrical attribute on a selected physical part. */
struct SetSelectedPartElectricalAttribute {
    /** Selected-part-owned attribute specification. */
    ElectricalAttributeSpec spec;
    /** Typed attribute value to store. */
    ElectricalAttributeValue value;
};

/** Progressively set component assembly intent without clearing unspecified fields. */
struct SetAssemblyIntent {
    /** Explicit do-not-populate value to set, when present. */
    std::optional<bool> dnp = std::nullopt;
    /** Selected-part override marker to set, when present. */
    std::optional<bool> selection_override = std::nullopt;
};

/** Closed set of progressive updates accepted for component instances. */
using ComponentUpdate =
    std::variant<SetComponentProperty, SetComponentElectricalAttribute, SelectPhysicalPart,
                 SetSelectedPartElectricalAttribute, SetAssemblyIntent>;

/** Set or replace one typed electrical attribute on a logical net. */
struct SetNetElectricalAttribute {
    /** Net-owned attribute specification. */
    ElectricalAttributeSpec spec;
    /** Typed attribute value to store. */
    ElectricalAttributeValue value;
};

/** Assign one existing net class to a logical net. */
struct AssignNetClass {
    /** Existing net class to assign. */
    NetClassId net_class;
};

/** Mark a logical net as an intentional exported stub. */
struct MarkIntentionalStub {};

/** Closed set of progressive updates accepted for logical nets. */
using NetUpdate = std::variant<SetNetElectricalAttribute, AssignNetClass, MarkIntentionalStub>;

} // namespace volt
