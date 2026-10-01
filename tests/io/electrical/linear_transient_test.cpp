#include "../../../src/circuit/electrical/transient_solve_internal.hpp"
#include "support/linear_transient_fixture.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <numbers>
#include <volt/electrical/transient_solve.hpp>
#include <volt/io/electrical/transient_solve_io.hpp>

namespace {
using namespace volt;
using Fixture = test::linear_transient::Fixture;

Quantity seconds(double time) { return {UnitDimension::Time, time}; }

TransientSolveOptions options(double initial = 5e-5, bool tight = true, double minimum = 1e-12,
                              std::size_t trials = 200000, std::size_t steps = 100000) {
    return {seconds(minimum),
            seconds(initial),
            seconds(initial),
            trials,
            steps,
            tight ? 1e-7 : 1e-4,
            Quantity{UnitDimension::Voltage, tight ? 1e-9 : 1e-6},
            Quantity{UnitDimension::Current, tight ? 1e-12 : 1e-9}};
}

TransientRequest request(const Fixture &fixture, std::vector<double> times,
                         std::vector<std::pair<ComponentId, double>> initial,
                         std::optional<TransientWaveform> waveform = TransientWaveform{Quantity{
                             UnitDimension::Voltage, 1}},
                         bool reverse = false, bool current_source = false) {
    const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
    std::vector<TransientInitialState> states;
    for (const auto &[occurrence, value] : initial) {
        const auto &part = *input.circuit().get(occurrence).selected_library_part_ref();
        const auto &model = *input.part(occurrence)->electrical_model();
        const auto &element = model.elements().front();
        const auto capacitor = std::holds_alternative<CapacitanceElement>(element);
        states.emplace_back(
            input.occurrence(occurrence), part, ModelElementKey{"body"},
            capacitor ? TransientStorageKind::CapacitorVoltage
                      : TransientStorageKind::InductorCurrent,
            Quantity{capacitor ? UnitDimension::Voltage : UnitDimension::Current, value});
    }
    std::vector<TransientSource> sources;
    if (waveform) {
        const auto pair =
            reverse ? ElectricalNetPair{input.net(fixture.reference), input.net(fixture.supply)}
                    : ElectricalNetPair{input.net(fixture.supply), input.net(fixture.reference)};
        if (current_source)
            sources.emplace_back(
                TransientCurrentSource{ElectricalSourceKey{"drive"}, pair, *waveform});
        else
            sources.emplace_back(
                TransientVoltageSource{ElectricalSourceKey{"drive"}, pair, *waveform});
    }
    std::vector<Quantity> grid;
    for (const auto time : times)
        grid.push_back(seconds(time));
    return {ElectricalRequestKey{"test-transient"},
            input,
            input.net(fixture.reference),
            TransientTimeGrid{std::move(grid)},
            std::move(sources),
            {},
            {},
            std::move(states)};
}

CompiledElectricalModel compiled(const TransientRequest &req) {
    auto report = compile_electrical(req);
    INFO("Compiler diagnostics count: " << report.diagnostics().size());
    REQUIRE(report.complete());
    return *report.model();
}

double potential(const TransientSample &sample, const CompiledElectricalModel &model, NetId net) {
    for (const auto &node : model.nodes())
        if (const auto *origin = std::get_if<ElectricalNetOrigin>(&node.origin))
            if (std::ranges::find(origin->nets, net) != origin->nets.end())
                return sample.nodes.at(node.id.index()).potential.value();
    throw std::runtime_error{"Missing node"};
}

const TransientBranchResult &branch(const TransientSample &sample,
                                    const CompiledElectricalModel &model, ComponentId occurrence) {
    for (const auto &entry : model.branches())
        if (const auto *origin = std::get_if<ElectricalElementOrigin>(&entry.origin))
            if (origin->occurrence == occurrence)
                return sample.branches.at(entry.id.index());
    throw std::runtime_error{"Missing branch"};
}

void close(double actual, double expected, double scale, bool voltage = true) {
    const auto absolute = voltage ? 1e-8 : 1e-11;
    CHECK(std::abs(actual - expected) <= absolute + 1e-3 * scale);
}

void successful(const TransientSolveReport &report) {
    INFO("Outcome: " << static_cast<int>(report.outcome()));
    for (const auto &diagnostic : report.diagnostics())
        INFO(diagnostic.message());
    REQUIRE(report.success());
    REQUIRE(report.solution());
    for (const auto &evaluation : report.evaluations()) {
        REQUIRE(evaluation.outcome == TransientSolveOutcome::Success);
        REQUIRE(evaluation.metrics.rank);
        CHECK(*evaluation.metrics.rank == evaluation.metrics.coordinate_count);
        CHECK(*evaluation.metrics.reciprocal_condition >= 1e-12);
        CHECK(*evaluation.metrics.scaled_residual <= 1e-9);
        CHECK(*evaluation.metrics.voltage_error_ratio <= 1);
        CHECK(*evaluation.metrics.current_error_ratio <= 1);
    }
    for (const auto &half : report.accepted_half_steps())
        CHECK(half.normalized_error <= 1);
}
} // namespace

