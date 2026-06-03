#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <volt/circuit/parts.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/ids.hpp>

namespace volt {

class Circuit;

/**
 * Owns typed electrical metadata and selected physical implementation state.
 *
 * Responsibility: stores typed electrical attributes (on pins, nets, components) and the
 *   selected physical part per component, keyed by entity ID.
 * Invariants: an attribute's value matches its spec's dimension; attributes attach only to
 *   entities the root has confirmed exist.
 * Collaborators: composed by Circuit; read by ERC rules; never references Circuit back (acyclic).
 */
class ElectricalModel {
  public:
    /** Return typed electrical metadata for a concrete component. */
    [[nodiscard]] const ElectricalAttributeMap &
    component_attributes(ComponentId component) const noexcept;

    /** Return typed electrical metadata for a reusable pin definition. */
    [[nodiscard]] const ElectricalAttributeMap &
    pin_definition_attributes(PinDefId pin_definition) const noexcept;

    /** Return typed electrical metadata for a logical net. */
    [[nodiscard]] const ElectricalAttributeMap &net_attributes(NetId net) const noexcept;

    /** Return the selected physical part for a concrete component, if present. */
    [[nodiscard]] const std::optional<PhysicalPart> &
    selected_physical_part(ComponentId component) const noexcept;

  private:
    friend class Circuit;

    /** Set typed electrical metadata for a concrete component. */
    void set_component_attribute(ComponentId component, const ElectricalAttributeSpec &spec,
                                 ElectricalAttributeValue value);

    /** Set typed electrical metadata for a reusable pin definition. */
    void set_pin_definition_attribute(PinDefId pin_definition, const ElectricalAttributeSpec &spec,
                                      ElectricalAttributeValue value);

    /** Set typed electrical metadata for a logical net. */
    void set_net_attribute(NetId net, const ElectricalAttributeSpec &spec,
                           ElectricalAttributeValue value);

    /** Select the physical part used to implement a concrete component. */
    void select_physical_part(ComponentId component, PhysicalPart physical_part,
                              const std::vector<PinDefId> &component_pins);

    /** Set typed electrical metadata for a selected physical part. */
    void set_selected_part_attribute(ComponentId component, const ElectricalAttributeSpec &spec,
                                     ElectricalAttributeValue value);

    template <typename Id>
    [[nodiscard]] static ElectricalAttributeMap &
    mutable_attributes(std::vector<std::pair<Id, ElectricalAttributeMap>> &entries, Id owner);

    template <typename Id>
    [[nodiscard]] static const ElectricalAttributeMap &
    attributes(const std::vector<std::pair<Id, ElectricalAttributeMap>> &entries,
               Id owner) noexcept;

    static void require_attribute_owner(const ElectricalAttributeSpec &spec,
                                        ElectricalAttributeOwner expected);

    static void
    require_physical_part_matches_component_definition(const std::vector<PinDefId> &component_pins,
                                                       const PhysicalPart &physical_part);

    std::vector<std::pair<ComponentId, ElectricalAttributeMap>> component_attributes_;
    std::vector<std::pair<PinDefId, ElectricalAttributeMap>> pin_definition_attributes_;
    std::vector<std::pair<NetId, ElectricalAttributeMap>> net_attributes_;
    std::vector<std::pair<ComponentId, std::optional<PhysicalPart>>> selected_physical_parts_;
};

} // namespace volt
