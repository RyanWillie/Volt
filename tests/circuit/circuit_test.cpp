#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/circuit/instances.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/circuit/parts.hpp>
#include <volt/circuit/queries.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/ids.hpp>

namespace {

volt::PhysicalPart make_resistor_physical_part(volt::PinDefId first_pin,
                                               volt::PinDefId second_pin) {
    return volt::PhysicalPart{
        volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
        volt::PackageRef{"0603"},
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{
            volt::PinPadMapping{first_pin, "1"},
            volt::PinPadMapping{second_pin, "2"},
        },
    };
}

} // namespace

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

    const auto first = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second = circuit.add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});

    CHECK(first == volt::PinDefId{0});
    CHECK(second == volt::PinDefId{1});
    CHECK(circuit.pin_definition(first).name() == "1");
    CHECK(circuit.pin_definition(second).number() == "2");
    CHECK(circuit.pin_definition_count() == 2);
}

TEST_CASE("Circuit stores component definitions") {
    volt::Circuit circuit;
    const auto pin_a = circuit.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto pin_b = circuit.add_pin_definition(volt::PinDefinition{
        "B", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});

    const auto resistor = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{pin_a, pin_b}});

    CHECK(resistor == volt::ComponentDefId{0});
    CHECK(circuit.component_definition(resistor).name() == "Resistor");
    REQUIRE(circuit.component_definition(resistor).pins().size() == 2);
    CHECK(circuit.component_definition_count() == 1);
}

TEST_CASE("Circuit stores component instances and concrete pin instances") {
    volt::Circuit circuit;
    const auto pin_def = circuit.add_pin_definition(
        volt::PinDefinition{"VDD", "1", volt::ConnectionRequirement::Required,
                            volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
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
    const auto pin_def = circuit.add_pin_definition(
        volt::PinDefinition{"VDD", "1", volt::ConnectionRequirement::Required,
                            volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
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
    const auto pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "GND", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Ground,
        volt::ElectricalDirection::Passive});
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
    const auto pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
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
    REQUIRE(volt::queries::net_of(circuit, pin).has_value());
    CHECK(volt::queries::net_of(circuit, pin).value() == net);
}

TEST_CASE("Circuit rejects connect operations with missing IDs") {
    volt::Circuit circuit;
    const auto net = circuit.add_net(volt::Net{volt::NetName{"NET_A"}, volt::NetKind::Signal});

    CHECK_THROWS_AS(circuit.connect(volt::NetId{99}, volt::PinId{0}), std::out_of_range);
    CHECK_THROWS_AS(circuit.connect(net, volt::PinId{99}), std::out_of_range);
}

TEST_CASE("Circuit enforces one net per concrete pin") {
    volt::Circuit circuit;
    const auto pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
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
    const auto pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{pin_def}});
    const auto component = circuit.add_component(
        volt::ComponentInstance{component_def, volt::ReferenceDesignator{"R1"}});
    const auto pin = circuit.add_pin(volt::PinInstance{component, pin_def});
    const auto net = circuit.add_net(volt::Net{volt::NetName{"NET_A"}, volt::NetKind::Signal});
    circuit.connect(net, pin);

    CHECK(circuit.disconnect(pin));
    CHECK_FALSE(circuit.disconnect(pin));
    CHECK_FALSE(volt::queries::net_of(circuit, pin).has_value());
    CHECK(circuit.net(net).pins().empty());
}

TEST_CASE("Circuit assigns and reads a selected physical part for a component") {
    volt::Circuit circuit;
    const auto first_pin = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"R1"});

    CHECK_FALSE(circuit.selected_physical_part(component).has_value());

    circuit.select_physical_part(component, make_resistor_physical_part(first_pin, second_pin));

    const auto &selected_part = circuit.selected_physical_part(component);
    REQUIRE(selected_part.has_value());
    CHECK(selected_part->manufacturer_part().manufacturer() == "Yageo");
    CHECK(selected_part->manufacturer_part().part_number() == "RC0603FR-07330RL");
    CHECK(selected_part->package().value() == "0603");
    CHECK(selected_part->footprint().name() == "R_0603_1608Metric");
}

TEST_CASE("Circuit stores component assembly intent and selected part alternates") {
    volt::Circuit circuit;
    const auto first_pin = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"R1"});

    CHECK_FALSE(circuit.component_dnp(component).has_value());
    circuit.set_component_selection_override(component, true);
    CHECK_FALSE(circuit.component_dnp(component).has_value());
    CHECK(circuit.is_component_selection_override(component));
    circuit.set_component_selection_override(component, false);
    CHECK_FALSE(circuit.is_component_selection_override(component));
    CHECK(circuit.component_assembly_intents().empty());

    circuit.set_component_dnp(component, true);
    circuit.set_component_selection_override(component, true);

    circuit.select_physical_part(component, volt::PhysicalPart{
                                                volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                                                volt::PackageRef{"0603"},
                                                volt::FootprintRef{"passives", "R_0603_1608Metric"},
                                                std::vector{volt::PinPadMapping{first_pin, "1"},
                                                            volt::PinPadMapping{second_pin, "2"}},
                                                {},
                                                std::nullopt,
                                                std::vector<std::string>{"RC0603FR-07330RLA"},
                                            });

    CHECK(circuit.component_dnp(component) == std::optional<bool>{true});
    CHECK(circuit.is_component_selection_override(component));
    REQUIRE(circuit.selected_physical_part(component).has_value());
    CHECK(circuit.selected_physical_part(component)->approved_alternate_mpns() ==
          std::vector<std::string>{"RC0603FR-07330RLA"});
}

