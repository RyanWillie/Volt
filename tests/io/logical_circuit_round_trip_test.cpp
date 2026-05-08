#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <string>

#include <volt/circuit/validation.hpp>
#include <volt/io/logical_circuit_reader.hpp>
#include <volt/io/logical_circuit_writer.hpp>

namespace {

std::string read_fixture(const std::string &name) {
    auto input = std::ifstream{std::string{VOLT_TEST_FIXTURE_DIR} + "/" + name};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void check_fixture_round_trips(const std::string &name) {
    const auto fixture = read_fixture(name);
    const auto first_read = volt::io::read_logical_circuit_text(fixture);
    const auto first_write = volt::io::write_logical_circuit(first_read);
    const auto second_read = volt::io::read_logical_circuit_text(first_write);

    CHECK(first_write == fixture);
    CHECK(volt::io::write_logical_circuit(second_read) == fixture);
}

} // namespace

TEST_CASE("Golden LED fixture round-trips without logical diagnostics") {
    const auto fixture = read_fixture("led_circuit.volt.json");
    const auto circuit = volt::io::read_logical_circuit_text(fixture);

    CHECK(volt::validate_circuit(circuit).empty());
    check_fixture_round_trips("led_circuit.volt.json");
}

TEST_CASE("Golden diagnostic fixture round-trips and preserves connectivity") {
    const auto fixture = read_fixture("single_pin_net.volt.json");
    const auto circuit = volt::io::read_logical_circuit_text(fixture);
    const auto report = volt::validate_circuit(circuit);

    REQUIRE(circuit.net_count() == 1);
    CHECK(circuit.net(volt::NetId{0}).pins().size() == 1);
    CHECK_FALSE(report.empty());
    check_fixture_round_trips("single_pin_net.volt.json");
}

TEST_CASE("Golden typed electrical attribute fixture round-trips") {
    check_fixture_round_trips("typed_electrical_attributes.volt.json");
}