TEST_CASE("native transient analytical RC startup discharge and orientation") {
    for (const bool free : {false, true})
        for (const bool reversed : {false, true}) {
            auto fixture = test::linear_transient::fixture(1000, .1, 1e-6, false, !free);
            const auto resistor = fixture.add("resistor", "R1", fixture.supply,
                                              free ? fixture.reference : fixture.output);
            const auto capacitor = fixture.add(
                "capacitor", "C1", free ? fixture.supply : fixture.output, fixture.reference);
            const auto req = request(
                fixture, {0, .00025, .001, .002, .003}, {{capacitor, free ? 1.0 : 0.0}},
                free ? std::nullopt
                     : std::optional{TransientWaveform{Quantity{UnitDimension::Voltage, 1}}},
                reversed);
            const auto model = compiled(req);
            const auto report = solve_transient(model, options());
            successful(report);
            const auto sign = reversed && !free ? -1.0 : 1.0;
            for (const auto &sample : report.solution()->samples()) {
                const auto t = sample.time.value();
                const auto v = free ? std::exp(-t / .001) : sign * (1 - std::exp(-t / .001));
                close(branch(sample, model, capacitor).voltage.value(), v, 1);
                const auto i = (free ? -1.0 : sign) * .001 * std::exp(-t / .001);
                close(branch(sample, model, capacitor).current.value(), i, .001, false);
                close(branch(sample, model, resistor).current.value(), free ? -i : i, .001, false);
            }
            REQUIRE(report.initial_derivatives().size() == 1);
            CHECK(std::abs(report.initial_derivatives().front().derivative -
                           (free ? -1000 : sign * 1000)) < 1e-9);
        }
}

TEST_CASE("native transient analytical RL startup and free decay") {
    for (const bool free : {false, true}) {
        auto fixture = test::linear_transient::fixture(100, .1, 1e-6, false, !free);
        const auto resistor = fixture.add("resistor", "R1", fixture.supply,
                                          free ? fixture.reference : fixture.output);
        const auto inductor = fixture.add("inductor", "L1", free ? fixture.supply : fixture.output,
                                          fixture.reference);
        const auto model = compiled(
            request(fixture, {0, .00025, .001, .002, .003}, {{inductor, free ? .01 : 0}},
                    free ? std::nullopt
                         : std::optional{TransientWaveform{Quantity{UnitDimension::Voltage, 1}}}));
        const auto report = solve_transient(model, options());
        successful(report);
        for (const auto &sample : report.solution()->samples()) {
            const auto decay = std::exp(-sample.time.value() / .001);
            const auto i = .01 * (free ? decay : 1 - decay);
            close(branch(sample, model, inductor).current.value(), i, .01, false);
            close(branch(sample, model, resistor).current.value(), free ? -i : i, .01, false);
            close(branch(sample, model, inductor).voltage.value(), free ? -decay : decay, 1);
        }
        CHECK(std::abs(report.initial_derivatives().front().derivative - (free ? -10 : 10)) < 1e-9);
    }
}

TEST_CASE("native transient analytical RLC free response and current signs") {
    auto fixture = test::linear_transient::fixture(10, .01, 1e-5);
    const auto r = fixture.add("resistor", "R1", fixture.supply, fixture.output);
    const auto l = fixture.add("inductor", "L1", fixture.output, fixture.reference);
    const auto c = fixture.add("capacitor", "C1", fixture.supply, fixture.reference);
    const auto model =
        compiled(request(fixture, {0, .00025, .001, .002, .003}, {{l, 0}, {c, 1}}, std::nullopt));
    const auto report = solve_transient(model, options());
    successful(report);
    constexpr double alpha = 500;
    const auto omega = std::sqrt(1 / (.01 * 1e-5) - alpha * alpha);
    for (const auto &sample : report.solution()->samples()) {
        const auto t = sample.time.value(), decay = std::exp(-alpha * t);
        const auto voltage = decay * (std::cos(omega * t) + alpha / omega * std::sin(omega * t));
        const auto current = decay * std::sin(omega * t) / (.01 * omega);
        close(branch(sample, model, c).voltage.value(), voltage, 1);
        close(branch(sample, model, l).current.value(), current, 1 / std::sqrt(.01 / 1e-5), false);
        close(branch(sample, model, r).current.value(), current, 1 / std::sqrt(.01 / 1e-5), false);
        close(branch(sample, model, c).current.value(), -current, 1 / std::sqrt(.01 / 1e-5), false);
    }
}

TEST_CASE("native transient ramp hold fall and exact off-grid knot landing") {
    auto fixture = test::linear_transient::fixture();
    fixture.add("resistor", "R1", fixture.supply, fixture.output);
    const auto c = fixture.add("capacitor", "C1", fixture.output, fixture.reference);
    const auto waveform = TransientWaveform{
        std::vector<TransientWaveformKnot>{{seconds(0), Quantity{UnitDimension::Voltage, 0}},
                                           {seconds(.001), Quantity{UnitDimension::Voltage, 1}},
                                           {seconds(.0017), Quantity{UnitDimension::Voltage, 1}},
                                           {seconds(.0027), Quantity{UnitDimension::Voltage, 0}}}};
    const auto model =
        compiled(request(fixture, {0, .00025, .001, .002, .003}, {{c, 0}}, waveform));
    const auto report = solve_transient(model, options());
    successful(report);
    for (const auto &sample : report.solution()->samples()) {
        const auto t = sample.time.value();
        const auto rise = [](double value) { return value / .001 - 1 + std::exp(-value / .001); };
        double expected;
        if (t <= .001)
            expected = rise(t);
        else if (t <= .0017)
            expected = 1 + (rise(.001) - 1) * std::exp(-(t - .001) / .001);
        else {
            const auto at_fall = 1 + (rise(.001) - 1) * std::exp(-.0007 / .001);
            const auto dt = std::min(t - .0017, .001);
            const auto fallen = 1 - dt / .001 + 1 + (at_fall - 2) * std::exp(-dt / .001);
            expected = t <= .0027 ? fallen : fallen * std::exp(-(t - .0027) / .001);
        }
        close(branch(sample, model, c).voltage.value(), expected, 1);
    }
    REQUIRE(report.solution()->samples().size() == 5);
    for (const auto knot : {.001, .0017, .0027}) {
        CHECK(std::ranges::any_of(report.accepted_half_steps(),
                                  [&](const auto &half) { return half.time.value() == knot; }));
        for (const auto &half : report.accepted_half_steps())
            CHECK_FALSE(
                (half.time.value() - half.duration.value() < knot && half.time.value() > knot));
    }
}

