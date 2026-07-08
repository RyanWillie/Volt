#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/intent/design_intent.hpp>

namespace {

template <typename Model>
concept CanMarkIntentionalStubNet =
    requires(Model model, volt::NetId net) { model.intent().mark_intentional_stub_net(net); };

template <typename Model>
concept CanMarkIntentionalNoConnectPin =
    requires(Model model, volt::PinId pin) { model.intent().mark_intentional_no_connect_pin(pin); };

static_assert(!CanMarkIntentionalStubNet<volt::DesignIntent>);
static_assert(!CanMarkIntentionalNoConnectPin<volt::DesignIntent>);

} // namespace

TEST_CASE("Circuit records intentional stub nets idempotently in deterministic order") {
    volt::Circuit circuit;
    const auto first =
        circuit.connectivity().add_net(volt::Net{volt::NetName{"STUB_A"}, volt::NetKind::Signal});
    const auto second =
        circuit.connectivity().add_net(volt::Net{volt::NetName{"STUB_B"}, volt::NetKind::Signal});
    const auto unmarked =
        circuit.connectivity().add_net(volt::Net{volt::NetName{"NORMAL"}, volt::NetKind::Signal});

    CHECK(circuit.intent().mark_intentional_stub_net(first));
    CHECK(circuit.intent().mark_intentional_stub_net(second));
    CHECK_FALSE(circuit.intent().mark_intentional_stub_net(first));

    CHECK(circuit.is_intentional_stub_net(first));
    CHECK_FALSE(circuit.is_intentional_stub_net(unmarked));
    CHECK(circuit.intentional_stub_nets() == std::vector{first, second});
}

TEST_CASE("Circuit records intentional no-connect pins idempotently in deterministic order") {
    volt::Circuit circuit;
    const auto first_definition = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "NC1", "1", volt::ConnectionRequirement::MustNotConnect,
        volt::ElectricalTerminalKind::NoConnect, volt::ElectricalDirection::Unspecified});
    const auto second_definition = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "NC2", "2", volt::ConnectionRequirement::MustNotConnect,
        volt::ElectricalTerminalKind::NoConnect, volt::ElectricalDirection::Unspecified});
    const auto unmarked_definition = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "NC3", "3", volt::ConnectionRequirement::MustNotConnect,
        volt::ElectricalTerminalKind::NoConnect, volt::ElectricalDirection::Unspecified});
    const auto component_definition =
        circuit.connectivity().add_component_definition(volt::ComponentDefinition{
            "Connector", std::vector{first_definition, second_definition, unmarked_definition}});
    const auto component = circuit.connectivity().add_component(
        volt::ComponentInstance{component_definition, volt::ReferenceDesignator{"J1"}});
    const auto first =
        circuit.connectivity().add_pin(volt::PinInstance{component, first_definition});
    const auto second =
        circuit.connectivity().add_pin(volt::PinInstance{component, second_definition});
    const auto unmarked =
        circuit.connectivity().add_pin(volt::PinInstance{component, unmarked_definition});

    CHECK(circuit.intent().mark_intentional_no_connect_pin(first));
    CHECK(circuit.intent().mark_intentional_no_connect_pin(second));
    CHECK_FALSE(circuit.intent().mark_intentional_no_connect_pin(first));

    CHECK(circuit.is_intentional_no_connect_pin(first));
    CHECK_FALSE(circuit.is_intentional_no_connect_pin(unmarked));
    CHECK(circuit.intentional_no_connect_pins() == std::vector{first, second});
}
