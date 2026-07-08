#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/circuit/connectivity/instances.hpp>
#include <volt/circuit/connectivity/nets.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/parts/parts.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/errors.hpp>
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

    const auto first = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second = circuit.connectivity().add_pin_definition(volt::PinDefinition{
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
    const auto pin_a = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto pin_b = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "B", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});

    const auto resistor = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{pin_a, pin_b}});

    CHECK(resistor == volt::ComponentDefId{0});
    CHECK(circuit.component_definition(resistor).name() == "Resistor");
    REQUIRE(circuit.component_definition(resistor).pins().size() == 2);
    CHECK(circuit.component_definition_count() == 1);
}

TEST_CASE("Circuit stores component instances and concrete pin instances") {
    volt::Circuit circuit;
    const auto pin_def = circuit.connectivity().add_pin_definition(
        volt::PinDefinition{"VDD", "1", volt::ConnectionRequirement::Required,
                            volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto component_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Regulator", std::vector{pin_def}});

    const auto component = circuit.connectivity().add_component(
        volt::ComponentInstance{component_def, volt::ReferenceDesignator{"U1"}});
    const auto pin = circuit.connectivity().add_pin(volt::PinInstance{component, pin_def});

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

    CHECK_THROWS_AS(circuit.connectivity().add_component(volt::ComponentInstance{
                        volt::ComponentDefId{9}, volt::ReferenceDesignator{"U_MISSING"}}),
                    std::out_of_range);
}

TEST_CASE("Circuit rejects pin instances with missing component or pin definitions") {
    volt::Circuit circuit;
    const auto pin_def = circuit.connectivity().add_pin_definition(
        volt::PinDefinition{"VDD", "1", volt::ConnectionRequirement::Required,
                            volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto component_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Regulator", std::vector{pin_def}});
    const auto component = circuit.connectivity().add_component(
        volt::ComponentInstance{component_def, volt::ReferenceDesignator{"U1"}});

    CHECK_THROWS_AS(
        circuit.connectivity().add_pin(volt::PinInstance{volt::ComponentId{42}, pin_def}),
        std::out_of_range);
    CHECK_THROWS_AS(
        circuit.connectivity().add_pin(volt::PinInstance{component, volt::PinDefId{42}}),
        std::out_of_range);
}

TEST_CASE("Circuit stores nets") {
    volt::Circuit circuit;
    const auto pin_def = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "GND", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Ground,
        volt::ElectricalDirection::Passive});
    const auto component_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Connector", std::vector{pin_def}});
    const auto component = circuit.connectivity().add_component(
        volt::ComponentInstance{component_def, volt::ReferenceDesignator{"J1"}});
    const auto pin = circuit.connectivity().add_pin(volt::PinInstance{component, pin_def});

    auto net = volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground};
    net.connect(pin);

    const auto net_id = circuit.connectivity().add_net(std::move(net));

    CHECK(net_id == volt::NetId{0});
    CHECK(circuit.net(net_id).name() == volt::NetName{"GND"});
    REQUIRE(circuit.net(net_id).pins().size() == 1);
    CHECK(circuit.net_count() == 1);
}

TEST_CASE("Circuit rejects nets that reference missing pins") {
    volt::Circuit circuit;
    auto net = volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground};
    net.connect(volt::PinId{99});

    CHECK_THROWS_AS(circuit.connectivity().add_net(std::move(net)), std::out_of_range);
}

TEST_CASE("Circuit connects existing pins to existing nets") {
    volt::Circuit circuit;
    const auto pin_def = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{pin_def}});
    const auto component = circuit.connectivity().add_component(
        volt::ComponentInstance{component_def, volt::ReferenceDesignator{"R1"}});
    const auto pin = circuit.connectivity().add_pin(volt::PinInstance{component, pin_def});
    const auto net =
        circuit.connectivity().add_net(volt::Net{volt::NetName{"NET_A"}, volt::NetKind::Signal});

    CHECK(circuit.connect(net, pin));
    CHECK_FALSE(circuit.connect(net, pin));
    REQUIRE(circuit.net(net).pins().size() == 1);
    CHECK(circuit.net(net).pins().front() == pin);
    REQUIRE(volt::queries::net_of(circuit, pin).has_value());
    CHECK(volt::queries::net_of(circuit, pin).value() == net);
}