TEST_CASE(
    "native transient internal production substep first-order convergence and default rejection") {
    for (const bool inductive : {false, true})
        for (const bool free : {false, true}) {
            auto fixture =
                test::linear_transient::fixture(inductive ? 100 : 1000, .1, 1e-6, false, !free);
            fixture.add("resistor", "R1", fixture.supply,
                        free ? fixture.reference : fixture.output);
            const auto storage =
                fixture.add(inductive ? "inductor" : "capacitor", "S1",
                            free ? fixture.supply : fixture.output, fixture.reference);
            const auto scale = inductive ? .01 : 1.0;
            const auto model = compiled(request(
                fixture, {0, .001}, {{storage, free ? scale : 0}},
                free ? std::nullopt
                     : std::optional{TransientWaveform{Quantity{UnitDimension::Voltage, 1}}}));
            auto initialized = detail::initialize_transient(model, options());
            REQUIRE(initialized.sample);
            const auto coordinate = [&](const TransientSample &sample) {
                const auto &observation = branch(sample, model, storage);
                return inductive ? observation.current.value() / scale
                                 : observation.voltage.value();
            };
            const auto full =
                detail::transient_be_substep(model, options(), *initialized.sample, seconds(.001));
            const auto half =
                detail::transient_be_substep(model, options(), *initialized.sample, seconds(.0005));
            REQUIRE(full.sample);
            REQUIRE(half.sample);
            const auto two =
                detail::transient_be_substep(model, options(), *half.sample, seconds(.001));
            REQUIRE(two.sample);
            CHECK(std::abs(coordinate(*full.sample) - .5) < 1e-12);
            CHECK(std::abs(coordinate(*two.sample) - (free ? 4.0 / 9 : 5.0 / 9)) < 1e-12);
            const auto adaptive = solve_transient(model, options(.001, false));
            successful(adaptive);
            REQUIRE(adaptive.evaluations().size() >= 4);
            REQUIRE(adaptive.evaluations()[3].normalized_error);
            CHECK(*adaptive.evaluations()[3].normalized_error > 1);
            CHECK_FALSE(adaptive.evaluations()[3].trial_accepted);
            CHECK(adaptive.trial_count() > adaptive.accepted_step_count());
            std::vector<double> errors;
            for (const auto count : {10, 20, 40}) {
                auto state = *initialized.sample;
                for (int index = 1; index <= count; ++index) {
                    auto substep = detail::transient_be_substep(model, options(), state,
                                                                seconds(.001 * index / count));
                    REQUIRE(substep.sample);
                    state = std::move(*substep.sample);
                }
                errors.push_back(
                    std::abs(coordinate(state) - (free ? std::exp(-1) : 1 - std::exp(-1))));
            }
            for (std::size_t index = 1; index < errors.size(); ++index) {
                CHECK(errors[index - 1] / errors[index] >= 1.7);
                CHECK(errors[index - 1] / errors[index] <= 2.3);
            }
        }
}

