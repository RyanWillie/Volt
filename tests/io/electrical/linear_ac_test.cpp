#include "support/linear_ac_fixture.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <numbers>
#include <volt/electrical/ac_solve.hpp>
#include <volt/io/electrical/ac_solve_io.hpp>

namespace {
using namespace volt;
using Complex = std::complex<double>;

AcRequest request(const test::linear_ac::Fixture &fixture, std::vector<double> frequencies,
                  double phase = 0, bool reverse = false) {
    const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
    std::vector<Quantity> sweep;
    for (auto f : frequencies)
        sweep.emplace_back(UnitDimension::Frequency, f);
    const auto pair =
        reverse ? ElectricalNetPair{input.net(fixture.reference), input.net(fixture.supply)}
                : ElectricalNetPair{input.net(fixture.supply), input.net(fixture.reference)};
    return AcRequest{ElectricalRequestKey{"test-ac"},
                     input,
                     input.net(fixture.reference),
                     AcFrequencySweep{std::move(sweep)},
                     {AcVoltageSource{ElectricalSourceKey{"drive"}, pair,
                                      Quantity{UnitDimension::Voltage, 1}, phase}}};
}

Complex potential(const AcFrequencyResult &point, const CompiledElectricalModel &model, NetId net) {
    for (const auto &node : model.nodes())
        if (const auto *origin = std::get_if<ElectricalNetOrigin>(&node.origin))
            if (std::ranges::find(origin->nets, net) != origin->nets.end())
                return point.nodes.at(node.id.index()).potential.value();
    throw std::runtime_error{"Missing node"};
}

void close(Complex actual, Complex expected, double absolute = 1e-9) {
    CHECK(std::abs(actual - expected) <= absolute + 1e-7 * std::abs(expected));
}
} // namespace

TEST_CASE("native AC solves analytical RC RL and series RLC laws") {
    SECTION("RC low pass and phase excitation") {
        for (const auto phase : {0.0, std::numbers::pi / 2}) {
            auto fixture = test::linear_ac::fixture();
            fixture.add("resistor", "R1", fixture.supply, fixture.output);
            fixture.add("capacitor", "C1", fixture.output, fixture.reference);
            const auto f = 1 / (2 * std::numbers::pi * 1000 * 1e-6);
            const auto compiled = compile_electrical(request(fixture, {f}, phase));
            REQUIRE(compiled.complete());
            const auto solved = solve_ac(*compiled.model());
            REQUIRE(solved.success());
            REQUIRE(solved.solution());
            close(potential(solved.solution()->points().front(), *compiled.model(), fixture.output),
                  Complex{.5, -.5} * std::polar(1.0, phase));
            CHECK(io::write_ac_solve_report(solved) ==
                  io::write_ac_solve_report(solve_ac(*compiled.model())));
        }
    }
    SECTION("RL high pass") {
        auto fixture = test::linear_ac::fixture(100, .1);
        fixture.add("resistor", "R1", fixture.supply, fixture.output);
        fixture.add("inductor", "L1", fixture.output, fixture.reference);
        const auto compiled =
            compile_electrical(request(fixture, {100 / (2 * std::numbers::pi * .1)}));
        REQUIRE(compiled.complete());
        const auto solved = solve_ac(*compiled.model());
        REQUIRE(solved.success());
        close(potential(solved.solution()->points().front(), *compiled.model(), fixture.output),
              {.5, .5});
    }
    SECTION("series RLC sweep retains all branch currents") {
        auto fixture = test::linear_ac::fixture(10, .01, 1e-5, true);
        fixture.add("resistor", "R1", fixture.supply, fixture.output);
        fixture.add("inductor", "L1", fixture.output, fixture.middle);
        fixture.add("capacitor", "C1", fixture.middle, fixture.reference);
        const auto resonance = 1 / (2 * std::numbers::pi * std::sqrt(.01 * 1e-5));
        const auto compiled =
            compile_electrical(request(fixture, {resonance / 2, resonance, resonance * 2}));
        REQUIRE(compiled.complete());
        const auto solved = solve_ac(*compiled.model());
        REQUIRE(solved.success());
        for (const auto &point : solved.solution()->points()) {
            const auto omega = 2 * std::numbers::pi * point.frequency.value();
            const Complex z{10, omega * .01 - 1 / (omega * 1e-5)};
            close(point.branches.front().current.value(), 1.0 / z, 1e-11);
            close(point.branches.back().current.value(), -1.0 / z, 1e-11);
        }
    }
}

