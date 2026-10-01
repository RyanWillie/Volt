#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <variant>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/electrical/compiled_electrical_model.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/io/parts/part_definition_writer.hpp>
#include <volt/io/project_bundle.hpp>

#include "io/support/project_bundle_v2_board_test_support.hpp"
#include "support/electrical_compilation_fixture.hpp"

namespace {

using volt::test::electrical_compilation::Fixture;
using volt::test::electrical_compilation::PartVariant;

[[nodiscard]] volt::ComponentId add_part(Fixture &fixture, const PartVariant &variant,
                                         std::string reference, std::optional<volt::NetId> a,
                                         std::optional<volt::NetId> b) {
    const auto occurrence = fixture.circuit->instantiate_component(
        variant.component,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{reference}});
    fixture.circuit->update(
        occurrence,
        volt::SelectLibraryPart{fixture.library, fixture.library.require(variant.part)});
    if (a) {
        fixture.circuit->connect(
            *a, volt::queries::pin_by_number(*fixture.circuit, occurrence, "1").value());
    }
    if (b) {
        fixture.circuit->connect(
            *b, volt::queries::pin_by_number(*fixture.circuit, occurrence, "2").value());
    }
    return occurrence;
}

[[nodiscard]] const volt::ElectricalNode *net_node(const volt::CompiledElectricalModel &model,
                                                   volt::NetId net) {
    const auto found = std::ranges::find_if(model.nodes(), [&](const volt::ElectricalNode &node) {
        const auto *origin = std::get_if<volt::ElectricalNetOrigin>(&node.origin);
        return origin != nullptr && std::ranges::find(origin->nets, net) != origin->nets.end();
    });
    return found == model.nodes().end() ? nullptr : &*found;
}

[[nodiscard]] const volt::ElectricalBranch *
element_branch(const volt::CompiledElectricalModel &model, volt::ComponentId occurrence,
               const volt::ModelElementKey &element) {
    const auto found =
        std::ranges::find_if(model.branches(), [&](const volt::ElectricalBranch &branch) {
            const auto *origin = std::get_if<volt::ElectricalElementOrigin>(&branch.origin);
            return origin != nullptr && origin->occurrence == occurrence &&
                   origin->element == element;
        });
    return found == model.branches().end() ? nullptr : &*found;
}

[[nodiscard]] bool has_incidence(const volt::ElectricalNode &node, volt::ElectricalBranchId branch,
                                 int sign) {
    return std::ranges::any_of(node.incidence, [&](const volt::ElectricalIncidence &incidence) {
        return incidence.branch == branch && incidence.sign == sign;
    });
}

[[nodiscard]] const volt::Diagnostic *diagnostic(const volt::ElectricalCompileReport &report,
                                                 std::string_view code) {
    const auto found = std::ranges::find_if(report.diagnostics(), [&](const auto &value) {
        return value.code() == volt::DiagnosticCode{std::string{code}};
    });
    return found == report.diagnostics().end() ? nullptr : &*found;
}

[[nodiscard]] volt::ModuleInstanceId add_alias(Fixture &fixture, std::string name,
                                               volt::NetId first, volt::NetId second) {
    const auto module = fixture.circuit->define_module(volt::ModuleSpec{
        .name = volt::ModuleName{name},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"joined"},
                                                      volt::NetKind::Signal}},
        .ports = {volt::ModulePortSpec{volt::PortName{"first"}, volt::NetName{"joined"}},
                  volt::ModulePortSpec{volt::PortName{"second"}, volt::NetName{"joined"}}},
    });
    const auto instance = fixture.circuit->instantiate_module(
        module, volt::ModuleInstanceSpec{.name = volt::ModuleInstanceName{name + "-instance"}});
    const auto &ports = fixture.circuit->get(module).ports();
    static_cast<void>(fixture.circuit->bind_port(instance, ports[0], first));
    static_cast<void>(fixture.circuit->bind_port(instance, ports[1], second));
    return instance;
}

} // namespace

