#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>

namespace {

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

TEST_CASE("Circuit records module instances with deterministic aggregate origins") {
    auto fixture = make_hierarchy_fixture();

    const auto instance = fixture.circuit.instantiate_module(
        fixture.module, volt::ModuleInstanceSpec{.name = volt::ModuleInstanceName{"DIV_A"}});
    CHECK(instance == volt::ModuleInstanceId{0});
    CHECK(fixture.circuit.get(instance).name() == volt::ModuleInstanceName{"DIV_A"});
    CHECK(fixture.circuit.get(instance).definition() == fixture.module);
    CHECK(fixture.circuit.all<volt::ModuleInstanceId>().size() == 1);
}

TEST_CASE("Circuit rejects duplicate root module instance names") {
    auto fixture = make_hierarchy_fixture();
    [[maybe_unused]] const auto instance = fixture.circuit.instantiate_module(
        fixture.module, volt::ModuleInstanceSpec{.name = volt::ModuleInstanceName{"DIV_A"}});

    CHECK_THROWS_AS(
        fixture.circuit.instantiate_module(
            fixture.module, volt::ModuleInstanceSpec{.name = volt::ModuleInstanceName{"DIV_A"}}),
        std::logic_error);
    CHECK(fixture.circuit.all<volt::ModuleInstanceId>().size() == 1);
}