TEST_CASE("complex observations reject overflow and have no zero phase") {
    CHECK_FALSE(AcComplexQuantity{UnitDimension::Voltage, 0, 0}.phase());
    CHECK_THROWS(AcComplexQuantity(UnitDimension::Voltage, std::numeric_limits<double>::max(),
                                   std::numeric_limits<double>::max()));
    CHECK_THROWS(
        AcComplexQuantity(UnitDimension::Voltage, std::numeric_limits<double>::infinity(), 0));
}

TEST_CASE("native AC matches separately recorded ngspice 46 complex values") {
    const auto corpus = nlohmann::json::parse(
        std::ifstream{std::string{VOLT_TEST_FIXTURE_DIR} + "/linear_ac/ngspice-46-results.json"});
    for (const auto &entry : corpus.at("cases")) {
        const auto id = entry.at("id").get<std::string>();
        auto fixture = test::linear_ac::fixture(id == "rc"   ? 1000
                                                : id == "rl" ? 100
                                                             : 10,
                                                id == "rl" ? .1 : .01, id == "rc" ? 1e-6 : 1e-5,
                                                id != "rc" && id != "rl");
        fixture.add("resistor", "R1", fixture.supply, fixture.output);
        if (id == "rc")
            fixture.add("capacitor", "C1", fixture.output, fixture.reference);
        else if (id == "rl")
            fixture.add("inductor", "L1", fixture.output, fixture.reference);
        else {
            fixture.add("inductor", "L1", fixture.output, fixture.middle);
            fixture.add("capacitor", "C1", fixture.middle, fixture.reference);
        }
        std::vector<double> frequencies;
        for (const auto &point : entry.at("points"))
            frequencies.push_back(point.at("frequency_hz").get<double>());
        auto req = request(fixture, frequencies);
        if (id == "impedance") {
            const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
            std::vector<Quantity> sweep;
            for (auto frequency : frequencies)
                sweep.emplace_back(UnitDimension::Frequency, frequency);
            const auto port =
                ElectricalNetPair{input.net(fixture.supply), input.net(fixture.reference)};
            req = AcRequest{
                ElectricalRequestKey{"impedance"},
                input,
                input.net(fixture.reference),
                AcFrequencySweep{std::move(sweep)},
                {AcCurrentSource{ElectricalSourceKey{"test"}, port,
                                 Quantity{UnitDimension::Current, 1}, 0}},
                {},
                {},
                {},
                {AcImpedanceProbe{ElectricalProbeKey{"z"}, port, ElectricalSourceKey{"test"}}}};
        }
        const auto compiled = compile_electrical(req);
        REQUIRE(compiled.complete());
        const auto solved = solve_ac(*compiled.model());
        REQUIRE(solved.success());
        for (std::size_t i = 0; i < frequencies.size(); ++i) {
            const auto &observations = entry.at("points").at(i).at("observations");
            const auto expected_value = [&](const char *key) {
                const auto &v = observations.at(key);
                return Complex{v.at("real").get<double>(), v.at("imaginary").get<double>()};
            };
            const auto &point = solved.solution()->points().at(i);
            close(potential(point, *compiled.model(), fixture.supply),
                  expected_value("input_voltage_v"));
            close(potential(point, *compiled.model(), fixture.output),
                  expected_value(id == "rc" || id == "rl" ? "output_voltage_v"
                                                          : "after_resistor_voltage_v"));
            if (id == "impedance")
                close(point.probes.back().value.value(), -expected_value("input_voltage_v"), 1e-6);
            else
                close(point.branches.back().current.value(), expected_value("source_current_a"),
                      1e-11);
        }
    }
}