TEST_CASE("Circuit rejects connect operations with missing IDs") {
    volt::Circuit circuit;
    const auto net =
        circuit.connectivity().add_net(volt::Net{volt::NetName{"NET_A"}, volt::NetKind::Signal});

    CHECK_THROWS_AS(circuit.connect(volt::NetId{99}, volt::PinId{0}), std::out_of_range);
    CHECK_THROWS_AS(circuit.connect(net, volt::PinId{99}), std::out_of_range);
}

TEST_CASE("Circuit enforces one net per concrete pin") {
    volt::Circuit circuit;
    const auto pin_def = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{pin_def}});
    const auto component = circuit.connectivity().add_component(
        volt::ComponentInstance{component_def, volt::ReferenceDesignator{"R1"}});
    const auto pin = circuit.connectivity().add_pin(volt::PinInstance{component, pin_def});
    const auto first_net =
        circuit.connectivity().add_net(volt::Net{volt::NetName{"NET_A"}, volt::NetKind::Signal});
    const auto second_net =
        circuit.connectivity().add_net(volt::Net{volt::NetName{"NET_B"}, volt::NetKind::Signal});

    CHECK(circuit.connect(first_net, pin));
    CHECK_THROWS_AS(circuit.connect(second_net, pin), std::logic_error);
    CHECK(circuit.net(first_net).contains(pin));
    CHECK_FALSE(circuit.net(second_net).contains(pin));
}

TEST_CASE("Circuit disconnects a pin from its current net") {
    volt::Circuit circuit;
    const auto pin_def = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{pin_def}});
    const auto component = circuit.connectivity().add_component(
        volt::ComponentInstance{component_def, volt::ReferenceDesignator{"R1"}});
    const auto pin = circuit.connectivity().add_pin(volt::PinInstance{component, pin_def});
    const auto net =
        circuit.connectivity().add_net(volt::Net{volt::NetName{"NET_A"}, volt::NetKind::Signal});
    circuit.connect(net, pin);

    CHECK(circuit.disconnect(pin));
    CHECK_FALSE(circuit.disconnect(pin));
    CHECK_FALSE(volt::queries::net_of(circuit, pin).has_value());
    CHECK(circuit.net(net).pins().empty());
}

TEST_CASE("Circuit assigns and reads a selected physical part for a component") {
    volt::Circuit circuit;
    const auto first_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"R1"});

    CHECK_FALSE(circuit.selected_physical_part(component).has_value());

    circuit.electrical().select_physical_part(component,
                                              make_resistor_physical_part(first_pin, second_pin));

    const auto &selected_part = circuit.selected_physical_part(component);
    REQUIRE(selected_part.has_value());
    CHECK(selected_part->manufacturer_part().manufacturer() == "Yageo");
    CHECK(selected_part->manufacturer_part().part_number() == "RC0603FR-07330RL");
    CHECK(selected_part->package().value() == "0603");
    CHECK(selected_part->footprint().name() == "R_0603_1608Metric");
}

