#include <catch2/catch_test_macros.hpp>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/nets.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/validation/validation.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>

#include "led_circuit.hpp"

TEST_CASE("LED example builds a valid logical circuit") {
    const auto circuit = volt::examples::build_led_circuit();

    CHECK(circuit.all<volt::ComponentId>().size() == 3);
    CHECK(circuit.all<volt::PinId>().size() == 6);
    CHECK(circuit.all<volt::NetId>().size() == 3);

    const auto vcc = volt::queries::net_by_name(circuit, volt::NetName{"VCC"});
    const auto led_a = volt::queries::net_by_name(circuit, volt::NetName{"LED_A"});
    const auto gnd = volt::queries::net_by_name(circuit, volt::NetName{"GND"});

    REQUIRE(vcc.has_value());
    REQUIRE(led_a.has_value());
    REQUIRE(gnd.has_value());
    CHECK(circuit.get(vcc.value()).kind() == volt::NetKind::Power);
    CHECK(circuit.get(led_a.value()).kind() == volt::NetKind::Signal);
    CHECK(circuit.get(gnd.value()).kind() == volt::NetKind::Ground);
    CHECK(circuit.get(vcc.value()).pins().size() == 2);
    CHECK(circuit.get(led_a.value()).pins().size() == 2);
    CHECK(circuit.get(gnd.value()).pins().size() == 2);

    const auto j1 = volt::queries::component_by_reference(circuit, volt::ReferenceDesignator{"J1"});
    const auto r1 = volt::queries::component_by_reference(circuit, volt::ReferenceDesignator{"R1"});
    const auto d1 = volt::queries::component_by_reference(circuit, volt::ReferenceDesignator{"D1"});
    REQUIRE(j1.has_value());
    REQUIRE(r1.has_value());
    REQUIRE(d1.has_value());
    CHECK(circuit.get(r1.value()).properties().get(volt::PropertyKey{"value"}) ==
          volt::PropertyValue{"330 ohm"});

    const auto &j1_part = volt::queries::selected_physical_part(circuit, j1.value());
    const auto &r1_part = volt::queries::selected_physical_part(circuit, r1.value());
    const auto &d1_part = volt::queries::selected_physical_part(circuit, d1.value());
    REQUIRE(j1_part.has_value());
    REQUIRE(r1_part.has_value());
    REQUIRE(d1_part.has_value());
    const auto &j1_pins = circuit.get(circuit.get(j1.value()).definition()).pins();
    const auto &r1_pins = circuit.get(circuit.get(r1.value()).definition()).pins();
    const auto &d1_pins = circuit.get(circuit.get(d1.value()).definition()).pins();

    CHECK(j1_part->package().value() == "2.54mm-1x02");
    CHECK(j1_part->footprint().name() == "PinHeader_1x02_P2.54mm_Vertical");
    REQUIRE(j1_part->pin_pad_mappings().size() == 2);
    CHECK(j1_part->pin_pad_mappings()[0].pin() == j1_pins[0]);
    CHECK(j1_part->pin_pad_mappings()[0].pad() == "1");
    CHECK(j1_part->pin_pad_mappings()[1].pin() == j1_pins[1]);
    CHECK(j1_part->pin_pad_mappings()[1].pad() == "2");
    CHECK(r1_part->package().value() == "0603");
    CHECK(r1_part->footprint().name() == "R_0603_1608Metric");
    REQUIRE(r1_part->pin_pad_mappings().size() == 2);
    CHECK(r1_part->pin_pad_mappings()[0].pin() == r1_pins[0]);
    CHECK(r1_part->pin_pad_mappings()[0].pad() == "1");
    CHECK(r1_part->pin_pad_mappings()[1].pin() == r1_pins[1]);
    CHECK(r1_part->pin_pad_mappings()[1].pad() == "2");
    CHECK(d1_part->package().value() == "0603");
    CHECK(d1_part->footprint().name() == "LED_0603_1608Metric");
    REQUIRE(d1_part->pin_pad_mappings().size() == 2);
    CHECK(d1_part->pin_pad_mappings()[0].pin() == d1_pins[1]);
    CHECK(d1_part->pin_pad_mappings()[0].pad() == "1");
    CHECK(d1_part->pin_pad_mappings()[1].pin() == d1_pins[0]);
    CHECK(d1_part->pin_pad_mappings()[1].pad() == "2");

    CHECK(volt::validate_circuit(circuit).empty());
}
