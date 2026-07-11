#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include <volt/authoring/component_library.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/core/properties.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/schematic/default_symbols.hpp>

TEST_CASE("Component library defines a component from a data-driven spec") {
    auto circuit = volt::Circuit{};
    const auto component_definition = volt::authoring::define_component(
        circuit,
        volt::authoring::ComponentSpec{
            "Sensor",
            std::vector{
                volt::authoring::PinSpec{"VDD", "1", volt::ConnectionRequirement::Required,
                                         volt::ElectricalTerminalKind::Power,
                                         volt::ElectricalDirection::Input},
                volt::authoring::PinSpec{"OUT", "2", volt::ConnectionRequirement::Optional,
                                         volt::ElectricalTerminalKind::Signal,
                                         volt::ElectricalDirection::Output,
                                         volt::ElectricalSignalDomain::Analog},
            },
            volt::PropertyMap{{volt::PropertyKey{"category"}, volt::PropertyValue{"sensor"}}},
            volt::DefinitionSource{"volt.sensors", "analog_sensor", "1.0.0"},
        });

    REQUIRE(circuit.pin_definition_count() == 2);
    REQUIRE(circuit.component_definition_count() == 1);

    const auto &definition = circuit.component_definition(component_definition);
    CHECK(definition.name() == "Sensor");
    REQUIRE(definition.pins().size() == 2);
    CHECK(definition.properties().get(volt::PropertyKey{"category"}).as_string() == "sensor");
    REQUIRE(definition.source().has_value());
    CHECK(definition.source()->namespace_name() == "volt.sensors");
    CHECK(definition.source()->name() == "analog_sensor");
    CHECK(definition.source()->version() == "1.0.0");

    const auto &power = circuit.pin_definition(definition.pins()[0]);
    CHECK(power.name() == "VDD");
    CHECK(power.number() == "1");
    CHECK(power.connection_requirement() == volt::ConnectionRequirement::Required);
    CHECK(power.terminal_kind() == volt::ElectricalTerminalKind::Power);
    CHECK(power.direction() == volt::ElectricalDirection::Input);

    const auto &output = circuit.pin_definition(definition.pins()[1]);
    CHECK(output.name() == "OUT");
    CHECK(output.number() == "2");
    CHECK(output.connection_requirement() == volt::ConnectionRequirement::Optional);
    CHECK(output.terminal_kind() == volt::ElectricalTerminalKind::Signal);
    CHECK(output.direction() == volt::ElectricalDirection::Output);
    CHECK(output.signal_domain() == volt::ElectricalSignalDomain::Analog);
}

TEST_CASE("Component library definition failures leave canonical bytes unchanged") {
    auto circuit = volt::Circuit{};
    static_cast<void>(volt::authoring::define_component(circuit, volt::authoring::resistor()));
    const auto before = volt::io::write_logical_circuit(circuit);

    CHECK_THROWS(volt::authoring::define_component(
        circuit, volt::authoring::ComponentSpec{"Broken",
                                                {volt::authoring::passive_pin("1", "1"),
                                                 volt::authoring::passive_pin("", "2")}}));

    CHECK(volt::io::write_logical_circuit(circuit) == before);
}

TEST_CASE("Passive component catalog specs define two required passive pins") {
    auto circuit = volt::Circuit{};

    const auto resistor = volt::authoring::define_component(circuit, volt::authoring::resistor());
    const auto capacitor = volt::authoring::define_component(circuit, volt::authoring::capacitor());

    const auto &resistor_definition = circuit.component_definition(resistor);
    REQUIRE(resistor_definition.pins().size() == 2);
    CHECK(circuit.pin_definition(resistor_definition.pins()[0]).name() == "1");
    CHECK(circuit.pin_definition(resistor_definition.pins()[0]).number() == "1");
    CHECK(circuit.pin_definition(resistor_definition.pins()[0]).connection_requirement() ==
          volt::ConnectionRequirement::Required);
    CHECK(circuit.pin_definition(resistor_definition.pins()[0]).terminal_kind() ==
          volt::ElectricalTerminalKind::Passive);
    CHECK(circuit.pin_definition(resistor_definition.pins()[0]).direction() ==
          volt::ElectricalDirection::Passive);
    CHECK(circuit.pin_definition(resistor_definition.pins()[1]).name() == "2");
    CHECK(circuit.pin_definition(resistor_definition.pins()[1]).number() == "2");
    CHECK(circuit.pin_definition(resistor_definition.pins()[1]).terminal_kind() ==
          volt::ElectricalTerminalKind::Passive);

    const auto &capacitor_definition = circuit.component_definition(capacitor);
    REQUIRE(capacitor_definition.pins().size() == 2);
    CHECK(circuit.pin_definition(capacitor_definition.pins()[0]).name() == "1");
    CHECK(circuit.pin_definition(capacitor_definition.pins()[0]).number() == "1");
    CHECK(circuit.pin_definition(capacitor_definition.pins()[0]).terminal_kind() ==
          volt::ElectricalTerminalKind::Passive);
    CHECK(circuit.pin_definition(capacitor_definition.pins()[1]).name() == "2");
    CHECK(circuit.pin_definition(capacitor_definition.pins()[1]).number() == "2");
    CHECK(circuit.pin_definition(capacitor_definition.pins()[1]).direction() ==
          volt::ElectricalDirection::Passive);
}