TEST_CASE("Circuit sets typed electrical attributes on selected physical parts") {
    volt::Circuit circuit;
    const auto first_pin = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"R1"});
    circuit.select_physical_part(component, make_resistor_physical_part(first_pin, second_pin));
    const auto voltage_rating = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_rating"},
        volt::ElectricalAttributeOwner::SelectedPart,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };

    circuit.set_selected_part_electrical_attribute(
        component, voltage_rating,
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 75.0}});

    REQUIRE(circuit.selected_physical_part(component).has_value());
    CHECK(circuit.selected_physical_part(component)
              .value()
              .electrical_attributes()
              .get(volt::ElectricalAttributeName{"voltage_rating"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 75.0});
}

TEST_CASE("Circuit sets component instance properties through an explicit mutation API") {
    volt::Circuit circuit;
    const auto pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Test point", std::vector{pin_def}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"TP1"});

    circuit.set_component_property(component, volt::PropertyKey{"value"},
                                   volt::PropertyValue{"VCC"});
    circuit.set_component_property(component, volt::PropertyKey{"fitted"},
                                   volt::PropertyValue{true});

    CHECK(circuit.component(component).properties().get(volt::PropertyKey{"value"}) ==
          volt::PropertyValue{"VCC"});
    CHECK(circuit.component(component).properties().get(volt::PropertyKey{"fitted"}) ==
          volt::PropertyValue{true});
}

TEST_CASE("Circuit sets typed electrical attributes on component instances") {
    volt::Circuit circuit;
    const auto pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{pin_def}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"R1"});
    const auto resistance = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"resistance"},
        volt::ElectricalAttributeOwner::ComponentInstance,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Resistance,
    };

    circuit.set_component_electrical_attribute(
        component, resistance,
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Resistance, 330.0}});

    CHECK(circuit.component_electrical_attributes(component)
              .get(volt::ElectricalAttributeName{"resistance"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Resistance, 330.0});
}

TEST_CASE("Circuit sets typed electrical attributes on pin definitions") {
    volt::Circuit circuit;
    const auto pin_definition = circuit.add_pin_definition(volt::PinDefinition{
        "VCC",
        "8",
        volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Power,
        volt::ElectricalDirection::Input,
    });
    const auto voltage_range = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_range"},
        volt::ElectricalAttributeOwner::PinSpec,
        volt::ElectricalAttributeKind::Constraint,
        volt::UnitDimension::Voltage,
    };

    circuit.set_pin_definition_electrical_attribute(
        pin_definition, voltage_range,
        volt::ElectricalAttributeValue{
            volt::QuantityRange::bounded(volt::Quantity{volt::UnitDimension::Voltage, 4.5},
                                         volt::Quantity{volt::UnitDimension::Voltage, 16.0})});

    const auto &stored_range = circuit.pin_definition_electrical_attributes(pin_definition)
                                   .get(volt::ElectricalAttributeName{"voltage_range"})
                                   .as_range();
    REQUIRE(stored_range.minimum().has_value());
    REQUIRE(stored_range.maximum().has_value());
    CHECK(stored_range.minimum().value() == volt::Quantity{volt::UnitDimension::Voltage, 4.5});
    CHECK(stored_range.maximum().value() == volt::Quantity{volt::UnitDimension::Voltage, 16.0});
}

TEST_CASE("Circuit sets typed electrical attributes on nets") {
    volt::Circuit circuit;
    const auto net = circuit.add_net(volt::Net{volt::NetName{"3V3"}, volt::NetKind::Power});
    const auto voltage = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage"},
        volt::ElectricalAttributeOwner::Net,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };

    circuit.set_net_electrical_attribute(
        net, voltage,
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}});

    CHECK(circuit.net_electrical_attributes(net)
              .get(volt::ElectricalAttributeName{"voltage"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 3.3});
}

TEST_CASE("Circuit rejects component property mutation for missing components") {
    volt::Circuit circuit;

    CHECK_THROWS_AS(circuit.set_component_property(volt::ComponentId{99},
                                                   volt::PropertyKey{"value"},
                                                   volt::PropertyValue{"VCC"}),
                    std::out_of_range);
}

