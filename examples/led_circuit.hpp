#pragma once

#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/circuit/instances.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/core/properties.hpp>

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
        ComponentDefinition{"Resistor", std::vector{resistor_pin_1, resistor_pin_2},
                            PropertyMap{{PropertyKey{"category"}, PropertyValue{"passive"}}}});
    const auto led = circuit.add_component_definition(
        ComponentDefinition{"LED", std::vector{led_anode, led_cathode}});

    const auto j1 = circuit.instantiate_component(connector, ReferenceDesignator{"J1"});
    const auto r1 = circuit.instantiate_component(
        resistor, ReferenceDesignator{"R1"},
        PropertyMap{{PropertyKey{"value"}, PropertyValue{"330 ohm"}}});
    const auto d1 = circuit.instantiate_component(led, ReferenceDesignator{"D1"});

    circuit.select_physical_part(
        j1, PhysicalPart{ManufacturerPart{"Generic", "HDR-1x02-2.54mm"}, PackageRef{"2.54mm-1x02"},
                         FootprintRef{"connectors", "PinHeader_1x02_P2.54mm_Vertical"},
                         std::vector{PinPadMapping{connector_positive, "1"},
                                     PinPadMapping{connector_negative, "2"}}});
    circuit.select_physical_part(r1, PhysicalPart{ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                                                  PackageRef{"0603"},
                                                  FootprintRef{"passives", "R_0603_1608Metric"},
                                                  std::vector{PinPadMapping{resistor_pin_1, "1"},
                                                              PinPadMapping{resistor_pin_2, "2"}}});
    circuit.select_physical_part(
        d1,
        PhysicalPart{ManufacturerPart{"Lite-On", "LTST-C190KRKT"}, PackageRef{"0603"},
                     FootprintRef{"leds", "LED_0603_1608Metric"},
                     std::vector{PinPadMapping{led_cathode, "1"}, PinPadMapping{led_anode, "2"}}});

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