TEST_CASE("native transient every actual half step obeys signed discrete energy identity") {
    for (const bool free : {false, true}) {
        auto fixture = test::linear_transient::fixture(10, .01, 1e-5);
        fixture.add("resistor", "R1", fixture.supply, fixture.output);
        const auto l = fixture.add("inductor", "L1", fixture.output, fixture.reference);
        const auto c = fixture.add("capacitor", "C1", fixture.supply, fixture.reference);
        // Drive through a current source to keep the frozen-state projector unique.
        const auto model = compiled(
            request(fixture, {0, .002}, {{l, 0}, {c, 1}},
                    free ? std::nullopt
                         : std::optional{TransientWaveform{Quantity{UnitDimension::Current, .002}}},
                    false, true));
        const auto policy = options(1e-4);
        const auto adaptive = solve_transient(model, policy);
        successful(adaptive);
        auto initialized = detail::initialize_transient(model, policy);
        REQUIRE(initialized.sample);
        auto previous = *initialized.sample;
        for (const auto &half : adaptive.accepted_half_steps()) {
            const auto current = detail::transient_be_substep(model, policy, previous, half.time);
            REQUIRE(current.sample);
            long double delta = 0, resistor_work = 0, damping = 0, source_work = 0;
            for (const auto &entry : model.branches()) {
                const auto &before = previous.branches.at(entry.id.index());
                const auto &after = current.sample->branches.at(entry.id.index());
                const auto v0 = before.voltage.value(), v1 = after.voltage.value();
                const auto i0 = before.current.value(), i1 = after.current.value();
                std::visit(
                    [&](const auto &law) {
                        using Law = std::decay_t<decltype(law)>;
                        if constexpr (std::same_as<Law, CapacitanceElement>) {
                            const auto coefficient = .5L * law.parameter().nominal().value();
                            delta += coefficient * (v1 * v1 - v0 * v0);
                            damping += coefficient * (v1 - v0) * (v1 - v0);
                        } else if constexpr (std::same_as<Law, InductanceElement>) {
                            const auto coefficient = .5L * law.parameter().nominal().value();
                            delta += coefficient * (i1 * i1 - i0 * i0);
                            damping += coefficient * (i1 - i0) * (i1 - i0);
                        } else if constexpr (std::same_as<Law, ResistanceElement>)
                            resistor_work +=
                                half.duration.value() * law.parameter().nominal().value() * i1 * i1;
                        else
                            source_work += half.duration.value() * v1 * i1;
                    },
                    entry.law);
            }
            CHECK(std::abs(delta + resistor_work + damping + source_work) <= 1e-8L * 1e-5L);
            CHECK(resistor_work >= 0);
            CHECK(damping >= 0);
            previous = *current.sample;
        }
    }
}

TEST_CASE("native transient tight lossless LC energy and phase with default damping disclosure") {
    auto fixture = test::linear_transient::fixture(0, .01, 1e-5);
    fixture.add("resistor", "R1", fixture.supply, fixture.output);
    const auto l = fixture.add("inductor", "L1", fixture.output, fixture.reference);
    const auto c = fixture.add("capacitor", "C1", fixture.supply, fixture.reference);
    const auto period = 2 * std::numbers::pi * std::sqrt(.01 * 1e-5);
    const auto model = compiled(request(fixture, {0, period}, {{l, 0}, {c, 1}}, std::nullopt));
    for (const bool tight : {false, true}) {
        const auto report = solve_transient(model, options(period / 20, tight));
        successful(report);
        const auto &final = report.solution()->samples().back();
        const auto v = branch(final, model, c).voltage.value();
        const auto i = branch(final, model, l).current.value();
        const auto energy_ratio = v * v + .01 / 1e-5 * i * i;
        const auto phase_error = std::abs(std::atan2(i * std::sqrt(.01 / 1e-5), v));
        INFO("Native LC tight=" << tight << " energy_loss=" << 1 - energy_ratio << " phase_error="
                                << phase_error << " trials=" << report.trial_count()
                                << " accepted=" << report.accepted_step_count()
                                << " solves=" << report.solve_count()
                                << " factorizations=" << report.factorization_count());
        if (tight) {
            CHECK(1 - energy_ratio <= .01);
            CHECK(phase_error <= .01);
        } else {
            CHECK(1 - energy_ratio > .01);
            CHECK(1 - energy_ratio < .10);
        }
    }
}

TEST_CASE("native transient stiff two-timescale RC remains finite and accurate") {
    auto fixture = test::linear_transient::fixture(1000, .1, 1e-6, true);
    fixture.add("resistor", "Rslow", fixture.supply, fixture.output);
    fixture.add("resistor_fast", "Rfast", fixture.supply, fixture.middle);
    const auto slow = fixture.add("capacitor", "Cslow", fixture.output, fixture.reference);
    const auto fast = fixture.add("capacitor", "Cfast", fixture.middle, fixture.reference);
    const auto model =
        compiled(request(fixture, {0, 1e-6, 1e-5, .001, .003}, {{slow, 0}, {fast, 0}}));
    const auto report = solve_transient(model, options(1e-4));
    successful(report);
    for (const auto &sample : report.solution()->samples()) {
        close(branch(sample, model, slow).voltage.value(),
              1 - std::exp(-sample.time.value() / .001), 1);
        close(branch(sample, model, fast).voltage.value(),
              1 - std::exp(-sample.time.value() / 1e-6), 1);
    }
}

TEST_CASE(
    "native transient frozen projector distinguishes incompatible from unsupported constraints") {
    SECTION("parallel capacitors") {
        for (const bool compatible : {false, true}) {
            auto fixture = test::linear_transient::fixture(1000, .1, 1e-6, false, false);
            fixture.add("resistor", "R1", fixture.supply, fixture.reference);
            const auto c1 = fixture.add("capacitor", "C1", fixture.supply, fixture.reference);
            const auto c2 = fixture.add("capacitor", "C2", fixture.supply, fixture.reference);
            const auto model = compiled(
                request(fixture, {0, .001}, {{c1, 1}, {c2, compatible ? 1.0 : 0.0}}, std::nullopt));
            const auto report = solve_transient(model, options());
            CHECK(report.outcome() ==
                  (compatible ? TransientSolveOutcome::UnsupportedInitializationTopology
                              : TransientSolveOutcome::InconsistentInitialState));
            CHECK(report.solution() == nullptr);
            REQUIRE(report.evaluations().size() == 1);
            CHECK(report.last_accepted_time().value() == 0);
            REQUIRE(report.evaluations().front().metrics.rank);
            REQUIRE(report.evaluations().front().metrics.augmented_rank);
            CHECK_FALSE(report.diagnostics().front().entities().empty());
        }
    }
    SECTION("ideal supply-rail capacitor") {
        for (const bool compatible : {false, true}) {
            auto fixture = test::linear_transient::fixture(1000, .1, 1e-6, false, false);
            const auto c = fixture.add("capacitor", "C1", fixture.supply, fixture.reference);
            const auto model = compiled(request(fixture, {0, .001}, {{c, compatible ? 1.0 : 0.0}}));
            const auto report = solve_transient(model, options());
            CHECK(report.outcome() ==
                  (compatible ? TransientSolveOutcome::UnsupportedInitializationTopology
                              : TransientSolveOutcome::InconsistentInitialState));
            CHECK(report.solution() == nullptr);
        }
    }
}

