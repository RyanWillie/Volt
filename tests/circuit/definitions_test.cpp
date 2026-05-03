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
        true,
    };

    CHECK(pin.name() == "VDD");
    CHECK(pin.number() == "17");
    CHECK(pin.role() == volt::PinRole::PowerInput);
    CHECK(pin.required());
}

TEST_CASE("PinDefinition can represent optional no-connect pins") {
    const auto pin = volt::PinDefinition{
        "NC",
        "4",
        volt::PinRole::NoConnect,
        false,
    };

    CHECK(pin.name() == "NC");
    CHECK(pin.number() == "4");
    CHECK(pin.role() == volt::PinRole::NoConnect);
    CHECK_FALSE(pin.required());
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
