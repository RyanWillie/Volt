#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/nets.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/hierarchy/hierarchy.hpp>
#include <volt/circuit/validation/validation.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/errors.hpp>
#include <volt/core/ids.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>

#include <support/circuit_test_helpers.hpp>

TEST_CASE("Circuit stores module definitions and template-local nets") {
    volt::Circuit circuit;

    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"BuckConverter"},
        .template_nets =
            {
                volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power},
                volt::TemplateNetDefinition{volt::NetName{"FB"}, volt::NetKind::Signal},
            },
    });
    const auto &nets = circuit.get(module).template_nets();
    const auto vin = nets[0];
    const auto fb = nets[1];

    CHECK(module == volt::ModuleDefId{0});
    CHECK(vin == volt::TemplateNetDefId{0});
    CHECK(fb == volt::TemplateNetDefId{1});
    CHECK(circuit.get(module).name() == volt::ModuleName{"BuckConverter"});
    CHECK(circuit.get(module).template_nets().size() == 2);
    CHECK(circuit.get(vin).name() == volt::NetName{"VIN"});
    CHECK(circuit.get(fb).kind() == volt::NetKind::Signal);
    CHECK(circuit.all<volt::ModuleDefId>().size() == 1);
    CHECK(circuit.all<volt::TemplateNetDefId>().size() == 2);
}

TEST_CASE("Circuit rejects duplicate module and template-local net names") {
    volt::Circuit circuit;

    static_cast<void>(
        circuit.define_module(volt::ModuleSpec{.name = volt::ModuleName{"BuckConverter"}}));

    CHECK_THROWS_AS(
        circuit.define_module(volt::ModuleSpec{.name = volt::ModuleName{"BuckConverter"}}),
        std::logic_error);

    CHECK_THROWS_AS(
        circuit.define_module(volt::ModuleSpec{
            .name = volt::ModuleName{"DuplicateNets"},
            .template_nets =
                {
                    volt::TemplateNetDefinition{volt::NetName{"FB"}, volt::NetKind::Signal},
                    volt::TemplateNetDefinition{volt::NetName{"FB"}, volt::NetKind::Signal},
                },
        }),
        std::logic_error);
}

TEST_CASE("Circuit stores ports that reference one internal template net") {
    volt::Circuit circuit;

    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"BuckConverter"},
        .template_nets =
            {
                volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power},
            },
        .ports = {volt::ModulePortSpec{volt::PortName{"VIN"}, volt::NetName{"VIN"},
                                       volt::PortRole::PowerInput}},
    });
    const auto vin = circuit.get(module).template_nets().front();
    const auto port = circuit.get(module).ports().front();

    CHECK(port == volt::PortDefId{0});
    CHECK(circuit.get(port).name() == volt::PortName{"VIN"});
    CHECK(circuit.get(port).internal_net() == vin);
    CHECK(circuit.get(port).role() == volt::PortRole::PowerInput);
    CHECK(circuit.get(port).required());
    CHECK(circuit.get(module).ports().size() == 1);
    CHECK(circuit.all<volt::PortDefId>().size() == 1);
}

TEST_CASE("Circuit complete modules reject invalid internal port nets and duplicate names") {
    volt::Circuit circuit;

    CHECK_THROWS_AS(circuit.define_module(volt::ModuleSpec{
                        .name = volt::ModuleName{"DuplicatePorts"},
                        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"VIN"},
                                                                      volt::NetKind::Power}},
                        .ports =
                            {
                                volt::ModulePortSpec{volt::PortName{"VIN"}, volt::NetName{"VIN"}},
                                volt::ModulePortSpec{volt::PortName{"VIN"}, volt::NetName{"VIN"}},
                            },
                    }),
                    std::logic_error);
    CHECK_THROWS_AS(
        circuit.define_module(volt::ModuleSpec{
            .name = volt::ModuleName{"MissingPortNet"},
            .ports = {volt::ModulePortSpec{volt::PortName{"VIN"}, volt::NetName{"MISSING"}}},
        }),
        std::logic_error);
}