TEST_CASE("native transient resistor-only samples have no invented integration metrics") {
    auto fixture = test::linear_transient::fixture(1000, .1, 1e-6, false, false);
    fixture.add("resistor", "R1", fixture.supply, fixture.reference);
    const auto waveform = TransientWaveform{
        std::vector<TransientWaveformKnot>{{seconds(0), Quantity{UnitDimension::Voltage, 0}},
                                           {seconds(.001), Quantity{UnitDimension::Voltage, 1}}}};
    const auto model = compiled(request(fixture, {0, .00025, .001, .002}, {}, waveform));
    const auto report = solve_transient(model, options());
    successful(report);
    CHECK(report.trial_count() == 0);
    CHECK(report.accepted_step_count() == 0);
    CHECK(report.accepted_half_steps().empty());
    CHECK(report.initial_derivatives().empty());
    CHECK(report.solve_count() == 4);
    REQUIRE(report.evaluations().size() == 4);
    for (const auto &sample : report.solution()->samples())
        close(potential(sample, model, fixture.supply), std::min(sample.time.value() / .001, 1.0),
              1);
    const auto encoded = nlohmann::json::parse(io::write_transient_solve_report(report));
    CHECK(encoded.at("accepted_half_steps").empty());
    CHECK(encoded.at("solution").at("samples").size() == 4);
    const auto repeated = solve_transient(model, options());
    REQUIRE(repeated.solution());
    CHECK(io::write_transient_solution(*report.solution()) ==
          io::write_transient_solution(*repeated.solution()));
}

TEST_CASE("native transient strict progress coefficient minimum and work failures publish no "
          "partial solution") {
    auto fixture = test::linear_transient::fixture();
    fixture.add("resistor", "R1", fixture.supply, fixture.output);
    const auto c = fixture.add("capacitor", "C1", fixture.output, fixture.reference);
    const auto model = compiled(request(fixture, {0, .0001, .001}, {{c, 0}}));
    SECTION("explicit rejected-trial exhaustion") {
        const auto report = solve_transient(model, options(.001, false, 1e-12, 1));
        CHECK(report.outcome() == TransientSolveOutcome::WorkLimit);
        CHECK(report.solution() == nullptr);
        CHECK(report.last_accepted_time().value() == 0);
        CHECK(report.trial_count() == 1);
    }
    SECTION("accepted work exhaustion retains last accepted time only") {
        const auto report = solve_transient(model, options(1e-6, false, 1e-12, 1000, 1));
        CHECK(report.outcome() == TransientSolveOutcome::WorkLimit);
        CHECK(report.solution() == nullptr);
        CHECK(report.last_accepted_time().value() > 0);
        CHECK(report.last_accepted_time().value() < .0001);
        CHECK(nlohmann::json::parse(io::write_transient_solve_report(report))
                  .at("solution")
                  .is_null());
    }
    SECTION("minimum step cannot accept requested local error") {
        const auto report = solve_transient(model, options(.001, false, .001));
        CHECK(report.outcome() == TransientSolveOutcome::StepLimit);
        CHECK(report.solution() == nullptr);
    }
    SECTION("representable horizon has no strict midpoint") {
        const auto tiny = std::numeric_limits<double>::denorm_min();
        const auto tiny_model = compiled(request(fixture, {0, tiny}, {{c, 0}}));
        const auto report = solve_transient(tiny_model, options(tiny, false, tiny));
        CHECK(report.outcome() == TransientSolveOutcome::StepLimit);
        CHECK(report.solution() == nullptr);
    }
    SECTION("storage companion coefficient overflows") {
        const auto tiny = 1e-320;
        const auto tiny_model = compiled(request(fixture, {0, tiny}, {{c, 0}}));
        const auto report = solve_transient(tiny_model, options(tiny, false, tiny));
        CHECK(report.outcome() == TransientSolveOutcome::NumericalFailure);
        CHECK(report.solution() == nullptr);
    }
    SECTION("rounded adjacent samples fail midpoint after an accepted boundary") {
        const auto next = std::nextafter(.001, 1.0);
        const auto adjacent = compiled(request(fixture, {0, .001, next}, {{c, 0}}));
        const auto report = solve_transient(adjacent, options());
        CHECK(report.outcome() == TransientSolveOutcome::StepLimit);
        CHECK(report.solution() == nullptr);
        CHECK(report.last_accepted_time().value() == .001);
    }
}

