#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <stdexcept>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/hierarchy/hierarchy_model.hpp>

namespace {

// These concepts lock the transitional facade boundary until its scheduled deletion in #266.
template <typename Model>
concept CanAddModuleComponent =
    requires(Model model, volt::ModuleDefId module, volt::ModuleComponentTemplate component) {
        model.hierarchy().add_module_component(module, component);
    };

template <typename Model>
concept CanConnectModulePin =
    requires(Model model, volt::ModuleDefId module, volt::TemplateNetDefId net,
             volt::ModuleComponentId component, volt::PinDefId pin) {
        model.hierarchy().connect_module_pin(module, net, component, pin);
    };

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
    volt::Circuit circuit;
    volt::ModuleDefId module;
    volt::TemplateNetDefId input;
    volt::TemplateNetDefId output;
    volt::PortDefId port;
};

HierarchyFixture make_hierarchy_fixture() {
    auto circuit = volt::Circuit{};
    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"Divider"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"IN"}, volt::NetKind::Signal},
                          volt::TemplateNetDefinition{volt::NetName{"OUT"}, volt::NetKind::Signal}},
        .ports = {volt::ModulePortSpec{volt::PortName{"IN"}, volt::NetName{"IN"},
                                       volt::PortRole::Input}},
    });
    const auto &stored = circuit.get(module);
    const auto input = stored.template_nets()[0];
    const auto output = stored.template_nets()[1];
    const auto port = stored.ports().front();
    return HierarchyFixture{std::move(circuit), module, input, output, port};
}

} // namespace

TEST_CASE("Circuit complete modules store local child entities deterministically") {
    auto fixture = make_hierarchy_fixture();
    CHECK(fixture.module == volt::ModuleDefId{0});
    CHECK(fixture.input == volt::TemplateNetDefId{0});
    CHECK(fixture.output == volt::TemplateNetDefId{1});
    CHECK(fixture.port == volt::PortDefId{0});
    CHECK(fixture.circuit.get(fixture.module).template_nets() ==
          std::vector{fixture.input, fixture.output});
    CHECK(fixture.circuit.get(fixture.module).ports() == std::vector{fixture.port});
}

TEST_CASE("Circuit complete modules reject duplicate names within local hierarchy scopes") {
    auto fixture = make_hierarchy_fixture();

    CHECK_THROWS_AS(
        fixture.circuit.define_module(volt::ModuleSpec{.name = volt::ModuleName{"Divider"}}),
        std::logic_error);
    CHECK_THROWS_AS(fixture.circuit.define_module(volt::ModuleSpec{
                        .name = volt::ModuleName{"DuplicateNets"},
                        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"IN"},
                                                                      volt::NetKind::Signal},
                                          volt::TemplateNetDefinition{volt::NetName{"IN"},
                                                                      volt::NetKind::Signal}},
                    }),
                    std::logic_error);
    CHECK_THROWS_AS(fixture.circuit.define_module(volt::ModuleSpec{
                        .name = volt::ModuleName{"DuplicatePorts"},
                        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"IN"},
                                                                      volt::NetKind::Signal}},
                        .ports = {volt::ModulePortSpec{volt::PortName{"IN"}, volt::NetName{"IN"}},
                                  volt::ModulePortSpec{volt::PortName{"IN"}, volt::NetName{"IN"}}},
                    }),
                    std::logic_error);
}

TEST_CASE("Legacy hierarchy facade rejects raw child IDs outside their owning module") {
    auto circuit = volt::Circuit{};
    const auto first = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"First"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"IN"}, volt::NetKind::Signal}},
    });
    const auto second = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"Second"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"IN"}, volt::NetKind::Signal}},
    });
    const auto second_net = circuit.get(second).template_nets().front();

    // Cross-module raw ID insertion remains only on the transitional facade until #266.
    CHECK_THROWS_AS(circuit.hierarchy().add_port_definition(
                        first, volt::PortDefinition{volt::PortName{"BAD"}, second_net}),
                    std::logic_error);
}

TEST_CASE("Circuit records module instances with deterministic aggregate origins") {
    auto fixture = make_hierarchy_fixture();

    const auto instance =
        fixture.circuit.instantiate_root_module(fixture.module, volt::ModuleInstanceName{"DIV_A"});
    CHECK(instance == volt::ModuleInstanceId{0});
    CHECK(fixture.circuit.get(instance).name() == volt::ModuleInstanceName{"DIV_A"});
    CHECK(fixture.circuit.get(instance).definition() == fixture.module);
    CHECK(fixture.circuit.all<volt::ModuleInstanceId>().size() == 1);
}

TEST_CASE("Circuit rejects duplicate root module instance names") {
    auto fixture = make_hierarchy_fixture();
    [[maybe_unused]] const auto instance =
        fixture.circuit.instantiate_root_module(fixture.module, volt::ModuleInstanceName{"DIV_A"});

    CHECK_THROWS_AS(
        fixture.circuit.instantiate_root_module(fixture.module, volt::ModuleInstanceName{"DIV_A"}),
        std::logic_error);
    CHECK(fixture.circuit.all<volt::ModuleInstanceId>().size() == 1);
}
