#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/hierarchy.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/circuit/validation.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/ids.hpp>

TEST_CASE("Circuit stores module definitions and template-local nets") {
    volt::Circuit circuit;

    const auto module = circuit.add_module_definition(volt::ModuleDefinition{
        volt::ModuleName{"BuckConverter"},
    });
    const auto vin = circuit.add_template_net(
        module, volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power});
    const auto fb = circuit.add_template_net(
        module, volt::TemplateNetDefinition{volt::NetName{"FB"}, volt::NetKind::Signal});

    CHECK(module == volt::ModuleDefId{0});
    CHECK(vin == volt::TemplateNetDefId{0});
    CHECK(fb == volt::TemplateNetDefId{1});
    CHECK(circuit.module_definition(module).name() == volt::ModuleName{"BuckConverter"});
    CHECK(circuit.module_definition(module).template_nets().size() == 2);
    CHECK(circuit.template_net_definition(vin).name() == volt::NetName{"VIN"});
    CHECK(circuit.template_net_definition(fb).kind() == volt::NetKind::Signal);
    CHECK(circuit.module_definition_count() == 1);
    CHECK(circuit.template_net_definition_count() == 2);
}

TEST_CASE("Circuit rejects duplicate module and template-local net names") {
    volt::Circuit circuit;

    const auto module = circuit.add_module_definition(volt::ModuleDefinition{
        volt::ModuleName{"BuckConverter"},
    });

    CHECK_THROWS_AS(circuit.add_module_definition(volt::ModuleDefinition{
                        volt::ModuleName{"BuckConverter"},
                    }),
                    std::logic_error);

    [[maybe_unused]] const auto fb = circuit.add_template_net(
        module, volt::TemplateNetDefinition{volt::NetName{"FB"}, volt::NetKind::Signal});
    CHECK_THROWS_AS(
        circuit.add_template_net(
            module, volt::TemplateNetDefinition{volt::NetName{"FB"}, volt::NetKind::Signal}),
        std::logic_error);
}

TEST_CASE("Circuit stores ports that reference one internal template net") {
    volt::Circuit circuit;

    const auto module = circuit.add_module_definition(volt::ModuleDefinition{
        volt::ModuleName{"BuckConverter"},
    });
    const auto vin = circuit.add_template_net(
        module, volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power});
    const auto port = circuit.add_port_definition(
        module, volt::PortDefinition{volt::PortName{"VIN"}, vin, volt::PortRole::PowerInput});

    CHECK(port == volt::PortDefId{0});
    CHECK(circuit.port_definition(port).name() == volt::PortName{"VIN"});
    CHECK(circuit.port_definition(port).internal_net() == vin);
    CHECK(circuit.port_definition(port).role() == volt::PortRole::PowerInput);
    CHECK(circuit.port_definition(port).required());
    CHECK(circuit.module_definition(module).ports().size() == 1);
    CHECK(circuit.port_definition_count() == 1);
}

TEST_CASE("Circuit rejects ports with invalid internal nets or duplicate names") {
    volt::Circuit circuit;

    const auto first = circuit.add_module_definition(volt::ModuleDefinition{
        volt::ModuleName{"First"},
    });
    const auto second = circuit.add_module_definition(volt::ModuleDefinition{
        volt::ModuleName{"Second"},
    });
    const auto first_net = circuit.add_template_net(
        first, volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power});
    const auto second_net = circuit.add_template_net(
        second, volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power});

    [[maybe_unused]] const auto first_port = circuit.add_port_definition(
        first, volt::PortDefinition{volt::PortName{"VIN"}, first_net, volt::PortRole::PowerInput});
    CHECK_THROWS_AS(
        circuit.add_port_definition(first, volt::PortDefinition{volt::PortName{"VIN"}, first_net}),
        std::logic_error);
    CHECK_THROWS_AS(
        circuit.add_port_definition(first, volt::PortDefinition{volt::PortName{"BAD"}, second_net}),
        std::logic_error);
    CHECK_THROWS_AS(
        circuit.add_port_definition(
            first, volt::PortDefinition{volt::PortName{"MISSING"}, volt::TemplateNetDefId{99}}),
        std::out_of_range);
}

TEST_CASE("Root module instantiation creates concrete nets for template-local nets") {
    volt::Circuit circuit;

    const auto module = circuit.add_module_definition(volt::ModuleDefinition{
        volt::ModuleName{"BuckConverter"},
    });
    const auto vin = circuit.add_template_net(
        module, volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power});
    const auto fb = circuit.add_template_net(
        module, volt::TemplateNetDefinition{volt::NetName{"FB"}, volt::NetKind::Signal});

    const auto first = circuit.instantiate_root_module(module, volt::ModuleInstanceName{"BUCK_A"});
    const auto second = circuit.instantiate_root_module(module, volt::ModuleInstanceName{"BUCK_B"});

    CHECK(first == volt::ModuleInstanceId{0});
    CHECK(second == volt::ModuleInstanceId{1});
    CHECK(circuit.module_instance(first).definition() == module);
    CHECK(circuit.module_instance(first).name() == volt::ModuleInstanceName{"BUCK_A"});
    CHECK(circuit.module_instance_count() == 2);
    CHECK(circuit.net_count() == 4);

    const auto first_vin = circuit.concrete_net_for(first, vin);
    const auto first_fb = circuit.concrete_net_for(first, fb);
    const auto second_fb = circuit.concrete_net_for(second, fb);
    REQUIRE(first_vin.has_value());
    REQUIRE(first_fb.has_value());
    REQUIRE(second_fb.has_value());
    CHECK(first_fb.value() != second_fb.value());
    CHECK(circuit.net(first_vin.value()).name() == volt::NetName{"BUCK_A/VIN"});
    CHECK(circuit.net(first_fb.value()).name() == volt::NetName{"BUCK_A/FB"});
    CHECK(circuit.net(second_fb.value()).name() == volt::NetName{"BUCK_B/FB"});
}