TEST_CASE("Circuit stores module component templates and template pin connectivity") {
    volt::Circuit circuit;
    const auto resistor = volt::test::define_component(
        circuit, "Resistor",
        {volt::test::passive_pin("1", "1"), volt::test::passive_pin("2", "2")});
    const auto &resistor_pins = circuit.get(resistor).pins();
    const auto left = resistor_pins[0];
    const auto right = resistor_pins[1];
    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"Divider"},
        .template_nets =
            {
                volt::TemplateNetDefinition{volt::NetName{"IN"}, volt::NetKind::Signal},
                volt::TemplateNetDefinition{volt::NetName{"OUT"}, volt::NetKind::Signal},
            },
        .components = {volt::ModuleComponentTemplate{resistor, volt::ReferenceDesignator{"R1"}}},
        .connections =
            {
                volt::ModulePinConnectionSpec{volt::NetName{"IN"}, volt::ReferenceDesignator{"R1"},
                                              left},
                volt::ModulePinConnectionSpec{volt::NetName{"OUT"}, volt::ReferenceDesignator{"R1"},
                                              right},
            },
    });
    const auto &definition = circuit.get(module);
    const auto input = definition.template_nets()[0];
    const auto output = definition.template_nets()[1];
    const auto component = definition.components().front();

    CHECK(component == volt::ModuleComponentId{0});
    CHECK(circuit.get(module).components().size() == 1);
    CHECK(circuit.get(component).definition() == resistor);
    CHECK(circuit.get(component).reference() == volt::ReferenceDesignator{"R1"});

    CHECK(volt::queries::template_net_for(circuit, module, component, left) == input);
    CHECK(volt::queries::template_net_for(circuit, module, component, right) == output);
    CHECK(circuit.all<volt::ModuleComponentId>().size() == 1);
    CHECK(circuit.module_pin_connections(module).size() == 2);
}

TEST_CASE("Root module instantiation materializes module component templates") {
    volt::Circuit circuit;
    const auto resistor = volt::test::define_component(
        circuit, "Resistor",
        {volt::test::passive_pin("1", "1"), volt::test::passive_pin("2", "2")});
    const auto &resistor_pins = circuit.get(resistor).pins();
    const auto left = resistor_pins[0];
    const auto right = resistor_pins[1];
    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"Divider"},
        .template_nets =
            {
                volt::TemplateNetDefinition{volt::NetName{"IN"}, volt::NetKind::Signal},
                volt::TemplateNetDefinition{volt::NetName{"OUT"}, volt::NetKind::Signal},
            },
        .components = {volt::ModuleComponentTemplate{resistor, volt::ReferenceDesignator{"R1"}}},
        .connections =
            {
                volt::ModulePinConnectionSpec{volt::NetName{"IN"}, volt::ReferenceDesignator{"R1"},
                                              left},
                volt::ModulePinConnectionSpec{volt::NetName{"OUT"}, volt::ReferenceDesignator{"R1"},
                                              right},
            },
    });
    const auto &definition = circuit.get(module);
    const auto input = definition.template_nets()[0];
    const auto output = definition.template_nets()[1];
    const auto component = definition.components().front();

    const auto instance =
        circuit.instantiate_root_module(module, volt::ModuleInstanceName{"DIV_A"});

    const auto concrete_component =
        volt::queries::concrete_component_for(circuit, instance, component);
    REQUIRE(concrete_component.has_value());
    CHECK(concrete_component.value() == volt::ComponentId{0});
    CHECK(circuit.get(concrete_component.value()).reference() ==
          volt::ReferenceDesignator{"DIV_A/R1"});
    REQUIRE(volt::queries::concrete_net_for(circuit, instance, input).has_value());
    REQUIRE(volt::queries::concrete_net_for(circuit, instance, output).has_value());
    REQUIRE(volt::queries::pin_by_number(circuit, concrete_component.value(), "1").has_value());
    REQUIRE(volt::queries::pin_by_number(circuit, concrete_component.value(), "2").has_value());
    CHECK(volt::queries::net_of(
              circuit,
              volt::queries::pin_by_number(circuit, concrete_component.value(), "1").value()) ==
          volt::queries::concrete_net_for(circuit, instance, input));
    CHECK(volt::queries::net_of(
              circuit,
              volt::queries::pin_by_number(circuit, concrete_component.value(), "2").value()) ==
          volt::queries::concrete_net_for(circuit, instance, output));
}

