#include <volt/circuit/electrical_mutations.hpp>

#include <utility>

namespace volt {

void CircuitElectrical::set_component_property(ComponentId component, PropertyKey key,
                                               PropertyValue value) {
    circuit_->set_component_property(component, std::move(key), std::move(value));
}
void CircuitElectrical::set_component_electrical_attribute(ComponentId component,
                                                           const ElectricalAttributeSpec &spec,
                                                           ElectricalAttributeValue value) {
    circuit_->set_component_electrical_attribute(component, spec, value);
}
void CircuitElectrical::set_pin_definition_electrical_attribute(PinDefId pin_definition,
                                                                const ElectricalAttributeSpec &spec,
                                                                ElectricalAttributeValue value) {
    circuit_->set_pin_definition_electrical_attribute(pin_definition, spec, value);
}
void CircuitElectrical::select_physical_part(ComponentId component, PhysicalPart physical_part) {
    circuit_->select_physical_part(component, std::move(physical_part));
}
void CircuitElectrical::set_selected_part_electrical_attribute(ComponentId component,
                                                               const ElectricalAttributeSpec &spec,
                                                               ElectricalAttributeValue value) {
    circuit_->set_selected_part_electrical_attribute(component, spec, value);
}
void CircuitElectrical::set_net_electrical_attribute(NetId net, const ElectricalAttributeSpec &spec,
                                                     ElectricalAttributeValue value) {
    circuit_->set_net_electrical_attribute(net, spec, value);
}

} // namespace volt
