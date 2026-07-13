#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <volt/authoring/connection_helpers.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/errors.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>

#include <support/circuit_test_helpers.hpp>

namespace {

volt::ComponentDefId add_one_pin_component(volt::Circuit &circuit) {
    return volt::test::define_component(circuit, "OnePin", {volt::test::passive_pin("1", "1")});
}

template <typename Operation>
void check_failure_is_byte_atomic(volt::Circuit &circuit, volt::ErrorCode expected_code,
                                  std::string_view expected_message, Operation operation) {
    const auto before = volt::io::write_logical_circuit(circuit);
    try {
        operation();
        FAIL("Invalid bulk connection must throw");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == expected_code);
        CHECK(std::string{error.what()} == std::string{expected_message});
    }
    CHECK(volt::io::write_logical_circuit(circuit) == before);
}

} // namespace

TEST_CASE("Authoring connect helper connects multiple pins to one net") {
    auto circuit = volt::Circuit{};
    const auto component_definition = add_one_pin_component(circuit);
    const auto first = volt::test::instantiate_component(circuit, component_definition, "J1");
    const auto second = volt::test::instantiate_component(circuit, component_definition, "J2");
    const auto first_pin = volt::queries::pin_by_number(circuit, first, "1").value();
    const auto second_pin = volt::queries::pin_by_number(circuit, second, "1").value();
    const auto net = volt::test::add_net(circuit, "BUS");

    volt::authoring::connect(circuit, net, {first_pin, second_pin});

    REQUIRE(circuit.get(net).pins().size() == 2);
    CHECK(circuit.get(net).pins()[0] == first_pin);
    CHECK(circuit.get(net).pins()[1] == second_pin);
    CHECK(volt::queries::net_of(circuit, first_pin) == net);
    CHECK(volt::queries::net_of(circuit, second_pin) == net);
}

TEST_CASE("Authoring connect helper accepts deterministic pin vectors") {
    auto circuit = volt::Circuit{};
    const auto component_definition = add_one_pin_component(circuit);
    const auto first = volt::test::instantiate_component(circuit, component_definition, "J1");
    const auto second = volt::test::instantiate_component(circuit, component_definition, "J2");
    const auto pins = std::vector{
        volt::queries::pin_by_number(circuit, first, "1").value(),
        volt::queries::pin_by_number(circuit, second, "1").value(),
    };
    const auto net = volt::test::add_net(circuit, "BUS");

    volt::authoring::connect(circuit, net, pins);

    CHECK(circuit.get(net).pins() == pins);
}

TEST_CASE("Authoring connect helper preserves Circuit structural checks") {
    auto circuit = volt::Circuit{};
    const auto component_definition = add_one_pin_component(circuit);
    const auto component = volt::test::instantiate_component(circuit, component_definition, "J1");
    const auto pin = volt::queries::pin_by_number(circuit, component, "1").value();
    const auto first_net = volt::test::add_net(circuit, "FIRST");
    const auto second_net = volt::test::add_net(circuit, "SECOND");

    volt::authoring::connect(circuit, first_net, {pin});

    CHECK_THROWS_AS(volt::authoring::connect(circuit, second_net, {pin}), std::logic_error);
    CHECK(volt::queries::net_of(circuit, pin) == first_net);
}

TEST_CASE("Authoring connect helper preflights the complete pin range before mutating") {
    auto circuit = volt::Circuit{};
    const auto component_definition = add_one_pin_component(circuit);
    const auto first = volt::test::instantiate_component(circuit, component_definition, "J1");
    const auto second = volt::test::instantiate_component(circuit, component_definition, "J2");
    const auto first_pin = volt::queries::pin_by_number(circuit, first, "1").value();
    const auto second_pin = volt::queries::pin_by_number(circuit, second, "1").value();
    const auto target_net = volt::test::add_net(circuit, "TARGET");
    const auto other_net = volt::test::add_net(circuit, "OTHER");
    CHECK(circuit.connect(other_net, second_pin));

    check_failure_is_byte_atomic(
        circuit, volt::ErrorCode::InvalidState, "Pin is already connected to another net",
        [&] { volt::authoring::connect(circuit, target_net, {first_pin, second_pin}); });
    check_failure_is_byte_atomic(
        circuit, volt::ErrorCode::UnknownEntity, "Pin ID does not belong to this circuit",
        [&] { volt::authoring::connect(circuit, target_net, {first_pin, volt::PinId{99}}); });
}
