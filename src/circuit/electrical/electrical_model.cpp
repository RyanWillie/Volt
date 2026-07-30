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

void Circuit::require_attribute_owner(const ElectricalAttributeSpec &spec,
                                      ElectricalAttributeOwner expected) {
    if (spec.owner() != expected) {
        throw KernelLogicError{ErrorCode::InvalidArgument,
                               "Electrical attribute spec owner is not valid here"};
    }
}

} // namespace volt
