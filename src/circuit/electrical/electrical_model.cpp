#include <volt/circuit/circuit.hpp>

#include <volt/core/errors.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace volt {

[[nodiscard]] ElectricalAttributeMap
Circuit::preflight_attributes(const std::vector<ElectricalAttributeAssignment> &assignments,
                              ElectricalAttributeOwner owner) {
    auto attributes = ElectricalAttributeMap{};
    for (const auto &assignment : assignments) {
        require_attribute_owner(assignment.spec, owner);
        attributes.set(assignment.spec, assignment.value);
    }
    return attributes;
}

void Circuit::restore_component_attributes(ComponentId component,
                                           ElectricalAttributeMap attributes) {
    if (attributes.empty()) {
        return;
    }
    const auto &stored = get(component);
    connectivity_.replace_component(component,
                                    ComponentInstance{stored.definition(), stored.reference(),
                                                      stored.properties(), std::move(attributes)});
}

void Circuit::restore_pin_definition_attributes(PinDefId pin_definition,
                                                ElectricalAttributeMap attributes) {
    if (attributes.empty()) {
        return;
    }
    const auto &stored = get(pin_definition);
    connectivity_.replace_pin_definition(
        pin_definition,
        PinDefinition{stored.name(), stored.number(), stored.connection_requirement(),
                      stored.terminal_kind(), stored.direction(), stored.signal_domain(),
                      stored.drive_kind(), stored.polarity(), std::move(attributes)});
}

void Circuit::set_component_attribute(ComponentId component, const ElectricalAttributeSpec &spec,
                                      ElectricalAttributeValue value) {
    require_attribute_owner(spec, ElectricalAttributeOwner::ComponentInstance);
    connectivity_.replace_component(component,
                                    get(component).with_electrical_attribute(spec, value));
}

void Circuit::set_net_attribute(NetId net, const ElectricalAttributeSpec &spec,
                                ElectricalAttributeValue value) {
    require_attribute_owner(spec, ElectricalAttributeOwner::Net);
    connectivity_.replace_net(net, get(net).with_electrical_attribute(spec, value));
}

void Circuit::select_physical_part(ComponentId component, PhysicalPart physical_part,
                                   const std::vector<PinDefId> &component_pins) {
    require_physical_part_matches_component_definition(component_pins, physical_part);
    connectivity_.replace_component(
        component, get(component).with_selected_physical_part(std::move(physical_part)));
}

void Circuit::set_selected_part_attribute(ComponentId component,
                                          const ElectricalAttributeSpec &spec,
                                          ElectricalAttributeValue value) {
    require_attribute_owner(spec, ElectricalAttributeOwner::SelectedPart);
    if (!get(component).selected_physical_part().has_value()) {
        throw KernelLogicError{ErrorCode::InvalidState, "Component has no selected physical part",
                               EntityRef::component(component)};
    }
    connectivity_.replace_component(
        component, get(component).with_selected_part_electrical_attribute(spec, value));
}

void Circuit::require_attribute_owner(const ElectricalAttributeSpec &spec,
                                      ElectricalAttributeOwner expected) {
    if (spec.owner() != expected) {
        throw KernelLogicError{ErrorCode::InvalidArgument,
                               "Electrical attribute spec owner is not valid here"};
    }
}

void Circuit::require_physical_part_matches_component_definition(
    const std::vector<PinDefId> &component_pins, const PhysicalPart &physical_part) {
    for (const auto &mapping : physical_part.pin_pad_mappings()) {
        if (std::find(component_pins.begin(), component_pins.end(), mapping.pin()) ==
            component_pins.end()) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Physical part maps a pin outside the component definition",
                                   EntityRef::pin_def(mapping.pin())};
        }
    }

    for (const auto pin : component_pins) {
        const auto mapped = std::any_of(
            physical_part.pin_pad_mappings().begin(), physical_part.pin_pad_mappings().end(),
            [pin](const PinPadMapping &mapping) { return mapping.pin() == pin; });
        if (!mapped) {
            throw KernelLogicError{ErrorCode::InvalidArgument,
                                   "Physical part must map every pin in the component definition",
                                   EntityRef::pin_def(pin)};
        }
    }
}

} // namespace volt
