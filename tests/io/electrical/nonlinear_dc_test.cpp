#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>
#include <limits>
#include <ranges>
#include <string>

#include <nlohmann/json.hpp>

#include <volt/electrical/dc_solve.hpp>
#include <volt/electrical/ngspice_dc.hpp>
#include <volt/io/electrical/dc_solve_io.hpp>
#include <volt/io/electrical/ngspice_dc_io.hpp>

#include "../../../src/circuit/electrical/nonlinear_dc_detail.hpp"
#include "support/diode_fixture.hpp"

namespace {
using namespace volt;
using namespace volt::test::diode;

const DcBranchResult &diode_result(const DcSolution &solution) {
    const auto branch = std::ranges::find_if(solution.model().branches(), [](const auto &item) {
        return std::holds_alternative<ShockleyDiodeElement>(item.law);
    });
    REQUIRE(branch != solution.model().branches().end());
    return solution.branches().at(branch->id.index());
}

void check_failure_codes(const DcSolveReport &report) {
    REQUIRE_FALSE(report.diagnostics().empty());
    for (const auto &diagnostic : report.diagnostics()) {
        CHECK(std::ranges::find(diagnostic_code_catalogs::Analysis, diagnostic.code().value()) !=
              diagnostic_code_catalogs::Analysis.end());
    }
}

void check_gates(const DcSolveReport &report) {
    REQUIRE(report.outcome() == DcSolveOutcome::Converged);
    REQUIRE(report.success());
    REQUIRE(report.solution() != nullptr);
    REQUIRE(report.nonlinear_options() != nullptr);
    REQUIRE(report.solution()->nonlinear_options() != nullptr);
    const auto &metrics = report.metrics();
    REQUIRE(metrics.rank == metrics.coordinate_count);
    REQUIRE(metrics.reciprocal_condition.has_value());
    CHECK(*metrics.reciprocal_condition >= report.options().minimum_reciprocal_condition());
    REQUIRE(metrics.correction_error_ratio.has_value());
    CHECK(*metrics.correction_error_ratio <= 1.0);
    REQUIRE(metrics.voltage_error_ratio.has_value());
    REQUIRE(metrics.current_error_ratio.has_value());
    CHECK(*metrics.voltage_error_ratio <= 1.0);
    CHECK(*metrics.current_error_ratio <= 1.0);
    CHECK(metrics.jacobian_evaluations == metrics.iterations + 1);
    CHECK(metrics.residual_evaluations >= metrics.jacobian_evaluations + 1);
    CHECK(metrics.residual_weights.size() == metrics.coordinate_count);
    auto power = 0.0;
    for (const auto &branch : report.solution()->branches()) {
        power += branch.power.value();
    }
    CHECK(std::abs(power) < 1e-10);
    for (const auto &node : report.model().nodes()) {
        auto kcl = 0.0;
        for (const auto &incidence : node.incidence) {
            kcl += incidence.sign *
                   report.solution()->branches().at(incidence.branch.index()).current.value();
        }
        CHECK(std::abs(kcl) <= 1e-12);
    }
}

DcSolveOptions picoamp_acceptance() {
    return DcSolveOptions{1e-12, 1e-12, 1e-9, Quantity{UnitDimension::Voltage, 1e-9},
                          Quantity{UnitDimension::Current, 1e-16}};
}
} // namespace

