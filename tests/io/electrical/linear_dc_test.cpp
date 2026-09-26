#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/electrical/dc_solve.hpp>

#include "support/electrical_compilation_fixture.hpp"

namespace {

using volt::test::electrical_compilation::Fixture;
using volt::test::electrical_compilation::PartVariant;
using Json = nlohmann::json;

struct CatalogCircuit {
    std::unique_ptr<volt::Circuit> circuit;
    volt::io::PartLibraryBundle library;
    volt::ComponentDefId component;
};

[[nodiscard]] CatalogCircuit make_catalog_circuit() {
    auto source = volt::test::electrical_compilation::make_fixture();
    auto circuit = std::make_unique<volt::Circuit>();
    const auto component = circuit->define_component(volt::ComponentSpec{
        .name = "S2 required two-terminal component",
        .pins = {volt::PinSpec{.name = "A", .number = "1"},
                 volt::PinSpec{.name = "B", .number = "2"}},
        .contract =
            volt::ComponentContractSpec{.key = volt::ComponentKey{"test.compile/required@1"},
                                        .pin_keys = {volt::PinKey{"A"}, volt::PinKey{"B"}}}});
    return {std::move(circuit), std::move(source.library), component};
}

[[nodiscard]] volt::ComponentId add_catalog_part(CatalogCircuit &fixture, std::string part,
                                                 std::string reference, volt::NetId from,
                                                 volt::NetId to) {
    const auto occurrence = fixture.circuit->instantiate_component(
        fixture.component,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{reference}});
    fixture.circuit->update(
        occurrence, volt::SelectLibraryPart{
                        fixture.library, fixture.library.require(volt::PartKey{std::move(part)})});
    fixture.circuit->connect(
        from, volt::queries::pin_by_number(*fixture.circuit, occurrence, "1").value());
    fixture.circuit->connect(
        to, volt::queries::pin_by_number(*fixture.circuit, occurrence, "2").value());
    return occurrence;
}

[[nodiscard]] const Json &corpus() {
    static const auto value = [] {
        auto input = std::ifstream{std::string{VOLT_TEST_FIXTURE_DIR} + "/linear_dc/corpus.json"};
        if (!input) {
            throw std::runtime_error{"Unable to open the linear-DC reference corpus"};
        }
        return Json::parse(input);
    }();
    return value;
}

[[nodiscard]] double expected(std::string_view case_id, std::string_view observation) {
    const auto &cases = corpus().at("supported_cases");
    const auto match = std::ranges::find_if(
        cases, [&](const Json &value) { return value.at("id").get<std::string>() == case_id; });
    if (match == cases.end()) {
        throw std::runtime_error{"Unknown linear-DC corpus case"};
    }
    return match->at("expectations").at(observation).at("value").get<double>();
}

void check_compiled_case(const volt::ElectricalCompileReport &compiled, std::string_view case_id) {
    REQUIRE(compiled.model() != nullptr);
    const auto &expected_compile = corpus().at("compiled_expectations").at(case_id);
    CHECK(expected_compile.at("coverage") == "complete");
    CHECK(compiled.model()->branches().size() == expected_compile.at("branch_count"));
    CHECK(compiled.model()->storage().size() == expected_compile.at("storage_count"));
    auto origins = std::vector<std::string>{};
    origins.reserve(compiled.model()->branches().size());
    for (const auto &branch : compiled.model()->branches()) {
        origins.push_back(std::visit(
            [&](const auto &origin) {
                using Origin = std::decay_t<decltype(origin)>;
                if constexpr (std::same_as<Origin, volt::ElectricalElementOrigin>) {
                    return compiled.model()
                               ->request()
                               .input()
                               .circuit()
                               .get(origin.occurrence)
                               .reference()
                               .value() +
                           "/" + origin.element.value();
                } else {
                    return std::string{"source:"} + origin.value();
                }
            },
            branch.origin));
    }
    CHECK(origins == expected_compile.at("origins").get<std::vector<std::string>>());
}

[[nodiscard]] volt::ComponentId add_part(Fixture &fixture, const PartVariant &variant,
                                         std::string reference, volt::NetId from, volt::NetId to) {
    const auto occurrence = fixture.circuit->instantiate_component(
        variant.component,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{reference}});
    fixture.circuit->update(
        occurrence,
        volt::SelectLibraryPart{fixture.library, fixture.library.require(variant.part)});
    fixture.circuit->connect(
        from, volt::queries::pin_by_number(*fixture.circuit, occurrence, "1").value());
    fixture.circuit->connect(
        to, volt::queries::pin_by_number(*fixture.circuit, occurrence, "2").value());
    return occurrence;
}

[[nodiscard]] volt::ElectricalCompileReport compile_divider(const Fixture &fixture) {
    const auto input = volt::test::electrical_compilation::input(fixture);
    return volt::compile_electrical(
        volt::test::electrical_compilation::divider_request(fixture, input));
}

[[nodiscard]] const volt::DcNodeResult &node_result(const volt::DcSolution &solution,
                                                    volt::NetId net) {
    const auto node =
        std::ranges::find_if(solution.model().nodes(), [&](const volt::ElectricalNode &candidate) {
            const auto *origin = std::get_if<volt::ElectricalNetOrigin>(&candidate.origin);
            return origin != nullptr && std::ranges::find(origin->nets, net) != origin->nets.end();
        });
    REQUIRE(node != solution.model().nodes().end());
    return solution.nodes().at(node->id.index());
}

