#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/intent/design_intent.hpp>

namespace {

template <typename Model>
concept CanMarkIntentionalStubNet =
    requires(Model model, volt::NetId net) { model.update(net, volt::MarkIntentionalStub{}); };

template <typename Model>
concept CanMarkIntentionalNoConnectPin =
    requires(Model model, volt::PinId pin) { model.mark_no_connect(pin); };

static_assert(!CanMarkIntentionalStubNet<volt::DesignIntent>);
static_assert(!CanMarkIntentionalNoConnectPin<volt::DesignIntent>);

} // namespace

TEST_CASE("Circuit records intentional stub nets idempotently in deterministic order") {
    volt::Circuit circuit;
    const auto first = circuit.add_net(volt::NetSpec{.name = volt::NetName{"STUB_A"}});
    const auto second = circuit.add_net(volt::NetSpec{.name = volt::NetName{"STUB_B"}});
    const auto unmarked = circuit.add_net(volt::NetSpec{.name = volt::NetName{"NORMAL"}});

    circuit.update(first, volt::MarkIntentionalStub{});
    circuit.update(second, volt::MarkIntentionalStub{});
    circuit.update(first, volt::MarkIntentionalStub{});

    CHECK(circuit.is_intentional_stub_net(first));
    CHECK_FALSE(circuit.is_intentional_stub_net(unmarked));
    CHECK(circuit.intentional_stub_nets() == std::vector{first, second});
}

TEST_CASE("Circuit records intentional no-connect pins idempotently in deterministic order") {
    volt::Circuit circuit;
    const auto component_definition = circuit.define_component(volt::ComponentSpec{
        .name = "Connector",
        .pins =
            {
                volt::PinSpec{.name = "NC1",
                              .number = "1",
                              .requirement = volt::ConnectionRequirement::MustNotConnect,
                              .terminal_kind = volt::ElectricalTerminalKind::NoConnect},
                volt::PinSpec{.name = "NC2",
                              .number = "2",
                              .requirement = volt::ConnectionRequirement::MustNotConnect,
                              .terminal_kind = volt::ElectricalTerminalKind::NoConnect},
                volt::PinSpec{.name = "NC3",
                              .number = "3",
                              .requirement = volt::ConnectionRequirement::MustNotConnect,
                              .terminal_kind = volt::ElectricalTerminalKind::NoConnect},
            },
    });
    const auto component = circuit.instantiate_component(
        component_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"J1"}});
    const auto &definitions = circuit.get(component_definition).pins();
    const auto first_definition = definitions[0];
    const auto second_definition = definitions[1];
    const auto unmarked_definition = definitions[2];
    const auto first =
        volt::queries::pin_by_definition(circuit, component, first_definition).value();
    const auto second =
        volt::queries::pin_by_definition(circuit, component, second_definition).value();
    const auto unmarked =
        volt::queries::pin_by_definition(circuit, component, unmarked_definition).value();

    circuit.mark_no_connect(first);
    circuit.mark_no_connect(second);
    circuit.mark_no_connect(first);

    CHECK(circuit.is_intentional_no_connect_pin(first));
    CHECK_FALSE(circuit.is_intentional_no_connect_pin(unmarked));
    CHECK(circuit.intentional_no_connect_pins() == std::vector{first, second});
}