TEST_CASE("native nonlinear DC matches independently bracketed exact SI scalar roots") {
    const auto fixture = make_fixture();
    const auto owner = input(fixture);
    for (const auto source : {1.0, 5.0}) {
        const auto compiled = compile_electrical(voltage_request(fixture, owner, source));
        REQUIRE(compiled.model() != nullptr);
        const auto report = solve_dc(*compiled.model(), NonlinearDcSolveOptions{});
        INFO("source voltage " << source);
        for (const auto &diagnostic : report.diagnostics()) {
            INFO(diagnostic.message());
        }
        check_gates(report);
        const auto &diode = diode_result(*report.solution());
        const auto voltage = source == 1.0 ? 0.517173532980596803 : 0.574476925589298085;
        const auto current = source == 1.0 ? 0.000482826467019403197 : 0.004425523074410701915;
        CHECK(diode.voltage.value() == Catch::Approx(voltage).margin(1e-9).epsilon(1e-9));
        CHECK(diode.current.value() == Catch::Approx(current).margin(1e-12).epsilon(1e-9));
        CHECK((source - diode.voltage.value()) / 1000.0 ==
              Catch::Approx(diode.current.value()).margin(1e-12));
        const auto snapshot = nlohmann::json::parse(io::write_dc_solve_report(report));
        CHECK(snapshot["version"] == 2);
        CHECK(snapshot["outcome"] == "converged");
        CHECK(snapshot["nonlinear_options"]["algorithm"] == "diode-newton");
        CHECK(snapshot["metrics"]["residual_weights"].size() == report.metrics().coordinate_count);
        CHECK(io::write_dc_solve_report(report) ==
              io::write_dc_solve_report(solve_dc(*compiled.model(), NonlinearDcSolveOptions{})));
    }
}

TEST_CASE("native nonlinear DC preserves picoamp forward and reverse leakage") {
    const auto fixture = make_fixture();
    const auto owner = input(fixture);
    for (const auto source : {0.1, -0.05}) {
        const auto compiled = compile_electrical(voltage_request(fixture, owner, source));
        REQUIRE(compiled.model() != nullptr);
        const auto report =
            solve_dc(*compiled.model(), NonlinearDcSolveOptions{picoamp_acceptance()});
        check_gates(report);
        const auto expected = source > 0.0 ? 46.7623551208893e-12 : -0.855303923528116e-12;
        const auto &diode = diode_result(*report.solution());
        CHECK(diode.current.value() == Catch::Approx(expected).margin(1e-18).epsilon(1e-7));
        CHECK(std::abs(diode.current.value()) > 1e-13);
    }
}

TEST_CASE("current driven native diode matches exact plus and minus emission voltage log two") {
    for (const auto ideality : {1.0, 2.0}) {
        const auto parameters = canonical_parameters(ideality);
        const auto fixture = make_fixture(std::nullopt, parameters);
        const auto owner = input(fixture);
        for (const auto current : {1e-12, -0.5e-12}) {
            const auto compiled = compile_electrical(current_request(fixture, owner, current));
            REQUIRE(compiled.model() != nullptr);
            const auto report =
                solve_dc(*compiled.model(), NonlinearDcSolveOptions{picoamp_acceptance()});
            check_gates(report);
            const auto &diode = diode_result(*report.solution());
            const auto expected =
                (current > 0.0 ? 1.0 : -1.0) * parameters.emission_voltage() * std::log(2.0);
            CHECK(diode.voltage.value() == Catch::Approx(expected).margin(1e-9).epsilon(1e-9));
            CHECK(diode.current.value() == Catch::Approx(current).margin(1e-18).epsilon(1e-7));
        }
    }
}

TEST_CASE("default linear DC refuses diode before numerical assembly") {
    const auto fixture = make_fixture();
    const auto compiled = compile_electrical(voltage_request(fixture, input(fixture), 1.0));
    REQUIRE(compiled.model() != nullptr);
    const auto report = solve_dc(*compiled.model());
    CHECK(report.outcome() == DcSolveOutcome::UnsupportedModel);
    CHECK_FALSE(report.success());
    check_failure_codes(report);
    CHECK(report.solution() == nullptr);
    CHECK(report.metrics().coordinate_count == 0);
    CHECK_FALSE(report.metrics().rank.has_value());
    REQUIRE_FALSE(report.diagnostics().empty());
    CHECK_FALSE(report.diagnostics().front().entities().empty());
}

