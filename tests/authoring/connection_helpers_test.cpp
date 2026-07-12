#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

#include <volt/authoring/connection_helpers.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>

#include <support/circuit_test_helpers.hpp>

namespace {

volt::ComponentDefId add_one_pin_component(volt::Circuit &circuit) {
    return volt::test::define_component(circuit, "OnePin", {volt::test::passive_pin("1", "1")});
}

} // namespace

TEST_CASE("Authoring connect helper connects multiple pins to one net") {
    auto circuit = volt::Circuit{};
    const auto component_definition = add_one_pin_component(circuit);
    const auto first = volt::test::instantiate_component(circuit, component_definition, "J1");
    const auto second = volt::test::instantiate_component(circuit, component_definition, "J2");
    const auto first_pin = volt::queries::pin_by_number(circuit, first, "1").value();
    const auto second_pin = volt::queries::pin_by_number(circuit, second, "1").value();
    const auto net = volt::test::add_net(circuit, "BUS");

    volt::authoring::connect(circuit, net, {first_pin, second_pin});

    REQUIRE(circuit.net(net).pins().size() == 2);
    CHECK(circuit.net(net).pins()[0] == first_pin);
    CHECK(circuit.net(net).pins()[1] == second_pin);
    CHECK(volt::queries::net_of(circuit, first_pin) == net);
    CHECK(volt::queries::net_of(circuit, second_pin) == net);
}

TEST_CASE("Authoring connect helper accepts deterministic pin vectors") {
    auto circuit = volt::Circuit{};
    const auto component_definition = add_one_pin_component(circuit);
    const auto first = volt::test::instantiate_component(circuit, component_definition, "J1");
    const auto second = volt::test::instantiate_component(circuit, component_definition, "J2");
    const auto pins = std::vector{
        volt::queries::pin_by_number(circuit, first, "1").value(),
        volt::queries::pin_by_number(circuit, second, "1").value(),
    };
    const auto net = volt::test::add_net(circuit, "BUS");

    volt::authoring::connect(circuit, net, pins);

    CHECK(circuit.net(net).pins() == pins);
}

TEST_CASE("Authoring connect helper preserves Circuit structural checks") {
    auto circuit = volt::Circuit{};
    const auto component_definition = add_one_pin_component(circuit);
    const auto component = volt::test::instantiate_component(circuit, component_definition, "J1");
    const auto pin = volt::queries::pin_by_number(circuit, component, "1").value();
    const auto first_net = volt::test::add_net(circuit, "FIRST");
    const auto second_net = volt::test::add_net(circuit, "SECOND");

    volt::authoring::connect(circuit, first_net, {pin});

    CHECK_THROWS_AS(volt::authoring::connect(circuit, second_net, {pin}), std::logic_error);
    CHECK(volt::queries::net_of(circuit, pin) == first_net);
}