TEST_CASE("native electrical compilation expands the shared divider with oriented provenance") {
    using namespace volt::test::electrical_compilation;

    const auto fixture = make_fixture();
    const auto owner = input(fixture);
    const auto request = divider_request(fixture, owner);
    const auto report = volt::compile_electrical(request);

    REQUIRE(report.complete());
    REQUIRE(report.model() != nullptr);
    const auto &model = *report.model();
    CHECK(model.nodes().size() == 3U);
    CHECK(model.branches().size() == 3U);
    CHECK(model.storage().empty());
    REQUIRE(model.probes().size() == 1U);

    const auto *supply = net_node(model, fixture.supply);
    const auto *midpoint = net_node(model, fixture.midpoint);
    const auto *reference = net_node(model, fixture.reference);
    REQUIRE(supply != nullptr);
    REQUIRE(midpoint != nullptr);
    REQUIRE(reference != nullptr);
    CHECK(model.reference() == reference->id);

    const auto *upper =
        element_branch(model, fixture.upper_resistor, volt::ModelElementKey{"body"});
    const auto *lower =
        element_branch(model, fixture.lower_resistor, volt::ModelElementKey{"body"});
    REQUIRE(upper != nullptr);
    REQUIRE(lower != nullptr);
    CHECK(upper->from == supply->id);
    CHECK(upper->to == midpoint->id);
    CHECK(lower->from == midpoint->id);
    CHECK(lower->to == reference->id);
    REQUIRE(std::holds_alternative<volt::ResistanceElement>(upper->law));
    CHECK(std::get<volt::ResistanceElement>(upper->law).parameter().nominal() ==
          volt::Quantity{volt::UnitDimension::Resistance, 1000.0});
    CHECK(has_incidence(*supply, upper->id, 1));
    CHECK(has_incidence(*midpoint, upper->id, -1));
    CHECK(has_incidence(*midpoint, lower->id, 1));
    CHECK(has_incidence(*reference, lower->id, -1));

    const auto again = volt::compile_electrical(request);
    REQUIRE(again.model() != nullptr);
    CHECK(again.model()->identity() == model.identity());
    CHECK(again.model()->request_identity() == model.request_identity());
}

TEST_CASE("request values perturb identities and current probes retain branch orientation") {
    using namespace volt::test::electrical_compilation;

    const auto fixture = make_fixture();
    const auto owner = input(fixture);
    const auto request = volt::DcRequest{
        volt::ElectricalRequestKey{"probe-orientation"},
        owner,
        owner.net(fixture.reference),
        {volt::DcVoltageSource{
            volt::ElectricalSourceKey{"drive"},
            volt::ElectricalNetPair{owner.net(fixture.supply), owner.net(fixture.reference)},
            volt::Quantity{volt::UnitDimension::Voltage, 5.0}}},
        {volt::DcSourceCurrentProbe{volt::ElectricalProbeKey{"source-current"},
                                    volt::ElectricalSourceKey{"drive"}},
         volt::DcModelElementCurrentProbe{volt::ElectricalProbeKey{"element-current"},
                                          owner.occurrence(fixture.upper_resistor),
                                          volt::ModelElementKey{"body"}}}};
    const auto report = volt::compile_electrical(request);
    REQUIRE(report.model() != nullptr);
    const auto &model = *report.model();
    const auto *element =
        element_branch(model, fixture.upper_resistor, volt::ModelElementKey{"body"});
    REQUIRE(element != nullptr);
    const auto source = std::ranges::find_if(model.branches(), [](const auto &branch) {
        const auto *origin = std::get_if<volt::ElectricalSourceKey>(&branch.origin);
        return origin != nullptr && *origin == volt::ElectricalSourceKey{"drive"};
    });
    REQUIRE(source != model.branches().end());
    REQUIRE(model.probes().size() == 2U);
    const auto element_probe = std::ranges::find_if(model.probes(), [](const auto &probe) {
        return probe.key == volt::ElectricalProbeKey{"element-current"};
    });
    const auto source_probe = std::ranges::find_if(model.probes(), [](const auto &probe) {
        return probe.key == volt::ElectricalProbeKey{"source-current"};
    });
    REQUIRE(element_probe != model.probes().end());
    REQUIRE(source_probe != model.probes().end());
    CHECK(std::get<volt::ElectricalCurrentObservation>(element_probe->target).branch ==
          element->id);
    CHECK(std::get<volt::ElectricalCurrentObservation>(source_probe->target).branch == source->id);

    const auto changed_request = volt::DcRequest{
        request.key(),
        owner,
        owner.net(fixture.reference),
        {volt::DcVoltageSource{
            volt::ElectricalSourceKey{"drive"},
            volt::ElectricalNetPair{owner.net(fixture.supply), owner.net(fixture.reference)},
            volt::Quantity{volt::UnitDimension::Voltage, 6.0}}},
        request.probes()};
    const auto changed = volt::compile_electrical(changed_request);
    REQUIRE(changed.model() != nullptr);
    CHECK(changed.model()->request_identity() != model.request_identity());
    CHECK(changed.model()->identity() != model.identity());
}

