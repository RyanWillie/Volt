#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

#include <volt/authoring/connection_helpers.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/circuit/connectivity/nets.hpp>
#include <volt/circuit/connectivity/queries.hpp>

namespace {

volt::ComponentDefId add_one_pin_component(volt::Circuit &circuit) {
    const auto pin = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
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
    const auto first_pin = volt::queries::pin_by_number(circuit, first, "1").value();
    const auto second_pin = volt::queries::pin_by_number(circuit, second, "1").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"BUS"}, volt::NetKind::Signal});

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
    const auto first =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"J1"});
    const auto second =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"J2"});
    const auto pins = std::vector{
        volt::queries::pin_by_number(circuit, first, "1").value(),
        volt::queries::pin_by_number(circuit, second, "1").value(),
    };
    const auto net = circuit.add_net(volt::Net{volt::NetName{"BUS"}, volt::NetKind::Signal});

    volt::authoring::connect(circuit, net, pins);

    CHECK(circuit.net(net).pins() == pins);
}

TEST_CASE("Authoring connect helper preserves Circuit structural checks") {
    auto circuit = volt::Circuit{};
    const auto component_definition = add_one_pin_component(circuit);
    const auto component =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"J1"});
    const auto pin = volt::queries::pin_by_number(circuit, component, "1").value();
    const auto first_net =
        circuit.add_net(volt::Net{volt::NetName{"FIRST"}, volt::NetKind::Signal});
    const auto second_net =
        circuit.add_net(volt::Net{volt::NetName{"SECOND"}, volt::NetKind::Signal});

    volt::authoring::connect(circuit, first_net, {pin});

    CHECK_THROWS_AS(volt::authoring::connect(circuit, second_net, {pin}), std::logic_error);
    CHECK(volt::queries::net_of(circuit, pin) == first_net);
}
