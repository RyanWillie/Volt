#pragma once

#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/circuit/instances.hpp>
#include <volt/circuit/nets.hpp>

namespace volt::examples {

inline Circuit build_led_circuit() {
    auto circuit = Circuit{};

    const auto connector_positive = circuit.add_pin_definition(
        PinDefinition{"+", "1", PinRole::Passive, ConnectionRequirement::Required});
    const auto connector_negative = circuit.add_pin_definition(
        PinDefinition{"-", "2", PinRole::Passive, ConnectionRequirement::Required});
    const auto resistor_pin_1 = circuit.add_pin_definition(
        PinDefinition{"1", "1", PinRole::Passive, ConnectionRequirement::Required});
    const auto resistor_pin_2 = circuit.add_pin_definition(
        PinDefinition{"2", "2", PinRole::Passive, ConnectionRequirement::Required});
    const auto led_anode = circuit.add_pin_definition(
        PinDefinition{"A", "1", PinRole::Passive, ConnectionRequirement::Required});
    const auto led_cathode = circuit.add_pin_definition(
        PinDefinition{"K", "2", PinRole::Passive, ConnectionRequirement::Required});

    const auto connector = circuit.add_component_definition(ComponentDefinition{
        "Two-pin connector", std::vector{connector_positive, connector_negative}});
    const auto resistor = circuit.add_component_definition(
        ComponentDefinition{"Resistor", std::vector{resistor_pin_1, resistor_pin_2}});
    const auto led = circuit.add_component_definition(
        ComponentDefinition{"LED", std::vector{led_anode, led_cathode}});

    const auto j1 = circuit.instantiate_component(connector, ReferenceDesignator{"J1"});
    const auto r1 = circuit.instantiate_component(resistor, ReferenceDesignator{"R1"});
    const auto d1 = circuit.instantiate_component(led, ReferenceDesignator{"D1"});

    const auto vcc = circuit.add_net(Net{NetName{"VCC"}, NetKind::Power});
    const auto led_a = circuit.add_net(Net{NetName{"LED_A"}, NetKind::Signal});
    const auto gnd = circuit.add_net(Net{NetName{"GND"}, NetKind::Ground});

    circuit.connect(vcc, circuit.pin_by_number(j1, "1").value());
    circuit.connect(vcc, circuit.pin_by_number(r1, "1").value());
    circuit.connect(led_a, circuit.pin_by_number(r1, "2").value());
    circuit.connect(led_a, circuit.pin_by_name(d1, "A").value());
    circuit.connect(gnd, circuit.pin_by_name(d1, "K").value());
    circuit.connect(gnd, circuit.pin_by_number(j1, "2").value());

    return circuit;
}

} // namespace volt::examples
