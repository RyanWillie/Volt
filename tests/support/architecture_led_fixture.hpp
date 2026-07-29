#pragma once

#include <vector>

#include <volt/authoring/component_library.hpp>
#include <volt/authoring/connection_helpers.hpp>
#include <volt/authoring/reference_designators.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/nets.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/properties.hpp>
#include <volt/pcb/footprints/footprints.hpp>

#include <support/compiled_board_export_helpers.hpp>

namespace volt::test {

inline Circuit build_architecture_led_fixture() {
    auto circuit = Circuit{};

    const auto lower_component_spec = [](const authoring::ComponentSpec &spec) {
        auto pins = std::vector<PinSpec>{};
        pins.reserve(spec.pins.size());
        for (const auto &pin : spec.pins) {
            auto attributes = std::vector<ElectricalAttributeAssignment>{};
            if (pin.voltage_range.has_value()) {
                attributes.push_back(ElectricalAttributeAssignment{
                    ElectricalAttributeSpec{
                        ElectricalAttributeName{"voltage_range"},
                        ElectricalAttributeOwner::PinSpec,
                        ElectricalAttributeKind::Constraint,
                        UnitDimension::Voltage,
                    },
                    ElectricalAttributeValue{*pin.voltage_range},
                });
            }
            pins.push_back(PinSpec{
                pin.name,
                pin.number,
                pin.requirement,
                pin.terminal_kind,
                pin.direction,
                pin.signal_domain,
                pin.drive_kind,
                pin.polarity,
                std::move(attributes),
            });
        }
        return ComponentSpec{spec.name, std::move(pins), spec.properties, spec.source,
                             spec.schematic_symbols};
    };
    const auto connector_spec = lower_component_spec(authoring::connector_1x02());
    const auto resistor_spec = lower_component_spec(authoring::resistor());
    const auto led_spec = lower_component_spec(authoring::led());
    const auto connector = circuit.define_component(connector_spec);
    const auto resistor = circuit.define_component(resistor_spec);
    const auto led = circuit.define_component(led_spec);

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

    const auto footprint = [](FootprintRef ref) {
        return FootprintDefinition{
            std::move(ref),
            std::vector{
                FootprintPad::surface_mount("1", FootprintPadShape::Rectangle,
                                            FootprintPoint{-0.75, 0.0}, FootprintSize{0.8, 0.9},
                                            FootprintLayerSet::front_smd()),
                FootprintPad::surface_mount("2", FootprintPadShape::Rectangle,
                                            FootprintPoint{0.75, 0.0}, FootprintSize{0.8, 0.9},
                                            FootprintLayerSet::front_smd()),
            }};
    };
    const auto connector_part =
        PhysicalPart{ManufacturerPart{"Generic", "HDR-1x02-2.54mm"}, PackageRef{"2.54mm-1x02"},
                     FootprintRef{"connectors", "PinHeader_1x02_P2.54mm_Vertical"},
                     std::vector{PinPadMapping{connector_positive, "1"},
                                 PinPadMapping{connector_negative, "2"}}};
    const auto resistor_part = PhysicalPart{
        ManufacturerPart{"Yageo", "RC0603FR-07330RL"}, PackageRef{"0603"},
        FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{PinPadMapping{resistor_pin_1, "1"}, PinPadMapping{resistor_pin_2, "2"}}};
    const auto led_part =
        PhysicalPart{ManufacturerPart{"Lite-On", "LTST-C190KRKT"}, PackageRef{"0603"},
                     FootprintRef{"leds", "LED_0603_1608Metric"},
                     std::vector{PinPadMapping{led_cathode, "1"}, PinPadMapping{led_anode, "2"}}};
    const auto library = make_export_fixture_library({
        ExportFixturePart{connector_spec, connector_part, footprint(connector_part.footprint()),
                          PartKey{"header"}},
        ExportFixturePart{resistor_spec, resistor_part, footprint(resistor_part.footprint()),
                          PartKey{"resistor"}},
        ExportFixturePart{led_spec, led_part, footprint(led_part.footprint()), PartKey{"led"}},
    });
    circuit.update(j1, SelectLibraryPart{library.bundle, library.bundle.require(library.keys[0])});
    circuit.update(r1, SelectLibraryPart{library.bundle, library.bundle.require(library.keys[1])});
    circuit.update(d1, SelectLibraryPart{library.bundle, library.bundle.require(library.keys[2])});

    const auto vcc = circuit.add_net(NetSpec{.name = NetName{"VCC"}, .kind = NetKind::Power});
    const auto led_a = circuit.add_net(NetSpec{.name = NetName{"LED_A"}, .kind = NetKind::Signal});
    const auto gnd = circuit.add_net(NetSpec{.name = NetName{"GND"}, .kind = NetKind::Ground});

    authoring::connect(circuit, vcc,
                       {queries::pin_by_number(circuit, j1, "1").value(),
                        queries::pin_by_number(circuit, r1, "1").value()});
    authoring::connect(circuit, led_a,
                       {queries::pin_by_number(circuit, r1, "2").value(),
                        queries::pin_by_name(circuit, d1, "A").value()});
    authoring::connect(circuit, gnd,
                       {queries::pin_by_name(circuit, d1, "K").value(),
                        queries::pin_by_number(circuit, j1, "2").value()});

    return circuit;
}

} // namespace volt::test
