#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <volt/circuit/parts.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/ids.hpp>

namespace volt {

/** Owns typed electrical metadata and selected physical implementation state. */
class ElectricalModel {
  public:
    void set_component_attribute(ComponentId component, const ElectricalAttributeSpec &spec,
                                 ElectricalAttributeValue value);

    void set_pin_definition_attribute(PinDefId pin_definition, const ElectricalAttributeSpec &spec,
                                      ElectricalAttributeValue value);

    void set_net_attribute(NetId net, const ElectricalAttributeSpec &spec,
                           ElectricalAttributeValue value);

    void select_physical_part(ComponentId component, PhysicalPart physical_part,
                              const std::vector<PinDefId> &component_pins);

    void set_selected_part_attribute(ComponentId component, const ElectricalAttributeSpec &spec,
                                     ElectricalAttributeValue value);

    [[nodiscard]] const ElectricalAttributeMap &
    component_attributes(ComponentId component) const noexcept;

    [[nodiscard]] const ElectricalAttributeMap &
    pin_definition_attributes(PinDefId pin_definition) const noexcept;

    [[nodiscard]] const ElectricalAttributeMap &net_attributes(NetId net) const noexcept;

    [[nodiscard]] const std::optional<PhysicalPart> &
    selected_physical_part(ComponentId component) const noexcept;

  private:
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
