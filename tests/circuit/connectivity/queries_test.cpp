#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/errors.hpp>

#include <support/circuit_test_helpers.hpp>

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
    fixture.component_definition = volt::test::define_component(
        fixture.circuit, "Diode",
        {volt::test::passive_pin("A", "1"), volt::test::passive_pin("K", "2")});
    const auto &pins = fixture.circuit.get(fixture.component_definition).pins();
    fixture.first_pin = pins[0];
    fixture.second_pin = pins[1];
    fixture.component =
        volt::test::instantiate_component(fixture.circuit, fixture.component_definition, "D1");
    fixture.net = volt::test::add_net(fixture.circuit, "LED_A");
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
    const auto resistor = volt::test::define_component(
        circuit, "Resistor",
        {volt::test::passive_pin("L", "1"), volt::test::passive_pin("R", "2")});
    const auto &pins = circuit.get(resistor).pins();
    const auto left = pins[0];
    const auto right = pins[1];
    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"Divider"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"IN"}, volt::NetKind::Signal},
                          volt::TemplateNetDefinition{volt::NetName{"OUT"}, volt::NetKind::Signal}},
        .components = {volt::ModuleComponentTemplate{resistor, volt::ReferenceDesignator{"R1"}}},
        .connections =
            {
                volt::ModulePinConnectionSpec{volt::NetName{"IN"}, volt::ReferenceDesignator{"R1"},
                                              left},
                volt::ModulePinConnectionSpec{volt::NetName{"OUT"}, volt::ReferenceDesignator{"R1"},
                                              right},
            },
        .ports = {volt::ModulePortSpec{volt::PortName{"IN"}, volt::NetName{"IN"},
                                       volt::PortRole::Passive}},
    });
    const auto input = circuit.get(module).template_nets()[0];
    const auto output = circuit.get(module).template_nets()[1];
    const auto port = circuit.get(module).ports().front();
    const auto component = circuit.get(module).components().front();
    const auto parent_net = volt::test::add_net(circuit, "PARENT");
    const auto instance =
        circuit.instantiate_root_module(module, volt::ModuleInstanceName{"DIV_A"});
    const auto binding = circuit.bind_port(instance, port, parent_net);

    CHECK(volt::queries::module_definition_by_name(circuit, volt::ModuleName{"Divider"}) == module);
    CHECK_FALSE(
        volt::queries::module_definition_by_name(circuit, volt::ModuleName{"Missing"}).has_value());
    CHECK(volt::queries::module_instance_by_name(circuit, volt::ModuleInstanceName{"DIV_A"}) ==
          instance);
    CHECK_FALSE(
        volt::queries::module_instance_by_name(circuit, volt::ModuleInstanceName{"MISSING_A"})
            .has_value());
    CHECK(volt::queries::template_net_by_name(circuit, module, volt::NetName{"OUT"}) == output);
    CHECK_FALSE(
        volt::queries::template_net_by_name(circuit, module, volt::NetName{"MISSING"}).has_value());
    CHECK(volt::queries::port_by_name(circuit, module, volt::PortName{"IN"}) == port);
    CHECK_FALSE(
        volt::queries::port_by_name(circuit, module, volt::PortName{"MISSING"}).has_value());
    CHECK(volt::queries::module_component_by_reference(
              circuit, module, volt::ReferenceDesignator{"R1"}) == component);
    CHECK_FALSE(volt::queries::module_component_by_reference(circuit, module,
                                                             volt::ReferenceDesignator{"R99"})
                    .has_value());
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
    const auto component_definition =
        volt::test::define_component(circuit, "Thing", {volt::test::passive_pin("A", "1")});
    const auto pin = circuit.get(component_definition).pins().front();

    const auto first_module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"First"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"FIRST"},
                                                      volt::NetKind::Signal}},
        .components = {volt::ModuleComponentTemplate{component_definition,
                                                     volt::ReferenceDesignator{"U1"}}},
        .connections = {volt::ModulePinConnectionSpec{volt::NetName{"FIRST"},
                                                      volt::ReferenceDesignator{"U1"}, pin}},
        .ports = {volt::ModulePortSpec{volt::PortName{"FIRST"}, volt::NetName{"FIRST"},
                                       volt::PortRole::Passive}},
    });
    const auto first_port = circuit.get(first_module).ports().front();
    const auto first_component = circuit.get(first_module).components().front();

    const auto second_module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"Second"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"SECOND"},
                                                      volt::NetKind::Signal}},
        .ports = {volt::ModulePortSpec{volt::PortName{"SECOND"}, volt::NetName{"SECOND"},
                                       volt::PortRole::Passive}},
    });
    const auto second_port = circuit.get(second_module).ports().front();

    const auto parent_net = volt::test::add_net(circuit, "PARENT");
    const auto first_instance =
        circuit.instantiate_root_module(first_module, volt::ModuleInstanceName{"FIRST_A"});
    [[maybe_unused]] const auto binding = circuit.bind_port(first_instance, first_port, parent_net);

    try {
        static_cast<void>(
            volt::queries::template_net_for(circuit, second_module, first_component, pin));
        FAIL("Cross-module component query must throw");
    } catch (const volt::KernelLogicError &error) {
        CHECK(error.code() == volt::ErrorCode::CrossReferenceViolation);
        CHECK(std::string{error.what()} == "Module component does not belong to module definition");
    }

    try {
        static_cast<void>(volt::queries::port_binding_for(circuit, first_instance, second_port));
        FAIL("Cross-module port query must throw");
    } catch (const volt::KernelLogicError &error) {
        CHECK(error.code() == volt::ErrorCode::CrossReferenceViolation);
        CHECK(std::string{error.what()} == "Port does not belong to module definition");
    }
}
