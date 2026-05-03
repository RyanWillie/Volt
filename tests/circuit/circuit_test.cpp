#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
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

TEST_CASE("Circuit rejects component instances that reference missing definitions") {
    volt::Circuit circuit;

    CHECK_THROWS_AS(circuit.add_component(volt::ComponentInstance{
                        volt::ComponentDefId{9}, volt::ReferenceDesignator{"U_MISSING"}}),
                    std::out_of_range);
}

TEST_CASE("Circuit rejects pin instances with missing component or pin definitions") {
    volt::Circuit circuit;
    const auto pin_def =
        circuit.add_pin_definition(volt::PinDefinition{"VDD", "1", volt::PinRole::PowerInput});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Regulator", std::vector{pin_def}});
    const auto component = circuit.add_component(
        volt::ComponentInstance{component_def, volt::ReferenceDesignator{"U1"}});

    CHECK_THROWS_AS(circuit.add_pin(volt::PinInstance{volt::ComponentId{42}, pin_def}),
                    std::out_of_range);
    CHECK_THROWS_AS(circuit.add_pin(volt::PinInstance{component, volt::PinDefId{42}}),
                    std::out_of_range);
}

TEST_CASE("Circuit stores nets") {
    volt::Circuit circuit;
    const auto pin_def =
        circuit.add_pin_definition(volt::PinDefinition{"GND", "1", volt::PinRole::Ground});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Connector", std::vector{pin_def}});
    const auto component = circuit.add_component(
        volt::ComponentInstance{component_def, volt::ReferenceDesignator{"J1"}});
    const auto pin = circuit.add_pin(volt::PinInstance{component, pin_def});

    auto net = volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground};
    net.connect(pin);

    const auto net_id = circuit.add_net(std::move(net));

    CHECK(net_id == volt::NetId{0});
    CHECK(circuit.net(net_id).name() == volt::NetName{"GND"});
    REQUIRE(circuit.net(net_id).pins().size() == 1);
    CHECK(circuit.net_count() == 1);
}

TEST_CASE("Circuit rejects nets that reference missing pins") {
    volt::Circuit circuit;
    auto net = volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground};
    net.connect(volt::PinId{99});

    CHECK_THROWS_AS(circuit.add_net(std::move(net)), std::out_of_range);
}

TEST_CASE("Circuit connects existing pins to existing nets") {
    volt::Circuit circuit;
    const auto pin_def =
        circuit.add_pin_definition(volt::PinDefinition{"1", "1", volt::PinRole::Passive});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{pin_def}});
    const auto component = circuit.add_component(
        volt::ComponentInstance{component_def, volt::ReferenceDesignator{"R1"}});
    const auto pin = circuit.add_pin(volt::PinInstance{component, pin_def});
    const auto net = circuit.add_net(volt::Net{volt::NetName{"NET_A"}, volt::NetKind::Signal});

    CHECK(circuit.connect(net, pin));
    CHECK_FALSE(circuit.connect(net, pin));
    REQUIRE(circuit.net(net).pins().size() == 1);
    CHECK(circuit.net(net).pins().front() == pin);
    REQUIRE(circuit.net_of(pin).has_value());
    CHECK(circuit.net_of(pin).value() == net);
}

TEST_CASE("Circuit rejects connect operations with missing IDs") {
    volt::Circuit circuit;
    const auto net = circuit.add_net(volt::Net{volt::NetName{"NET_A"}, volt::NetKind::Signal});

    CHECK_THROWS_AS(circuit.connect(volt::NetId{99}, volt::PinId{0}), std::out_of_range);
    CHECK_THROWS_AS(circuit.connect(net, volt::PinId{99}), std::out_of_range);
}

TEST_CASE("Circuit enforces one net per concrete pin") {
    volt::Circuit circuit;
    const auto pin_def =
        circuit.add_pin_definition(volt::PinDefinition{"1", "1", volt::PinRole::Passive});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{pin_def}});
    const auto component = circuit.add_component(
        volt::ComponentInstance{component_def, volt::ReferenceDesignator{"R1"}});
    const auto pin = circuit.add_pin(volt::PinInstance{component, pin_def});
    const auto first_net =
        circuit.add_net(volt::Net{volt::NetName{"NET_A"}, volt::NetKind::Signal});
    const auto second_net =
        circuit.add_net(volt::Net{volt::NetName{"NET_B"}, volt::NetKind::Signal});

    CHECK(circuit.connect(first_net, pin));
    CHECK_THROWS_AS(circuit.connect(second_net, pin), std::logic_error);
    CHECK(circuit.net(first_net).contains(pin));
    CHECK_FALSE(circuit.net(second_net).contains(pin));
}

TEST_CASE("Circuit disconnects a pin from its current net") {
    volt::Circuit circuit;
    const auto pin_def =
        circuit.add_pin_definition(volt::PinDefinition{"1", "1", volt::PinRole::Passive});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{pin_def}});
    const auto component = circuit.add_component(
        volt::ComponentInstance{component_def, volt::ReferenceDesignator{"R1"}});
    const auto pin = circuit.add_pin(volt::PinInstance{component, pin_def});
    const auto net = circuit.add_net(volt::Net{volt::NetName{"NET_A"}, volt::NetKind::Signal});
    circuit.connect(net, pin);

    CHECK(circuit.disconnect(pin));
    CHECK_FALSE(circuit.disconnect(pin));
    CHECK_FALSE(circuit.net_of(pin).has_value());
    CHECK(circuit.net(net).pins().empty());
}