TEST_CASE("native AC failures retain per-frequency evidence and publish no sweep") {
    SECTION("undefined gain denominator") {
        auto fixture = test::linear_ac::fixture(1000, .1, 1e-6, false, false);
        fixture.add("resistor", "R1", fixture.supply, fixture.reference);
        const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
        const auto req =
            AcRequest{ElectricalRequestKey{"undefined-gain"},
                      input,
                      input.net(fixture.reference),
                      AcFrequencySweep{{Quantity{UnitDimension::Frequency, 100}}},
                      {AcVoltageSource{ElectricalSourceKey{"drive"},
                                       ElectricalNetPair{input.net(fixture.supply),
                                                         input.net(fixture.reference)},
                                       Quantity{UnitDimension::Voltage, 1}, 0}},
                      {DcVoltageProbe{ElectricalProbeKey{"signal"},
                                      ElectricalNetPair{input.net(fixture.supply),
                                                        input.net(fixture.reference)}},
                       DcVoltageProbe{ElectricalProbeKey{"zero"},
                                      ElectricalNetPair{input.net(fixture.reference),
                                                        input.net(fixture.reference)}}},
                      {},
                      {AcGainProbe{ElectricalProbeKey{"gain"}, ElectricalProbeKey{"signal"},
                                   ElectricalProbeKey{"zero"}}}};
        const auto compiled = compile_electrical(req);
        REQUIRE(compiled.complete());
        const auto solved = solve_ac(*compiled.model());
        CHECK(solved.outcome() == AcSolveOutcome::UndefinedMeasurement);
        CHECK(solved.solution() == nullptr);
        REQUIRE(solved.points().size() == 1);
        CHECK(solved.points().front().metrics.rank.has_value());
        CHECK_FALSE(solved.points().front().diagnostics.empty());
    }
    SECTION("frequency product overflow") {
        auto fixture = test::linear_ac::fixture(1000, .1, 1e100, false, false);
        fixture.add("resistor", "R1", fixture.supply, fixture.reference);
        fixture.add("capacitor", "C1", fixture.supply, fixture.reference);
        const auto compiled = compile_electrical(request(fixture, {1e-100, 1e300}));
        REQUIRE(compiled.complete());
        const auto solved = solve_ac(*compiled.model());
        CHECK_FALSE(solved.success());
        CHECK(solved.solution() == nullptr);
        REQUIRE(solved.points().size() == 2);
        CHECK(solved.points().front().outcome == AcSolveOutcome::Success);
        CHECK(solved.points().back().outcome == AcSolveOutcome::NumericalFailure);
    }
    SECTION("parallel zero-ohm branches have nonunique currents") {
        auto fixture = test::linear_ac::fixture(0);
        fixture.add("resistor", "R1", fixture.supply, fixture.output);
        fixture.add("resistor", "R2", fixture.supply, fixture.output);
        fixture.add("capacitor", "C1", fixture.output, fixture.reference);
        const auto compiled = compile_electrical(request(fixture, {100}));
        REQUIRE(compiled.complete());
        const auto solved = solve_ac(*compiled.model());
        CHECK(solved.outcome() == AcSolveOutcome::RankDeficient);
        CHECK(solved.solution() == nullptr);
        CHECK(solved.points().front().metrics.augmented_rank.has_value());
    }
}