TEST_CASE("Circuit stores component assembly intent and selected part alternates") {
    volt::Circuit circuit;
    const auto first_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"R1"});

    CHECK_FALSE(circuit.component_dnp(component).has_value());
    circuit.intent().set_component_selection_override(component, true);
    CHECK_FALSE(circuit.component_dnp(component).has_value());
    CHECK(circuit.is_component_selection_override(component));
    circuit.intent().set_component_selection_override(component, false);
    CHECK_FALSE(circuit.is_component_selection_override(component));
    CHECK(circuit.component_assembly_intents().empty());

    circuit.intent().set_component_dnp(component, true);
    circuit.intent().set_component_selection_override(component, true);

    circuit.electrical().select_physical_part(
        component,
        volt::PhysicalPart{
            volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
            volt::PackageRef{"0603"},
            volt::FootprintRef{"passives", "R_0603_1608Metric"},
            std::vector{volt::PinPadMapping{first_pin, "1"}, volt::PinPadMapping{second_pin, "2"}},
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
    const auto first_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"R1"});
    circuit.electrical().select_physical_part(component,
                                              make_resistor_physical_part(first_pin, second_pin));
    const auto voltage_rating = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_rating"},
        volt::ElectricalAttributeOwner::SelectedPart,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };

    circuit.electrical().set_selected_part_electrical_attribute(
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
    const auto pin_def = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Test point", std::vector{pin_def}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"TP1"});

    circuit.connectivity().set_component_property(component, volt::PropertyKey{"value"},
                                                  volt::PropertyValue{"VCC"});
    circuit.connectivity().set_component_property(component, volt::PropertyKey{"fitted"},
                                                  volt::PropertyValue{true});

    CHECK(circuit.component(component).properties().get(volt::PropertyKey{"value"}) ==
          volt::PropertyValue{"VCC"});
    CHECK(circuit.component(component).properties().get(volt::PropertyKey{"fitted"}) ==
          volt::PropertyValue{true});
}

TEST_CASE("Circuit sets typed electrical attributes on component instances") {
    volt::Circuit circuit;
    const auto pin_def = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{pin_def}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"R1"});
    const auto resistance = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"resistance"},
        volt::ElectricalAttributeOwner::ComponentInstance,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Resistance,
    };

    circuit.electrical().set_component_electrical_attribute(
        component, resistance,
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Resistance, 330.0}});

    CHECK(circuit.component_electrical_attributes(component)
              .get(volt::ElectricalAttributeName{"resistance"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Resistance, 330.0});
}

TEST_CASE("Circuit sets typed electrical attributes on pin definitions") {
    volt::Circuit circuit;
    const auto pin_definition = circuit.connectivity().add_pin_definition(volt::PinDefinition{
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

    circuit.electrical().set_pin_definition_electrical_attribute(
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
    const auto net =
        circuit.connectivity().add_net(volt::Net{volt::NetName{"3V3"}, volt::NetKind::Power});
    const auto voltage = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage"},
        volt::ElectricalAttributeOwner::Net,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };

    circuit.electrical().set_net_electrical_attribute(
        net, voltage,
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}});

    CHECK(circuit.net_electrical_attributes(net)
              .get(volt::ElectricalAttributeName{"voltage"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 3.3});
}

TEST_CASE("Circuit rejects component property mutation for missing components") {
    volt::Circuit circuit;

    CHECK_THROWS_AS(circuit.connectivity().set_component_property(volt::ComponentId{99},
                                                                  volt::PropertyKey{"value"},
                                                                  volt::PropertyValue{"VCC"}),
                    std::out_of_range);
}

TEST_CASE("Circuit rejects incompatible component electrical attributes") {
    volt::Circuit circuit;
    const auto pin_def = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.connectivity().add_component_definition(
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
        circuit.electrical().set_component_electrical_attribute(
            component, resistance,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}}),
        std::invalid_argument);
    try {
        circuit.electrical().set_component_electrical_attribute(
            component, resistance,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}});
        FAIL("Electrical attribute dimension mismatch must throw");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::InvalidArgument);
    }
    CHECK_THROWS_AS(
        circuit.electrical().set_component_electrical_attribute(
            component, selected_part_rating,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 75.0}}),
        std::logic_error);
    CHECK_THROWS_AS(
        circuit.electrical().set_component_electrical_attribute(
            volt::ComponentId{99}, resistance,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Resistance, 330.0}}),
        std::out_of_range);
}

