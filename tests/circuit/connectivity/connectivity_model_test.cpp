#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>

#include <support/circuit_test_helpers.hpp>

namespace {

struct ConnectivityFixture {
    volt::Circuit circuit;
    volt::PinDefId first_pin;
    volt::PinDefId second_pin;
    volt::ComponentDefId component_definition;
};

ConnectivityFixture make_connectivity_fixture() {
    ConnectivityFixture fixture{
        .circuit = {},
        .first_pin = volt::PinDefId{0},
        .second_pin = volt::PinDefId{0},
        .component_definition = volt::ComponentDefId{0},
    };
    fixture.component_definition = volt::test::define_component(
        fixture.circuit, "Resistor",
        {volt::test::passive_pin("A", "1"), volt::test::passive_pin("B", "2")});
    const auto &pins = fixture.circuit.get(fixture.component_definition).pins();
    fixture.first_pin = pins[0];
    fixture.second_pin = pins[1];
    return fixture;
}

} // namespace

TEST_CASE("Circuit complete definitions preserve deterministic connectivity IDs") {
    volt::Circuit circuit;

    const auto resistor = circuit.define_component(volt::ComponentSpec{
        .name = "Resistor",
        .pins = {volt::test::passive_pin("A", "1"), volt::test::passive_pin("B", "2")},
    });
    const auto &pins = circuit.get(resistor).pins();
    const auto first = pins[0];
    const auto second = pins[1];
    CHECK(first == volt::PinDefId{0});
    CHECK(second == volt::PinDefId{1});
    CHECK(resistor == volt::ComponentDefId{0});
    CHECK(circuit.get(first).name() == "A");
    CHECK(circuit.get(resistor).pins() == std::vector{first, second});
}

TEST_CASE("Legacy connectivity facade rejects definitions with missing raw pin IDs") {
    volt::Circuit circuit;

    // ComponentSpec owns complete PinSpec values; raw PinDefId input remains only until #266.
    CHECK_THROWS_AS(circuit.connectivity().add_component_definition(
                        volt::ComponentDefinition{"Broken", std::vector{volt::PinDefId{99}}}),
                    std::out_of_range);
}

TEST_CASE("Circuit instantiates complete components and pins deterministically") {
    auto fixture = make_connectivity_fixture();

    const auto component = fixture.circuit.instantiate_component(fixture.component_definition,
                                                                 volt::ReferenceDesignator{"R1"});
    CHECK(component == volt::ComponentId{0});
    CHECK(fixture.circuit.get(component).reference() == volt::ReferenceDesignator{"R1"});
    CHECK(fixture.circuit.all<volt::PinId>().size() == 2);
    CHECK(volt::queries::pins_for(fixture.circuit, component) ==
          std::vector{volt::PinId{0}, volt::PinId{1}});
    CHECK(fixture.circuit.get(volt::PinId{0}).definition() == fixture.first_pin);
    CHECK(fixture.circuit.get(volt::PinId{1}).definition() == fixture.second_pin);
}

TEST_CASE("Circuit enforces unique component references") {
    auto fixture = make_connectivity_fixture();
    [[maybe_unused]] const auto first = fixture.circuit.instantiate_component(
        fixture.component_definition, volt::ReferenceDesignator{"R1"});

    try {
        static_cast<void>(fixture.circuit.instantiate_component(fixture.component_definition,
                                                                volt::ReferenceDesignator{"R1"}));
        FAIL("Duplicate component references must throw");
    } catch (const volt::KernelLogicError &error) {
        CHECK(error.code() == volt::ErrorCode::DuplicateName);
        CHECK(std::string{error.what()} == "Component reference designator already exists");
    }
}

TEST_CASE("Legacy connectivity facade rejects raw pins outside their component definition") {
    auto fixture = make_connectivity_fixture();
    const auto component =
        volt::test::instantiate_component(fixture.circuit, fixture.component_definition, "R1");
    const auto unrelated_definition = volt::test::define_component(
        fixture.circuit, "Unrelated", {volt::test::passive_pin("C", "3")});
    const auto unrelated_pin = fixture.circuit.get(unrelated_definition).pins().front();

    // Raw PinInstance insertion remains a focused transitional-facade contract until #266.
    CHECK_THROWS_AS(
        fixture.circuit.connectivity().add_pin(volt::PinInstance{component, unrelated_pin}),
        std::logic_error);
}

TEST_CASE("Circuit enforces unique net names") {
    auto fixture = make_connectivity_fixture();
    const auto net = fixture.circuit.add_net(volt::NetSpec{.name = volt::NetName{"NET_A"}});

    CHECK(net == volt::NetId{0});
    try {
        static_cast<void>(fixture.circuit.add_net(volt::NetSpec{.name = volt::NetName{"NET_A"}}));
        FAIL("Duplicate net names must throw");
    } catch (const volt::KernelLogicError &error) {
        CHECK(error.code() == volt::ErrorCode::DuplicateName);
        CHECK(std::string{error.what()} == "Net name already exists");
    }
}

TEST_CASE("Legacy connectivity facade rejects preconnected nets with dangling pins") {
    auto fixture = make_connectivity_fixture();
    auto dangling = volt::Net{volt::NetName{"BROKEN"}, volt::NetKind::Signal};
    dangling.connect(volt::PinId{99});
    // NetSpec creates an empty net; raw preconnected Net insertion remains only until #266.
    CHECK_THROWS_AS(fixture.circuit.connectivity().add_net(std::move(dangling)), std::out_of_range);
}

TEST_CASE("Circuit enforces one net per pin at mutation boundaries") {
    auto fixture = make_connectivity_fixture();
    const auto component = fixture.circuit.instantiate_component(fixture.component_definition,
                                                                 volt::ReferenceDesignator{"R1"});
    const auto pin = volt::queries::pins_for(fixture.circuit, component).front();
    const auto first_net = fixture.circuit.add_net(volt::NetSpec{.name = volt::NetName{"NET_A"}});
    const auto second_net = fixture.circuit.add_net(volt::NetSpec{.name = volt::NetName{"NET_B"}});

    CHECK(fixture.circuit.connect(first_net, pin));
    CHECK_FALSE(fixture.circuit.connect(first_net, pin));
    CHECK_THROWS_AS(fixture.circuit.connect(second_net, pin), std::logic_error);
    CHECK(fixture.circuit.net_of(pin) == first_net);

    CHECK(fixture.circuit.disconnect(pin));
    CHECK_FALSE(fixture.circuit.disconnect(pin));
    CHECK_FALSE(fixture.circuit.net_of(pin).has_value());
}