TEST_CASE("native AC derived gain and canonical orientation") {
    auto fixture = test::linear_ac::fixture();
    fixture.add("resistor", "R1", fixture.output, fixture.supply);
    fixture.add("capacitor", "C1", fixture.output, fixture.reference);
    const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
    const auto frequency = 1 / (2 * std::numbers::pi * 1000 * 1e-6);
    const auto req = AcRequest{
        ElectricalRequestKey{"gain"},
        input,
        input.net(fixture.reference),
        AcFrequencySweep{{Quantity{UnitDimension::Frequency, frequency}}},
        {AcVoltageSource{ElectricalSourceKey{"drive"},
                         ElectricalNetPair{input.net(fixture.reference), input.net(fixture.supply)},
                         Quantity{UnitDimension::Voltage, 1}, 0}},
        {DcVoltageProbe{ElectricalProbeKey{"vin"},
                        ElectricalNetPair{input.net(fixture.supply), input.net(fixture.reference)}},
         DcVoltageProbe{
             ElectricalProbeKey{"vout"},
             ElectricalNetPair{input.net(fixture.output), input.net(fixture.reference)}}},
        {},
        {AcGainProbe{ElectricalProbeKey{"gain"}, ElectricalProbeKey{"vout"},
                     ElectricalProbeKey{"vin"}}}};
    const auto compiled = compile_electrical(req);
    REQUIRE(compiled.complete());
    const auto solved = solve_ac(*compiled.model());
    REQUIRE(solved.success());
    const auto &point = solved.solution()->points().front();
    close(potential(point, *compiled.model(), fixture.supply), {-1, 0});
    close(potential(point, *compiled.model(), fixture.output), {-.5, .5});
    close(point.branches.front().current.value(), {.0005, .0005}, 1e-11);
    const auto gain_result =
        std::ranges::find(point.probes, ElectricalProbeKey{"gain"}, &AcProbeResult::key);
    REQUIRE(gain_result != point.probes.end());
    const auto &gain = gain_result->value;
    close(gain.value(), {.5, -.5});
    CHECK(gain.dimension() == UnitDimension::Ratio);
    CHECK(std::abs(gain.magnitude() - std::sqrt(.5)) <= 1e-7 * std::sqrt(.5));
    REQUIRE(gain.phase());
    CHECK(std::abs(*gain.phase() + std::numbers::pi / 4) <= 1e-5);
}

TEST_CASE("native AC trust policy declines lossless resonance and poor conditioning") {
    SECTION("lossless driven series resonance") {
        auto fixture = test::linear_ac::fixture(0, .01, 1e-5, true);
        fixture.add("resistor", "R1", fixture.supply, fixture.output);
        fixture.add("inductor", "L1", fixture.output, fixture.middle);
        fixture.add("capacitor", "C1", fixture.middle, fixture.reference);
        const auto compiled = compile_electrical(
            request(fixture, {1 / (2 * std::numbers::pi * std::sqrt(.01 * 1e-5))}));
        REQUIRE(compiled.complete());
        const auto solved = solve_ac(*compiled.model());
        CHECK(solved.outcome() == AcSolveOutcome::Inconsistent);
        CHECK(solved.solution() == nullptr);
        REQUIRE(solved.points().front().metrics.rank);
        REQUIRE(solved.points().front().metrics.augmented_rank);
        CHECK(*solved.points().front().metrics.augmented_rank >
              *solved.points().front().metrics.rank);
    }
    SECTION("a nearly lossless resonance is ill conditioned") {
        auto fixture = test::linear_ac::fixture(1e-10, .01, 1e-5, true);
        fixture.add("resistor", "R1", fixture.supply, fixture.output);
        fixture.add("inductor", "L1", fixture.output, fixture.middle);
        fixture.add("capacitor", "C1", fixture.middle, fixture.reference);
        const auto compiled = compile_electrical(
            request(fixture, {1 / (2 * std::numbers::pi * std::sqrt(.01 * 1e-5))}));
        REQUIRE(compiled.complete());
        const auto solved = solve_ac(*compiled.model(), AcSolveOptions{1e-16, 1e-10});
        CHECK(solved.outcome() == AcSolveOutcome::IllConditioned);
        CHECK(solved.solution() == nullptr);
        REQUIRE(solved.points().front().metrics.reciprocal_condition);
        CHECK(*solved.points().front().metrics.reciprocal_condition < 1e-10);
    }
}
