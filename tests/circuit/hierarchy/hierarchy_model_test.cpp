#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <stdexcept>
#include <utility>
#include <vector>

#include <volt/circuit/hierarchy/hierarchy_model.hpp>

namespace {

template <typename Model>
concept CanAddModuleComponent =
    requires(Model model, volt::ModuleDefId module, volt::ModuleComponentTemplate component) {
        model.add_module_component(module, component);
    };

template <typename Model>
concept CanConnectModulePin =
    requires(Model model, volt::ModuleDefId module, volt::TemplateNetDefId net,
             volt::ModuleComponentId component,
             volt::PinDefId pin) { model.connect_module_pin(module, net, component, pin); };

template <typename Model>
concept CanRestoreRootModuleInstance = requires(
    Model model, volt::ModuleDefId definition, volt::ModuleInstanceName name,
    std::vector<std::pair<volt::TemplateNetDefId, volt::NetId>> net_origins,
    std::vector<std::pair<volt::ModuleComponentId, volt::ComponentId>> component_origins) {
    model.restore_root_module_instance(definition, std::move(name), net_origins, component_origins);
};

template <typename Model>
concept CanRecordModuleNetOrigin =
    requires(Model model, volt::ModuleInstanceId instance, volt::TemplateNetDefId template_net,
             volt::NetId concrete_net) {
        model.record_module_net_origin(instance, template_net, concrete_net);
    };

template <typename Model>
concept CanRecordModuleComponentOrigin =
    requires(Model model, volt::ModuleInstanceId instance, volt::ModuleComponentId component,
             volt::ComponentId concrete_component) {
        model.record_module_component_origin(instance, component, concrete_component);
    };

template <typename Model>
concept CanBindPort = requires(Model model, volt::ModuleInstanceId instance, volt::PortDefId port,
                               volt::NetId internal_net, volt::NetId parent_net) {
    model.bind_port(instance, port, internal_net, parent_net);
};

static_assert(!CanAddModuleComponent<volt::HierarchyModel>);
static_assert(!CanConnectModulePin<volt::HierarchyModel>);
static_assert(!CanRestoreRootModuleInstance<volt::HierarchyModel>);
static_assert(!CanRecordModuleNetOrigin<volt::HierarchyModel>);
static_assert(!CanRecordModuleComponentOrigin<volt::HierarchyModel>);
static_assert(!CanBindPort<volt::HierarchyModel>);

struct HierarchyFixture {
    volt::HierarchyModel model;
    volt::ModuleDefId module;
    volt::TemplateNetDefId input;
    volt::TemplateNetDefId output;
};

HierarchyFixture make_hierarchy_fixture() {
    auto model = volt::HierarchyModel{};
    const auto module =
        model.add_module_definition(volt::ModuleDefinition{volt::ModuleName{"Divider"}});
    const auto input = model.add_template_net(
        module, volt::TemplateNetDefinition{volt::NetName{"IN"}, volt::NetKind::Signal});
    const auto output = model.add_template_net(
        module, volt::TemplateNetDefinition{volt::NetName{"OUT"}, volt::NetKind::Signal});
    return HierarchyFixture{std::move(model), module, input, output};
}

} // namespace

TEST_CASE("HierarchyModel stores module definitions and local child entities deterministically") {
    auto fixture = make_hierarchy_fixture();
    const auto port = fixture.model.add_port_definition(
        fixture.module,
        volt::PortDefinition{volt::PortName{"IN"}, fixture.input, volt::PortRole::Input});

    CHECK(fixture.module == volt::ModuleDefId{0});
    CHECK(fixture.input == volt::TemplateNetDefId{0});
    CHECK(fixture.output == volt::TemplateNetDefId{1});
    CHECK(port == volt::PortDefId{0});
    CHECK(fixture.model.module_definition(fixture.module).template_nets() ==
          std::vector{fixture.input, fixture.output});
    CHECK(fixture.model.module_definition(fixture.module).ports() == std::vector{port});
}

TEST_CASE("HierarchyModel rejects duplicate names within local hierarchy scopes") {
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
}

TEST_CASE("HierarchyModel rejects local child entities outside their owning module") {
    auto fixture = make_hierarchy_fixture();
    const auto other_module =
        fixture.model.add_module_definition(volt::ModuleDefinition{volt::ModuleName{"Other"}});
    const auto other_net = fixture.model.add_template_net(
        other_module, volt::TemplateNetDefinition{volt::NetName{"IN"}, volt::NetKind::Signal});

    CHECK_THROWS_AS(fixture.model.add_port_definition(
                        fixture.module, volt::PortDefinition{volt::PortName{"BAD"}, other_net}),
                    std::logic_error);
}

TEST_CASE("HierarchyModel records module instances without concrete origin metadata") {
    auto fixture = make_hierarchy_fixture();

    const auto instance =
        fixture.model.instantiate_root_module(fixture.module, volt::ModuleInstanceName{"DIV_A"});

    CHECK(instance == volt::ModuleInstanceId{0});
    CHECK(fixture.model.module_instance(instance).name() == volt::ModuleInstanceName{"DIV_A"});
    CHECK(fixture.model.module_instance(instance).definition() == fixture.module);
    CHECK(fixture.model.module_instance_count() == 1);
}

TEST_CASE("HierarchyModel rejects duplicate local module instance names") {
    auto fixture = make_hierarchy_fixture();
    [[maybe_unused]] const auto instance =
        fixture.model.instantiate_root_module(fixture.module, volt::ModuleInstanceName{"DIV_A"});

    CHECK_THROWS_AS(
        fixture.model.instantiate_root_module(fixture.module, volt::ModuleInstanceName{"DIV_A"}),
        std::logic_error);
    CHECK(fixture.model.module_instance_count() == 1);
}