TEST_CASE("native nonlinear DC reports bounded failures without partial observations") {
    const auto fixture = make_fixture();
    const auto compiled = compile_electrical(voltage_request(fixture, input(fixture), 5.0));
    REQUIRE(compiled.model() != nullptr);
    SECTION("accepted correction budget") {
        const auto report =
            solve_dc(*compiled.model(), NonlinearDcSolveOptions{DcSolveOptions{}, 1});
        CHECK(report.outcome() == DcSolveOutcome::IterationLimit);
        CHECK(report.metrics().iterations == 1);
        check_failure_codes(report);
        CHECK(report.solution() == nullptr);
    }
    SECTION("residual budget") {
        const auto report =
            solve_dc(*compiled.model(), NonlinearDcSolveOptions{DcSolveOptions{}, 80, 24, 1, 81});
        CHECK(report.outcome() == DcSolveOutcome::EvaluationLimit);
        CHECK(report.metrics().residual_evaluations == 1);
        check_failure_codes(report);
        CHECK(report.solution() == nullptr);
    }
    SECTION("Jacobian budget") {
        const auto report =
            solve_dc(*compiled.model(), NonlinearDcSolveOptions{DcSolveOptions{}, 80, 24, 2048, 1});
        CHECK(report.outcome() == DcSolveOutcome::EvaluationLimit);
        CHECK(report.metrics().jacobian_evaluations == 1);
        check_failure_codes(report);
        CHECK(report.solution() == nullptr);
    }
    SECTION("conditioning policy") {
        const auto report =
            solve_dc(*compiled.model(), NonlinearDcSolveOptions{DcSolveOptions{1e-12, 0.999999}});
        CHECK(report.outcome() == DcSolveOutcome::IllConditioned);
        check_failure_codes(report);
        CHECK(report.solution() == nullptr);
    }
    SECTION("tiny final evaluation budget cannot publish cached residuals") {
        const auto successful = solve_dc(*compiled.model(), NonlinearDcSolveOptions{});
        REQUIRE(successful.success());
        const auto budget = successful.metrics().residual_evaluations - 1;
        const auto report = solve_dc(*compiled.model(),
                                     NonlinearDcSolveOptions{DcSolveOptions{}, 80, 24, budget, 81});
        CHECK(report.outcome() == DcSolveOutcome::EvaluationLimit);
        CHECK(report.metrics().correction_error_ratio.has_value());
        check_failure_codes(report);
        CHECK(report.solution() == nullptr);
    }
}

TEST_CASE("native nonlinear DC domain termination does not claim nonexistence") {
    const auto fixture = make_fixture(std::nullopt);
    const auto compiled = compile_electrical(voltage_request(fixture, input(fixture), 1e12));
    REQUIRE(compiled.model() != nullptr);
    const auto report = solve_dc(*compiled.model(), NonlinearDcSolveOptions{});
    CHECK(report.outcome() == DcSolveOutcome::DomainLimited);
    check_failure_codes(report);
    CHECK(report.solution() == nullptr);
    CHECK(report.metrics().iterations == 0);
    REQUIRE_FALSE(report.diagnostics().empty());
    CHECK(std::ranges::any_of(report.diagnostics(), [](const auto &diagnostic) {
        return !diagnostic.entities().empty();
    }));
}

TEST_CASE("native nonlinear DC singular Jacobian stays distinct from linear inconsistency") {
    auto fixture = make_fixture();
    const auto first = fixture.circuit->add_net(NetSpec{.name = NetName{"floating-first"}});
    const auto second = fixture.circuit->add_net(NetSpec{.name = NetName{"floating-second"}});
    const auto resistor = fixture.circuit->instantiate_component(
        fixture.definition, ComponentInstanceSpec{.reference = ReferenceDesignator{"RF"}});
    fixture.circuit->update(
        resistor, SelectLibraryPart{fixture.library, fixture.library.require(PartKey{"resistor"})});
    fixture.circuit->connect(first,
                             queries::pin_by_number(*fixture.circuit, resistor, "1").value());
    fixture.circuit->connect(second,
                             queries::pin_by_number(*fixture.circuit, resistor, "2").value());
    const auto request = voltage_request(fixture, input(fixture), 1.0);
    const auto compiled = compile_electrical(request);
    REQUIRE(compiled.model() != nullptr);
    const auto report = solve_dc(*compiled.model(), NonlinearDcSolveOptions{});
    CHECK(report.outcome() == DcSolveOutcome::JacobianSingular);
    check_failure_codes(report);
    CHECK(report.solution() == nullptr);
    CHECK(report.metrics().rank < report.metrics().coordinate_count);
    CHECK_FALSE(report.metrics().augmented_rank.has_value());
}