TEST_CASE("Circuit rejects incompatible pin definition electrical attributes") {
    volt::Circuit circuit;
    const auto pin_definition = circuit.connectivity().add_pin_definition(
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
        circuit.electrical().set_pin_definition_electrical_attribute(
            pin_definition, voltage_range,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Current, 0.01}}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        circuit.electrical().set_pin_definition_electrical_attribute(
            pin_definition, component_resistance,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Resistance, 330.0}}),
        std::logic_error);
    CHECK_THROWS_AS(circuit.electrical().set_pin_definition_electrical_attribute(
                        volt::PinDefId{99}, voltage_range,
                        volt::ElectricalAttributeValue{volt::QuantityRange::bounded(
                            volt::Quantity{volt::UnitDimension::Voltage, 4.5},
                            volt::Quantity{volt::UnitDimension::Voltage, 16.0})}),
                    std::out_of_range);
}

TEST_CASE("Circuit rejects incompatible net electrical attributes") {
    volt::Circuit circuit;
    const auto net =
        circuit.connectivity().add_net(volt::Net{volt::NetName{"3V3"}, volt::NetKind::Power});
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
        circuit.electrical().set_net_electrical_attribute(
            net, voltage,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Current, 0.01}}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        circuit.electrical().set_net_electrical_attribute(
            net, component_resistance,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Resistance, 330.0}}),
        std::logic_error);
    CHECK_THROWS_AS(
        circuit.electrical().set_net_electrical_attribute(
            volt::NetId{99}, voltage,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}}),
        std::out_of_range);
}

TEST_CASE("Circuit rejects selected-part operations for missing components") {
    volt::Circuit circuit;
    const auto first_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});

    CHECK_THROWS_AS(circuit.electrical().select_physical_part(
                        volt::ComponentId{99}, make_resistor_physical_part(first_pin, second_pin)),
                    std::out_of_range);
    CHECK_THROWS_AS(circuit.selected_physical_part(volt::ComponentId{99}), std::out_of_range);
}

TEST_CASE("Circuit rejects incompatible selected part electrical attributes") {
    volt::Circuit circuit;
    const auto first_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.connectivity().add_component_definition(
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
        circuit.electrical().set_selected_part_electrical_attribute(
            component, voltage_rating,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 75.0}}),
        std::logic_error);

    circuit.electrical().select_physical_part(component,
                                              make_resistor_physical_part(first_pin, second_pin));

    CHECK_THROWS_AS(
        circuit.electrical().set_selected_part_electrical_attribute(
            component, voltage_rating,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Current, 1.0}}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        circuit.electrical().set_selected_part_electrical_attribute(
            component, component_resistance,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Resistance, 330.0}}),
        std::logic_error);
}

TEST_CASE("Circuit rejects selected parts with mappings outside the component definition") {
    volt::Circuit circuit;
    const auto first_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto foreign_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "3", "3", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"R1"});

    CHECK_THROWS_AS(circuit.electrical().select_physical_part(
                        component, make_resistor_physical_part(first_pin, foreign_pin)),
                    std::logic_error);
    CHECK_FALSE(circuit.selected_physical_part(component).has_value());
}

TEST_CASE("Circuit rejects selected parts that do not map every component-definition pin") {
    volt::Circuit circuit;
    const auto first_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_def = circuit.connectivity().add_component_definition(
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

    CHECK_THROWS_AS(
        circuit.electrical().select_physical_part(component, std::move(incomplete_part)),
        std::logic_error);
    CHECK_FALSE(circuit.selected_physical_part(component).has_value());
}

TEST_CASE("Circuit copies keep name lookups independent and uniqueness enforced") {
    volt::Circuit circuit;
    const auto pin_def = circuit.connectivity().add_pin_definition(
        volt::PinDefinition{"VDD", "1", volt::ConnectionRequirement::Required,
                            volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto component_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Regulator", std::vector{pin_def}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"U1"});
    const auto net =
        circuit.connectivity().add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});
    const auto pin = volt::queries::pin_by_number(circuit, component, "1").value();
    CHECK(circuit.connect(net, pin));

    auto copy = circuit;

    CHECK(volt::queries::component_by_reference(copy, volt::ReferenceDesignator{"U1"}) ==
          component);
    CHECK(volt::queries::net_by_name(copy, volt::NetName{"VCC"}) == net);
    CHECK(volt::queries::net_of(copy, pin) == net);
    CHECK_THROWS_AS(copy.connectivity().add_component(
                        volt::ComponentInstance{component_def, volt::ReferenceDesignator{"U1"}}),
                    std::logic_error);
    CHECK_THROWS_AS(
        copy.connectivity().add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power}),
        std::logic_error);

    const auto copy_only =
        copy.instantiate_component(component_def, volt::ReferenceDesignator{"U2"});
    const auto copy_only_net =
        copy.connectivity().add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});
    CHECK(copy.disconnect(pin));

    CHECK(volt::queries::component_by_reference(copy, volt::ReferenceDesignator{"U2"}) ==
          copy_only);
    CHECK(volt::queries::net_by_name(copy, volt::NetName{"GND"}) == copy_only_net);
    CHECK_FALSE(volt::queries::net_of(copy, pin).has_value());

    CHECK_FALSE(volt::queries::component_by_reference(circuit, volt::ReferenceDesignator{"U2"})
                    .has_value());
    CHECK_FALSE(volt::queries::net_by_name(circuit, volt::NetName{"GND"}).has_value());
    CHECK(volt::queries::net_of(circuit, pin) == net);
    CHECK(circuit.component_count() == 1);
    CHECK(circuit.net_count() == 1);
}

