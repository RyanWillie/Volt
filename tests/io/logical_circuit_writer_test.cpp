#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

#include <volt/io/logical_circuit_writer.hpp>

#include "led_circuit.hpp"

namespace {

std::string read_fixture(const std::string &name) {
    auto input = std::ifstream{std::string{VOLT_TEST_FIXTURE_DIR} + "/" + name};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

} // namespace

TEST_CASE("Logical circuit writer emits deterministic output") {
    const auto circuit = volt::examples::build_led_circuit();

    CHECK(volt::io::write_logical_circuit(circuit) == volt::io::write_logical_circuit(circuit));
}

TEST_CASE("Logical circuit writer escapes JSON control characters") {
    volt::Circuit circuit;
    const auto pin = circuit.add_pin_definition(
        volt::PinDefinition{"CTRL\x01\x1f", "1", volt::PinRole::Passive});
    const auto component_def =
        circuit.add_component_definition(volt::ComponentDefinition{"Escaped", std::vector{pin}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"U1"});
    circuit.set_component_property(component, volt::PropertyKey{"note"},
                                   volt::PropertyValue{"line\nbreak\x01"});

    const auto output = volt::io::write_logical_circuit(circuit);

    CHECK(output.find("CTRL\\u0001\\u001f") != std::string::npos);
    CHECK(output.find("line\\nbreak\\u0001") != std::string::npos);
}

TEST_CASE("Logical circuit writer preserves double precision and rejects non-finite numbers") {
    volt::Circuit circuit;
    const auto pin =
        circuit.add_pin_definition(volt::PinDefinition{"1", "1", volt::PinRole::Passive});
    const auto component_def =
        circuit.add_component_definition(volt::ComponentDefinition{"Precise", std::vector{pin}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"U1"});
    circuit.set_component_property(component, volt::PropertyKey{"ratio"},
                                   volt::PropertyValue{0.12345678901234567});
    circuit.set_component_property(component, volt::PropertyKey{"invalid"},
                                   volt::PropertyValue{std::numeric_limits<double>::infinity()});

    CHECK_THROWS_AS(volt::io::write_logical_circuit(circuit), std::logic_error);
    circuit.set_component_property(component, volt::PropertyKey{"invalid"},
                                   volt::PropertyValue{1.0});

    CHECK(volt::io::write_logical_circuit(circuit).find("0.12345678901234566") !=
          std::string::npos);
}

TEST_CASE("Logical circuit writer matches the LED golden fixture") {
    const auto circuit = volt::examples::build_led_circuit();

    CHECK(volt::io::write_logical_circuit(circuit) == read_fixture("led_circuit.volt.json"));
}