TEST_CASE("native nonlinear DC fails nonfinite correction reconstruction explicitly") {
    const auto fixture = make_fixture(std::nullopt);
    const auto compiled = compile_electrical(current_request(fixture, input(fixture), 1e308));
    REQUIRE(compiled.model() != nullptr);
    const auto report = solve_dc(*compiled.model(), NonlinearDcSolveOptions{});
    CHECK(report.outcome() == DcSolveOutcome::NumericalFailure);
    check_failure_codes(report);
    CHECK(report.solution() == nullptr);
}

TEST_CASE("native nonlinear settings are validated and all budgets affect identity") {
    CHECK_THROWS_AS((NonlinearDcSolveOptions{DcSolveOptions{}, 0}), std::invalid_argument);
    CHECK_THROWS_AS((NonlinearDcSolveOptions{DcSolveOptions{}, 80, 0}), std::invalid_argument);
    CHECK_THROWS_AS((NonlinearDcSolveOptions{DcSolveOptions{}, 80, 25}), std::invalid_argument);
    CHECK_THROWS_AS((NonlinearDcSolveOptions{DcSolveOptions{}, 80, 24, 0}), std::invalid_argument);
    CHECK_THROWS_AS((NonlinearDcSolveOptions{DcSolveOptions{}, 80, 24, 2048, 0}),
                    std::invalid_argument);
    const auto fixture = make_fixture();
    const auto compiled = compile_electrical(voltage_request(fixture, input(fixture), 1.0));
    REQUIRE(compiled.model() != nullptr);
    const auto baseline = solve_dc(*compiled.model(), NonlinearDcSolveOptions{});
    for (const auto &options : {NonlinearDcSolveOptions{DcSolveOptions{}, 79},
                                NonlinearDcSolveOptions{DcSolveOptions{}, 80, 23},
                                NonlinearDcSolveOptions{DcSolveOptions{}, 80, 24, 2047},
                                NonlinearDcSolveOptions{DcSolveOptions{}, 80, 24, 2048, 80},
                                NonlinearDcSolveOptions{picoamp_acceptance()}}) {
        CHECK(solve_dc(*compiled.model(), options).analysis_identity() !=
              baseline.analysis_identity());
    }
}

TEST_CASE(
    "native nonlinear assembled Jacobian matches independent equation directional differences") {
    const auto fixture = make_fixture();
    const auto compiled = compile_electrical(voltage_request(fixture, input(fixture), 1.0));
    REQUIRE(compiled.model() != nullptr);
    const auto count = compiled.model()->nodes().size() - 1 + compiled.model()->branches().size();
    for (const auto voltage : {0.0, 0.2}) {
        auto state = std::vector<double>(count, 0.0);
        auto direction = std::vector<double>(count, 0.0);
        state.at(0) = voltage;
        state.at(1) = voltage + 0.1;
        direction.at(0) = 0.02;
        direction.at(1) = -0.01;
        for (std::size_t column = 2; column < count; ++column) {
            state.at(column) = 1e-12 * static_cast<double>(column);
            direction.at(column) = -1e-12 * static_cast<double>(column + 1);
        }
        const auto exact = detail::evaluate_nonlinear_dc_equations(
            *compiled.model(), DcSolveOptions{}, state, direction);
        REQUIRE(exact.status == ShockleyDiodeEvaluationStatus::Valid);
        for (const auto h : {1e-3, 1e-4, 1e-5}) {
            auto plus = state;
            auto minus = state;
            for (std::size_t column = 0; column < count; ++column) {
                plus.at(column) += h * direction.at(column);
                minus.at(column) -= h * direction.at(column);
            }
            const auto upper = detail::evaluate_nonlinear_dc_equations(
                *compiled.model(), DcSolveOptions{}, plus, direction);
            const auto lower = detail::evaluate_nonlinear_dc_equations(
                *compiled.model(), DcSolveOptions{}, minus, direction);
            REQUIRE(upper.status == ShockleyDiodeEvaluationStatus::Valid);
            REQUIRE(lower.status == ShockleyDiodeEvaluationStatus::Valid);
            for (std::size_t row = 0; row < count; ++row) {
                INFO("row " << row << " h " << h << " voltage " << voltage);
                const auto difference =
                    (upper.residual.at(row) - lower.residual.at(row)) / (2.0 * h);
                const auto derivative = exact.jacobian_direction.at(row);
                REQUIRE(std::abs(derivative) > 1e-14);
                CHECK(std::abs(difference / derivative - 1.0) <= 1e-6);
            }
        }
    }
}