[[nodiscard]] volt::DcBranchResult element_result(const volt::DcSolution &solution,
                                                  volt::ComponentId occurrence, std::string key) {
    const auto branch = std::ranges::find_if(
        solution.model().branches(), [&](const volt::ElectricalBranch &candidate) {
            const auto *origin = std::get_if<volt::ElectricalElementOrigin>(&candidate.origin);
            return origin != nullptr && origin->occurrence == occurrence &&
                   origin->element == volt::ModelElementKey{key};
        });
    REQUIRE(branch != solution.model().branches().end());
    return solution.branches().at(branch->id.index());
}

[[nodiscard]] volt::DcBranchResult source_result(const volt::DcSolution &solution,
                                                 std::string key) {
    const auto branch = std::ranges::find_if(
        solution.model().branches(), [&](const volt::ElectricalBranch &candidate) {
            const auto *origin = std::get_if<volt::DcSourceKey>(&candidate.origin);
            return origin != nullptr && *origin == volt::DcSourceKey{key};
        });
    REQUIRE(branch != solution.model().branches().end());
    return solution.branches().at(branch->id.index());
}

void check_close(double actual, double expected, double margin = 1e-12) {
    CHECK(actual == Catch::Approx(expected).margin(margin).epsilon(1e-9));
}

} // namespace

TEST_CASE("linear DC solves the shared 5 V divider with signed power and residual evidence") {
    using namespace volt::test::electrical_compilation;

    const auto fixture = make_fixture();
    const auto compiled = compile_divider(fixture);
    check_compiled_case(compiled, "divider");
    const auto report = volt::solve_dc(*compiled.model());

    REQUIRE(report.outcome() == volt::DcSolveOutcome::Success);
    REQUIRE(report.solution() != nullptr);
    const auto &solution = *report.solution();
    check_close(node_result(solution, fixture.supply).potential.value(),
                expected("divider", "supply_voltage_v"));
    check_close(node_result(solution, fixture.midpoint).potential.value(),
                expected("divider", "midpoint_voltage_v"));
    check_close(node_result(solution, fixture.reference).potential.value(), 0.0);
    const auto &upper = element_result(solution, fixture.upper_resistor, "body");
    const auto &lower = element_result(solution, fixture.lower_resistor, "body");
    const auto &source = source_result(solution, "supply-5v");
    check_close(upper.current.value(), expected("divider", "upper_current_a"));
    check_close(lower.current.value(), expected("divider", "lower_current_a"));
    check_close(source.current.value(), expected("divider", "source_current_a"));
    check_close(upper.power.value(), expected("divider", "resistor_power_w_each"));
    check_close(lower.power.value(), expected("divider", "resistor_power_w_each"));
    check_close(source.power.value(), expected("divider", "source_power_w"));
    check_close(upper.power.value() + lower.power.value() + source.power.value(),
                expected("divider", "power_sum_w"));
    REQUIRE(solution.probes().size() == 1U);
    check_close(solution.probes().front().value.value(), expected("divider", "midpoint_voltage_v"));
    REQUIRE(report.metrics().rank.has_value());
    CHECK(*report.metrics().rank == report.metrics().coordinate_count);
    REQUIRE(report.metrics().voltage_residual.has_value());
    REQUIRE(report.metrics().current_residual.has_value());
    CHECK(report.metrics().voltage_residual->value() <=
          report.options().absolute_voltage_tolerance().value());
    CHECK(report.metrics().current_residual->value() <=
          report.options().absolute_current_tolerance().value());
}