TEST_CASE("native transient solve options validate physical dimensions and finite policy") {
    CHECK_THROWS(TransientSolveOptions(Quantity{UnitDimension::Voltage, 1e-6}, seconds(1e-5),
                                       seconds(1e-4), 10, 10));
    CHECK_THROWS(options(1e-5, true, 1e-4));
    CHECK_THROWS(options(1e-5, true, 1e-12, 0));
    CHECK_THROWS(TransientSolveOptions(seconds(1e-6), seconds(1e-5), seconds(1e-4), 10, 10, 0));
    CHECK_THROWS(TransientSolveOptions(seconds(1e-6), seconds(1e-5), seconds(1e-4), 10, 10, 1));
    CHECK_THROWS(TransientSolveOptions(seconds(1e-6), seconds(1e-5), seconds(1e-4), 10, 10,
                                       std::numeric_limits<double>::quiet_NaN()));
    CHECK_THROWS(TransientSolveOptions(seconds(1e-6), seconds(1e-5), seconds(1e-4), 10, 10, 1e-4,
                                       Quantity{UnitDimension::Current, 1e-6}));
}

TEST_CASE("native transient matches independently authored ngspice 46 UIC trap oracle") {
    const auto corpus = nlohmann::json::parse(std::ifstream{
        std::string{VOLT_TEST_FIXTURE_DIR} + "/linear_transient/ngspice-46-results.json"});
    REQUIRE(corpus.at("version") == "46");
    REQUIRE(corpus.at("method") == "trap");
    for (const auto &entry : corpus.at("cases")) {
        const auto id = entry.at("case").get<std::string>();
        INFO(id);
        const auto inductive = id.starts_with("rl_");
        const auto rlc = id == "rlc_free";
        const auto free = id == "rc_discharge" || id == "rl_decay" || rlc;
        auto fixture =
            test::linear_transient::fixture(inductive ? 100
                                            : rlc     ? 10
                                                      : 1000,
                                            rlc ? .01 : .1, rlc ? 1e-5 : 1e-6, false, !free || rlc);
        std::vector<std::pair<ComponentId, double>> initial;
        std::optional<ComponentId> inductor;
        fixture.add("resistor", "R1", fixture.supply,
                    free && !rlc ? fixture.reference : fixture.output);
        if (rlc || inductive) {
            inductor = fixture.add("inductor", "L1", rlc || !free ? fixture.output : fixture.supply,
                                   fixture.reference);
            initial.emplace_back(*inductor, id == "rl_decay" ? .01 : 0);
        }
        if (!inductive) {
            const auto capacitor =
                fixture.add("capacitor", "C1", rlc || free ? fixture.supply : fixture.output,
                            fixture.reference);
            initial.emplace_back(capacitor, free ? 1 : 0);
        }
        auto waveform = free
                            ? std::optional<TransientWaveform>{}
                            : std::optional{TransientWaveform{Quantity{UnitDimension::Voltage, 1}}};
        if (id == "rc_ramp")
            waveform = TransientWaveform{std::vector<TransientWaveformKnot>{
                {seconds(0), Quantity{UnitDimension::Voltage, 0}},
                {seconds(.001), Quantity{UnitDimension::Voltage, 1}},
                {seconds(.002), Quantity{UnitDimension::Voltage, 1}},
                {seconds(.0025), Quantity{UnitDimension::Voltage, 0}}}};
        std::vector<double> times{0};
        for (const auto &sample : entry.at("samples"))
            times.push_back(sample.at("time_s").get<double>());
        const auto model = compiled(request(fixture, times, initial, waveform));
        const auto report = solve_transient(model, options());
        successful(report);
        for (std::size_t index = 0; index < entry.at("samples").size(); ++index) {
            const auto &expected = entry.at("samples")[index];
            const auto &sample = report.solution()->samples()[index + 1];
            CHECK(sample.time.value() == expected.at("time_s").get<double>());
            const auto observed_net = free ? fixture.supply : fixture.output;
            const auto &values = expected.at("values");
            close(potential(sample, model, observed_net), values[0].get<double>(), 1);
            if (inductive || rlc)
                close(branch(sample, model, *inductor).current.value(), values[1].get<double>(),
                      rlc ? std::sqrt(1e-5 / .01) : .01, false);
            if (!free) {
                const auto source_current = sample.branches.back().current.value();
                close(source_current, values[inductive ? 2 : 1].get<double>(),
                      inductive ? .01 : .001, false);
            }
        }
    }
}

