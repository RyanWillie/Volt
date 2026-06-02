#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

#include <volt/circuit/hierarchy_model.hpp>

namespace {

struct HierarchyFixture {
    volt::HierarchyModel model;
    volt::ModuleDefId module;
    volt::TemplateNetDefId input;
    volt::TemplateNetDefId output;
    volt::ModuleComponentId component;
};

HierarchyFixture make_hierarchy_fixture() {
    HierarchyFixture fixture{
        .module = volt::ModuleDefId{0},
        .input = volt::TemplateNetDefId{0},
        .output = volt::TemplateNetDefId{0},
        .component = volt::ModuleComponentId{0},
    };
    fixture.module =
        fixture.model.add_module_definition(volt::ModuleDefinition{volt::ModuleName{"Divider"}});
    fixture.input = fixture.model.add_template_net(
        fixture.module, volt::TemplateNetDefinition{volt::NetName{"IN"}, volt::NetKind::Signal});
    fixture.output = fixture.model.add_template_net(
        fixture.module, volt::TemplateNetDefinition{volt::NetName{"OUT"}, volt::NetKind::Signal});
    fixture.component = fixture.model.add_module_component(
        fixture.module,
        volt::ModuleComponentTemplate{volt::ComponentDefId{0}, volt::ReferenceDesignator{"R1"}});
    return fixture;
}

} // namespace

TEST_CASE("HierarchyModel stores module definitions and child entities in deterministic order") {
    auto fixture = make_hierarchy_fixture();
    const auto port = fixture.model.add_port_definition(
        fixture.module,
        volt::PortDefinition{volt::PortName{"IN"}, fixture.input, volt::PortRole::Input});

    CHECK(fixture.module == volt::ModuleDefId{0});
    CHECK(fixture.input == volt::TemplateNetDefId{0});
    CHECK(fixture.output == volt::TemplateNetDefId{1});
    CHECK(fixture.component == volt::ModuleComponentId{0});
    CHECK(port == volt::PortDefId{0});
    CHECK(fixture.model.module_definition(fixture.module).template_nets() ==
          std::vector{fixture.input, fixture.output});
    CHECK(fixture.model.module_definition(fixture.module).ports() == std::vector{port});
    CHECK(fixture.model.module_definition(fixture.module).components() ==
          std::vector{fixture.component});
}

TEST_CASE("HierarchyModel rejects duplicate names within their local scope") {
    auto fixture = make_hierarchy_fixture();

    CHECK_THROWS_AS(
        fixture.model.add_module_definition(volt::ModuleDefinition{volt::ModuleName{"Divider"}}),
        std::logic_error);
    CHECK_THROWS_AS(fixture.model.add_template_net(
                        fixture.module,
                        volt::TemplateNetDefinition{volt::NetName{"IN"}, volt::NetKind::Signal}),
                    std::logic_error);
    [[maybe_unused]] const auto port = fixture.model.add_port_definition(
        fixture.module,
        volt::PortDefinition{volt::PortName{"IN"}, fixture.input, volt::PortRole::Input});
    CHECK_THROWS_AS(fixture.model.add_port_definition(
                        fixture.module, volt::PortDefinition{volt::PortName{"IN"}, fixture.input,
                                                             volt::PortRole::Input}),
                    std::logic_error);
    CHECK_THROWS_AS(
        fixture.model.add_module_component(
            fixture.module, volt::ModuleComponentTemplate{volt::ComponentDefId{0},
                                                          volt::ReferenceDesignator{"R1"}}),
        std::logic_error);
}

TEST_CASE("HierarchyModel rejects child entities outside their owning module") {
    auto fixture = make_hierarchy_fixture();
    const auto other_module =
        fixture.model.add_module_definition(volt::ModuleDefinition{volt::ModuleName{"Other"}});
    const auto other_net = fixture.model.add_template_net(
        other_module, volt::TemplateNetDefinition{volt::NetName{"IN"}, volt::NetKind::Signal});
    const auto other_component = fixture.model.add_module_component(
        other_module,
        volt::ModuleComponentTemplate{volt::ComponentDefId{0}, volt::ReferenceDesignator{"R1"}});

    CHECK_THROWS_AS(fixture.model.add_port_definition(
                        fixture.module, volt::PortDefinition{volt::PortName{"BAD"}, other_net}),
                    std::logic_error);
    CHECK_THROWS_AS(fixture.model.connect_module_pin(fixture.module, other_net, fixture.component,
                                                     volt::PinDefId{0}),
                    std::logic_error);
    CHECK_THROWS_AS(fixture.model.connect_module_pin(fixture.module, fixture.input, other_component,
                                                     volt::PinDefId{0}),
                    std::logic_error);
}

TEST_CASE("HierarchyModel stores module pin template connections") {
    auto fixture = make_hierarchy_fixture();

    CHECK(fixture.model.connect_module_pin(fixture.module, fixture.input, fixture.component,
                                           volt::PinDefId{0}));
    CHECK_FALSE(fixture.model.connect_module_pin(fixture.module, fixture.input, fixture.component,
                                                 volt::PinDefId{0}));
    CHECK_THROWS_AS(fixture.model.connect_module_pin(fixture.module, fixture.output,
                                                     fixture.component, volt::PinDefId{0}),
                    std::logic_error);

    CHECK(fixture.model.template_net_for(fixture.module, fixture.component, volt::PinDefId{0}) ==
          fixture.input);
    REQUIRE(fixture.model.module_pin_connections(fixture.module).size() == 1);
    CHECK(fixture.model.module_pin_connection_count() == 1);
}

