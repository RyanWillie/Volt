#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>

#include <fstream>
#include <iterator>
#include <string>

#include <volt/io/logical_circuit_reader.hpp>
#include <volt/io/logical_circuit_writer.hpp>

namespace {

std::string read_fixture(const std::string &name) {
    auto input = std::ifstream{std::string{VOLT_TEST_FIXTURE_DIR} + "/" + name};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

} // namespace

TEST_CASE("Logical circuit reader round-trips the LED fixture") {
    const auto fixture = read_fixture("led_circuit.volt.json");

    const auto circuit = volt::io::read_logical_circuit_text(fixture);

    CHECK(volt::io::write_logical_circuit(circuit) == fixture);
}

TEST_CASE("Logical circuit reader preserves component definition source metadata") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["component_definitions"][1]["source"] = {
        {"namespace", "volt.passives"}, {"name", "resistor_2pin"}, {"version", "1.0.0"}};

    const auto circuit = volt::io::read_logical_circuit(fixture);
    const auto output = nlohmann::json::parse(volt::io::write_logical_circuit(circuit));

    CHECK(output["component_definitions"][1]["source"] ==
          fixture["component_definitions"][1]["source"]);
}

TEST_CASE("Logical circuit reader preserves typed electrical attributes") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["components"][1]["electrical_attributes"] = {
        {"resistance", {{"type", "quantity"}, {"dimension", "resistance"}, {"value", 330.0}}},
        {"tolerance",
         {{"type", "tolerance"},
          {"mode", "percent"},
          {"dimension", "ratio"},
          {"minus", 0.01},
          {"plus", 0.01}}},
    };
    fixture["components"][1]["selected_physical_part"]["electrical_attributes"] = {
        {"voltage_rating", {{"type", "quantity"}, {"dimension", "voltage"}, {"value", 75.0}}},
    };

    const auto circuit = volt::io::read_logical_circuit(fixture);
    const auto &component = circuit.component(volt::ComponentId{1});
    const auto &selected_part = circuit.selected_physical_part(volt::ComponentId{1}).value();

    CHECK(component.electrical_attributes()
              .get(volt::ElectricalAttributeName{"resistance"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Resistance, 330.0});
    CHECK(component.electrical_attributes()
              .get(volt::ElectricalAttributeName{"tolerance"})
              .as_tolerance()
              .plus() == volt::Quantity{volt::UnitDimension::Ratio, 0.01});
    CHECK(selected_part.electrical_attributes()
              .get(volt::ElectricalAttributeName{"voltage_rating"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 75.0});
}

TEST_CASE("Logical circuit reader preserves net typed electrical attributes") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["nets"][0]["electrical_attributes"] = {
        {"voltage", {{"type", "quantity"}, {"dimension", "voltage"}, {"value", 3.3}}},
    };

    const auto circuit = volt::io::read_logical_circuit(fixture);

    CHECK(circuit.net(volt::NetId{0})
              .electrical_attributes()
              .get(volt::ElectricalAttributeName{"voltage"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 3.3});
}

TEST_CASE("Logical circuit reader preserves pin electrical semantics") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    auto &pin = fixture["pin_definitions"][0];
    pin["terminal_kind"] = "Signal";
    pin["direction"] = "Output";
    pin["signal_domain"] = "Digital";
    pin["drive_kind"] = "PushPull";
    pin["polarity"] = "ActiveHigh";
    pin["electrical_attributes"] = {
        {"voltage_range",
         {{"type", "range"}, {"dimension", "voltage"}, {"minimum", 0.0}, {"maximum", 5.5}}},
    };

    const auto circuit = volt::io::read_logical_circuit(fixture);
    const auto &definition = circuit.pin_definition(volt::PinDefId{0});

    CHECK(definition.terminal_kind() == volt::ElectricalTerminalKind::Signal);
    CHECK(definition.direction() == volt::ElectricalDirection::Output);
    CHECK(definition.signal_domain() == volt::ElectricalSignalDomain::Digital);
    CHECK(definition.drive_kind() == volt::ElectricalDriveKind::PushPull);
    CHECK(definition.polarity() == volt::ElectricalPolarity::ActiveHigh);

    const auto &range = definition.electrical_attributes()
                            .get(volt::ElectricalAttributeName{"voltage_range"})
                            .as_range();
    REQUIRE(range.minimum().has_value());
    REQUIRE(range.maximum().has_value());
    CHECK(range.minimum().value() == volt::Quantity{volt::UnitDimension::Voltage, 0.0});
    CHECK(range.maximum().value() == volt::Quantity{volt::UnitDimension::Voltage, 5.5});
}

TEST_CASE("Logical circuit reader defaults missing typed electrical attributes to empty maps") {
    const auto circuit = volt::io::read_logical_circuit_text(read_fixture("led_circuit.volt.json"));

    CHECK(circuit.pin_definition(volt::PinDefId{0}).terminal_kind() ==
          volt::ElectricalTerminalKind::Unspecified);
    CHECK(circuit.pin_definition(volt::PinDefId{0}).electrical_attributes().empty());
    CHECK(circuit.component(volt::ComponentId{1}).electrical_attributes().empty());
    CHECK(circuit.net(volt::NetId{0}).electrical_attributes().empty());
    REQUIRE(circuit.selected_physical_part(volt::ComponentId{1}).has_value());
    CHECK(circuit.selected_physical_part(volt::ComponentId{1})
              .value()
              .electrical_attributes()
              .empty());
}

TEST_CASE("Logical circuit reader rejects duplicate net pin references") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["nets"][0]["pins"].push_back("pin:0");

    CHECK_THROWS_AS(volt::io::read_logical_circuit(fixture), std::logic_error);
}

TEST_CASE("Logical circuit reader rejects invalid pin electrical enum values") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["pin_definitions"][0]["terminal_kind"] = "ThresholdInput";

    CHECK_THROWS_AS(volt::io::read_logical_circuit(fixture), std::logic_error);
}

TEST_CASE("Logical circuit reader rejects dangling references") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["component_definitions"][0]["pins"][0] = "pin_def:999";

    CHECK_THROWS_AS(volt::io::read_logical_circuit(fixture), std::logic_error);
}

TEST_CASE("Logical circuit reader rejects wrong typed local IDs") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["pins"][0]["id"] = "component:99";

    CHECK_THROWS_AS(volt::io::read_logical_circuit(fixture), std::logic_error);
}

TEST_CASE("Logical circuit reader reports unsupported versions deterministically") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["version"] = 2;

    CHECK_THROWS_MATCHES(volt::io::read_logical_circuit(fixture), std::logic_error,
                         Catch::Matchers::Message("Unsupported logical circuit format version: 2"));
}

TEST_CASE("Logical circuit reader reports large unsupported versions deterministically") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["version"] = 2147483648LL;

    CHECK_THROWS_MATCHES(
        volt::io::read_logical_circuit(fixture), std::logic_error,
        Catch::Matchers::Message("Unsupported logical circuit format version: 2147483648"));
}

TEST_CASE("Logical circuit reader reports unsupported formats deterministically") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["format"] = "volt.other";

    CHECK_THROWS_MATCHES(
        volt::io::read_logical_circuit(fixture), std::logic_error,
        Catch::Matchers::Message("Unsupported logical circuit format: volt.other"));
}

TEST_CASE(
    "Logical circuit reader rejects selected part mappings outside the component definition") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["components"][0]["selected_physical_part"]["pin_pad_mappings"][0]["pin"] = "pin_def:2";

    CHECK_THROWS_AS(volt::io::read_logical_circuit(fixture), std::logic_error);
}
