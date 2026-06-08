#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

#include <volt/circuit/connectivity_model.hpp>

namespace {

struct ConnectivityFixture {
    volt::ConnectivityModel model;
    volt::PinDefId first_pin;
    volt::PinDefId second_pin;
    volt::ComponentDefId component_definition;
};

ConnectivityFixture make_connectivity_fixture() {
    ConnectivityFixture fixture{
        .model = {},
        .first_pin = volt::PinDefId{0},
        .second_pin = volt::PinDefId{0},
        .component_definition = volt::ComponentDefId{0},
    };
    fixture.first_pin = fixture.model.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    fixture.second_pin = fixture.model.add_pin_definition(volt::PinDefinition{
        "B", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    fixture.component_definition = fixture.model.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{fixture.first_pin, fixture.second_pin}});
    return fixture;
}

} // namespace

TEST_CASE("ConnectivityModel stores reusable definitions in deterministic order") {
    volt::ConnectivityModel model;

    const auto first = model.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second = model.add_pin_definition(volt::PinDefinition{
        "B", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto resistor = model.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first, second}});

    CHECK(first == volt::PinDefId{0});
    CHECK(second == volt::PinDefId{1});
    CHECK(resistor == volt::ComponentDefId{0});
    CHECK(model.pin_definition(first).name() == "A");
    CHECK(model.component_definition(resistor).pins() == std::vector{first, second});
}

TEST_CASE("ConnectivityModel rejects component definitions with missing pin definitions") {
    volt::ConnectivityModel model;

    CHECK_THROWS_AS(model.add_component_definition(
                        volt::ComponentDefinition{"Broken", std::vector{volt::PinDefId{99}}}),
                    std::out_of_range);
}

TEST_CASE("ConnectivityModel instantiates components and pins deterministically") {
    auto fixture = make_connectivity_fixture();

    const auto component = fixture.model.instantiate_component(fixture.component_definition,
                                                               volt::ReferenceDesignator{"R1"});

    CHECK(component == volt::ComponentId{0});
    CHECK(fixture.model.component(component).reference() == volt::ReferenceDesignator{"R1"});
    CHECK(fixture.model.pin_count() == 2);
    CHECK(fixture.model.pins_for(component) == std::vector{volt::PinId{0}, volt::PinId{1}});
    CHECK(fixture.model.pin(volt::PinId{0}).definition() == fixture.first_pin);
    CHECK(fixture.model.pin(volt::PinId{1}).definition() == fixture.second_pin);
}

TEST_CASE("ConnectivityModel enforces unique component references") {
    auto fixture = make_connectivity_fixture();
    [[maybe_unused]] const auto first = fixture.model.instantiate_component(
        fixture.component_definition, volt::ReferenceDesignator{"R1"});

    CHECK_THROWS_AS(fixture.model.instantiate_component(fixture.component_definition,
                                                        volt::ReferenceDesignator{"R1"}),
                    std::logic_error);
}

TEST_CASE("ConnectivityModel rejects pin instances outside their component definition") {
    auto fixture = make_connectivity_fixture();
    const auto component = fixture.model.add_component(
        volt::ComponentInstance{fixture.component_definition, volt::ReferenceDesignator{"R1"}});
    const auto unrelated_pin = fixture.model.add_pin_definition(volt::PinDefinition{
        "C", "3", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});

    CHECK_THROWS_AS(fixture.model.add_pin(volt::PinInstance{component, unrelated_pin}),
                    std::logic_error);
}

TEST_CASE("ConnectivityModel enforces unique net names and dangling pin rejection") {
    auto fixture = make_connectivity_fixture();
    const auto component = fixture.model.instantiate_component(fixture.component_definition,
                                                               volt::ReferenceDesignator{"R1"});
    const auto pin = fixture.model.pins_for(component).front();
    auto net_with_pin = volt::Net{volt::NetName{"NET_A"}, volt::NetKind::Signal};
    net_with_pin.connect(pin);

    const auto net = fixture.model.add_net(std::move(net_with_pin));

    CHECK(net == volt::NetId{0});
    CHECK_THROWS_AS(fixture.model.add_net(volt::Net{volt::NetName{"NET_A"}, volt::NetKind::Signal}),
                    std::logic_error);

    auto dangling = volt::Net{volt::NetName{"BROKEN"}, volt::NetKind::Signal};
    dangling.connect(volt::PinId{99});
    CHECK_THROWS_AS(fixture.model.add_net(std::move(dangling)), std::out_of_range);
}

TEST_CASE("ConnectivityModel enforces one net per pin at mutation boundaries") {
    auto fixture = make_connectivity_fixture();
    const auto component = fixture.model.instantiate_component(fixture.component_definition,
                                                               volt::ReferenceDesignator{"R1"});
    const auto pin = fixture.model.pins_for(component).front();
    const auto first_net =
        fixture.model.add_net(volt::Net{volt::NetName{"NET_A"}, volt::NetKind::Signal});
    const auto second_net =
        fixture.model.add_net(volt::Net{volt::NetName{"NET_B"}, volt::NetKind::Signal});

    CHECK(fixture.model.connect(first_net, pin));
    CHECK_FALSE(fixture.model.connect(first_net, pin));
    CHECK_THROWS_AS(fixture.model.connect(second_net, pin), std::logic_error);
    CHECK(fixture.model.net_of(pin) == first_net);

    CHECK(fixture.model.disconnect(pin));
    CHECK_FALSE(fixture.model.disconnect(pin));
    CHECK_FALSE(fixture.model.net_of(pin).has_value());
}