TEST_CASE("native linear DC matches every checked-in supported reference case") {
    SECTION("series-parallel load") {
        auto fixture = make_catalog_circuit();
        const auto supply =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"supply"}});
        const auto load = fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"load"}});
        const auto reference =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"reference"}});
        const auto series = add_catalog_part(fixture, "resistor", "Rseries", supply, load);
        const auto load_1k = add_catalog_part(fixture, "resistor", "Rload1", load, reference);
        const auto load_2k = add_catalog_part(fixture, "resistor-2k", "Rload2", load, reference);
        const auto input = volt::io::prepare_dc_input(*fixture.circuit, fixture.library);
        const auto request = volt::DcRequest{
            volt::DcRequestKey{"series-parallel-load"},
            input,
            input.net(reference),
            {volt::DcVoltageSource{volt::DcSourceKey{"drive"},
                                   volt::DcNetPair{input.net(supply), input.net(reference)},
                                   volt::Quantity{volt::UnitDimension::Voltage, 5.0}}}};
        const auto compiled = volt::compile_electrical(request);
        check_compiled_case(compiled, "series_parallel_load");
        const auto solved = volt::solve_dc(*compiled.model());
        REQUIRE(solved.solution() != nullptr);
        check_close(node_result(*solved.solution(), load).potential.value(),
                    expected("series_parallel_load", "load_voltage_v"));
        check_close(element_result(*solved.solution(), series, "body").current.value(),
                    expected("series_parallel_load", "series_current_a"));
        check_close(element_result(*solved.solution(), load_1k, "body").current.value(),
                    expected("series_parallel_load", "load_1k_current_a"));
        check_close(element_result(*solved.solution(), load_2k, "body").current.value(),
                    expected("series_parallel_load", "load_2k_current_a"));
        check_close(source_result(*solved.solution(), "drive").current.value(),
                    expected("series_parallel_load", "source_current_a"));
    }

    SECTION("source polarity") {
        auto fixture = make_catalog_circuit();
        const auto reference =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"reference"}});
        const auto positive =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"positive"}});
        const auto negative =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"negative"}});
        const auto current_driven =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"current-driven"}});
        const auto positive_resistor =
            add_catalog_part(fixture, "resistor", "Rpositive", positive, reference);
        const auto negative_resistor =
            add_catalog_part(fixture, "resistor", "Rnegative", reference, negative);
        const auto current_resistor =
            add_catalog_part(fixture, "resistor", "Rcurrent", current_driven, reference);
        const auto input = volt::io::prepare_dc_input(*fixture.circuit, fixture.library);
        const auto request = volt::DcRequest{
            volt::DcRequestKey{"source-polarity"},
            input,
            input.net(reference),
            {volt::DcVoltageSource{volt::DcSourceKey{"positive"},
                                   volt::DcNetPair{input.net(positive), input.net(reference)},
                                   volt::Quantity{volt::UnitDimension::Voltage, 5.0}},
             volt::DcVoltageSource{volt::DcSourceKey{"negative"},
                                   volt::DcNetPair{input.net(negative), input.net(reference)},
                                   volt::Quantity{volt::UnitDimension::Voltage, -5.0}},
             volt::DcCurrentSource{volt::DcSourceKey{"inject"},
                                   volt::DcNetPair{input.net(reference), input.net(current_driven)},
                                   volt::Quantity{volt::UnitDimension::Current, 0.002}}}};
        const auto compiled = volt::compile_electrical(request);
        check_compiled_case(compiled, "source_polarity");
        const auto solved = volt::solve_dc(*compiled.model());
        REQUIRE(solved.solution() != nullptr);
        check_close(node_result(*solved.solution(), positive).potential.value(),
                    expected("source_polarity", "positive_node_v"));
        check_close(source_result(*solved.solution(), "positive").current.value(),
                    expected("source_polarity", "positive_source_current_a"));
        check_close(node_result(*solved.solution(), negative).potential.value(),
                    expected("source_polarity", "negative_node_v"));
        check_close(source_result(*solved.solution(), "negative").current.value(),
                    expected("source_polarity", "negative_source_current_a"));
        check_close(element_result(*solved.solution(), negative_resistor, "body").current.value(),
                    expected("source_polarity", "reversed_resistor_current_a"));
        check_close(node_result(*solved.solution(), current_driven).potential.value(),
                    expected("source_polarity", "current_driven_node_v"));
        check_close(source_result(*solved.solution(), "inject").current.value(),
                    expected("source_polarity", "authored_current_source_a"));
        CHECK(element_result(*solved.solution(), positive_resistor, "body").current.value() > 0.0);
        CHECK(element_result(*solved.solution(), current_resistor, "body").current.value() > 0.0);
    }

    SECTION("zero-ohm constraint") {
        auto fixture = make_catalog_circuit();
        const auto supply =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"supply"}});
        const auto load = fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"load"}});
        const auto reference =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"reference"}});
        const auto wire = add_catalog_part(fixture, "zero-resistor", "Rwire", supply, load);
        static_cast<void>(add_catalog_part(fixture, "resistor", "Rload", load, reference));
        const auto input = volt::io::prepare_dc_input(*fixture.circuit, fixture.library);
        const auto request = volt::DcRequest{
            volt::DcRequestKey{"zero-ohm"},
            input,
            input.net(reference),
            {volt::DcVoltageSource{volt::DcSourceKey{"drive"},
                                   volt::DcNetPair{input.net(supply), input.net(reference)},
                                   volt::Quantity{volt::UnitDimension::Voltage, 5.0}}}};
        const auto compiled = volt::compile_electrical(request);
        check_compiled_case(compiled, "zero_ohm_constraint");
        const auto solved = volt::solve_dc(*compiled.model());
        REQUIRE(solved.solution() != nullptr);
        check_close(node_result(*solved.solution(), load).potential.value(),
                    expected("zero_ohm_constraint", "load_voltage_v"));
        check_close(element_result(*solved.solution(), wire, "body").current.value(),
                    expected("zero_ohm_constraint", "constraint_current_a"));
        check_close(source_result(*solved.solution(), "drive").current.value(),
                    expected("zero_ohm_constraint", "source_current_a"));
    }

    SECTION("ideal capacitor DC") {
        auto fixture = make_catalog_circuit();
        const auto supply =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"supply"}});
        const auto downstream =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"downstream"}});
        const auto reference =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"reference"}});
        const auto capacitor =
            add_catalog_part(fixture, "ideal-capacitor", "Cstorage", supply, downstream);
        static_cast<void>(
            add_catalog_part(fixture, "resistor", "Rreference", downstream, reference));
        const auto input = volt::io::prepare_dc_input(*fixture.circuit, fixture.library);
        const auto request = volt::DcRequest{
            volt::DcRequestKey{"ideal-capacitor"},
            input,
            input.net(reference),
            {volt::DcVoltageSource{volt::DcSourceKey{"drive"},
                                   volt::DcNetPair{input.net(supply), input.net(reference)},
                                   volt::Quantity{volt::UnitDimension::Voltage, 5.0}}}};
        const auto compiled = volt::compile_electrical(request);
        check_compiled_case(compiled, "ideal_capacitor_dc");
        const auto solved = volt::solve_dc(*compiled.model());
        REQUIRE(solved.solution() != nullptr);
        check_close(node_result(*solved.solution(), downstream).potential.value(),
                    expected("ideal_capacitor_dc", "downstream_voltage_v"));
        check_close(element_result(*solved.solution(), capacitor, "storage").current.value(),
                    expected("ideal_capacitor_dc", "capacitor_current_a"));
        check_close(source_result(*solved.solution(), "drive").current.value(),
                    expected("ideal_capacitor_dc", "source_current_a"));
    }

    SECTION("ideal inductor DC") {
        auto fixture = make_catalog_circuit();
        const auto supply =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"supply"}});
        const auto load = fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"load"}});
        const auto reference =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"reference"}});
        const auto inductor = add_catalog_part(fixture, "ideal-inductor", "Lstorage", supply, load);
        static_cast<void>(add_catalog_part(fixture, "resistor", "Rload", load, reference));
        const auto input = volt::io::prepare_dc_input(*fixture.circuit, fixture.library);
        const auto request = volt::DcRequest{
            volt::DcRequestKey{"ideal-inductor"},
            input,
            input.net(reference),
            {volt::DcVoltageSource{volt::DcSourceKey{"drive"},
                                   volt::DcNetPair{input.net(supply), input.net(reference)},
                                   volt::Quantity{volt::UnitDimension::Voltage, 5.0}}}};
        const auto compiled = volt::compile_electrical(request);
        check_compiled_case(compiled, "ideal_inductor_dc");
        const auto solved = volt::solve_dc(*compiled.model());
        REQUIRE(solved.solution() != nullptr);
        check_close(node_result(*solved.solution(), load).potential.value(),
                    expected("ideal_inductor_dc", "load_voltage_v"));
        check_close(element_result(*solved.solution(), inductor, "storage").current.value(),
                    expected("ideal_inductor_dc", "inductor_current_a"));
        check_close(source_result(*solved.solution(), "drive").current.value(),
                    expected("ideal_inductor_dc", "source_current_a"));
    }

    SECTION("composite capacitor DC") {
        auto fixture = make_catalog_circuit();
        const auto terminal_a =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"terminal-a"}});
        const auto reference =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"reference"}});
        const auto composite =
            add_catalog_part(fixture, "composite-capacitor", "C1", terminal_a, reference);
        const auto input = volt::io::prepare_dc_input(*fixture.circuit, fixture.library);
        const auto request = volt::DcRequest{
            volt::DcRequestKey{"composite-capacitor"},
            input,
            input.net(reference),
            {volt::DcVoltageSource{volt::DcSourceKey{"drive"},
                                   volt::DcNetPair{input.net(terminal_a), input.net(reference)},
                                   volt::Quantity{volt::UnitDimension::Voltage, 5.0}}}};
        const auto compiled = volt::compile_electrical(request);
        check_compiled_case(compiled, "composite_capacitor_dc");
        const auto solved = volt::solve_dc(*compiled.model());
        REQUIRE(solved.solution() != nullptr);
        check_close(node_result(*solved.solution(), terminal_a).potential.value(),
                    expected("composite_capacitor_dc", "terminal_a_voltage_v"));
        check_close(element_result(*solved.solution(), composite, "esr").current.value(),
                    expected("composite_capacitor_dc", "esr_current_a"));
        check_close(element_result(*solved.solution(), composite, "esl").current.value(),
                    expected("composite_capacitor_dc", "esl_current_a"));
        check_close(element_result(*solved.solution(), composite, "storage").current.value(),
                    expected("composite_capacitor_dc", "capacitor_current_a"));
        check_close(source_result(*solved.solution(), "drive").current.value(),
                    expected("composite_capacitor_dc", "source_current_a"));
    }
}