TEST_CASE("Circuit exposes hierarchy inspection views") {
    volt::Circuit circuit;
    const auto resistor = volt::test::define_component(
        circuit, "Resistor",
        {volt::test::passive_pin("1", "1"), volt::test::passive_pin("2", "2")});
    const auto &resistor_pins = circuit.get(resistor).pins();
    const auto left = resistor_pins[0];
    const auto right = resistor_pins[1];
    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"Divider"},
        .template_nets =
            {
                volt::TemplateNetDefinition{volt::NetName{"IN"}, volt::NetKind::Power},
                volt::TemplateNetDefinition{volt::NetName{"OUT"}, volt::NetKind::Signal},
            },
        .components = {volt::ModuleComponentTemplate{resistor, volt::ReferenceDesignator{"R1"}}},
        .connections =
            {
                volt::ModulePinConnectionSpec{volt::NetName{"IN"}, volt::ReferenceDesignator{"R1"},
                                              left},
                volt::ModulePinConnectionSpec{volt::NetName{"OUT"}, volt::ReferenceDesignator{"R1"},
                                              right},
            },
        .ports = {volt::ModulePortSpec{volt::PortName{"IN"}, volt::NetName{"IN"},
                                       volt::PortRole::PowerInput}},
    });
    const auto &definition = circuit.get(module);
    const auto input = definition.template_nets()[0];
    const auto port = definition.ports().front();
    const auto component = definition.components().front();

    const auto parent = volt::test::add_net(circuit, "VIN", volt::NetKind::Power);
    const auto instance =
        circuit.instantiate_root_module(module, volt::ModuleInstanceName{"DIV_A"});
    const auto binding = circuit.bind_port(instance, port, parent);

    CHECK(circuit.module_pin_connections(module).size() == 2);
    CHECK(circuit.module_pin_connections(module)[0].net() == input);
    CHECK(circuit.module_net_origins(instance).size() == 2);
    CHECK(circuit.module_net_origins(instance)[0].first == input);
    CHECK(circuit.module_component_origins(instance).size() == 1);
    CHECK(circuit.module_component_origins(instance)[0].first == component);
    CHECK(volt::queries::port_bindings_for(circuit, instance) == std::vector{binding});
}

TEST_CASE("Root module instantiation creates concrete nets for template-local nets") {
    volt::Circuit circuit;

    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"BuckConverter"},
        .template_nets =
            {
                volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power},
                volt::TemplateNetDefinition{volt::NetName{"FB"}, volt::NetKind::Signal},
            },
    });
    const auto &template_nets = circuit.get(module).template_nets();
    const auto vin = template_nets[0];
    const auto fb = template_nets[1];

    const auto first = circuit.instantiate_root_module(module, volt::ModuleInstanceName{"BUCK_A"});
    const auto second = circuit.instantiate_root_module(module, volt::ModuleInstanceName{"BUCK_B"});

    CHECK(first == volt::ModuleInstanceId{0});
    CHECK(second == volt::ModuleInstanceId{1});
    CHECK(circuit.get(first).definition() == module);
    CHECK(circuit.get(first).name() == volt::ModuleInstanceName{"BUCK_A"});
    CHECK(circuit.all<volt::ModuleInstanceId>().size() == 2);
    CHECK(circuit.all<volt::NetId>().size() == 4);

    const auto first_vin = volt::queries::concrete_net_for(circuit, first, vin);
    const auto first_fb = volt::queries::concrete_net_for(circuit, first, fb);
    const auto second_fb = volt::queries::concrete_net_for(circuit, second, fb);
    REQUIRE(first_vin.has_value());
    REQUIRE(first_fb.has_value());
    REQUIRE(second_fb.has_value());
    CHECK(first_fb.value() != second_fb.value());
    CHECK(circuit.get(first_vin.value()).name() == volt::NetName{"BUCK_A/VIN"});
    CHECK(circuit.get(first_fb.value()).name() == volt::NetName{"BUCK_A/FB"});
    CHECK(circuit.get(second_fb.value()).name() == volt::NetName{"BUCK_B/FB"});
}