TEST_CASE("Moved-from circuits reset to empty and stay safely usable") {
    volt::Circuit circuit;
    const auto pin_def = circuit.connectivity().add_pin_definition(
        volt::PinDefinition{"VDD", "1", volt::ConnectionRequirement::Required,
                            volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto component_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Regulator", std::vector{pin_def}});
    [[maybe_unused]] const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"U1"});
    [[maybe_unused]] const auto net =
        circuit.connectivity().add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});

    const auto moved = std::move(circuit);

    CHECK(moved.component_count() == 1);
    CHECK(moved.net_count() == 1);
    CHECK(circuit.component_count() == 0);
    CHECK(circuit.pin_count() == 0);
    CHECK(circuit.net_count() == 0);
    CHECK_FALSE(volt::queries::component_by_reference(circuit, volt::ReferenceDesignator{"U1"})
                    .has_value());

    const auto reused_pin_def = circuit.connectivity().add_pin_definition(
        volt::PinDefinition{"VDD", "1", volt::ConnectionRequirement::Required,
                            volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto reused_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Regulator", std::vector{reused_pin_def}});
    [[maybe_unused]] const auto reused =
        circuit.instantiate_component(reused_def, volt::ReferenceDesignator{"U1"});
    CHECK(circuit.component_count() == 1);
    CHECK(moved.component_count() == 1);

    volt::Circuit assigned;
    assigned = std::move(circuit);
    CHECK(assigned.component_count() == 1);
    CHECK(circuit.component_count() == 0);
    [[maybe_unused]] const auto after_move_assign =
        circuit.connectivity().add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});
    CHECK(circuit.net_count() == 1);
    CHECK(assigned.net_count() == 0);
}

TEST_CASE("Structural rejections carry machine-readable error codes") {
    volt::Circuit circuit;
    const auto pin_def = circuit.connectivity().add_pin_definition(
        volt::PinDefinition{"VDD", "1", volt::ConnectionRequirement::Required,
                            volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto component_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Regulator", std::vector{pin_def}});
    [[maybe_unused]] const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"U1"});

    try {
        [[maybe_unused]] const auto duplicate =
            circuit.instantiate_component(component_def, volt::ReferenceDesignator{"U1"});
        FAIL("Duplicate reference designator must throw");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::DuplicateName);
    }

    try {
        circuit.connectivity().set_component_property(volt::ComponentId{42}, volt::PropertyKey{"k"},
                                                      volt::PropertyValue{"v"});
        FAIL("Unknown component ID must throw");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::UnknownEntity);
        REQUIRE(error.entity().has_value());
        CHECK(error.entity()->kind() == volt::EntityKind::Component);
        CHECK(error.entity()->index() == 42);
    }

    try {
        [[maybe_unused]] const auto empty_name = volt::NetName{""};
        FAIL("Empty net name must throw");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::InvalidArgument);
    }
}
