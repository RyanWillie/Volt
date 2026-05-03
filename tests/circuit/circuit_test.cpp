#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/circuit/instances.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/core/ids.hpp>

TEST_CASE("Circuit starts with empty entity tables") {
    const volt::Circuit circuit;

    CHECK(circuit.pin_definition_count() == 0);
    CHECK(circuit.component_definition_count() == 0);
    CHECK(circuit.component_count() == 0);
    CHECK(circuit.pin_count() == 0);
    CHECK(circuit.net_count() == 0);
}

TEST_CASE("Circuit stores pin definitions in deterministic order") {
    volt::Circuit circuit;

    const auto first =
        circuit.add_pin_definition(volt::PinDefinition{"1", "1", volt::PinRole::Passive});
    const auto second =
        circuit.add_pin_definition(volt::PinDefinition{"2", "2", volt::PinRole::Passive});

    CHECK(first == volt::PinDefId{0});
    CHECK(second == volt::PinDefId{1});
    CHECK(circuit.pin_definition(first).name() == "1");
    CHECK(circuit.pin_definition(second).number() == "2");
    CHECK(circuit.pin_definition_count() == 2);
}

TEST_CASE("Circuit stores component definitions") {
    volt::Circuit circuit;
    const auto pin_a =
        circuit.add_pin_definition(volt::PinDefinition{"A", "1", volt::PinRole::Passive});
    const auto pin_b =
        circuit.add_pin_definition(volt::PinDefinition{"B", "2", volt::PinRole::Passive});

    const auto resistor = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{pin_a, pin_b}});

    CHECK(resistor == volt::ComponentDefId{0});
    CHECK(circuit.component_definition(resistor).name() == "Resistor");
    REQUIRE(circuit.component_definition(resistor).pins().size() == 2);
    CHECK(circuit.component_definition_count() == 1);
}

TEST_CASE("Circuit stores component instances and concrete pin instances") {
    volt::Circuit circuit;
    const auto pin_def =
        circuit.add_pin_definition(volt::PinDefinition{"VDD", "1", volt::PinRole::PowerInput});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Regulator", std::vector{pin_def}});

    const auto component = circuit.add_component(
        volt::ComponentInstance{component_def, volt::ReferenceDesignator{"U1"}});
    const auto pin = circuit.add_pin(volt::PinInstance{component, pin_def});

    CHECK(component == volt::ComponentId{0});
    CHECK(circuit.component(component).reference() == volt::ReferenceDesignator{"U1"});
    CHECK(pin == volt::PinId{0});
    CHECK(circuit.pin(pin).component() == component);
    CHECK(circuit.pin(pin).definition() == pin_def);
    CHECK(circuit.component_count() == 1);
    CHECK(circuit.pin_count() == 1);
}

TEST_CASE("Circuit stores nets") {
    volt::Circuit circuit;
    auto net = volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground};
    net.connect(volt::PinId{0});

    const auto net_id = circuit.add_net(std::move(net));

    CHECK(net_id == volt::NetId{0});
    CHECK(circuit.net(net_id).name() == volt::NetName{"GND"});
    REQUIRE(circuit.net(net_id).pins().size() == 1);
    CHECK(circuit.net_count() == 1);
}
