#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>

TEST_CASE("Circuit records intentional stub nets idempotently in deterministic order") {
    volt::Circuit circuit;
    const auto first = circuit.add_net(volt::NetSpec{.name = volt::NetName{"STUB_A"}});
    const auto second = circuit.add_net(volt::NetSpec{.name = volt::NetName{"STUB_B"}});
    const auto unmarked = circuit.add_net(volt::NetSpec{.name = volt::NetName{"NORMAL"}});

    circuit.update(second, volt::MarkIntentionalStub{});
    circuit.update(first, volt::MarkIntentionalStub{});
    circuit.update(first, volt::MarkIntentionalStub{});

    CHECK(volt::queries::is_intentional_stub_net(circuit, first));
    CHECK_FALSE(volt::queries::is_intentional_stub_net(circuit, unmarked));
    CHECK(volt::queries::intentional_stub_nets(circuit) == std::vector{second, first});
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

    circuit.mark_no_connect(second);
    circuit.mark_no_connect(first);
    circuit.mark_no_connect(first);

    CHECK(volt::queries::is_intentional_no_connect_pin(circuit, first));
    CHECK_FALSE(volt::queries::is_intentional_no_connect_pin(circuit, unmarked));
    CHECK(volt::queries::intentional_no_connect_pins(circuit) == std::vector{second, first});
}

TEST_CASE("Circuit preserves first-authored component assembly-intent order") {
    volt::Circuit circuit;
    const auto definition = circuit.define_component(volt::ComponentSpec{
        .name = "Resistor",
        .pins = {volt::PinSpec{.name = "1", .number = "1"}},
    });
    const auto first = circuit.instantiate_component(
        definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    const auto second = circuit.instantiate_component(
        definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R2"}});

    circuit.update(second, volt::SetAssemblyIntent{.selection_override = true});
    circuit.update(first, volt::SetAssemblyIntent{.dnp = true});

    auto intents = volt::queries::component_assembly_intents(circuit);
    REQUIRE(intents.size() == 2);
    CHECK(intents[0].component() == second);
    CHECK(intents[1].component() == first);

    circuit.update(second, volt::SetAssemblyIntent{.selection_override = false});
    circuit.update(second, volt::SetAssemblyIntent{.selection_override = true});

    intents = volt::queries::component_assembly_intents(circuit);
    REQUIRE(intents.size() == 2);
    CHECK(intents[0].component() == first);
    CHECK(intents[1].component() == second);
}
