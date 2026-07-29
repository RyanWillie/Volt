#include <catch2/catch_test_macros.hpp>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/nets.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/validation/validation.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>

#include <support/architecture_led_fixture.hpp>

TEST_CASE("Purpose-built LED fixture builds a valid logical circuit") {
    const auto circuit = volt::test::build_architecture_led_fixture();

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

    const auto &j1_part = volt::queries::selected_library_part_ref(circuit, j1.value());
    const auto &r1_part = volt::queries::selected_library_part_ref(circuit, r1.value());
    const auto &d1_part = volt::queries::selected_library_part_ref(circuit, d1.value());
    REQUIRE(j1_part.has_value());
    REQUIRE(r1_part.has_value());
    REQUIRE(d1_part.has_value());
    CHECK(j1_part->library_namespace() == "test.export");
    CHECK(j1_part->part_key().value() == "header");
    CHECK(r1_part->part_key().value() == "resistor");
    CHECK(d1_part->part_key().value() == "led");

    CHECK(volt::validate_circuit(circuit).empty());
}