TEST_CASE("native transient observes authored composite private storage without substitutions") {
    auto fixture = test::electrical_compilation::make_fixture();
    const auto occurrence = fixture.circuit->instantiate_component(
        fixture.composite.component,
        ComponentInstanceSpec{.reference = ReferenceDesignator{"Ccomposite"}});
    fixture.circuit->update(
        occurrence,
        SelectLibraryPart{fixture.library, fixture.library.require(fixture.composite.part)});
    fixture.circuit->connect(fixture.supply,
                             queries::pin_by_number(*fixture.circuit, occurrence, "1").value());
    fixture.circuit->connect(fixture.reference,
                             queries::pin_by_number(*fixture.circuit, occurrence, "2").value());
    const auto input = test::electrical_compilation::input(fixture);
    const auto &part = *input.circuit().get(occurrence).selected_library_part_ref();
    const auto req = TransientRequest{
        ElectricalRequestKey{"composite"},
        input,
        input.net(fixture.reference),
        TransientTimeGrid{{seconds(0), seconds(1e-6), seconds(2e-6)}},
        {TransientVoltageSource{
            ElectricalSourceKey{"drive"},
            ElectricalNetPair{input.net(fixture.supply), input.net(fixture.reference)},
            TransientWaveform{Quantity{UnitDimension::Voltage, 5}}}},
        {},
        {},
        {TransientInitialState{input.occurrence(occurrence), part, ModelElementKey{"storage"},
                               TransientStorageKind::CapacitorVoltage,
                               Quantity{UnitDimension::Voltage, 0}},
         TransientInitialState{input.occurrence(occurrence), part, ModelElementKey{"esl"},
                               TransientStorageKind::InductorCurrent,
                               Quantity{UnitDimension::Current, 0}}}};
    const auto model = compiled(req);
    const auto report = solve_transient(model, options(1e-7, true, 1e-15));
    successful(report);
    REQUIRE(report.initial_derivatives().size() == 2);
    CHECK(std::ranges::count_if(model.nodes(), [](const auto &node) {
              return std::holds_alternative<ElectricalInternalNodeOrigin>(node.origin);
          }) == 2);
    for (const auto &entry : model.branches()) {
        const auto *origin = std::get_if<ElectricalElementOrigin>(&entry.origin);
        if (!origin || origin->occurrence != occurrence)
            continue;
        const auto &initial = report.solution()->samples().front().branches.at(entry.id.index());
        if (origin->element == ModelElementKey{"storage"})
            CHECK(initial.voltage.value() == 0);
        if (origin->element == ModelElementKey{"esl"})
            CHECK(initial.current.value() == 0);
    }
}

TEST_CASE("native transient rejects nonfinite normalization and storage arithmetic") {
    SECTION("finite observations overflow the temporal denominator") {
        auto fixture = test::linear_transient::fixture(1e300);
        fixture.add("resistor", "R1", fixture.supply, fixture.output);
        const auto c = fixture.add("capacitor", "C1", fixture.output, fixture.reference);
        const auto model =
            compiled(request(fixture, {0, 1e294}, {{c, 0}},
                             TransientWaveform{Quantity{UnitDimension::Voltage, 1e300}}));
        const auto policy = TransientSolveOptions{
            seconds(1),
            seconds(1e294),
            seconds(1e294),
            10,
            10,
            .9,
            Quantity{UnitDimension::Voltage, std::numeric_limits<double>::max()},
            Quantity{UnitDimension::Current, 1}};
        const auto report = solve_transient(model, policy);
        INFO((report.diagnostics().empty() ? "No diagnostic"
                                           : report.diagnostics().front().message()));
        CHECK(report.outcome() == TransientSolveOutcome::NumericalFailure);
        CHECK(report.solution() == nullptr);
        CHECK(report.last_accepted_time().value() == 0);
        REQUIRE(report.evaluations().size() == 4);
        for (const auto &evaluation : report.evaluations())
            CHECK(evaluation.outcome == TransientSolveOutcome::Success);
        CHECK_FALSE(report.evaluations().back().normalized_error);
    }
    SECTION("positive storage companion underflows to zero") {
        auto fixture = test::linear_transient::fixture(
            1000, .1, std::numeric_limits<double>::denorm_min(), false, false);
        fixture.add("resistor", "R1", fixture.supply, fixture.reference);
        const auto c = fixture.add("capacitor", "C1", fixture.supply, fixture.reference);
        const auto model = compiled(request(fixture, {0, 1e10}, {{c, 0}}, std::nullopt));
        const auto report = solve_transient(model, options(1e10));
        CHECK(report.outcome() == TransientSolveOutcome::NumericalFailure);
        CHECK(report.solution() == nullptr);
        REQUIRE(report.evaluations().size() == 2);
        CHECK(report.evaluations().front().outcome == TransientSolveOutcome::Success);
    }
    SECTION("initial continuous derivative cannot be reconstructed finitely") {
        auto fixture =
            test::linear_transient::fixture(1000, .1, std::numeric_limits<double>::denorm_min());
        fixture.add("resistor", "R1", fixture.supply, fixture.output);
        const auto c = fixture.add("capacitor", "C1", fixture.output, fixture.reference);
        const auto model = compiled(request(fixture, {0, .001}, {{c, 0}}));
        const auto report = solve_transient(model, options());
        CHECK(report.outcome() == TransientSolveOutcome::NumericalFailure);
        CHECK(report.solution() == nullptr);
        REQUIRE(report.evaluations().size() == 1);
    }
}