TEST_CASE(
    "native nonlinear solves mixed storage and unequal fixed temperature composite diode chains") {
    auto fixture = make_fixture();
    const auto &component = fixture.circuit->get(fixture.definition);
    auto builder = PartElectricalModelBuilder{component};
    const auto anode = builder.terminal(ModelTerminalKey{"anode"}, PinKey{"A"});
    const auto cathode = builder.terminal(ModelTerminalKey{"cathode"}, PinKey{"K"});
    const auto middle = builder.internal_node(ModelInternalNodeKey{"middle"});
    const auto resistor_node = builder.internal_node(ModelInternalNodeKey{"resistor"});
    const auto inductor_node = builder.internal_node(ModelInternalNodeKey{"inductor"});
    builder.add<ShockleyDiodeElement>(ModelElementKey{"junction"}, anode, middle,
                                      canonical_parameters(1.0, 300.15));
    builder.add<ShockleyDiodeElement>(ModelElementKey{"second"}, middle, resistor_node,
                                      canonical_parameters(2.0, 320.0));
    builder.add<ResistanceElement>(ModelElementKey{"private-resistance"}, resistor_node,
                                   inductor_node,
                                   ModelParameter{Quantity{UnitDimension::Resistance, 500.0}});
    builder.add<InductanceElement>(ModelElementKey{"series-inductance"}, inductor_node, cathode,
                                   ModelParameter{Quantity{UnitDimension::Inductance, 0.1}});
    builder.add<CapacitanceElement>(ModelElementKey{"shunt-capacitance"}, anode, cathode,
                                    ModelParameter{Quantity{UnitDimension::Capacitance, 1e-6}});
    replace_model(fixture, builder.build());
    const auto compiled = compile_electrical(voltage_request(fixture, input(fixture), 1.0));
    REQUIRE(compiled.model() != nullptr);
    const auto report = solve_dc(*compiled.model(), NonlinearDcSolveOptions{});
    check_gates(report);
    const auto current = diode_result(*report.solution()).current.value();
    const auto first_voltage =
        canonical_parameters(1.0, 300.15).emission_voltage() * std::log1p(current / 1e-12);
    const auto second_voltage =
        canonical_parameters(2.0, 320.0).emission_voltage() * std::log1p(current / 1e-12);
    CHECK(first_voltage + second_voltage + 1500.0 * current == Catch::Approx(1.0).margin(1e-9));
    for (const auto &branch : compiled.model()->branches()) {
        const auto &observation = report.solution()->branches().at(branch.id.index());
        if (std::holds_alternative<CapacitanceElement>(branch.law)) {
            CHECK(observation.current.value() == Catch::Approx(0.0).margin(1e-12));
        } else if (std::holds_alternative<InductanceElement>(branch.law)) {
            CHECK(observation.voltage.value() == Catch::Approx(0.0).margin(1e-9));
            CHECK(observation.current.value() == Catch::Approx(current).margin(1e-12));
        }
    }
}