TEST_CASE("linear DC preserves voltage and current source polarity") {
    using namespace volt::test::electrical_compilation;

    SECTION("negative voltage") {
        const auto fixture = make_fixture();
        const auto input = volt::test::electrical_compilation::input(fixture);
        const auto request = volt::DcRequest{
            volt::DcRequestKey{"negative-voltage"},
            input,
            input.net(fixture.reference),
            {volt::DcVoltageSource{
                volt::DcSourceKey{"drive"},
                volt::DcNetPair{input.net(fixture.supply), input.net(fixture.reference)},
                volt::Quantity{volt::UnitDimension::Voltage, -5.0}}}};
        const auto compiled = volt::compile_electrical(request);
        REQUIRE(compiled.model() != nullptr);
        const auto solved = volt::solve_dc(*compiled.model());
        REQUIRE(solved.solution() != nullptr);
        check_close(node_result(*solved.solution(), fixture.midpoint).potential.value(), -2.5);
        check_close(source_result(*solved.solution(), "drive").current.value(), 0.0025);
    }

    SECTION("reversed current source") {
        const auto fixture = make_fixture();
        const auto input = volt::test::electrical_compilation::input(fixture);
        const auto request = volt::DcRequest{
            volt::DcRequestKey{"current-source"},
            input,
            input.net(fixture.reference),
            {volt::DcCurrentSource{
                volt::DcSourceKey{"inject"},
                volt::DcNetPair{input.net(fixture.reference), input.net(fixture.supply)},
                volt::Quantity{volt::UnitDimension::Current, 0.005}}},
            {volt::DcSourceCurrentProbe{volt::DcProbeKey{"injected-current"},
                                        volt::DcSourceKey{"inject"}}}};
        const auto compiled = volt::compile_electrical(request);
        REQUIRE(compiled.model() != nullptr);
        const auto solved = volt::solve_dc(*compiled.model());
        REQUIRE(solved.solution() != nullptr);
        check_close(node_result(*solved.solution(), fixture.supply).potential.value(), 10.0);
        check_close(source_result(*solved.solution(), "inject").current.value(), 0.005);
        REQUIRE(solved.solution()->probes().size() == 1U);
        check_close(solved.solution()->probes().front().value.value(), 0.005);
    }

    SECTION("negative current value") {
        const auto fixture = make_fixture();
        const auto input = volt::test::electrical_compilation::input(fixture);
        const auto request = volt::DcRequest{
            volt::DcRequestKey{"negative-current"},
            input,
            input.net(fixture.reference),
            {volt::DcCurrentSource{
                volt::DcSourceKey{"withdraw"},
                volt::DcNetPair{input.net(fixture.supply), input.net(fixture.reference)},
                volt::Quantity{volt::UnitDimension::Current, -0.005}}}};
        const auto compiled = volt::compile_electrical(request);
        REQUIRE(compiled.model() != nullptr);
        const auto solved = volt::solve_dc(*compiled.model());
        REQUIRE(solved.solution() != nullptr);
        check_close(node_result(*solved.solution(), fixture.supply).potential.value(), 10.0);
        check_close(source_result(*solved.solution(), "withdraw").current.value(), -0.005);
    }
}

