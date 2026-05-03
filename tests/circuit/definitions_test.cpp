#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include <volt/circuit/definitions.hpp>
#include <volt/core/ids.hpp>

TEST_CASE("PinDefinition stores logical pin metadata") {
    const auto pin = volt::PinDefinition{
        "VDD",
        "17",
        volt::PinRole::PowerInput,
        volt::ConnectionRequirement::Required,
    };

    CHECK(pin.name() == "VDD");
    CHECK(pin.number() == "17");
    CHECK(pin.role() == volt::PinRole::PowerInput);
    CHECK(pin.connection_requirement() == volt::ConnectionRequirement::Required);
}

TEST_CASE("PinDefinition can represent explicit connection requirements") {
    const auto optional = volt::PinDefinition{
        "GPIO0",
        "4",
        volt::PinRole::Bidirectional,
        volt::ConnectionRequirement::Optional,
    };
    const auto must_not_connect = volt::PinDefinition{
        "NC",
        "5",
        volt::PinRole::NoConnect,
        volt::ConnectionRequirement::MustNotConnect,
    };

    CHECK(optional.connection_requirement() == volt::ConnectionRequirement::Optional);
    CHECK(must_not_connect.connection_requirement() == volt::ConnectionRequirement::MustNotConnect);
}

TEST_CASE("PinDefinition rejects empty names and numbers") {
    CHECK_THROWS_AS(volt::PinDefinition("", "1", volt::PinRole::Passive), std::invalid_argument);
    CHECK_THROWS_AS(volt::PinDefinition("A", "", volt::PinRole::Passive), std::invalid_argument);
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
}

TEST_CASE("ComponentDefinition rejects empty names and empty pin lists") {
    CHECK_THROWS_AS(volt::ComponentDefinition("", std::vector{volt::PinDefId{0}}),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::ComponentDefinition("MechanicalOnly", std::vector<volt::PinDefId>{}),
                    std::invalid_argument);
}
