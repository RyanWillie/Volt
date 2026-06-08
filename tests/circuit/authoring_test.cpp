#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/circuit/instances.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/circuit/queries.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>

TEST_CASE("Circuit rejects component definitions with missing pin definitions") {
    volt::Circuit circuit;

    CHECK_THROWS_AS(circuit.add_component_definition(
                        volt::ComponentDefinition{"Broken", std::vector{volt::PinDefId{7}}}),
                    std::out_of_range);
}

TEST_CASE("Circuit finds components by reference designator") {
    volt::Circuit circuit;
    const auto pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{pin_def}});
    const auto component = circuit.add_component(
        volt::ComponentInstance{component_def, volt::ReferenceDesignator{"R1"}});

    const auto found =
        volt::queries::component_by_reference(circuit, volt::ReferenceDesignator{"R1"});

    REQUIRE(found.has_value());
    CHECK(found.value() == component);
    CHECK_FALSE(volt::queries::component_by_reference(circuit, volt::ReferenceDesignator{"R2"})
                    .has_value());
}

TEST_CASE("Circuit rejects duplicate component reference designators") {
    volt::Circuit circuit;
    const auto pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{pin_def}});

    const auto first = circuit.add_component(
        volt::ComponentInstance{component_def, volt::ReferenceDesignator{"R1"}});
    CHECK(first == volt::ComponentId{0});

    CHECK_THROWS_AS(circuit.add_component(
                        volt::ComponentInstance{component_def, volt::ReferenceDesignator{"R1"}}),
                    std::logic_error);
}

TEST_CASE("Circuit finds nets by name and rejects duplicate net names") {
    volt::Circuit circuit;

    const auto net = circuit.add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});

    const auto found = volt::queries::net_by_name(circuit, volt::NetName{"GND"});

    REQUIRE(found.has_value());
    CHECK(found.value() == net);
    CHECK_FALSE(volt::queries::net_by_name(circuit, volt::NetName{"LED_A"}).has_value());
    CHECK_THROWS_AS(circuit.add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground}),
                    std::logic_error);
}

TEST_CASE("Circuit instantiates component pins from the component definition") {
    volt::Circuit circuit;
    const auto anode = circuit.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto cathode = circuit.add_pin_definition(volt::PinDefinition{
        "K", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto led = circuit.add_component_definition(
        volt::ComponentDefinition{"LED", std::vector{anode, cathode}});

    const auto component = circuit.instantiate_component(led, volt::ReferenceDesignator{"D1"});
    const auto pins = volt::queries::pins_for(circuit, component);

    CHECK(component == volt::ComponentId{0});
    CHECK(circuit.component(component).definition() == led);
    CHECK(circuit.component(component).reference() == volt::ReferenceDesignator{"D1"});
    CHECK(circuit.component_count() == 1);
    REQUIRE(pins.size() == 2);
    CHECK(pins[0] == volt::PinId{0});
    CHECK(pins[1] == volt::PinId{1});
    CHECK(circuit.pin(pins[0]).component() == component);
    CHECK(circuit.pin(pins[0]).definition() == anode);
    CHECK(circuit.pin(pins[1]).component() == component);
    CHECK(circuit.pin(pins[1]).definition() == cathode);
}

TEST_CASE("Circuit instantiates components with instance properties") {
    volt::Circuit circuit;
    const auto pin = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto resistor =
        circuit.add_component_definition(volt::ComponentDefinition{"Resistor", std::vector{pin}});

    const auto component = circuit.instantiate_component(
        resistor, volt::ReferenceDesignator{"R1"},
        volt::PropertyMap{{volt::PropertyKey{"value"}, volt::PropertyValue{"330 ohm"}}});

    CHECK(circuit.component(component).properties().get(volt::PropertyKey{"value"}) ==
          volt::PropertyValue{"330 ohm"});
}

TEST_CASE("Circuit finds component pins by definition name and number") {
    volt::Circuit circuit;
    const auto anode = circuit.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto cathode = circuit.add_pin_definition(volt::PinDefinition{
        "K", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto led = circuit.add_component_definition(
        volt::ComponentDefinition{"LED", std::vector{anode, cathode}});
    const auto component = circuit.instantiate_component(led, volt::ReferenceDesignator{"D1"});
    const auto pins = volt::queries::pins_for(circuit, component);

    const auto by_name = volt::queries::pin_by_name(circuit, component, "A");
    const auto by_number = volt::queries::pin_by_number(circuit, component, "2");

    REQUIRE(by_name.has_value());
    REQUIRE(by_number.has_value());
    CHECK(by_name.value() == pins[0]);
    CHECK(by_number.value() == pins[1]);
    CHECK_FALSE(volt::queries::pin_by_name(circuit, component, "MISSING").has_value());
    CHECK_FALSE(volt::queries::pin_by_number(circuit, component, "99").has_value());
}

TEST_CASE("Circuit rejects authoring lookups for missing component IDs") {
    volt::Circuit circuit;

    CHECK_THROWS_AS(circuit.instantiate_component(volt::ComponentDefId{5},
                                                  volt::ReferenceDesignator{"U_MISSING"}),
                    std::out_of_range);
    CHECK_THROWS_AS(volt::queries::pins_for(circuit, volt::ComponentId{5}), std::out_of_range);
    CHECK_THROWS_AS(volt::queries::pin_by_name(circuit, volt::ComponentId{5}, "A"),
                    std::out_of_range);
    CHECK_THROWS_AS(volt::queries::pin_by_number(circuit, volt::ComponentId{5}, "1"),
                    std::out_of_range);
}