TEST_CASE("native transient refuses floating and nonunique or contradictory ideal coordinates") {
    SECTION("unconnected authored net has no unique potential") {
        auto fixture = test::linear_transient::fixture();
        fixture.add("resistor", "R1", fixture.supply, fixture.reference);
        const auto model = compiled(request(fixture, {0, .001}, {}));
        const auto report = solve_transient(model, options());
        CHECK(report.outcome() == TransientSolveOutcome::UnsupportedInitializationTopology);
        CHECK(report.solution() == nullptr);
        CHECK(report.evaluations().front().metrics.rank <
              report.evaluations().front().metrics.coordinate_count);
    }
    SECTION("parallel zero-ohm branches have nonunique currents") {
        auto fixture = test::linear_transient::fixture(0);
        fixture.add("resistor", "R1", fixture.supply, fixture.output);
        fixture.add("resistor", "R2", fixture.supply, fixture.output);
        const auto c = fixture.add("capacitor", "C1", fixture.output, fixture.reference);
        const auto model = compiled(request(fixture, {0, .001}, {{c, 0}}, std::nullopt));
        const auto report = solve_transient(model, options());
        CHECK(report.outcome() == TransientSolveOutcome::UnsupportedInitializationTopology);
        CHECK(report.solution() == nullptr);
    }
    SECTION("contradictory voltage-source loop passes pair checks but fails initializer") {
        auto fixture = test::linear_transient::fixture();
        const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
        const auto source = [&](std::string key, NetId from, NetId to, double value) {
            return TransientVoltageSource{
                ElectricalSourceKey{std::move(key)},
                ElectricalNetPair{input.net(from), input.net(to)},
                TransientWaveform{Quantity{UnitDimension::Voltage, value}}};
        };
        const auto model =
            compiled(TransientRequest{ElectricalRequestKey{"loop"},
                                      input,
                                      input.net(fixture.reference),
                                      TransientTimeGrid{{seconds(0), seconds(.001)}},
                                      {source("a", fixture.supply, fixture.reference, 1),
                                       source("b", fixture.output, fixture.reference, 1),
                                       source("c", fixture.supply, fixture.output, 1)}});
        const auto report = solve_transient(model, options());
        CHECK(report.outcome() == TransientSolveOutcome::InconsistentInitialState);
        CHECK(report.solution() == nullptr);
        CHECK(report.evaluations().front().metrics.augmented_rank >
              report.evaluations().front().metrics.rank);
    }
}

TEST_CASE("native transient overflowing voltage probes retain timed numerical failure") {
    for (const bool ramp : {false, true}) {
        auto fixture = test::linear_transient::fixture(5e307, .1, 1e-6, true);
        const auto negative_midpoint =
            fixture.circuit->add_net(NetSpec{.name = NetName{"negative-midpoint"}});
        fixture.add("resistor", "RP1", fixture.supply, fixture.output);
        fixture.add("resistor", "RP2", fixture.output, fixture.reference);
        fixture.add("resistor", "RN1", fixture.middle, negative_midpoint);
        fixture.add("resistor", "RN2", negative_midpoint, fixture.reference);
        const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
        const auto drive = ramp ? TransientWaveform{std::vector<TransientWaveformKnot>{
                                      {seconds(0), Quantity{UnitDimension::Current, 0}},
                                      {seconds(1), Quantity{UnitDimension::Current, .9}}}}
                                : TransientWaveform{Quantity{UnitDimension::Current, .9}};
        const auto sources = std::vector<TransientSource>{
            TransientCurrentSource{
                ElectricalSourceKey{"positive"},
                ElectricalNetPair{input.net(fixture.reference), input.net(fixture.supply)}, drive},
            TransientCurrentSource{
                ElectricalSourceKey{"negative"},
                ElectricalNetPair{input.net(fixture.middle), input.net(fixture.reference)}, drive}};
        for (const bool observe : {false, true}) {
            std::vector<DcProbe> probes;
            if (observe)
                probes.emplace_back(DcVoltageProbe{
                    ElectricalProbeKey{"rails"},
                    ElectricalNetPair{input.net(fixture.supply), input.net(fixture.middle)}});
            const auto model =
                compiled(TransientRequest{ElectricalRequestKey{"probe-overflow"},
                                          input,
                                          input.net(fixture.reference),
                                          TransientTimeGrid{std::vector{seconds(0), seconds(1)}},
                                          sources,
                                          std::move(probes),
                                          {},
                                          {}});
            const auto report = solve_transient(model, options());
            if (!observe) {
                successful(report);
                const auto &sample = report.solution()->samples().back();
                CHECK(potential(sample, model, fixture.supply) > 8e307);
                CHECK(potential(sample, model, fixture.middle) < -8e307);
                for (const auto &node : sample.nodes)
                    CHECK(std::isfinite(node.potential.value()));
                for (const auto &entry : sample.branches) {
                    CHECK(std::isfinite(entry.voltage.value()));
                    CHECK(std::isfinite(entry.current.value()));
                    CHECK(std::isfinite(entry.power.value()));
                }
            } else {
                CHECK(report.outcome() == TransientSolveOutcome::NumericalFailure);
                CHECK_FALSE(report.solution());
                CHECK(report.last_accepted_time().value() == 0);
                REQUIRE(report.evaluations().size() == (ramp ? 2 : 1));
                const auto &failed = report.evaluations().back();
                CHECK(failed.time.value() == (ramp ? 1 : 0));
                CHECK(failed.outcome == TransientSolveOutcome::NumericalFailure);
                REQUIRE_FALSE(failed.diagnostics.empty());
                const auto &diagnostic = failed.diagnostics.back();
                CHECK(diagnostic.code().value() == "TRANSIENT_SOLVE_NUMERICAL_FAILURE");
                CHECK(diagnostic.message().find("rails") != std::string::npos);
                CHECK(std::ranges::find(diagnostic.entities(), EntityRef::net(fixture.supply)) !=
                      diagnostic.entities().end());
                CHECK(std::ranges::find(diagnostic.entities(), EntityRef::net(fixture.middle)) !=
                      diagnostic.entities().end());
                CHECK_FALSE(io::write_transient_solve_report(report).empty());
            }
        }
    }
}