TEST_CASE("Circuit records port bindings as explicit edges without merging nets") {
    volt::Circuit circuit;

    const auto module = circuit.add_module_definition(volt::ModuleDefinition{
        volt::ModuleName{"BuckConverter"},
    });
    const auto vin = circuit.add_template_net(
        module, volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power});
    const auto port = circuit.add_port_definition(
        module, volt::PortDefinition{volt::PortName{"VIN"}, vin, volt::PortRole::PowerInput});
    const auto instance =
        circuit.instantiate_root_module(module, volt::ModuleInstanceName{"BUCK_A"});
    const auto parent_net = circuit.add_net(volt::Net{volt::NetName{"VIN"}, volt::NetKind::Power});

    const auto binding = circuit.bind_port(instance, port, parent_net);

    REQUIRE(circuit.concrete_net_for(instance, vin).has_value());
    const auto internal_net = circuit.concrete_net_for(instance, vin).value();
    CHECK(binding == volt::PortBindingId{0});
    CHECK(circuit.port_binding(binding).instance() == instance);
    CHECK(circuit.port_binding(binding).port() == port);
    CHECK(circuit.port_binding(binding).internal_net() == internal_net);
    CHECK(circuit.port_binding(binding).parent_net() == parent_net);
    CHECK(internal_net != parent_net);
    CHECK(circuit.port_binding_count() == 1);
}

TEST_CASE("Circuit rejects duplicate port bindings for one module instance port") {
    volt::Circuit circuit;

    const auto module = circuit.add_module_definition(volt::ModuleDefinition{
        volt::ModuleName{"BuckConverter"},
    });
    const auto vin = circuit.add_template_net(
        module, volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power});
    const auto port = circuit.add_port_definition(
        module, volt::PortDefinition{volt::PortName{"VIN"}, vin, volt::PortRole::PowerInput});
    const auto instance =
        circuit.instantiate_root_module(module, volt::ModuleInstanceName{"BUCK_A"});
    const auto first_parent =
        circuit.add_net(volt::Net{volt::NetName{"VIN"}, volt::NetKind::Power});
    const auto second_parent =
        circuit.add_net(volt::Net{volt::NetName{"VIN_ALT"}, volt::NetKind::Power});

    [[maybe_unused]] const auto binding = circuit.bind_port(instance, port, first_parent);

    CHECK_THROWS_AS(circuit.bind_port(instance, port, second_parent), std::logic_error);
    CHECK(circuit.port_binding_count() == 1);
}

TEST_CASE("Root module instantiation preflights concrete net names before mutating") {
    volt::Circuit circuit;

    const auto module = circuit.add_module_definition(volt::ModuleDefinition{
        volt::ModuleName{"BuckConverter"},
    });
    [[maybe_unused]] const auto vin = circuit.add_template_net(
        module, volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power});
    [[maybe_unused]] const auto existing_net =
        circuit.add_net(volt::Net{volt::NetName{"BUCK_A/VIN"}, volt::NetKind::Power});

    CHECK_THROWS_AS(circuit.instantiate_root_module(module, volt::ModuleInstanceName{"BUCK_A"}),
                    std::logic_error);
    CHECK(circuit.module_instance_count() == 0);
    CHECK(circuit.net_count() == 1);
}

TEST_CASE("Circuit validation reports unbound required module ports") {
    volt::Circuit circuit;

    const auto module = circuit.add_module_definition(volt::ModuleDefinition{
        volt::ModuleName{"BuckConverter"},
    });
    const auto vin = circuit.add_template_net(
        module, volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power});
    const auto port = circuit.add_port_definition(
        module, volt::PortDefinition{volt::PortName{"VIN"}, vin, volt::PortRole::PowerInput});
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

    const auto module = circuit.add_module_definition(volt::ModuleDefinition{
        volt::ModuleName{"BuckConverter"},
    });
    const auto vin = circuit.add_template_net(
        module, volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power});
    const auto port = circuit.add_port_definition(
        module, volt::PortDefinition{volt::PortName{"VIN"}, vin, volt::PortRole::PowerInput});
    const auto instance =
        circuit.instantiate_root_module(module, volt::ModuleInstanceName{"BUCK_A"});
    const auto parent_net = circuit.add_net(volt::Net{volt::NetName{"VIN"}, volt::NetKind::Power});
    [[maybe_unused]] const auto binding = circuit.bind_port(instance, port, parent_net);

    const auto report = volt::validate_connectivity(circuit);

    for (const auto &diagnostic : report.diagnostics()) {
        CHECK(diagnostic.code() != volt::DiagnosticCode{"UNBOUND_REQUIRED_PORT"});
    }
}
