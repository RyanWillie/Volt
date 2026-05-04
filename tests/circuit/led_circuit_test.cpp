#include <catch2/catch_test_macros.hpp>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/circuit/validation.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>

#include "led_circuit.hpp"

TEST_CASE("LED example builds a valid logical circuit") {
    const auto circuit = volt::examples::build_led_circuit();

    CHECK(circuit.component_count() == 3);
    CHECK(circuit.pin_count() == 6);
    CHECK(circuit.net_count() == 3);

    const auto vcc = circuit.net_by_name(volt::NetName{"VCC"});
    const auto led_a = circuit.net_by_name(volt::NetName{"LED_A"});
    const auto gnd = circuit.net_by_name(volt::NetName{"GND"});

    REQUIRE(vcc.has_value());
    REQUIRE(led_a.has_value());
    REQUIRE(gnd.has_value());
    CHECK(circuit.net(vcc.value()).kind() == volt::NetKind::Power);
    CHECK(circuit.net(led_a.value()).kind() == volt::NetKind::Signal);
    CHECK(circuit.net(gnd.value()).kind() == volt::NetKind::Ground);
    CHECK(circuit.net(vcc.value()).pins().size() == 2);
    CHECK(circuit.net(led_a.value()).pins().size() == 2);
    CHECK(circuit.net(gnd.value()).pins().size() == 2);

    const auto r1 = circuit.component_by_reference(volt::ReferenceDesignator{"R1"});
    REQUIRE(r1.has_value());
    CHECK(circuit.component(r1.value()).properties().get(volt::PropertyKey{"value"}) ==
          volt::PropertyValue{"330 ohm"});

    CHECK(volt::validate_circuit(circuit).empty());
}
