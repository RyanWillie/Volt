#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <volt/authoring/component_library.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/core/properties.hpp>

TEST_CASE("Component library defines a component from a data-driven spec") {
    auto circuit = volt::Circuit{};
    const auto component_definition = volt::authoring::define_component(
        circuit,
        volt::authoring::ComponentSpec{
            "Sensor",
            std::vector{
                volt::authoring::PinSpec{"VDD", "1", volt::PinRole::PowerInput,
                                         volt::ConnectionRequirement::Required},
                volt::authoring::PinSpec{"OUT", "2", volt::PinRole::AnalogOutput,
                                         volt::ConnectionRequirement::Optional},
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
    CHECK(power.role() == volt::PinRole::PowerInput);
    CHECK(power.connection_requirement() == volt::ConnectionRequirement::Required);

    const auto &output = circuit.pin_definition(definition.pins()[1]);
    CHECK(output.name() == "OUT");
    CHECK(output.number() == "2");
    CHECK(output.role() == volt::PinRole::AnalogOutput);
    CHECK(output.connection_requirement() == volt::ConnectionRequirement::Optional);
}

TEST_CASE("Passive component catalog specs define two required passive pins") {
    auto circuit = volt::Circuit{};

    const auto resistor = volt::authoring::define_component(circuit, volt::authoring::resistor());
    const auto capacitor = volt::authoring::define_component(circuit, volt::authoring::capacitor());

    const auto &resistor_definition = circuit.component_definition(resistor);
    REQUIRE(resistor_definition.pins().size() == 2);
    CHECK(circuit.pin_definition(resistor_definition.pins()[0]).name() == "1");
    CHECK(circuit.pin_definition(resistor_definition.pins()[0]).number() == "1");
    CHECK(circuit.pin_definition(resistor_definition.pins()[0]).role() == volt::PinRole::Passive);
    CHECK(circuit.pin_definition(resistor_definition.pins()[0]).connection_requirement() ==
          volt::ConnectionRequirement::Required);
    CHECK(circuit.pin_definition(resistor_definition.pins()[1]).name() == "2");
    CHECK(circuit.pin_definition(resistor_definition.pins()[1]).number() == "2");

    const auto &capacitor_definition = circuit.component_definition(capacitor);
    REQUIRE(capacitor_definition.pins().size() == 2);
    CHECK(circuit.pin_definition(capacitor_definition.pins()[0]).name() == "1");
    CHECK(circuit.pin_definition(capacitor_definition.pins()[0]).number() == "1");
    CHECK(circuit.pin_definition(capacitor_definition.pins()[1]).name() == "2");
    CHECK(circuit.pin_definition(capacitor_definition.pins()[1]).number() == "2");
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
    CHECK(circuit.pin_definition(led_definition.pins()[1]).name() == "K");
    CHECK(circuit.pin_definition(led_definition.pins()[1]).number() == "1");

    const auto &connector_definition = circuit.component_definition(connector);
    REQUIRE(connector_definition.pins().size() == 2);
    CHECK(circuit.pin_definition(connector_definition.pins()[0]).role() ==
          volt::PinRole::Bidirectional);
    CHECK(circuit.pin_definition(connector_definition.pins()[0]).number() == "1");
    CHECK(circuit.pin_definition(connector_definition.pins()[1]).role() ==
          volt::PinRole::Bidirectional);
    CHECK(circuit.pin_definition(connector_definition.pins()[1]).number() == "2");
}
