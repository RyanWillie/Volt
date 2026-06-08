#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/queries.hpp>

namespace {

struct QueryFixture {
    volt::Circuit circuit;
    volt::PinDefId first_pin;
    volt::PinDefId second_pin;
    volt::ComponentDefId component_definition;
    volt::ComponentId component;
    volt::NetId net;
};

QueryFixture make_query_fixture() {
    QueryFixture fixture{
        .circuit = {},
        .first_pin = volt::PinDefId{0},
        .second_pin = volt::PinDefId{0},
        .component_definition = volt::ComponentDefId{0},
        .component = volt::ComponentId{0},
        .net = volt::NetId{0},
    };
    fixture.first_pin = fixture.circuit.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    fixture.second_pin = fixture.circuit.add_pin_definition(volt::PinDefinition{
        "K", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    fixture.component_definition = fixture.circuit.add_component_definition(
        volt::ComponentDefinition{"Diode", std::vector{fixture.first_pin, fixture.second_pin}});
    fixture.component = fixture.circuit.instantiate_component(fixture.component_definition,
                                                              volt::ReferenceDesignator{"D1"});
    fixture.net = fixture.circuit.add_net(volt::Net{volt::NetName{"LED_A"}, volt::NetKind::Signal});
    fixture.circuit.connect(
        fixture.net, volt::queries::pin_by_number(fixture.circuit, fixture.component, "1").value());
    return fixture;
}

} // namespace

TEST_CASE("Circuit queries find connectivity entities through the const read surface") {
    const auto fixture = make_query_fixture();
    const auto &circuit = fixture.circuit;

    CHECK(volt::queries::component_by_reference(circuit, volt::ReferenceDesignator{"D1"}) ==
          fixture.component);
    CHECK_FALSE(volt::queries::component_by_reference(circuit, volt::ReferenceDesignator{"D2"})
                    .has_value());

    CHECK(volt::queries::net_by_name(circuit, volt::NetName{"LED_A"}) == fixture.net);
    CHECK_FALSE(volt::queries::net_by_name(circuit, volt::NetName{"MISSING"}).has_value());

    const auto pins = volt::queries::pins_for(circuit, fixture.component);
    REQUIRE(pins.size() == 2);
    CHECK(volt::queries::pin_by_name(circuit, fixture.component, "A") == pins.front());
    CHECK(volt::queries::pin_by_number(circuit, fixture.component, "2") == pins.back());
    CHECK(volt::queries::pin_by_definition(circuit, fixture.component, fixture.second_pin) ==
          pins.back());
    CHECK(volt::queries::net_of(circuit, pins.front()) == fixture.net);
    CHECK_FALSE(volt::queries::net_of(circuit, pins.back()).has_value());
}

TEST_CASE("Circuit queries inspect hierarchy views through const Circuit") {
    volt::Circuit circuit;
    const auto left = circuit.add_pin_definition(volt::PinDefinition{
        "L", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto right = circuit.add_pin_definition(volt::PinDefinition{
        "R", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto resistor = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{left, right}});
    const auto module =
        circuit.add_module_definition(volt::ModuleDefinition{volt::ModuleName{"Divider"}});
    const auto input = circuit.add_template_net(
        module, volt::TemplateNetDefinition{volt::NetName{"IN"}, volt::NetKind::Signal});
    const auto output = circuit.add_template_net(
        module, volt::TemplateNetDefinition{volt::NetName{"OUT"}, volt::NetKind::Signal});
    const auto port = circuit.add_port_definition(
        module, volt::PortDefinition{volt::PortName{"IN"}, input, volt::PortRole::Passive});
    const auto component = circuit.add_module_component(
        module, volt::ModuleComponentTemplate{resistor, volt::ReferenceDesignator{"R1"}});
    CHECK(circuit.connect_module_pin(module, input, component, left));
    CHECK(circuit.connect_module_pin(module, output, component, right));
    const auto parent_net =
        circuit.add_net(volt::Net{volt::NetName{"PARENT"}, volt::NetKind::Signal});
    const auto instance =
        circuit.instantiate_root_module(module, volt::ModuleInstanceName{"DIV_A"});
    const auto binding = circuit.bind_port(instance, port, parent_net);

    CHECK(volt::queries::module_definition_by_name(circuit, volt::ModuleName{"Divider"}) == module);
    CHECK(volt::queries::module_instance_by_name(circuit, volt::ModuleInstanceName{"DIV_A"}) ==
          instance);
    CHECK(volt::queries::template_net_by_name(circuit, module, volt::NetName{"OUT"}) == output);
    CHECK(volt::queries::port_by_name(circuit, module, volt::PortName{"IN"}) == port);
    CHECK(volt::queries::module_component_by_reference(
              circuit, module, volt::ReferenceDesignator{"R1"}) == component);
    CHECK(volt::queries::template_net_for(circuit, module, component, right) == output);
    CHECK(volt::queries::port_binding_for(circuit, instance, port) == binding);
    CHECK(volt::queries::port_bindings_for(circuit, instance) == std::vector{binding});
    CHECK(volt::queries::concrete_component_for(circuit, instance, component).has_value());
    CHECK(volt::queries::concrete_net_for(circuit, instance, input).has_value());
    CHECK(volt::queries::module_net_origins(circuit, instance).size() == 2);
    CHECK(volt::queries::module_component_origins(circuit, instance).size() == 1);
    CHECK(volt::queries::is_module_origin_net(
        circuit, volt::queries::concrete_net_for(circuit, instance, input).value()));
    CHECK(volt::queries::is_module_origin_component(
        circuit, volt::queries::concrete_component_for(circuit, instance, component).value()));
}

TEST_CASE("Circuit hierarchy queries preserve model-owned validation contracts") {
    volt::Circuit circuit;
    const auto pin = circuit.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_definition =
        circuit.add_component_definition(volt::ComponentDefinition{"Thing", std::vector{pin}});

    const auto first_module =
        circuit.add_module_definition(volt::ModuleDefinition{volt::ModuleName{"First"}});
    const auto first_net = circuit.add_template_net(
        first_module, volt::TemplateNetDefinition{volt::NetName{"FIRST"}, volt::NetKind::Signal});
    const auto first_port = circuit.add_port_definition(
        first_module,
        volt::PortDefinition{volt::PortName{"FIRST"}, first_net, volt::PortRole::Passive});
    const auto first_component = circuit.add_module_component(
        first_module,
        volt::ModuleComponentTemplate{component_definition, volt::ReferenceDesignator{"U1"}});
    CHECK(circuit.connect_module_pin(first_module, first_net, first_component, pin));

    const auto second_module =
        circuit.add_module_definition(volt::ModuleDefinition{volt::ModuleName{"Second"}});
    const auto second_net = circuit.add_template_net(
        second_module, volt::TemplateNetDefinition{volt::NetName{"SECOND"}, volt::NetKind::Signal});
    const auto second_port = circuit.add_port_definition(
        second_module,
        volt::PortDefinition{volt::PortName{"SECOND"}, second_net, volt::PortRole::Passive});

    const auto parent_net =
        circuit.add_net(volt::Net{volt::NetName{"PARENT"}, volt::NetKind::Signal});
    const auto first_instance =
        circuit.instantiate_root_module(first_module, volt::ModuleInstanceName{"FIRST_A"});
    [[maybe_unused]] const auto binding = circuit.bind_port(first_instance, first_port, parent_net);

    CHECK_THROWS_AS(volt::queries::template_net_for(circuit, second_module, first_component, pin),
                    std::logic_error);
    CHECK_THROWS_AS(volt::queries::port_binding_for(circuit, first_instance, second_port),
                    std::logic_error);
}
