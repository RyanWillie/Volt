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

TEST_CASE("Logical circuit reader rejects duplicate net pin references") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["nets"][0]["pins"].push_back("pin:0");

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