TEST_CASE("HierarchyModel records module instances and origin metadata") {
    auto fixture = make_hierarchy_fixture();

    const auto instance =
        fixture.model.instantiate_root_module(fixture.module, volt::ModuleInstanceName{"DIV_A"});
    fixture.model.record_module_net_origin(instance, fixture.input, volt::NetId{3});
    fixture.model.record_module_component_origin(instance, fixture.component, volt::ComponentId{2});

    CHECK(instance == volt::ModuleInstanceId{0});
    CHECK(fixture.model.module_instance(instance).name() == volt::ModuleInstanceName{"DIV_A"});
    CHECK(fixture.model.concrete_net_for(instance, fixture.input) == volt::NetId{3});
    CHECK(fixture.model.concrete_component_for(instance, fixture.component) ==
          volt::ComponentId{2});
    CHECK(fixture.model.module_net_origins(instance) ==
          std::vector<std::pair<volt::TemplateNetDefId, volt::NetId>>{
              {fixture.input, volt::NetId{3}}});
    CHECK(fixture.model.module_component_origins(instance) ==
          std::vector<std::pair<volt::ModuleComponentId, volt::ComponentId>>{
              {fixture.component, volt::ComponentId{2}}});
    CHECK(fixture.model.is_module_origin_net(volt::NetId{3}));
    CHECK(fixture.model.is_module_origin_component(volt::ComponentId{2}));
}

TEST_CASE("HierarchyModel rejects duplicate module instances and origins") {
    auto fixture = make_hierarchy_fixture();
    const auto instance =
        fixture.model.instantiate_root_module(fixture.module, volt::ModuleInstanceName{"DIV_A"});
    fixture.model.record_module_net_origin(instance, fixture.input, volt::NetId{3});
    fixture.model.record_module_component_origin(instance, fixture.component, volt::ComponentId{2});

    CHECK_THROWS_AS(
        fixture.model.instantiate_root_module(fixture.module, volt::ModuleInstanceName{"DIV_A"}),
        std::logic_error);
    CHECK_THROWS_AS(
        fixture.model.record_module_net_origin(instance, fixture.output, volt::NetId{3}),
        std::logic_error);
    CHECK_THROWS_AS(fixture.model.record_module_component_origin(instance, fixture.component,
                                                                 volt::ComponentId{4}),
                    std::logic_error);
}

TEST_CASE("HierarchyModel restore preflights origins before mutating") {
    auto fixture = make_hierarchy_fixture();

    CHECK_THROWS_AS(
        fixture.model.restore_root_module_instance(
            fixture.module, volt::ModuleInstanceName{"DIV_A"},
            {{fixture.input, volt::NetId{3}}, {volt::TemplateNetDefId{99}, volt::NetId{4}}},
            {{fixture.component, volt::ComponentId{2}}}),
        std::out_of_range);

    CHECK(fixture.model.module_instance_count() == 0);
    CHECK_FALSE(fixture.model.is_module_origin_net(volt::NetId{3}));
    CHECK_FALSE(fixture.model.is_module_origin_component(volt::ComponentId{2}));
}

TEST_CASE("HierarchyModel stores explicit port bindings in module port order") {
    auto fixture = make_hierarchy_fixture();
    const auto input_port = fixture.model.add_port_definition(
        fixture.module,
        volt::PortDefinition{volt::PortName{"IN"}, fixture.input, volt::PortRole::Input});
    const auto output_port = fixture.model.add_port_definition(
        fixture.module,
        volt::PortDefinition{volt::PortName{"OUT"}, fixture.output, volt::PortRole::Output});
    const auto instance =
        fixture.model.instantiate_root_module(fixture.module, volt::ModuleInstanceName{"DIV_A"});
    fixture.model.record_module_net_origin(instance, fixture.input, volt::NetId{3});
    fixture.model.record_module_net_origin(instance, fixture.output, volt::NetId{4});

    const auto output_binding =
        fixture.model.bind_port(instance, output_port, volt::NetId{4}, volt::NetId{20});
    const auto input_binding =
        fixture.model.bind_port(instance, input_port, volt::NetId{3}, volt::NetId{10});

    CHECK(output_binding == volt::PortBindingId{0});
    CHECK(input_binding == volt::PortBindingId{1});
    CHECK(fixture.model.port_bindings_for(instance) == std::vector{input_binding, output_binding});
    CHECK(fixture.model.port_binding_for(instance, input_port) == input_binding);
    CHECK_THROWS_AS(fixture.model.bind_port(instance, input_port, volt::NetId{3}, volt::NetId{11}),
                    std::logic_error);
    CHECK_THROWS_AS(fixture.model.bind_port(instance, output_port, volt::NetId{4}, volt::NetId{4}),
                    std::logic_error);
}

TEST_CASE("HierarchyModel rejects port bindings to module-origin parent nets") {
    auto fixture = make_hierarchy_fixture();
    const auto input_port = fixture.model.add_port_definition(
        fixture.module,
        volt::PortDefinition{volt::PortName{"IN"}, fixture.input, volt::PortRole::Input});
    const auto output_port = fixture.model.add_port_definition(
        fixture.module,
        volt::PortDefinition{volt::PortName{"OUT"}, fixture.output, volt::PortRole::Output});
    const auto instance =
        fixture.model.instantiate_root_module(fixture.module, volt::ModuleInstanceName{"DIV_A"});
    fixture.model.record_module_net_origin(instance, fixture.input, volt::NetId{3});
    fixture.model.record_module_net_origin(instance, fixture.output, volt::NetId{4});

    CHECK_THROWS_AS(fixture.model.bind_port(instance, input_port, volt::NetId{3}, volt::NetId{4}),
                    std::logic_error);
    [[maybe_unused]] const auto output_binding =
        fixture.model.bind_port(instance, output_port, volt::NetId{4}, volt::NetId{20});
    CHECK(fixture.model.port_binding_count() == 1);
}