TEST_CASE("Circuit rejects incompatible component electrical attributes") {
    volt::Circuit circuit;
    const auto pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{pin_def}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"R1"});
    const auto resistance = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"resistance"},
        volt::ElectricalAttributeOwner::ComponentInstance,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Resistance,
    };
    const auto selected_part_rating = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_rating"},
        volt::ElectricalAttributeOwner::SelectedPart,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };

    CHECK_THROWS_AS(
        circuit.set_component_electrical_attribute(
            component, resistance,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        circuit.set_component_electrical_attribute(
            component, selected_part_rating,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 75.0}}),
        std::logic_error);
    CHECK_THROWS_AS(
        circuit.set_component_electrical_attribute(
            volt::ComponentId{99}, resistance,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Resistance, 330.0}}),
        std::out_of_range);
}

TEST_CASE("Circuit rejects incompatible pin definition electrical attributes") {
    volt::Circuit circuit;
    const auto pin_definition = circuit.add_pin_definition(
        volt::PinDefinition{"VCC", "8", volt::ConnectionRequirement::Required,
                            volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto voltage_range = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_range"},
        volt::ElectricalAttributeOwner::PinSpec,
        volt::ElectricalAttributeKind::Constraint,
        volt::UnitDimension::Voltage,
    };
    const auto component_resistance = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"resistance"},
        volt::ElectricalAttributeOwner::ComponentInstance,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Resistance,
    };

    CHECK_THROWS_AS(
        circuit.set_pin_definition_electrical_attribute(
            pin_definition, voltage_range,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Current, 0.01}}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        circuit.set_pin_definition_electrical_attribute(
            pin_definition, component_resistance,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Resistance, 330.0}}),
        std::logic_error);
    CHECK_THROWS_AS(circuit.set_pin_definition_electrical_attribute(
                        volt::PinDefId{99}, voltage_range,
                        volt::ElectricalAttributeValue{volt::QuantityRange::bounded(
                            volt::Quantity{volt::UnitDimension::Voltage, 4.5},
                            volt::Quantity{volt::UnitDimension::Voltage, 16.0})}),
                    std::out_of_range);
}

TEST_CASE("Circuit rejects incompatible net electrical attributes") {
    volt::Circuit circuit;
    const auto net = circuit.add_net(volt::Net{volt::NetName{"3V3"}, volt::NetKind::Power});
    const auto voltage = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage"},
        volt::ElectricalAttributeOwner::Net,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };
    const auto component_resistance = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"resistance"},
        volt::ElectricalAttributeOwner::ComponentInstance,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Resistance,
    };

    CHECK_THROWS_AS(
        circuit.set_net_electrical_attribute(
            net, voltage,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Current, 0.01}}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        circuit.set_net_electrical_attribute(
            net, component_resistance,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Resistance, 330.0}}),
        std::logic_error);
    CHECK_THROWS_AS(
        circuit.set_net_electrical_attribute(
            volt::NetId{99}, voltage,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}}),
        std::out_of_range);
}

TEST_CASE("Circuit rejects selected-part operations for missing components") {
    volt::Circuit circuit;
    const auto first_pin = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});

    CHECK_THROWS_AS(circuit.select_physical_part(
                        volt::ComponentId{99}, make_resistor_physical_part(first_pin, second_pin)),
                    std::out_of_range);
    CHECK_THROWS_AS(circuit.selected_physical_part(volt::ComponentId{99}), std::out_of_range);
}

TEST_CASE("Circuit rejects incompatible selected part electrical attributes") {
    volt::Circuit circuit;
    const auto first_pin = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"R1"});
    const auto voltage_rating = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_rating"},
        volt::ElectricalAttributeOwner::SelectedPart,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };
    const auto component_resistance = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"resistance"},
        volt::ElectricalAttributeOwner::ComponentInstance,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Resistance,
    };

    CHECK_THROWS_AS(
        circuit.set_selected_part_electrical_attribute(
            component, voltage_rating,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 75.0}}),
        std::logic_error);

    circuit.select_physical_part(component, make_resistor_physical_part(first_pin, second_pin));

    CHECK_THROWS_AS(
        circuit.set_selected_part_electrical_attribute(
            component, voltage_rating,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Current, 1.0}}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        circuit.set_selected_part_electrical_attribute(
            component, component_resistance,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Resistance, 330.0}}),
        std::logic_error);
}

TEST_CASE("Circuit rejects selected parts with mappings outside the component definition") {
    volt::Circuit circuit;
    const auto first_pin = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto foreign_pin = circuit.add_pin_definition(volt::PinDefinition{
        "3", "3", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"R1"});

    CHECK_THROWS_AS(circuit.select_physical_part(
                        component, make_resistor_physical_part(first_pin, foreign_pin)),
                    std::logic_error);
    CHECK_FALSE(circuit.selected_physical_part(component).has_value());
}

TEST_CASE("Circuit rejects selected parts that do not map every component-definition pin") {
    volt::Circuit circuit;
    const auto first_pin = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"R1"});
    auto incomplete_part = volt::PhysicalPart{
        volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
        volt::PackageRef{"0603"},
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{
            volt::PinPadMapping{first_pin, "1"},
        },
    };

    CHECK_THROWS_AS(circuit.select_physical_part(component, std::move(incomplete_part)),
                    std::logic_error);
    CHECK_FALSE(circuit.selected_physical_part(component).has_value());
}