TEST_CASE("repeated composite Parts retain private nodes and C and L storage") {
    using namespace volt::test::electrical_compilation;

    auto fixture = make_fixture();
    const auto first =
        add_part(fixture, fixture.composite, "C1", fixture.supply, fixture.reference);
    const auto second =
        add_part(fixture, fixture.composite, "C2", fixture.supply, fixture.reference);
    const auto owner = input(fixture);
    const auto report = volt::compile_electrical(divider_request(fixture, owner));

    REQUIRE(report.model() != nullptr);
    const auto &model = *report.model();
    CHECK(model.branches().size() == 9U);
    CHECK(model.storage().size() == 4U);
    for (const auto occurrence : {first, second}) {
        const auto internal_count =
            std::ranges::count_if(model.nodes(), [&](const volt::ElectricalNode &node) {
                const auto *origin = std::get_if<volt::ElectricalInternalNodeOrigin>(&node.origin);
                return origin != nullptr && origin->occurrence == occurrence;
            });
        CHECK(internal_count == 2);
        CHECK(element_branch(model, occurrence, volt::ModelElementKey{"esr"}) != nullptr);
        CHECK(element_branch(model, occurrence, volt::ModelElementKey{"esl"}) != nullptr);
        CHECK(element_branch(model, occurrence, volt::ModelElementKey{"storage"}) != nullptr);
    }
}

TEST_CASE("zero resistance and ideal storage laws survive coincident endpoints") {
    using namespace volt::test::electrical_compilation;

    auto fixture = make_fixture();
    const auto zero =
        add_part(fixture, fixture.zero_resistor, "R0", fixture.supply, fixture.supply);
    const auto capacitor =
        add_part(fixture, fixture.capacitor, "C1", fixture.midpoint, fixture.midpoint);
    const auto inductor =
        add_part(fixture, fixture.inductor, "L1", fixture.reference, fixture.reference);
    const auto owner = input(fixture);
    const auto report = volt::compile_electrical(divider_request(fixture, owner));

    REQUIRE(report.model() != nullptr);
    const auto &model = *report.model();
    const auto *zero_branch = element_branch(model, zero, volt::ModelElementKey{"body"});
    const auto *capacitor_branch =
        element_branch(model, capacitor, volt::ModelElementKey{"storage"});
    const auto *inductor_branch = element_branch(model, inductor, volt::ModelElementKey{"storage"});
    REQUIRE(zero_branch != nullptr);
    REQUIRE(capacitor_branch != nullptr);
    REQUIRE(inductor_branch != nullptr);
    CHECK(zero_branch->from == zero_branch->to);
    CHECK(capacitor_branch->from == capacitor_branch->to);
    CHECK(inductor_branch->from == inductor_branch->to);
    CHECK(std::get<volt::ResistanceElement>(zero_branch->law).parameter().nominal().value() == 0.0);
    CHECK(model.storage().size() == 2U);
    CHECK(std::ranges::count_if(model.storage(), [](const volt::ElectricalStorage &storage) {
              return std::holds_alternative<volt::ElectricalCapacitorStorage>(storage);
          }) == 1);
    CHECK(std::ranges::count_if(model.storage(), [](const volt::ElectricalStorage &storage) {
              return std::holds_alternative<volt::ElectricalInductorStorage>(storage);
          }) == 1);
    const auto *shorted =
        diagnostic(report, volt::analysis_diagnostic_codes::ElectricalShortedBranch);
    REQUIRE(shorted != nullptr);
    CHECK(shorted->message().find("model element 'body' on component:" +
                                  std::to_string(zero.index())) != std::string::npos);
    CHECK(shorted->entities().size() == 2U);
}

