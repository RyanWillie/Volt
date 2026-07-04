#include <volt/circuit/electrical/electrical_model.hpp>

#include "../circuit_storage.hpp"

#include <algorithm>
#include <memory>
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

ElectricalModel::ElectricalModel() : ElectricalModel{std::make_shared<detail::ElectricalState>()} {}

ElectricalModel::ElectricalModel(std::shared_ptr<const detail::ElectricalState> state)
    : state_{std::move(state)} {}

ElectricalModel::ElectricalModel(const ElectricalModel &other)
    : ElectricalModel{std::make_shared<detail::ElectricalState>(other.state())} {}

ElectricalModel::ElectricalModel(ElectricalModel &&other) noexcept = default;

ElectricalModel &ElectricalModel::operator=(const ElectricalModel &other) {
    if (this != &other) {
        state_ = std::make_shared<detail::ElectricalState>(other.state());
    }
    return *this;
}

ElectricalModel &ElectricalModel::operator=(ElectricalModel &&other) noexcept = default;

ElectricalModel::~ElectricalModel() = default;

void Circuit::ElectricalStorage::set_component_attribute(ComponentId component,
                                                         const ElectricalAttributeSpec &spec,
                                                         ElectricalAttributeValue value) {
    detail::require_attribute_owner(spec, ElectricalAttributeOwner::ComponentInstance);
    detail::mutable_attributes(mutable_state().component_attributes, component).set(spec, value);
}

void Circuit::ElectricalStorage::set_pin_definition_attribute(PinDefId pin_definition,
                                                              const ElectricalAttributeSpec &spec,
                                                              ElectricalAttributeValue value) {
    detail::require_attribute_owner(spec, ElectricalAttributeOwner::PinSpec);
    detail::mutable_attributes(mutable_state().pin_definition_attributes, pin_definition)
        .set(spec, value);
}

void Circuit::ElectricalStorage::set_net_attribute(NetId net, const ElectricalAttributeSpec &spec,
                                                   ElectricalAttributeValue value) {
    detail::require_attribute_owner(spec, ElectricalAttributeOwner::Net);
    detail::mutable_attributes(mutable_state().net_attributes, net).set(spec, value);
}

void Circuit::ElectricalStorage::select_physical_part(ComponentId component,
                                                      PhysicalPart physical_part,
                                                      const std::vector<PinDefId> &component_pins) {
    detail::require_physical_part_matches_component_definition(component_pins, physical_part);
    const auto existing =
        std::find_if(mutable_state().selected_physical_parts.begin(),
                     mutable_state().selected_physical_parts.end(),
                     [component](const auto &entry) { return entry.first == component; });
    if (existing == mutable_state().selected_physical_parts.end()) {
        mutable_state().selected_physical_parts.emplace_back(
            component, std::optional<PhysicalPart>{std::move(physical_part)});
        return;
    }

    existing->second = std::move(physical_part);
}

void Circuit::ElectricalStorage::set_selected_part_attribute(ComponentId component,
                                                             const ElectricalAttributeSpec &spec,
                                                             ElectricalAttributeValue value) {
    detail::require_attribute_owner(spec, ElectricalAttributeOwner::SelectedPart);
    const auto existing =
        std::find_if(mutable_state().selected_physical_parts.begin(),
                     mutable_state().selected_physical_parts.end(),
                     [component](const auto &entry) { return entry.first == component; });
    if (existing == mutable_state().selected_physical_parts.end()) {
        throw std::logic_error{"Component has no selected physical part"};
    }

    existing->second = existing->second->with_electrical_attribute(spec, value);
}

[[nodiscard]] const ElectricalAttributeMap &
ElectricalModel::component_attributes(ComponentId component) const noexcept {
    return attributes(state().component_attributes, component);
}

[[nodiscard]] const ElectricalAttributeMap &
ElectricalModel::pin_definition_attributes(PinDefId pin_definition) const noexcept {
    return attributes(state().pin_definition_attributes, pin_definition);
}

[[nodiscard]] const ElectricalAttributeMap &
ElectricalModel::net_attributes(NetId net) const noexcept {
    return attributes(state().net_attributes, net);
}

[[nodiscard]] const std::optional<PhysicalPart> &
ElectricalModel::selected_physical_part(ComponentId component) const noexcept {
    const auto existing =
        std::find_if(state().selected_physical_parts.begin(), state().selected_physical_parts.end(),
                     [component](const auto &entry) { return entry.first == component; });
    if (existing == state().selected_physical_parts.end()) {
        return empty_selected_part();
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

[[nodiscard]] const detail::ElectricalState &ElectricalModel::state() const noexcept {
    return *state_;
}

void detail::require_attribute_owner(const ElectricalAttributeSpec &spec,
                                     ElectricalAttributeOwner expected) {
    if (spec.owner() != expected) {
        throw std::logic_error{"Electrical attribute spec owner is not valid here"};
    }
}

void detail::require_physical_part_matches_component_definition(
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