TEST_CASE("Circuit records port bindings as explicit edges without merging nets") {
    volt::Circuit circuit;

    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"BuckConverter"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power}},
        .ports = {volt::ModulePortSpec{volt::PortName{"VIN"}, volt::NetName{"VIN"},
                                       volt::PortRole::PowerInput}},
    });
    const auto vin = circuit.get(module).template_nets().front();
    const auto port = circuit.get(module).ports().front();
    const auto instance =
        circuit.instantiate_root_module(module, volt::ModuleInstanceName{"BUCK_A"});
    const auto parent_net = volt::test::add_net(circuit, "VIN", volt::NetKind::Power);

    const auto binding = circuit.bind_port(instance, port, parent_net);

    REQUIRE(volt::queries::concrete_net_for(circuit, instance, vin).has_value());
    const auto internal_net = volt::queries::concrete_net_for(circuit, instance, vin).value();
    CHECK(binding == volt::PortBindingId{0});
    CHECK(circuit.get(binding).instance() == instance);
    CHECK(circuit.get(binding).port() == port);
    CHECK(circuit.get(binding).internal_net() == internal_net);
    CHECK(circuit.get(binding).parent_net() == parent_net);
    CHECK(internal_net != parent_net);
    CHECK(circuit.all<volt::PortBindingId>().size() == 1);
}

TEST_CASE("Circuit rejects duplicate port bindings for one module instance port") {
    volt::Circuit circuit;

    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"BuckConverter"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power}},
        .ports = {volt::ModulePortSpec{volt::PortName{"VIN"}, volt::NetName{"VIN"},
                                       volt::PortRole::PowerInput}},
    });
    const auto port = circuit.get(module).ports().front();
    const auto instance =
        circuit.instantiate_root_module(module, volt::ModuleInstanceName{"BUCK_A"});
    const auto first_parent = volt::test::add_net(circuit, "VIN", volt::NetKind::Power);
    const auto second_parent = volt::test::add_net(circuit, "VIN_ALT", volt::NetKind::Power);

    [[maybe_unused]] const auto binding = circuit.bind_port(instance, port, first_parent);

    CHECK_THROWS_AS(circuit.bind_port(instance, port, second_parent), std::logic_error);
    CHECK(circuit.all<volt::PortBindingId>().size() == 1);
}

TEST_CASE("Circuit rejects binding a module port to its own internal net") {
    volt::Circuit circuit;

    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"BuckConverter"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power}},
        .ports = {volt::ModulePortSpec{volt::PortName{"VIN"}, volt::NetName{"VIN"},
                                       volt::PortRole::PowerInput}},
    });
    const auto vin = circuit.get(module).template_nets().front();
    const auto port = circuit.get(module).ports().front();
    const auto instance =
        circuit.instantiate_root_module(module, volt::ModuleInstanceName{"BUCK_A"});
    const auto internal_net = volt::queries::concrete_net_for(circuit, instance, vin);
    REQUIRE(internal_net.has_value());

    CHECK_THROWS_AS(circuit.bind_port(instance, port, internal_net.value()), std::logic_error);
    CHECK(circuit.all<volt::PortBindingId>().size() == 0);
}