TEST_CASE("open model terminals honor required optional and must-not-connect PinSpecs") {
    using namespace volt::test::electrical_compilation;

    SECTION("required remains blocking even when intentionally marked no-connect") {
        auto fixture = make_fixture();
        const auto occurrence =
            add_part(fixture, fixture.resistor, "R3", fixture.supply, std::nullopt);
        fixture.circuit->mark_no_connect(
            volt::queries::pin_by_number(*fixture.circuit, occurrence, "2").value());
        const auto owner = input(fixture);
        const auto report = volt::compile_electrical(divider_request(fixture, owner));
        CHECK_FALSE(report.complete());
        const auto *failure =
            diagnostic(report, volt::analysis_diagnostic_codes::ElectricalRequiredPinUnconnected);
        REQUIRE(failure != nullptr);
        CHECK(failure->entities().size() == 3U);
    }

    SECTION("optional compiles to a distinct open-pin node") {
        auto fixture = make_fixture();
        const auto occurrence =
            add_part(fixture, fixture.optional_resistor, "R3", fixture.supply, std::nullopt);
        const auto owner = input(fixture);
        const auto report = volt::compile_electrical(divider_request(fixture, owner));
        REQUIRE(report.model() != nullptr);
        CHECK(std::ranges::any_of(report.model()->nodes(), [&](const volt::ElectricalNode &node) {
            const auto *origin = std::get_if<volt::ElectricalOpenPinOrigin>(&node.origin);
            return origin != nullptr && origin->occurrence == occurrence;
        }));
    }

    SECTION("must-not-connect compiles to a distinct open-pin node") {
        auto fixture = make_fixture();
        const auto occurrence = add_part(fixture, fixture.must_not_connect_resistor, "R3",
                                         fixture.supply, std::nullopt);
        const auto owner = input(fixture);
        const auto report = volt::compile_electrical(divider_request(fixture, owner));
        REQUIRE(report.model() != nullptr);
        CHECK(std::ranges::any_of(report.model()->nodes(), [&](const volt::ElectricalNode &node) {
            const auto *origin = std::get_if<volt::ElectricalOpenPinOrigin>(&node.origin);
            return origin != nullptr && origin->occurrence == occurrence;
        }));
    }
}

TEST_CASE("missing required model coverage never exposes a partial compiled model") {
    using namespace volt::test::electrical_compilation;

    auto fixture = make_fixture();
    static_cast<void>(add_part(fixture, fixture.absent, "U1", fixture.supply, fixture.reference));
    const auto owner = input(fixture);
    const auto report = volt::compile_electrical(divider_request(fixture, owner));

    CHECK_FALSE(report.complete());
    CHECK(report.model() == nullptr);
    const auto *failure =
        diagnostic(report, volt::analysis_diagnostic_codes::DcOccurrenceModelAbsent);
    REQUIRE(failure != nullptr);
    CHECK_FALSE(failure->entities().empty());
}

TEST_CASE("resolved hierarchy continuity diagnoses a nonzero voltage source short") {
    using namespace volt::test::electrical_compilation;

    auto fixture = make_fixture();
    static_cast<void>(add_alias(fixture, "source-short", fixture.supply, fixture.reference));
    const auto owner = input(fixture);
    const auto report = volt::compile_electrical(divider_request(fixture, owner));

    CHECK_FALSE(report.complete());
    CHECK(report.model() == nullptr);
    const auto *failure =
        diagnostic(report, volt::analysis_diagnostic_codes::ElectricalContradictoryVoltageSource);
    REQUIRE(failure != nullptr);
    CHECK(failure->message().find("request source 'supply-5v'") != std::string::npos);
    CHECK(failure->entities().size() >= 2U);
}

