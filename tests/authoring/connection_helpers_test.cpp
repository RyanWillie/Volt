#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

#include <volt/authoring/connection_helpers.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/circuit_view.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/circuit/nets.hpp>

namespace {

volt::ComponentDefId add_one_pin_component(volt::Circuit &circuit) {
    const auto pin =
        circuit.add_pin_definition(volt::PinDefinition{"1", "1", volt::PinRole::Passive});
    return circuit.add_component_definition(volt::ComponentDefinition{"OnePin", std::vector{pin}});
}

} // namespace

TEST_CASE("Authoring connect helper connects multiple pins to one net") {
    auto circuit = volt::Circuit{};
    const auto component_definition = add_one_pin_component(circuit);
    const auto first =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"J1"});
    const auto second =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"J2"});
    const auto first_pin = circuit.view().pin_by_number(first, "1").value();
    const auto second_pin = circuit.view().pin_by_number(second, "1").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"BUS"}, volt::NetKind::Signal});

    volt::authoring::connect(circuit, net, {first_pin, second_pin});

    REQUIRE(circuit.view().net(net).pins().size() == 2);
    CHECK(circuit.view().net(net).pins()[0] == first_pin);
    CHECK(circuit.view().net(net).pins()[1] == second_pin);
    CHECK(circuit.view().net_of(first_pin) == net);
    CHECK(circuit.view().net_of(second_pin) == net);
}

TEST_CASE("Authoring connect helper accepts deterministic pin vectors") {
    auto circuit = volt::Circuit{};
    const auto component_definition = add_one_pin_component(circuit);
    const auto first =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"J1"});
    const auto second =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"J2"});
    const auto pins = std::vector{
        circuit.view().pin_by_number(first, "1").value(),
        circuit.view().pin_by_number(second, "1").value(),
    };
    const auto net = circuit.add_net(volt::Net{volt::NetName{"BUS"}, volt::NetKind::Signal});

    volt::authoring::connect(circuit, net, pins);

    CHECK(circuit.view().net(net).pins() == pins);
}

TEST_CASE("Authoring connect helper preserves Circuit structural checks") {
    auto circuit = volt::Circuit{};
    const auto component_definition = add_one_pin_component(circuit);
    const auto component =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"J1"});
    const auto pin = circuit.view().pin_by_number(component, "1").value();
    const auto first_net =
        circuit.add_net(volt::Net{volt::NetName{"FIRST"}, volt::NetKind::Signal});
    const auto second_net =
        circuit.add_net(volt::Net{volt::NetName{"SECOND"}, volt::NetKind::Signal});

    volt::authoring::connect(circuit, first_net, {pin});

    CHECK_THROWS_AS(volt::authoring::connect(circuit, second_net, {pin}), std::logic_error);
    CHECK(circuit.view().net_of(pin) == first_net);
}
