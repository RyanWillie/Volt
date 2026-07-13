#pragma once

#include <vector>

#include <volt/authoring/component_library.hpp>
#include <volt/authoring/connection_helpers.hpp>
#include <volt/authoring/reference_designators.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/nets.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/properties.hpp>

namespace volt::examples {

inline Circuit build_led_circuit() {
    auto circuit = Circuit{};

    const auto connector = authoring::define_component(circuit, authoring::connector_1x02());
    const auto resistor = authoring::define_component(circuit, authoring::resistor());
    const auto led = authoring::define_component(circuit, authoring::led());

    const auto &connector_pins = circuit.get(connector).pins();
    const auto connector_positive = connector_pins[0];
    const auto connector_negative = connector_pins[1];
    const auto &resistor_pins = circuit.get(resistor).pins();
    const auto resistor_pin_1 = resistor_pins[0];
    const auto resistor_pin_2 = resistor_pins[1];
    const auto &led_pins = circuit.get(led).pins();
    const auto led_anode = led_pins[0];
    const auto led_cathode = led_pins[1];

    const auto j1 = authoring::instantiate(circuit, connector, "J");
    const auto r1 = authoring::instantiate(
        circuit, resistor, "R", PropertyMap{{PropertyKey{"value"}, PropertyValue{"330 ohm"}}});
    const auto d1 = authoring::instantiate(circuit, led, "D");

    circuit.update(j1,
                   SelectPhysicalPart{PhysicalPart{
                       ManufacturerPart{"Generic", "HDR-1x02-2.54mm"}, PackageRef{"2.54mm-1x02"},
                       FootprintRef{"connectors", "PinHeader_1x02_P2.54mm_Vertical"},
                       std::vector{PinPadMapping{connector_positive, "1"},
                                   PinPadMapping{connector_negative, "2"}}}});
    circuit.update(r1, SelectPhysicalPart{PhysicalPart{
                           ManufacturerPart{"Yageo", "RC0603FR-07330RL"}, PackageRef{"0603"},
                           FootprintRef{"passives", "R_0603_1608Metric"},
                           std::vector{PinPadMapping{resistor_pin_1, "1"},
                                       PinPadMapping{resistor_pin_2, "2"}}}});
    circuit.update(
        d1, SelectPhysicalPart{PhysicalPart{
                ManufacturerPart{"Lite-On", "LTST-C190KRKT"}, PackageRef{"0603"},
                FootprintRef{"leds", "LED_0603_1608Metric"},
                std::vector{PinPadMapping{led_cathode, "1"}, PinPadMapping{led_anode, "2"}}}});

    const auto vcc = circuit.add_net(NetSpec{.name = NetName{"VCC"}, .kind = NetKind::Power});
    const auto led_a = circuit.add_net(NetSpec{.name = NetName{"LED_A"}, .kind = NetKind::Signal});
    const auto gnd = circuit.add_net(NetSpec{.name = NetName{"GND"}, .kind = NetKind::Ground});

    authoring::connect(circuit, vcc,
                       {volt::queries::pin_by_number(circuit, j1, "1").value(),
                        volt::queries::pin_by_number(circuit, r1, "1").value()});
    authoring::connect(circuit, led_a,
                       {volt::queries::pin_by_number(circuit, r1, "2").value(),
                        volt::queries::pin_by_name(circuit, d1, "A").value()});
    authoring::connect(circuit, gnd,
                       {volt::queries::pin_by_name(circuit, d1, "K").value(),
                        volt::queries::pin_by_number(circuit, j1, "2").value()});

    return circuit;
}

} // namespace volt::examples