TEST_CASE("hierarchy-resolved unequal sources report both origins and joined nets") {
    using namespace volt::test::electrical_compilation;

    auto fixture = make_fixture();
    const auto other_supply =
        fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"other-supply"}});
    const auto other_reference =
        fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"other-reference"}});
    static_cast<void>(add_alias(fixture, "supply-alias", fixture.supply, other_supply));
    static_cast<void>(add_alias(fixture, "reference-alias", fixture.reference, other_reference));
    const auto owner = input(fixture);
    const auto request = volt::DcRequest{
        volt::ElectricalRequestKey{"unequal-resolved-sources"},
        owner,
        owner.net(fixture.reference),
        {volt::DcVoltageSource{
             volt::ElectricalSourceKey{"first"},
             volt::ElectricalNetPair{owner.net(fixture.supply), owner.net(fixture.reference)},
             volt::Quantity{volt::UnitDimension::Voltage, 5.0}},
         volt::DcVoltageSource{
             volt::ElectricalSourceKey{"second"},
             volt::ElectricalNetPair{owner.net(other_supply), owner.net(other_reference)},
             volt::Quantity{volt::UnitDimension::Voltage, 3.0}}}};
    const auto report = volt::compile_electrical(request);

    CHECK_FALSE(report.complete());
    const auto *failure =
        diagnostic(report, volt::analysis_diagnostic_codes::ElectricalContradictoryVoltageSource);
    REQUIRE(failure != nullptr);
    CHECK(failure->message().find("request source 'first'") != std::string::npos);
    CHECK(failure->message().find("request source 'second'") != std::string::npos);
    CHECK(failure->entities().size() >= 4U);
}

TEST_CASE("required module ports block compilation while optional ports retain distinct nets") {
    using namespace volt::test::electrical_compilation;

    SECTION("required") {
        auto fixture = make_fixture();
        const auto module = fixture.circuit->define_module(volt::ModuleSpec{
            .name = volt::ModuleName{"required-port"},
            .template_nets = {volt::TemplateNetDefinition{volt::NetName{"open"},
                                                          volt::NetKind::Signal}},
            .ports = {volt::ModulePortSpec{volt::PortName{"open"}, volt::NetName{"open"},
                                           volt::PortRole::Passive, true}},
        });
        static_cast<void>(fixture.circuit->instantiate_module(
            module, volt::ModuleInstanceSpec{.name = volt::ModuleInstanceName{"required"}}));
        const auto owner = input(fixture);
        const auto report = volt::compile_electrical(divider_request(fixture, owner));
        CHECK_FALSE(report.complete());
        const auto *failure =
            diagnostic(report, volt::analysis_diagnostic_codes::ElectricalRequiredPortUnbound);
        REQUIRE(failure != nullptr);
        CHECK(failure->entities().size() == 3U);
    }

    SECTION("optional") {
        auto fixture = make_fixture();
        const auto module = fixture.circuit->define_module(volt::ModuleSpec{
            .name = volt::ModuleName{"optional-port"},
            .template_nets = {volt::TemplateNetDefinition{volt::NetName{"open"},
                                                          volt::NetKind::Signal}},
            .ports = {volt::ModulePortSpec{volt::PortName{"open"}, volt::NetName{"open"},
                                           volt::PortRole::Passive, false}},
        });
        static_cast<void>(fixture.circuit->instantiate_module(
            module, volt::ModuleInstanceSpec{.name = volt::ModuleInstanceName{"optional"}}));
        const auto owner = input(fixture);
        const auto report = volt::compile_electrical(divider_request(fixture, owner));
        REQUIRE(report.model() != nullptr);
        CHECK(report.model()->nodes().size() == 4U);
    }
}

TEST_CASE("source-free ProjectBundle input compiles without changing canonical inputs") {
    using namespace volt::test::electrical_compilation;
    using volt::test::project_bundle_v2::TempDirectory;

    const auto temporary = TempDirectory{};
    const auto path = temporary.path() / "electrical-compilation.volt";
    auto fixture = make_fixture();
    const auto circuit_before = volt::io::write_logical_circuit(*fixture.circuit);
    const auto input_before = input(fixture);
    const auto part_before =
        volt::io::write_part_definition(*input_before.part(fixture.upper_resistor));
    source_free_project(fixture).write(path);

    const auto bundle = volt::io::ProjectBundle::open(path);
    const auto circuits = bundle.graph().loaded_project().circuits();
    REQUIRE(circuits.size() == 1U);
    const auto reopened = circuits.front().electrical_input();
    const auto report = volt::compile_electrical(
        divider_request(reopened, fixture.supply, fixture.midpoint, fixture.reference));

    REQUIRE(report.model() != nullptr);
    CHECK(reopened.identity() == input_before.identity());
    CHECK(volt::io::write_logical_circuit(reopened.circuit()) == circuit_before);
    REQUIRE(reopened.part(fixture.upper_resistor) != nullptr);
    CHECK(volt::io::write_part_definition(*reopened.part(fixture.upper_resistor)) == part_before);
    CHECK(volt::io::write_logical_circuit(*fixture.circuit) == circuit_before);
}
