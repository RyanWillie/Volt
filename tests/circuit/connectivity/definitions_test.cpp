#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/circuit/parts/parts.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>

TEST_CASE("PinDefinition stores logical pin metadata") {
    const auto pin = volt::PinDefinition{
        "VDD",
        "17",
        volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Power,
        volt::ElectricalDirection::Input,
    };

    CHECK(pin.name() == "VDD");
    CHECK(pin.number() == "17");
    CHECK(pin.connection_requirement() == volt::ConnectionRequirement::Required);
    CHECK(pin.terminal_kind() == volt::ElectricalTerminalKind::Power);
    CHECK(pin.direction() == volt::ElectricalDirection::Input);
    CHECK(pin.signal_domain() == volt::ElectricalSignalDomain::Unspecified);
    CHECK(pin.drive_kind() == volt::ElectricalDriveKind::Unspecified);
    CHECK(pin.polarity() == volt::ElectricalPolarity::None);
}

TEST_CASE("PinDefinition can represent explicit connection requirements") {
    const auto optional = volt::PinDefinition{
        "GPIO0",
        "4",
        volt::ConnectionRequirement::Optional,
        volt::ElectricalTerminalKind::Signal,
        volt::ElectricalDirection::Bidirectional,
    };
    const auto must_not_connect = volt::PinDefinition{
        "NC",
        "5",
        volt::ConnectionRequirement::MustNotConnect,
        volt::ElectricalTerminalKind::NoConnect,
        volt::ElectricalDirection::Unspecified,
    };

    CHECK(optional.connection_requirement() == volt::ConnectionRequirement::Optional);
    CHECK(must_not_connect.connection_requirement() == volt::ConnectionRequirement::MustNotConnect);
    CHECK(must_not_connect.terminal_kind() == volt::ElectricalTerminalKind::NoConnect);
}

TEST_CASE("PartModel3D enforces normalized asset identity") {
    CHECK_NOTHROW(volt::PartModel3D{"glb", "resistor-body.glb", {0.0, 0.0, 0.0}, 0.0});
    CHECK_NOTHROW(volt::PartModel3D{"step", "resistor-body.step", {0.0, 0.0, 0.0}, 0.0});

    CHECK_THROWS_AS((volt::PartModel3D{"GLB", "resistor-body.glb", {0.0, 0.0, 0.0}, 0.0}),
                    std::invalid_argument);
    CHECK_THROWS_AS((volt::PartModel3D{"stp", "resistor-body.stp", {0.0, 0.0, 0.0}, 0.0}),
                    std::invalid_argument);
    CHECK_THROWS_AS((volt::PartModel3D{"glb", "models/resistor-body.glb", {0.0, 0.0, 0.0}, 0.0}),
                    std::invalid_argument);
    CHECK_THROWS_AS((volt::PartModel3D{"glb", R"(models\resistor-body.glb)", {0.0, 0.0, 0.0}, 0.0}),
                    std::invalid_argument);
}

TEST_CASE("PinDefinition stores fundamental electrical pin semantics") {
    const auto power = volt::PinDefinition{
        "VCC",
        "8",
        volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Power,
        volt::ElectricalDirection::Input,
    };
    const auto reset = volt::PinDefinition{
        "RESET",
        "4",
        volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Signal,
        volt::ElectricalDirection::Input,
        volt::ElectricalSignalDomain::Digital,
        volt::ElectricalDriveKind::HighImpedance,
        volt::ElectricalPolarity::ActiveLow,
    };
    const auto output = volt::PinDefinition{
        "OUT",
        "3",
        volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Signal,
        volt::ElectricalDirection::Output,
        volt::ElectricalSignalDomain::Digital,
        volt::ElectricalDriveKind::PushPull,
    };
    const auto no_connect = volt::PinDefinition{
        "NC",
        "5",
        volt::ConnectionRequirement::MustNotConnect,
        volt::ElectricalTerminalKind::NoConnect,
    };

    CHECK(power.terminal_kind() == volt::ElectricalTerminalKind::Power);
    CHECK(power.direction() == volt::ElectricalDirection::Input);
    CHECK(reset.terminal_kind() == volt::ElectricalTerminalKind::Signal);
    CHECK(reset.direction() == volt::ElectricalDirection::Input);
    CHECK(reset.signal_domain() == volt::ElectricalSignalDomain::Digital);
    CHECK(reset.drive_kind() == volt::ElectricalDriveKind::HighImpedance);
    CHECK(reset.polarity() == volt::ElectricalPolarity::ActiveLow);
    CHECK(output.direction() == volt::ElectricalDirection::Output);
    CHECK(output.drive_kind() == volt::ElectricalDriveKind::PushPull);
    CHECK(no_connect.terminal_kind() == volt::ElectricalTerminalKind::NoConnect);
    CHECK(no_connect.connection_requirement() == volt::ConnectionRequirement::MustNotConnect);
}

