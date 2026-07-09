#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

#include <volt/circuit/circuit.hpp>

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
    fixture.first_pin = fixture.circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    fixture.second_pin = fixture.circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "B", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    fixture.component_definition = fixture.circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{fixture.first_pin, fixture.second_pin}});
    return fixture;
}

} // namespace

TEST_CASE("ConnectivityModel stores reusable definitions in deterministic order") {
    volt::Circuit circuit;

    const auto first = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "B", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto resistor = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first, second}});
    const auto &model = circuit.connectivity_model();

    CHECK(first == volt::PinDefId{0});
    CHECK(second == volt::PinDefId{1});
    CHECK(resistor == volt::ComponentDefId{0});
    CHECK(model.pin_definition(first).name() == "A");
    CHECK(model.component_definition(resistor).pins() == std::vector{first, second});
}

TEST_CASE("ConnectivityModel rejects component definitions with missing pin definitions") {
    volt::Circuit circuit;

    CHECK_THROWS_AS(circuit.connectivity().add_component_definition(
                        volt::ComponentDefinition{"Broken", std::vector{volt::PinDefId{99}}}),
                    std::out_of_range);
}

TEST_CASE("ConnectivityModel instantiates components and pins deterministically") {
    auto fixture = make_connectivity_fixture();

    const auto component = fixture.circuit.instantiate_component(fixture.component_definition,
                                                                 volt::ReferenceDesignator{"R1"});
    const auto &model = fixture.circuit.connectivity_model();

    CHECK(component == volt::ComponentId{0});
    CHECK(model.component(component).reference() == volt::ReferenceDesignator{"R1"});
    CHECK(model.pin_count() == 2);
    CHECK(model.pins_for(component) == std::vector{volt::PinId{0}, volt::PinId{1}});
    CHECK(model.pin(volt::PinId{0}).definition() == fixture.first_pin);
    CHECK(model.pin(volt::PinId{1}).definition() == fixture.second_pin);
}

TEST_CASE("ConnectivityModel enforces unique component references") {
    auto fixture = make_connectivity_fixture();
    [[maybe_unused]] const auto first = fixture.circuit.instantiate_component(
        fixture.component_definition, volt::ReferenceDesignator{"R1"});

    CHECK_THROWS_AS(fixture.circuit.instantiate_component(fixture.component_definition,
                                                          volt::ReferenceDesignator{"R1"}),
                    std::logic_error);
}

TEST_CASE("ConnectivityModel rejects pin instances outside their component definition") {
    auto fixture = make_connectivity_fixture();
    const auto component = fixture.circuit.connectivity().add_component(
        volt::ComponentInstance{fixture.component_definition, volt::ReferenceDesignator{"R1"}});
    const auto unrelated_pin =
        fixture.circuit.connectivity().add_pin_definition(volt::PinDefinition{
            "C", "3", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
            volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
            volt::ElectricalDriveKind::Passive});

    CHECK_THROWS_AS(
        fixture.circuit.connectivity().add_pin(volt::PinInstance{component, unrelated_pin}),
        std::logic_error);
}

TEST_CASE("ConnectivityModel enforces unique net names and dangling pin rejection") {
    auto fixture = make_connectivity_fixture();
    const auto component = fixture.circuit.instantiate_component(fixture.component_definition,
                                                                 volt::ReferenceDesignator{"R1"});
    const auto pin = fixture.circuit.connectivity_model().pins_for(component).front();
    auto net_with_pin = volt::Net{volt::NetName{"NET_A"}, volt::NetKind::Signal};
    net_with_pin.connect(pin);

    const auto net = fixture.circuit.connectivity().add_net(std::move(net_with_pin));

    CHECK(net == volt::NetId{0});
    CHECK_THROWS_AS(fixture.circuit.connectivity().add_net(
                        volt::Net{volt::NetName{"NET_A"}, volt::NetKind::Signal}),
                    std::logic_error);

    auto dangling = volt::Net{volt::NetName{"BROKEN"}, volt::NetKind::Signal};
    dangling.connect(volt::PinId{99});
    CHECK_THROWS_AS(fixture.circuit.connectivity().add_net(std::move(dangling)), std::out_of_range);
}

TEST_CASE("ConnectivityModel enforces one net per pin at mutation boundaries") {
    auto fixture = make_connectivity_fixture();
    const auto component = fixture.circuit.instantiate_component(fixture.component_definition,
                                                                 volt::ReferenceDesignator{"R1"});
    const auto pin = fixture.circuit.connectivity_model().pins_for(component).front();
    const auto first_net = fixture.circuit.connectivity().add_net(
        volt::Net{volt::NetName{"NET_A"}, volt::NetKind::Signal});
    const auto second_net = fixture.circuit.connectivity().add_net(
        volt::Net{volt::NetName{"NET_B"}, volt::NetKind::Signal});

    CHECK(fixture.circuit.connect(first_net, pin));
    CHECK_FALSE(fixture.circuit.connect(first_net, pin));
    CHECK_THROWS_AS(fixture.circuit.connect(second_net, pin), std::logic_error);
    CHECK(fixture.circuit.connectivity_model().net_of(pin) == first_net);

    CHECK(fixture.circuit.disconnect(pin));
    CHECK_FALSE(fixture.circuit.disconnect(pin));
    CHECK_FALSE(fixture.circuit.connectivity_model().net_of(pin).has_value());
}