TEST_CASE("linear DC handles empty, source-only, and coincident formulations") {
    SECTION("reference-only model has no coordinates") {
        auto fixture = make_catalog_circuit();
        const auto reference =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"reference"}});
        const auto input = volt::io::prepare_dc_input(*fixture.circuit, fixture.library);
        const auto compiled = volt::compile_electrical(
            volt::DcRequest{volt::DcRequestKey{"reference-only"}, input, input.net(reference)});
        REQUIRE(compiled.model() != nullptr);
        const auto solved = volt::solve_dc(*compiled.model());
        CHECK(solved.outcome() == volt::DcSolveOutcome::Success);
        REQUIRE(solved.solution() != nullptr);
        CHECK(solved.metrics().coordinate_count == 0U);
        REQUIRE(solved.metrics().rank.has_value());
        CHECK(*solved.metrics().rank == 0U);
        check_close(node_result(*solved.solution(), reference).potential.value(), 0.0);
    }

    SECTION("source-only model retains a unique zero current") {
        auto fixture = make_catalog_circuit();
        const auto supply =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"supply"}});
        const auto reference =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"reference"}});
        const auto input = volt::io::prepare_dc_input(*fixture.circuit, fixture.library);
        const auto compiled = volt::compile_electrical(volt::DcRequest{
            volt::DcRequestKey{"source-only"},
            input,
            input.net(reference),
            {volt::DcVoltageSource{volt::DcSourceKey{"drive"},
                                   volt::DcNetPair{input.net(supply), input.net(reference)},
                                   volt::Quantity{volt::UnitDimension::Voltage, 5.0}}}});
        REQUIRE(compiled.model() != nullptr);
        const auto solved = volt::solve_dc(*compiled.model());
        CHECK(solved.outcome() == volt::DcSolveOutcome::Success);
        REQUIRE(solved.solution() != nullptr);
        check_close(node_result(*solved.solution(), supply).potential.value(), 5.0);
        check_close(source_result(*solved.solution(), "drive").current.value(), 0.0);
    }

    SECTION("coincident positive resistance and capacitance are uniquely zero") {
        auto fixture = make_catalog_circuit();
        const auto reference =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"reference"}});
        const auto resistor = add_catalog_part(fixture, "resistor", "Rsame", reference, reference);
        const auto capacitor =
            add_catalog_part(fixture, "ideal-capacitor", "Csame", reference, reference);
        const auto input = volt::io::prepare_dc_input(*fixture.circuit, fixture.library);
        const auto compiled = volt::compile_electrical(
            volt::DcRequest{volt::DcRequestKey{"coincident-passive"}, input, input.net(reference)});
        REQUIRE(compiled.model() != nullptr);
        const auto solved = volt::solve_dc(*compiled.model());
        CHECK(solved.outcome() == volt::DcSolveOutcome::Success);
        REQUIRE(solved.solution() != nullptr);
        check_close(element_result(*solved.solution(), resistor, "body").current.value(), 0.0);
        check_close(element_result(*solved.solution(), capacitor, "storage").current.value(), 0.0);
    }

    for (const auto &[part, reference] :
         {std::pair{"zero-resistor", "Rsame"}, std::pair{"ideal-inductor", "Lsame"}}) {
        DYNAMIC_SECTION("coincident " << part << " has a nonunique ideal-branch current") {
            auto fixture = make_catalog_circuit();
            const auto reference_net =
                fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"reference"}});
            static_cast<void>(
                add_catalog_part(fixture, part, reference, reference_net, reference_net));
            const auto input = volt::io::prepare_dc_input(*fixture.circuit, fixture.library);
            const auto compiled = volt::compile_electrical(volt::DcRequest{
                volt::DcRequestKey{"coincident-ideal"}, input, input.net(reference_net)});
            REQUIRE(compiled.model() != nullptr);
            const auto solved = volt::solve_dc(*compiled.model());
            CHECK(solved.outcome() == volt::DcSolveOutcome::RankDeficient);
            CHECK(solved.solution() == nullptr);
        }
    }

    SECTION("a source cannot author coincident logical endpoints") {
        auto fixture = make_catalog_circuit();
        const auto reference =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"reference"}});
        const auto input = volt::io::prepare_dc_input(*fixture.circuit, fixture.library);
        CHECK_THROWS(volt::DcVoltageSource{
            volt::DcSourceKey{"zero"}, volt::DcNetPair{input.net(reference), input.net(reference)},
            volt::Quantity{volt::UnitDimension::Voltage, 0.0}});
    }
}