TEST_CASE("Root module instantiation preflights concrete net names before mutating") {
    volt::Circuit circuit;

    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"BuckConverter"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power}},
    });
    [[maybe_unused]] const auto existing_net =
        volt::test::add_net(circuit, "BUCK_A/VIN", volt::NetKind::Power);
    const auto before = volt::io::write_logical_circuit(circuit);

    CHECK_THROWS_AS(circuit.instantiate_root_module(module, volt::ModuleInstanceName{"BUCK_A"}),
                    std::logic_error);
    CHECK(volt::io::write_logical_circuit(circuit) == before);
    CHECK(circuit.all<volt::ModuleInstanceId>().size() == 0);
    CHECK(circuit.all<volt::NetId>().size() == 1);
}

TEST_CASE("Root module instantiation preflights concrete component references before mutating") {
    volt::Circuit circuit;
    const auto definition =
        volt::test::define_component(circuit, "Thing", {volt::test::passive_pin("1", "1")});
    [[maybe_unused]] const auto existing =
        volt::test::instantiate_component(circuit, definition, "DIV_A/R1");
    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"Divider"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"IN"}, volt::NetKind::Signal}},
        .components = {volt::ModuleComponentTemplate{definition, volt::ReferenceDesignator{"R1"}}},
    });
    const auto before = volt::io::write_logical_circuit(circuit);

    CHECK_THROWS_AS(circuit.instantiate_root_module(module, volt::ModuleInstanceName{"DIV_A"}),
                    std::logic_error);
    CHECK(volt::io::write_logical_circuit(circuit) == before);
    CHECK(circuit.all<volt::ModuleInstanceId>().size() == 0);
    CHECK(circuit.all<volt::ComponentId>().size() == 1);
    CHECK(circuit.all<volt::NetId>().size() == 0);
}

TEST_CASE("Circuit validation reports unbound required module ports") {
    volt::Circuit circuit;

    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"BuckConverter"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power}},
        .ports = {volt::ModulePortSpec{volt::PortName{"VIN"}, volt::NetName{"VIN"},
                                       volt::PortRole::PowerInput}},
    });
    const auto port = circuit.get(module).ports().front();
    const auto instance =
        circuit.instantiate_root_module(module, volt::ModuleInstanceName{"BUCK_A"});

    const auto report = volt::validate_circuit(circuit);

    REQUIRE_FALSE(report.diagnostics().empty());
    const auto &diagnostic = report.diagnostics().front();
    CHECK(diagnostic.code() == volt::DiagnosticCode{"UNBOUND_REQUIRED_PORT"});
    CHECK(diagnostic.entities() == std::vector{volt::EntityRef::module_instance(instance),
                                               volt::EntityRef::module_def(module),
                                               volt::EntityRef::port_def(port)});
}

TEST_CASE("Circuit validation accepts bound required module ports") {
    volt::Circuit circuit;

    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"BuckConverter"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power}},
        .ports = {volt::ModulePortSpec{volt::PortName{"VIN"}, volt::NetName{"VIN"},
                                       volt::PortRole::PowerInput}},
    });
    const auto port = circuit.get(module).ports().front();
    const auto instance =
        circuit.instantiate_root_module(module, volt::ModuleInstanceName{"BUCK_A"});
    const auto parent_net = volt::test::add_net(circuit, "VIN", volt::NetKind::Power);
    [[maybe_unused]] const auto binding = circuit.bind_port(instance, port, parent_net);

    const auto report = volt::validate_connectivity(circuit);

    for (const auto &diagnostic : report.diagnostics()) {
        CHECK(diagnostic.code() != volt::DiagnosticCode{"UNBOUND_REQUIRED_PORT"});
    }
}