TEST_CASE("LED and connector catalog specs preserve expected logical pin conventions") {
    auto circuit = volt::Circuit{};

    const auto led = volt::authoring::define_component(circuit, volt::authoring::led());
    const auto connector =
        volt::authoring::define_component(circuit, volt::authoring::connector_1x02());

    const auto &led_definition = circuit.component_definition(led);
    REQUIRE(led_definition.pins().size() == 2);
    CHECK(circuit.pin_definition(led_definition.pins()[0]).name() == "A");
    CHECK(circuit.pin_definition(led_definition.pins()[0]).number() == "2");
    CHECK(circuit.pin_definition(led_definition.pins()[0]).terminal_kind() ==
          volt::ElectricalTerminalKind::Passive);
    CHECK(circuit.pin_definition(led_definition.pins()[1]).name() == "K");
    CHECK(circuit.pin_definition(led_definition.pins()[1]).number() == "1");
    CHECK(circuit.pin_definition(led_definition.pins()[1]).direction() ==
          volt::ElectricalDirection::Passive);

    const auto &connector_definition = circuit.component_definition(connector);
    REQUIRE(connector_definition.pins().size() == 2);
    CHECK(circuit.pin_definition(connector_definition.pins()[0]).terminal_kind() ==
          volt::ElectricalTerminalKind::Signal);
    CHECK(circuit.pin_definition(connector_definition.pins()[0]).direction() ==
          volt::ElectricalDirection::Bidirectional);
    CHECK(circuit.pin_definition(connector_definition.pins()[0]).number() == "1");
    CHECK(circuit.pin_definition(connector_definition.pins()[1]).terminal_kind() ==
          volt::ElectricalTerminalKind::Signal);
    CHECK(circuit.pin_definition(connector_definition.pins()[1]).direction() ==
          volt::ElectricalDirection::Bidirectional);
    CHECK(circuit.pin_definition(connector_definition.pins()[1]).number() == "2");
}

TEST_CASE("Common component catalog specs carry stable default schematic symbol references") {
    struct CatalogCase {
        std::string source_name;
        volt::authoring::ComponentSpec spec;
        std::string symbol_name;
    };

    const auto cases = std::vector<CatalogCase>{
        {"resistor_2pin", volt::authoring::resistor(), "volt.passives:resistor"},
        {"capacitor_2pin", volt::authoring::capacitor(), "volt.passives:capacitor"},
        {"capacitor_polarized_2pin", volt::authoring::polarized_capacitor(),
         "volt.passives:capacitor_polarized"},
        {"inductor_2pin", volt::authoring::inductor(), "volt.passives:inductor"},
        {"diode_2pin", volt::authoring::diode(), "volt.discretes:diode"},
        {"led_2pin", volt::authoring::led(), "volt.optos:led"},
        {"switch_spst", volt::authoring::switch_spst(), "volt.switches:switch_spst"},
        {"crystal_2pin", volt::authoring::crystal_2pin(), "volt.frequency:crystal_2pin"},
        {"test_point_1pin", volt::authoring::test_point(), "volt.testpoints:test_point"},
        {"connector_1x01", volt::authoring::connector_1x01(), "volt.connectors:connector_1x01"},
        {"connector_1x02", volt::authoring::connector_1x02(), "volt.connectors:connector_1x02"},
        {"connector_1x03", volt::authoring::connector_1x03(), "volt.connectors:connector_1x03"},
        {"regulator_3pin", volt::authoring::regulator_3pin(), "volt.power:regulator_3pin"},
        {"op_amp_5pin", volt::authoring::op_amp_5pin(), "volt.analog:op_amp_5pin"},
    };

    for (const auto &item : cases) {
        INFO(item.source_name);
        REQUIRE(item.spec.source.has_value());
        CHECK(item.spec.source->name() == item.source_name);
        REQUIRE(item.spec.schematic_symbols.size() == 1);
        CHECK(item.spec.schematic_symbols[0].name() == item.symbol_name);
        CHECK(item.spec.schematic_symbols[0].variant() == "default");

        const auto symbol = volt::default_schematic_symbol(item.symbol_name);
        REQUIRE(symbol.has_value());
        CHECK(symbol->name() == item.symbol_name);
        CHECK(symbol->pins().size() == item.spec.pins.size());
        CHECK_FALSE(symbol->primitives().empty());
        for (const auto &symbol_pin : symbol->pins()) {
            CHECK(std::any_of(item.spec.pins.begin(), item.spec.pins.end(),
                              [&](const auto &pin) { return pin.number == symbol_pin.number(); }));
        }
    }
}

TEST_CASE("Op amp catalog spec models both supply rails as power inputs") {
    auto circuit = volt::Circuit{};

    const auto op_amp = volt::authoring::define_component(circuit, volt::authoring::op_amp_5pin());
    const auto &definition = circuit.component_definition(op_amp);

    const auto find_pin = [&](const std::string &name) -> const volt::PinDefinition * {
        for (const auto pin_id : definition.pins()) {
            const auto &pin = circuit.pin_definition(pin_id);
            if (pin.name() == name) {
                return &pin;
            }
        }
        return nullptr;
    };

    const auto *positive_supply = find_pin("V+");
    const auto *negative_supply = find_pin("V-");

    REQUIRE(positive_supply != nullptr);
    CHECK(positive_supply->terminal_kind() == volt::ElectricalTerminalKind::Power);
    CHECK(positive_supply->direction() == volt::ElectricalDirection::Input);

    REQUIRE(negative_supply != nullptr);
    CHECK(negative_supply->terminal_kind() == volt::ElectricalTerminalKind::Power);
    CHECK(negative_supply->direction() == volt::ElectricalDirection::Input);
}
