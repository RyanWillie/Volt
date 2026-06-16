#include <volt/circuit/electrical/electrical_model.hpp>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace volt {

namespace {

[[nodiscard]] const ElectricalAttributeMap &empty_attributes() noexcept {
    static const auto empty = ElectricalAttributeMap{};
    return empty;
}

[[nodiscard]] const std::optional<PhysicalPart> &empty_selected_part() noexcept {
    static const auto empty = std::optional<PhysicalPart>{};
    return empty;
}

} // namespace

void ElectricalModel::set_component_attribute(ComponentId component,
                                              const ElectricalAttributeSpec &spec,
                                              ElectricalAttributeValue value) {
    require_attribute_owner(spec, ElectricalAttributeOwner::ComponentInstance);
    mutable_attributes(component_attributes_, component).set(spec, value);
}

void ElectricalModel::set_pin_definition_attribute(PinDefId pin_definition,
                                                   const ElectricalAttributeSpec &spec,
                                                   ElectricalAttributeValue value) {
    require_attribute_owner(spec, ElectricalAttributeOwner::PinSpec);
    mutable_attributes(pin_definition_attributes_, pin_definition).set(spec, value);
}

void ElectricalModel::set_net_attribute(NetId net, const ElectricalAttributeSpec &spec,
                                        ElectricalAttributeValue value) {
    require_attribute_owner(spec, ElectricalAttributeOwner::Net);
    mutable_attributes(net_attributes_, net).set(spec, value);
}

void ElectricalModel::select_physical_part(ComponentId component, PhysicalPart physical_part,
                                           const std::vector<PinDefId> &component_pins) {
    require_physical_part_matches_component_definition(component_pins, physical_part);
    const auto existing =
        std::find_if(selected_physical_parts_.begin(), selected_physical_parts_.end(),
                     [component](const auto &entry) { return entry.first == component; });
    if (existing == selected_physical_parts_.end()) {
        selected_physical_parts_.emplace_back(
            component, std::optional<PhysicalPart>{std::move(physical_part)});
        return;
    }

    existing->second = std::move(physical_part);
}

void ElectricalModel::set_selected_part_attribute(ComponentId component,
                                                  const ElectricalAttributeSpec &spec,
                                                  ElectricalAttributeValue value) {
    require_attribute_owner(spec, ElectricalAttributeOwner::SelectedPart);
    const auto existing =
        std::find_if(selected_physical_parts_.begin(), selected_physical_parts_.end(),
                     [component](const auto &entry) { return entry.first == component; });
    if (existing == selected_physical_parts_.end()) {
        throw std::logic_error{"Component has no selected physical part"};
    }

    existing->second->set_electrical_attribute(spec, value);
}

[[nodiscard]] const ElectricalAttributeMap &
ElectricalModel::component_attributes(ComponentId component) const noexcept {
    return attributes(component_attributes_, component);
}

[[nodiscard]] const ElectricalAttributeMap &
ElectricalModel::pin_definition_attributes(PinDefId pin_definition) const noexcept {
    return attributes(pin_definition_attributes_, pin_definition);
}

[[nodiscard]] const ElectricalAttributeMap &
ElectricalModel::net_attributes(NetId net) const noexcept {
    return attributes(net_attributes_, net);
}

[[nodiscard]] const std::optional<PhysicalPart> &
ElectricalModel::selected_physical_part(ComponentId component) const noexcept {
    const auto existing =
        std::find_if(selected_physical_parts_.begin(), selected_physical_parts_.end(),
                     [component](const auto &entry) { return entry.first == component; });
    if (existing == selected_physical_parts_.end()) {
        return empty_selected_part();
    }

    return existing->second;
}

template <typename Id>
[[nodiscard]] ElectricalAttributeMap &
ElectricalModel::mutable_attributes(std::vector<std::pair<Id, ElectricalAttributeMap>> &entries,
                                    Id owner) {
    const auto existing = std::find_if(entries.begin(), entries.end(),
                                       [owner](const auto &entry) { return entry.first == owner; });
    if (existing == entries.end()) {
        entries.emplace_back(owner, ElectricalAttributeMap{});
        return entries.back().second;
    }

    return existing->second;
}

template <typename Id>
[[nodiscard]] const ElectricalAttributeMap &
ElectricalModel::attributes(const std::vector<std::pair<Id, ElectricalAttributeMap>> &entries,
                            Id owner) noexcept {
    const auto existing = std::find_if(entries.begin(), entries.end(),
                                       [owner](const auto &entry) { return entry.first == owner; });
    if (existing == entries.end()) {
        return empty_attributes();
    }

    return existing->second;
}

void ElectricalModel::require_attribute_owner(const ElectricalAttributeSpec &spec,
                                              ElectricalAttributeOwner expected) {
    if (spec.owner() != expected) {
        throw std::logic_error{"Electrical attribute spec owner is not valid here"};
    }
}

void ElectricalModel::require_physical_part_matches_component_definition(
    const std::vector<PinDefId> &component_pins, const PhysicalPart &physical_part) {
    for (const auto &mapping : physical_part.pin_pad_mappings()) {
        if (std::find(component_pins.begin(), component_pins.end(), mapping.pin()) ==
            component_pins.end()) {
            throw std::logic_error{"Physical part maps a pin outside the component definition"};
        }
    }

    for (const auto pin : component_pins) {
        const auto mapped = std::any_of(
            physical_part.pin_pad_mappings().begin(), physical_part.pin_pad_mappings().end(),
            [pin](const PinPadMapping &mapping) { return mapping.pin() == pin; });
        if (!mapped) {
            throw std::logic_error{"Physical part must map every pin in the component definition"};
        }
    }
}

} // namespace volt