TEST_CASE("linear DC solves series-parallel loading") {
    using namespace volt::test::electrical_compilation;

    auto fixture = make_fixture();
    const auto parallel =
        add_part(fixture, fixture.resistor, "R3", fixture.midpoint, fixture.reference);
    const auto compiled = compile_divider(fixture);
    REQUIRE(compiled.model() != nullptr);
    const auto solved = volt::solve_dc(*compiled.model());

    REQUIRE(solved.solution() != nullptr);
    check_close(node_result(*solved.solution(), fixture.midpoint).potential.value(), 5.0 / 3.0);
    check_close(element_result(*solved.solution(), fixture.upper_resistor, "body").current.value(),
                1.0 / 300.0);
    check_close(element_result(*solved.solution(), fixture.lower_resistor, "body").current.value(),
                1.0 / 600.0);
    check_close(element_result(*solved.solution(), parallel, "body").current.value(), 1.0 / 600.0);
}

TEST_CASE("linear DC retains zero-ohm ideal current and capacitor and inductor behavior") {
    using namespace volt::test::electrical_compilation;

    SECTION("zero ohm") {
        auto fixture = make_fixture();
        const auto wire =
            add_part(fixture, fixture.zero_resistor, "R0", fixture.supply, fixture.midpoint);
        const auto compiled = compile_divider(fixture);
        REQUIRE(compiled.model() != nullptr);
        const auto solved = volt::solve_dc(*compiled.model());
        REQUIRE(solved.solution() != nullptr);
        const auto &result = element_result(*solved.solution(), wire, "body");
        check_close(result.voltage.value(), 0.0);
        check_close(result.current.value(), 0.005);
    }

    SECTION("ideal capacitor") {
        auto fixture = make_fixture();
        const auto capacitor =
            add_part(fixture, fixture.capacitor, "C1", fixture.midpoint, fixture.reference);
        const auto compiled = compile_divider(fixture);
        REQUIRE(compiled.model() != nullptr);
        const auto solved = volt::solve_dc(*compiled.model());
        REQUIRE(solved.solution() != nullptr);
        check_close(element_result(*solved.solution(), capacitor, "storage").current.value(), 0.0);
        check_close(node_result(*solved.solution(), fixture.midpoint).potential.value(), 2.5);
    }

    SECTION("ideal inductor") {
        auto fixture = make_fixture();
        const auto inductor =
            add_part(fixture, fixture.inductor, "L1", fixture.supply, fixture.midpoint);
        const auto compiled = compile_divider(fixture);
        REQUIRE(compiled.model() != nullptr);
        const auto solved = volt::solve_dc(*compiled.model());
        REQUIRE(solved.solution() != nullptr);
        const auto &result = element_result(*solved.solution(), inductor, "storage");
        check_close(result.voltage.value(), 0.0);
        check_close(result.current.value(), 0.005);
    }
}

TEST_CASE("linear DC preserves composite private-node values and zero steady current") {
    using namespace volt::test::electrical_compilation;

    auto fixture = make_fixture();
    const auto composite =
        add_part(fixture, fixture.composite, "C1", fixture.supply, fixture.reference);
    const auto compiled = compile_divider(fixture);
    REQUIRE(compiled.model() != nullptr);
    const auto solved = volt::solve_dc(*compiled.model());

    REQUIRE(solved.solution() != nullptr);
    for (const auto *key : {"esr", "esl", "storage"}) {
        check_close(element_result(*solved.solution(), composite, key).current.value(), 0.0);
    }
    check_close(element_result(*solved.solution(), composite, "esr").voltage.value(), 0.0);
    check_close(element_result(*solved.solution(), composite, "esl").voltage.value(), 0.0);
    check_close(element_result(*solved.solution(), composite, "storage").voltage.value(), 5.0);
}