TEST_CASE("native nonlinear retains tied diode endpoint cancellation and zero observations") {
    const auto fixture = make_fixture(1000.0, canonical_parameters(), true);
    const auto owner = input(fixture);
    const auto request = voltage_request(fixture, owner, 0.2);
    const auto compiled = compile_electrical(request);
    REQUIRE(compiled.model() != nullptr);
    const auto report = solve_dc(*compiled.model(), NonlinearDcSolveOptions{});
    check_gates(report);
    CHECK(diode_result(*report.solution()).voltage.value() == 0.0);
    CHECK(diode_result(*report.solution()).current.value() == 0.0);
}

TEST_CASE(
    "native nonlinear original equations match independently archived ngspice 46 observations") {
    auto stream =
        std::ifstream{std::string{VOLT_TEST_FIXTURE_DIR} + "/nonlinear_dc/ngspice-46-results.json"};
    REQUIRE(stream.good());
    const auto oracle = nlohmann::json::parse(stream);
    const auto fixture = make_fixture();
    const auto owner = input(fixture);
    for (const auto &[key, voltage] :
         {std::pair{"forward_1v", 1.0}, std::pair{"forward_5v", 5.0},
          std::pair{"picoamp_forward", 0.1}, std::pair{"mild_reverse", -0.05}}) {
        const auto compiled = compile_electrical(voltage_request(fixture, owner, voltage));
        REQUIRE(compiled.model() != nullptr);
        const auto report =
            solve_dc(*compiled.model(), NonlinearDcSolveOptions{picoamp_acceptance()});
        check_gates(report);
        const auto &branch = diode_result(*report.solution());
        const auto &observations = oracle.at("cases").at(key).at("observations");
        CHECK(
            branch.voltage.value() ==
            Catch::Approx(observations.at("v(junction)").get<double>()).margin(1e-6).epsilon(1e-6));
        CHECK(branch.current.value() ==
              Catch::Approx(observations.at("@djunction[id]").get<double>())
                  .margin(1e-9)
                  .epsilon(1e-6));
    }
}

TEST_CASE("native nonlinear accepts an empty reference only compiled DC model") {
    const auto fixture = make_fixture();
    auto circuit = Circuit{};
    const auto reference = circuit.add_net(NetSpec{.name = NetName{"reference"}});
    const auto owner = io::prepare_electrical_input(circuit, fixture.library);
    const auto request =
        DcRequest{ElectricalRequestKey{"empty-reference"}, owner, owner.net(reference), {}, {}};
    const auto compiled = compile_electrical(request);
    REQUIRE(compiled.model() != nullptr);
    const auto report = solve_dc(*compiled.model(), NonlinearDcSolveOptions{});
    check_gates(report);
    CHECK(report.solution()->branches().empty());
    CHECK(report.metrics().coordinate_count == 0);
}

TEST_CASE(
    "native nonlinear repeated parallel diode parts agree with independent scalar bisection") {
    auto fixture = make_fixture();
    const auto repeated = fixture.circuit->instantiate_component(
        fixture.definition, ComponentInstanceSpec{.reference = ReferenceDesignator{"D2"}});
    fixture.circuit->update(
        repeated, SelectLibraryPart{fixture.library, fixture.library.require(PartKey{"diode"})});
    fixture.circuit->connect(fixture.junction,
                             queries::pin_by_number(*fixture.circuit, repeated, "1").value());
    fixture.circuit->connect(fixture.reference,
                             queries::pin_by_number(*fixture.circuit, repeated, "2").value());
    const auto compiled = compile_electrical(voltage_request(fixture, input(fixture), 5.0));
    REQUIRE(compiled.model() != nullptr);
    const auto report = solve_dc(*compiled.model(), NonlinearDcSolveOptions{});
    check_gates(report);
    auto low = 0.0L;
    auto high = 0.8L;
    // Independent monotone scalar reduction with exact SI constants; no native evaluator.
    const auto emission = 1.380649e-23L * 300.15L / 1.602176634e-19L;
    for (auto iteration = 0; iteration < 100; ++iteration) {
        const auto middle = (low + high) / 2.0L;
        const auto current = 1e-12L * std::expm1(middle / emission);
        if (middle + 2000.0L * current < 5.0L) {
            low = middle;
        } else {
            high = middle;
        }
    }
    const auto expected_voltage = static_cast<double>((low + high) / 2.0L);
    const auto expected_current = (5.0 - expected_voltage) / 2000.0;
    auto diode_count = 0;
    for (const auto &branch : report.model().branches()) {
        if (std::holds_alternative<ShockleyDiodeElement>(branch.law)) {
            const auto &value = report.solution()->branches().at(branch.id.index());
            CHECK(value.voltage.value() ==
                  Catch::Approx(expected_voltage).margin(1e-9).epsilon(1e-9));
            CHECK(value.current.value() ==
                  Catch::Approx(expected_current).margin(1e-12).epsilon(1e-9));
            ++diode_count;
        }
    }
    CHECK(diode_count == 2);
}

