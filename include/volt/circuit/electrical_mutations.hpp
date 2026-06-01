#pragma once

#include <volt/circuit/circuit.hpp>

namespace volt {

/** Kernel-owned electrical and physical-part mutation surface over a Circuit invariant core. */
class CircuitElectrical {
  public:
    /** Construct electrical mutations over an existing logical circuit. */
    explicit CircuitElectrical(Circuit &circuit) noexcept : circuit_{&circuit} {}

    /** Set or replace a metadata property on an existing component instance. */
    void set_component_property(ComponentId component, PropertyKey key, PropertyValue value);

    /** Set or replace a typed electrical attribute on an existing component instance. */
    void set_component_electrical_attribute(ComponentId component,
                                            const ElectricalAttributeSpec &spec,
                                            ElectricalAttributeValue value);

    /** Set or replace a typed electrical attribute on an existing reusable pin definition. */
    void set_pin_definition_electrical_attribute(PinDefId pin_definition,
                                                 const ElectricalAttributeSpec &spec,
                                                 ElectricalAttributeValue value);

    /** Assign a selected physical implementation to an existing component instance. */
    void select_physical_part(ComponentId component, PhysicalPart physical_part);

    /** Set or replace a typed electrical attribute on a component's selected physical part. */
    void set_selected_part_electrical_attribute(ComponentId component,
                                                const ElectricalAttributeSpec &spec,
                                                ElectricalAttributeValue value);

    /** Set or replace a typed electrical attribute on an existing net. */
    void set_net_electrical_attribute(NetId net, const ElectricalAttributeSpec &spec,
                                      ElectricalAttributeValue value);

  private:
    Circuit *circuit_;
};

} // namespace volt