TEST_CASE("linear DC rejects rank-deficient floating and nonunique ideal coordinates") {
    using namespace volt::test::electrical_compilation;

    SECTION("floating resistor island") {
        auto fixture = make_fixture();
        const auto first =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"floating-a"}});
        const auto second =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"floating-b"}});
        static_cast<void>(add_part(fixture, fixture.resistor, "RF", first, second));
        const auto compiled = compile_divider(fixture);
        REQUIRE(compiled.model() != nullptr);
        const auto solved = volt::solve_dc(*compiled.model());
        CHECK(solved.outcome() == volt::DcSolveOutcome::RankDeficient);
        CHECK(solved.solution() == nullptr);
        REQUIRE(solved.metrics().rank.has_value());
        CHECK(*solved.metrics().rank < solved.metrics().coordinate_count);
    }

    SECTION("capacitor-only floating potential") {
        auto fixture = make_fixture();
        const auto floating =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"capacitor-floating"}});
        static_cast<void>(add_part(fixture, fixture.capacitor, "CF", floating, fixture.reference));
        const auto compiled = compile_divider(fixture);
        REQUIRE(compiled.model() != nullptr);
        const auto solved = volt::solve_dc(*compiled.model());
        CHECK(solved.outcome() == volt::DcSolveOutcome::RankDeficient);
        CHECK(solved.solution() == nullptr);
    }

    SECTION("parallel ideal branches have nonunique currents") {
        auto fixture = make_fixture();
        static_cast<void>(
            add_part(fixture, fixture.zero_resistor, "R0A", fixture.supply, fixture.midpoint));
        static_cast<void>(
            add_part(fixture, fixture.zero_resistor, "R0B", fixture.supply, fixture.midpoint));
        const auto compiled = compile_divider(fixture);
        REQUIRE(compiled.model() != nullptr);
        const auto solved = volt::solve_dc(*compiled.model());
        CHECK(solved.outcome() == volt::DcSolveOutcome::RankDeficient);
        CHECK(solved.solution() == nullptr);
    }
}

TEST_CASE("linear DC rejects a globally inconsistent ideal-source loop") {
    using namespace volt::test::electrical_compilation;

    const auto fixture = make_fixture();
    const auto input = volt::test::electrical_compilation::input(fixture);
    const auto request =
        volt::DcRequest{volt::DcRequestKey{"inconsistent-loop"},
                        input,
                        input.net(fixture.reference),
                        {volt::DcVoltageSource{volt::DcSourceKey{"supply-midpoint"},
                                               volt::DcNetPair{input.net(fixture.supply),
                                                               input.net(fixture.midpoint)},
                                               volt::Quantity{volt::UnitDimension::Voltage, 2.0}},
                         volt::DcVoltageSource{volt::DcSourceKey{"midpoint-reference"},
                                               volt::DcNetPair{input.net(fixture.midpoint),
                                                               input.net(fixture.reference)},
                                               volt::Quantity{volt::UnitDimension::Voltage, 2.0}},
                         volt::DcVoltageSource{volt::DcSourceKey{"supply-reference"},
                                               volt::DcNetPair{input.net(fixture.supply),
                                                               input.net(fixture.reference)},
                                               volt::Quantity{volt::UnitDimension::Voltage, 5.0}}}};
    const auto compiled = volt::compile_electrical(request);
    REQUIRE(compiled.model() != nullptr);
    const auto solved = volt::solve_dc(*compiled.model());

    CHECK(solved.outcome() == volt::DcSolveOutcome::Inconsistent);
    CHECK(solved.solution() == nullptr);
    CHECK_FALSE(solved.diagnostics().empty());
}