TEST_CASE("PinDefinition rejects empty names and numbers") {
    CHECK_THROWS_AS(volt::PinDefinition("", "1"), std::invalid_argument);
    CHECK_THROWS_AS(volt::PinDefinition("A", ""), std::invalid_argument);
}

TEST_CASE("ComponentDefinition stores reusable component metadata and pin definitions") {
    const auto component = volt::ComponentDefinition{
        "Resistor",
        std::vector{volt::PinDefId{0}, volt::PinDefId{1}},
    };

    CHECK(component.name() == "Resistor");
    REQUIRE(component.pins().size() == 2);
    CHECK(component.pins()[0] == volt::PinDefId{0});
    CHECK(component.pins()[1] == volt::PinDefId{1});
    CHECK(component.properties().empty());
}

TEST_CASE("DefinitionSource stores reusable definition provenance") {
    const auto source = volt::DefinitionSource{"volt.passives", "resistor_2pin", "1.0.0"};

    CHECK(source.namespace_name() == "volt.passives");
    CHECK(source.name() == "resistor_2pin");
    CHECK(source.version() == "1.0.0");
}

TEST_CASE("DefinitionSource rejects empty provenance fields") {
    CHECK_THROWS_AS(volt::DefinitionSource("", "resistor_2pin", "1.0.0"), std::invalid_argument);
    CHECK_THROWS_AS(volt::DefinitionSource("volt.passives", "", "1.0.0"), std::invalid_argument);
    CHECK_THROWS_AS(volt::DefinitionSource("volt.passives", "resistor_2pin", ""),
                    std::invalid_argument);
}

TEST_CASE("ComponentDefinition stores explicit properties") {
    const auto component = volt::ComponentDefinition{
        "Resistor",
        std::vector{volt::PinDefId{0}, volt::PinDefId{1}},
        volt::PropertyMap{
            {volt::PropertyKey{"category"}, volt::PropertyValue{"passive"}},
            {volt::PropertyKey{"polarized"}, volt::PropertyValue{false}},
        },
    };

    CHECK(component.properties().size() == 2);
    CHECK(component.properties().get(volt::PropertyKey{"category"}) ==
          volt::PropertyValue{"passive"});
    CHECK(component.properties().get(volt::PropertyKey{"polarized"}) == volt::PropertyValue{false});
}

TEST_CASE("ComponentDefinition stores default schematic symbol variants") {
    const auto component = volt::ComponentDefinition{
        "Sensor",
        std::vector{volt::PinDefId{0}, volt::PinDefId{1}},
        {},
        std::nullopt,
        std::vector{
            volt::SchematicSymbolReference{"volt.test:Sensor"},
            volt::SchematicSymbolReference{"volt.test:SensorVertical", "vertical"},
        },
    };

    REQUIRE(component.schematic_symbols().size() == 2);
    CHECK(component.schematic_symbols()[0].name() == "volt.test:Sensor");
    CHECK(component.schematic_symbols()[0].variant() == "default");
    CHECK(component.schematic_symbols()[1].name() == "volt.test:SensorVertical");
    CHECK(component.schematic_symbols()[1].variant() == "vertical");
}

TEST_CASE("ComponentDefinition rejects invalid schematic symbol variants") {
    CHECK_THROWS_AS(([] { return volt::SchematicSymbolReference{""}; }()), std::invalid_argument);
    CHECK_THROWS_AS(([] { return volt::SchematicSymbolReference{"volt.test:Sensor", ""}; }()),
                    std::invalid_argument);

    const auto duplicate_default_variant = [] {
        return volt::ComponentDefinition{
            "Sensor",
            std::vector{volt::PinDefId{0}},
            {},
            std::nullopt,
            std::vector{volt::SchematicSymbolReference{"volt.test:Sensor"},
                        volt::SchematicSymbolReference{"volt.test:SensorAlt"}},
        };
    };
    CHECK_THROWS_AS(duplicate_default_variant(), std::invalid_argument);
}

TEST_CASE("ComponentDefinition stores optional source provenance") {
    const auto component = volt::ComponentDefinition{
        "Resistor",
        std::vector{volt::PinDefId{0}, volt::PinDefId{1}},
        {},
        volt::DefinitionSource{"volt.passives", "resistor_2pin", "1.0.0"},
    };

    REQUIRE(component.source().has_value());
    CHECK(component.source()->namespace_name() == "volt.passives");
    CHECK(component.source()->name() == "resistor_2pin");
    CHECK(component.source()->version() == "1.0.0");
}

TEST_CASE("ComponentDefinition rejects empty names and empty pin lists") {
    CHECK_THROWS_AS(volt::ComponentDefinition("", std::vector{volt::PinDefId{0}}),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::ComponentDefinition("MechanicalOnly", std::vector<volt::PinDefId>{}),
                    std::invalid_argument);
}