TEST_CASE("small initial current residual alone cannot accept a current driven diode") {
    const auto fixture = make_fixture(std::nullopt);
    const auto compiled = compile_electrical(current_request(fixture, input(fixture), 1e-12));
    REQUIRE(compiled.model() != nullptr);
    const auto report = solve_dc(*compiled.model(), NonlinearDcSolveOptions{});
    check_gates(report);
    // At all-zero, source residual equals the current floor, but undamped voltage
    // correction is one emission voltage, so accepting that state would be false convergence.
    CHECK(report.metrics().iterations > 0);
    CHECK(diode_result(*report.solution()).voltage.value() ==
          Catch::Approx(canonical_parameters().emission_voltage() * std::log(2.0)).margin(1e-9));
}

TEST_CASE(
    "native nonlinear reports Armijo line search budget failure with finite admissible trials") {
    const auto fixture = make_fixture();
    const auto compiled = compile_electrical(voltage_request(fixture, input(fixture), 5.0));
    REQUIRE(compiled.model() != nullptr);
    const auto acceptance =
        DcSolveOptions{1e-12, 1e-12, 1e-9, Quantity{UnitDimension::Voltage, 1e-9},
                       Quantity{UnitDimension::Current, 1e-30}};
    const auto report = solve_dc(*compiled.model(), NonlinearDcSolveOptions{acceptance, 80, 1});
    CHECK(report.outcome() == DcSolveOutcome::LineSearchFailed);
    check_failure_codes(report);
    CHECK(report.solution() == nullptr);
    CHECK(report.metrics().iterations == 0);
    CHECK(report.metrics().backtracks == 1);
    CHECK(report.metrics().domain_rejections == 0);
    CHECK(report.metrics().nonfinite_rejections == 0);
    CHECK(report.metrics().residual_evaluations == 3);
}

TEST_CASE("public ngspice diode capability refusal retains model and creates no executable deck") {
    const auto fixture = make_fixture();
    const auto compiled = compile_electrical(voltage_request(fixture, input(fixture), 1.0));
    REQUIRE(compiled.model() != nullptr);
    const auto analysis = prepare_ngspice_dc(*compiled.model());
    CHECK_FALSE(analysis.complete());
    CHECK(analysis.deck().empty());
    CHECK(analysis.deck_identity() == sha256_content_hash(""));
    CHECK(analysis.model().identity() == compiled.model()->identity());
    REQUIRE_FALSE(analysis.diagnostics().empty());
    for (const auto &diagnostic : analysis.diagnostics()) {
        CHECK(std::ranges::find(diagnostic_code_catalogs::Analysis, diagnostic.code().value()) !=
              diagnostic_code_catalogs::Analysis.end());
    }
    CHECK_FALSE(analysis.diagnostics().front().entities().empty());
    CHECK(analysis.mapping_identity() == prepare_ngspice_dc(*compiled.model()).mapping_identity());
    const auto report = nlohmann::json::parse(io::write_ngspice_dc_analysis(analysis));
    CHECK(report.at("projection").at("kind") == "unavailable");
    CHECK_FALSE(report.at("projection").at("losses").empty());
    CHECK(report.at("mapping").is_null());
    CHECK(report.at("unlowered_branches").size() == compiled.model()->branches().size());
}