TEST_CASE("linear DC enforces conditioning and finite-result trust gates") {
    using namespace volt::test::electrical_compilation;

    SECTION("explicit conditioning policy") {
        const auto fixture = make_fixture();
        const auto compiled = compile_divider(fixture);
        REQUIRE(compiled.model() != nullptr);
        const auto options = volt::DcSolveOptions{1e-12, 0.999999, 1e-9};
        const auto solved = volt::solve_dc(*compiled.model(), options);
        CHECK(solved.outcome() == volt::DcSolveOutcome::IllConditioned);
        CHECK(solved.solution() == nullptr);
        REQUIRE(solved.metrics().reciprocal_condition.has_value());
        CHECK(*solved.metrics().reciprocal_condition < options.minimum_reciprocal_condition());
    }

    SECTION("near-parallel resistance and ideal source fail the default conditioning floor") {
        auto fixture = make_fixture();
        static_cast<void>(
            add_part(fixture, fixture.resistor_tiny, "Rtiny", fixture.supply, fixture.reference));
        const auto compiled = compile_divider(fixture);
        REQUIRE(compiled.model() != nullptr);
        const auto options = volt::DcSolveOptions{1e-16};
        const auto solved = volt::solve_dc(*compiled.model(), options);
        CHECK(solved.outcome() == volt::DcSolveOutcome::IllConditioned);
        CHECK(solved.solution() == nullptr);
        REQUIRE(solved.metrics().rank.has_value());
        CHECK(*solved.metrics().rank == solved.metrics().coordinate_count);
        REQUIRE(solved.metrics().reciprocal_condition.has_value());
        CHECK(*solved.metrics().reciprocal_condition < options.minimum_reciprocal_condition());
    }

    SECTION("original-law residual policy can reject an otherwise finite solution") {
        auto fixture = make_fixture();
        const auto tail = fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"tail"}});
        static_cast<void>(add_part(fixture, fixture.resistor_2k, "R3", fixture.midpoint, tail));
        static_cast<void>(add_part(fixture, fixture.resistor, "R4", tail, fixture.reference));
        const auto compiled = compile_divider(fixture);
        REQUIRE(compiled.model() != nullptr);
        constexpr auto strict_tolerance = 1e-25;
        const auto options =
            volt::DcSolveOptions{1e-12, 1e-12, strict_tolerance,
                                 volt::Quantity{volt::UnitDimension::Voltage, strict_tolerance},
                                 volt::Quantity{volt::UnitDimension::Current, strict_tolerance}};
        const auto solved = volt::solve_dc(*compiled.model(), options);
        CHECK(solved.outcome() == volt::DcSolveOutcome::ResidualFailure);
        CHECK(solved.solution() == nullptr);
        CHECK((solved.metrics().scaled_residual.value_or(0.0) >
                   options.relative_residual_tolerance() ||
               solved.metrics().voltage_error_ratio.value_or(0.0) > 1.0 ||
               solved.metrics().current_error_ratio.value_or(0.0) > 1.0));
    }

    SECTION("finite input with overflowing derived power") {
        const auto fixture = make_fixture();
        const auto input = volt::test::electrical_compilation::input(fixture);
        const auto request = volt::DcRequest{
            volt::DcRequestKey{"finite-overflow"},
            input,
            input.net(fixture.reference),
            {volt::DcVoltageSource{
                volt::DcSourceKey{"drive"},
                volt::DcNetPair{input.net(fixture.supply), input.net(fixture.reference)},
                volt::Quantity{volt::UnitDimension::Voltage, std::numeric_limits<double>::max()}}}};
        const auto compiled = volt::compile_electrical(request);
        REQUIRE(compiled.model() != nullptr);
        const auto solved = volt::solve_dc(*compiled.model());
        CHECK(solved.outcome() == volt::DcSolveOutcome::NumericalFailure);
        CHECK(solved.solution() == nullptr);
    }
}

TEST_CASE("linear DC options reject invalid numerical policy") {
    const auto voltage = volt::Quantity{volt::UnitDimension::Voltage, 1e-9};
    const auto current = volt::Quantity{volt::UnitDimension::Current, 1e-12};

    CHECK_THROWS_AS(volt::DcSolveOptions{0.0}, std::invalid_argument);
    CHECK_THROWS_AS(volt::DcSolveOptions{1.0}, std::invalid_argument);
    CHECK_THROWS_AS((volt::DcSolveOptions{1e-12, 0.0}), std::invalid_argument);
    CHECK_THROWS_AS((volt::DcSolveOptions{1e-12, 1.0}), std::invalid_argument);
    CHECK_THROWS_AS((volt::DcSolveOptions{1e-12, 1e-12, 0.0}), std::invalid_argument);
    CHECK_THROWS_AS(volt::DcSolveOptions{std::numeric_limits<double>::quiet_NaN()},
                    std::invalid_argument);
    CHECK_THROWS_AS(
        (volt::DcSolveOptions{1e-12, 1e-12, 1e-9,
                              volt::Quantity{volt::UnitDimension::Current, 1e-9}, current}),
        std::invalid_argument);
    CHECK_THROWS_AS((volt::DcSolveOptions{1e-12, 1e-12, 1e-9, voltage,
                                          volt::Quantity{volt::UnitDimension::Voltage, 1e-12}}),
                    std::invalid_argument);
}

TEST_CASE("linear DC analysis identity is deterministic and includes effective settings") {
    using namespace volt::test::electrical_compilation;

    const auto fixture = make_fixture();
    const auto compiled = compile_divider(fixture);
    REQUIRE(compiled.model() != nullptr);
    const auto first = volt::solve_dc(*compiled.model());
    const auto again = volt::solve_dc(*compiled.model());
    const auto changed =
        volt::solve_dc(*compiled.model(), volt::DcSolveOptions{1e-11, 1e-12, 1e-9});

    CHECK(first.analysis_identity() == again.analysis_identity());
    CHECK(first.analysis_identity() != changed.analysis_identity());
    REQUIRE(first.solution() != nullptr);
    CHECK(first.solution()->analysis_identity() == first.analysis_identity());
    CHECK(first.solution()->model().identity() == compiled.model()->identity());
    CHECK(std::string{first.backend()} == "eigen-5.0.0-full-piv-lu-double-dense-branch-tableau");
}

TEST_CASE("incomplete electrical compilation cannot enter linear DC evaluation") {
    using namespace volt::test::electrical_compilation;

    auto fixture = make_fixture();
    static_cast<void>(add_part(fixture, fixture.absent, "U1", fixture.supply, fixture.reference));
    const auto compiled = compile_divider(fixture);

    CHECK_FALSE(compiled.complete());
    CHECK(compiled.model() == nullptr);
}
