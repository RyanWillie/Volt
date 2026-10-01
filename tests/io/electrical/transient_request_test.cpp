#include "support/dc_request_fixture.hpp"
#include "support/electrical_compilation_fixture.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <volt/electrical/dc_solve.hpp>
#include <volt/electrical/transient_request.hpp>
#include <volt/io/electrical/compiled_electrical_model_io.hpp>

namespace {
using namespace volt;

Quantity seconds(double value) { return Quantity{UnitDimension::Time, value}; }

Quantity volts(double value) { return Quantity{UnitDimension::Voltage, value}; }

Quantity amps(double value) { return Quantity{UnitDimension::Current, value}; }

std::vector<TransientInitialState> composite_state(const ElectricalInput &input,
                                                   double voltage = 1.0) {
    const auto occurrence = input.occurrence(ComponentId{0});
    const auto part = *input.circuit().get(ComponentId{0}).selected_library_part_ref();
    return {{occurrence, part, ModelElementKey{"storage"}, TransientStorageKind::CapacitorVoltage,
             volts(voltage)},
            {occurrence, part, ModelElementKey{"esl"}, TransientStorageKind::InductorCurrent,
             amps(0.0)}};
}

TransientRequest composite_request(const ElectricalInput &input, TransientInitialConditions initial,
                                   std::vector<TransientSource> sources = {}) {
    const auto dc = test::dc_request::complete_request(input);
    auto exclusions = dc.exclusions();
    exclusions[0] =
        DcOccurrenceExclusion{input.occurrence(ComponentId{1}), DcOutsideAnalysisExclusion{}};
    return TransientRequest{ElectricalRequestKey{"transient"},
                            input,
                            dc.reference(),
                            TransientTimeGrid{{seconds(0.0), seconds(0.25), seconds(1.0)}},
                            std::move(sources),
                            {},
                            exclusions,
                            std::move(initial)};
}
} // namespace

TEST_CASE("Transient output grid enforces finite typed seconds zero origin and strict progress") {
    CHECK_THROWS(TransientTimeGrid{{}});
    CHECK_THROWS(TransientTimeGrid{{seconds(0.0)}});
    CHECK_THROWS(TransientTimeGrid{{seconds(1.0), seconds(2.0)}});
    CHECK_THROWS(TransientTimeGrid{{seconds(0.0), seconds(0.0)}});
    CHECK_THROWS(TransientTimeGrid{{seconds(0.0), seconds(-1.0)}});
    CHECK_THROWS(TransientTimeGrid{{seconds(0.0), volts(1.0)}});
    CHECK_THROWS(
        TransientTimeGrid{{seconds(0.0), seconds(std::numeric_limits<double>::infinity())}});
    CHECK_THROWS(TransientTimeGrid::uniform(seconds(0.0), 3));
    CHECK_THROWS(TransientTimeGrid::uniform(seconds(1.0), 1));
    CHECK_THROWS(TransientTimeGrid::uniform(seconds(std::numeric_limits<double>::denorm_min()), 3));
    const auto grid = TransientTimeGrid::uniform(seconds(1.0), 5);
    CHECK(grid.times() ==
          std::vector{seconds(0.0), seconds(0.25), seconds(0.5), seconds(0.75), seconds(1.0)});
}

TEST_CASE(
    "Transient waveform is constant or continuous native linear interpolation with final hold") {
    const auto constant = TransientWaveform{volts(-2.0)};
    CHECK(constant.constant());
    CHECK(constant.value_at(seconds(9.0)) == volts(-2.0));
    const auto ramp = TransientWaveform{std::vector<TransientWaveformKnot>{
        {seconds(0.0), volts(-1.0)}, {seconds(0.4), volts(1.0)}, {seconds(0.8), volts(0.0)}}};
    CHECK_FALSE(ramp.constant());
    CHECK(ramp.value_at(seconds(0.0)) == volts(-1.0));
    CHECK(ramp.value_at(seconds(0.2)) == volts(0.0));
    CHECK(ramp.value_at(seconds(0.4)) == volts(1.0));
    CHECK(ramp.value_at(seconds(0.6)).value() == Catch::Approx(0.5));
    CHECK(ramp.value_at(seconds(1.0)) == volts(0.0));
    CHECK_THROWS(ramp.value_at(volts(1.0)));
    CHECK_THROWS(ramp.value_at(seconds(-1.0)));
    CHECK_THROWS(TransientWaveform{seconds(0.0)});
    CHECK_THROWS(TransientWaveform{std::vector<TransientWaveformKnot>{}});
    CHECK_THROWS(TransientWaveform{std::vector<TransientWaveformKnot>{{seconds(0.0), volts(1.0)}}});
    CHECK_THROWS(TransientWaveform{std::vector<TransientWaveformKnot>{{seconds(0.1), volts(1.0)},
                                                                      {seconds(1.0), volts(2.0)}}});
    CHECK_THROWS(TransientWaveform{std::vector<TransientWaveformKnot>{{seconds(0.0), volts(1.0)},
                                                                      {seconds(0.0), volts(2.0)}}});
    CHECK_THROWS(TransientWaveform{
        std::vector<TransientWaveformKnot>{{seconds(0.0), volts(1.0)}, {seconds(1.0), amps(2.0)}}});
}

TEST_CASE("Transient exact storage targets require selected Part kind participation and one-to-one "
          "identity") {
    auto fixture = test::dc_request::make_fixture();
    const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
    const auto state = composite_state(input);
    CHECK(assess_transient_request(composite_request(input, state)).complete());
    CHECK_FALSE(assess_transient_request(composite_request(input, {})).complete());
    CHECK_FALSE(assess_transient_request(composite_request(input, {state[0]})).complete());
    CHECK_THROWS(composite_request(input, {state[0], state[0], state[1]}));
    CHECK_THROWS(TransientInitialState{state[0].occurrence(), state[0].part(), state[0].element(),
                                       TransientStorageKind::CapacitorVoltage, amps(1.0)});
    CHECK_THROWS(
        composite_request(input, {{state[0].occurrence(), state[0].part(), ModelElementKey{"esr"},
                                   TransientStorageKind::CapacitorVoltage, volts(1.0)}}));
    CHECK_THROWS(
        composite_request(input, {{state[0].occurrence(), state[0].part(), ModelElementKey{"esl"},
                                   TransientStorageKind::CapacitorVoltage, volts(1.0)}}));
    const auto wrong_part = *input.circuit().get(ComponentId{1}).selected_library_part_ref();
    CHECK_THROWS(composite_request(input, {{state[0].occurrence(), wrong_part, state[0].element(),
                                            state[0].kind(), state[0].value()}}));
    CHECK_THROWS(composite_request(
        input, {{input.occurrence(ComponentId{1}), wrong_part, ModelElementKey{"storage"},
                 TransientStorageKind::CapacitorVoltage, volts(1.0)}}));
    static_cast<void>(fixture.circuit->add_net(NetSpec{.name = NetName{"extra"}}));
    const auto changed = io::prepare_electrical_input(*fixture.circuit, fixture.library);
    CHECK_THROWS(composite_request(changed, state));
}

TEST_CASE("Transient source union knots catches constraints equal only at zero and horizon") {
    const auto fixture = test::dc_request::make_fixture();
    const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
    const auto nets = ElectricalNetPair{input.net(fixture.positive), input.net(fixture.negative)};
    const auto zero =
        TransientVoltageSource{ElectricalSourceKey{"zero"}, nets, TransientWaveform{volts(0.0)}};
    const auto pulse = TransientVoltageSource{
        ElectricalSourceKey{"pulse"}, nets,
        TransientWaveform{std::vector<TransientWaveformKnot>{
            {seconds(0.0), volts(0.0)}, {seconds(0.4), volts(1.0)}, {seconds(1.0), volts(0.0)}}}};
    const auto contradicted = composite_request(input, composite_state(input), {zero, pulse});
    CHECK_FALSE(assess_transient_request(contradicted).complete());
    CHECK_FALSE(compile_electrical(contradicted).complete());
    const auto reversed = TransientVoltageSource{
        ElectricalSourceKey{"reverse"},
        ElectricalNetPair{input.net(fixture.negative), input.net(fixture.positive)},
        TransientWaveform{volts(-0.0)}};
    CHECK(
        assess_transient_request(composite_request(input, composite_state(input), {zero, reversed}))
            .complete());
    CHECK_THROWS(composite_request(input, composite_state(input), {zero, zero}));
    CHECK_THROWS(
        TransientVoltageSource{ElectricalSourceKey{"bad"}, nets, TransientWaveform{amps(1.0)}});
    CHECK_THROWS(TransientCurrentSource{
        ElectricalSourceKey{"bad"},
        ElectricalNetPair{input.net(fixture.positive), input.net(fixture.positive)},
        TransientWaveform{amps(1.0)}});
    const auto beyond =
        TransientVoltageSource{ElectricalSourceKey{"beyond"}, nets,
                               TransientWaveform{std::vector<TransientWaveformKnot>{
                                   {seconds(0.0), volts(0.0)}, {seconds(2.0), volts(1.0)}}}};
    CHECK_THROWS(composite_request(input, composite_state(input), {beyond}));
}

TEST_CASE("Transient compilation preserves composite storage origins laws observations and exact "
          "identity") {
    const auto fixture = test::dc_request::make_fixture();
    const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
    const auto compiled = compile_electrical(composite_request(input, composite_state(input)));
    REQUIRE(compiled.complete());
    const auto &model = *compiled.model();
    REQUIRE(model.transient_request());
    CHECK(model.ac_request() == nullptr);
    CHECK_THROWS(model.request());
    CHECK_THROWS(solve_dc(model));
    CHECK(model.storage().size() == 2);
    CHECK(model.branches().size() == 3);
    CHECK(model.nodes().size() == 4);
    const auto altered = compile_electrical(composite_request(input, composite_state(input, 2.0)));
    REQUIRE(altered.complete());
    CHECK(altered.model()->identity() != model.identity());
    CHECK(io::write_compiled_electrical_model(model).find("volt.transient-request") !=
          std::string::npos);
}

TEST_CASE("DC initial storage copy is explicit complete oriented and retains result provenance") {
    const auto fixture = test::dc_request::make_fixture();
    const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
    const auto compiled = compile_electrical(test::dc_request::complete_request(input));
    REQUIRE(compiled.complete());
    const auto dc = solve_dc(*compiled.model());
    REQUIRE(dc.success());
    const auto copied = initial_state_from(dc);
    REQUIRE(copied.entries().size() == 2);
    REQUIRE(copied.dc_provenance());
    CHECK(copied.dc_provenance()->analysis_identity == dc.analysis_identity());
    for (const auto &entry : copied.entries()) {
        CHECK(entry.part() ==
              *input.circuit().get(entry.occurrence().id()).selected_library_part_ref());
        CHECK(entry.value().value() ==
              Catch::Approx(entry.kind() == TransientStorageKind::CapacitorVoltage ? 5.0 : 0.0)
                  .margin(1e-15));
    }
    CHECK(assess_transient_request(composite_request(input, copied)).complete());
    CHECK_THROWS(
        TransientInitialConditions{std::vector{copied.entries()[0]}, copied.dc_provenance()});
    const auto request = composite_request(input, copied);
    auto exclusions = request.exclusions();
    exclusions.emplace_back(input.occurrence(ComponentId{0}), DcOutsideAnalysisExclusion{});
    CHECK_THROWS(TransientRequest{
        request.key(), input, request.reference(), request.grid(), {}, {}, exclusions, copied});
    auto failed_fixture = test::dc_request::make_fixture();
    static_cast<void>(failed_fixture.circuit->add_net(NetSpec{.name = NetName{"floating"}}));
    const auto failed_input =
        io::prepare_electrical_input(*failed_fixture.circuit, failed_fixture.library);
    const auto failed_compiled =
        compile_electrical(test::dc_request::complete_request(failed_input));
    REQUIRE(failed_compiled.complete());
    const auto failed_report = solve_dc(*failed_compiled.model());
    REQUIRE_FALSE(failed_report.success());
    CHECK_THROWS(initial_state_from(failed_report));
}

TEST_CASE(
    "Empty DC storage copy retains exact provenance and rejects changed participation and input") {
    auto fixture = test::electrical_compilation::make_fixture();
    const auto input = test::electrical_compilation::input(fixture);
    const auto compiled =
        compile_electrical(test::electrical_compilation::divider_request(fixture, input));
    REQUIRE(compiled.complete());
    const auto dc = solve_dc(*compiled.model());
    REQUIRE(dc.success());
    const auto copied = initial_state_from(dc);
    CHECK(copied.entries().empty());
    REQUIRE(copied.dc_provenance());
    CHECK(copied.dc_provenance()->input == input.identity());
    const auto target = TransientRequest{ElectricalRequestKey{"resistor-only"},
                                         input,
                                         input.net(fixture.reference),
                                         TransientTimeGrid::uniform(seconds(1.0), 2),
                                         {},
                                         {},
                                         {},
                                         copied};
    CHECK(assess_transient_request(target).complete());
    CHECK_THROWS(TransientRequest{target.key(),
                                  input,
                                  target.reference(),
                                  target.grid(),
                                  {},
                                  {},
                                  {DcOccurrenceExclusion{input.occurrence(fixture.upper_resistor),
                                                         DcOutsideAnalysisExclusion{}}},
                                  copied});
    static_cast<void>(fixture.circuit->add_net(NetSpec{.name = NetName{"stale"}}));
    const auto stale = test::electrical_compilation::input(fixture);
    CHECK_THROWS(TransientRequest{
        target.key(), stale, stale.net(fixture.reference), target.grid(), {}, {}, {}, copied});
}

TEST_CASE(
    "Transient compiler checks changing voltage constraints against preserved zero-ohm laws") {
    auto fixture = test::electrical_compilation::make_fixture();
    const auto shorted = fixture.circuit->instantiate_component(
        fixture.zero_resistor.component,
        ComponentInstanceSpec{.reference = ReferenceDesignator{"R0"}});
    fixture.circuit->update(
        shorted,
        SelectLibraryPart{fixture.library, fixture.library.require(fixture.zero_resistor.part)});
    fixture.circuit->connect(fixture.supply,
                             queries::pin_by_number(*fixture.circuit, shorted, "1").value());
    fixture.circuit->connect(fixture.reference,
                             queries::pin_by_number(*fixture.circuit, shorted, "2").value());
    const auto input = test::electrical_compilation::input(fixture);
    const auto target = TransientRequest{
        ElectricalRequestKey{"timed-short"},
        input,
        input.net(fixture.reference),
        TransientTimeGrid{{seconds(0.0), seconds(1.0)}},
        {TransientVoltageSource{
            ElectricalSourceKey{"ramp"},
            ElectricalNetPair{input.net(fixture.supply), input.net(fixture.reference)},
            TransientWaveform{std::vector<TransientWaveformKnot>{{seconds(0.0), volts(0.0)},
                                                                 {seconds(0.4), volts(1.0)},
                                                                 {seconds(1.0), volts(0.0)}}}}}};
    CHECK(assess_transient_request(target).complete());
    const auto compiled = compile_electrical(target);
    REQUIRE_FALSE(compiled.complete());
    CHECK(std::ranges::any_of(compiled.diagnostics(), [](const auto &diagnostic) {
        return diagnostic.code().value() ==
                   analysis_diagnostic_codes::ElectricalContradictoryVoltageSource &&
               diagnostic.message().find("0.4") != std::string::npos &&
               !diagnostic.entities().empty();
    }));
}

TEST_CASE("Transient native interpolation keeps finite convex extremes and canonical zero") {
    const auto maximum = std::numeric_limits<double>::max();
    const auto law = TransientWaveform{std::vector<TransientWaveformKnot>{
        {seconds(0.0), volts(-maximum)}, {seconds(1.0), volts(maximum)}}};
    CHECK(law.value_at(seconds(0.5)) == volts(0.0));
    CHECK(std::isfinite(law.value_at(seconds(0.25)).value()));
    CHECK_FALSE(std::signbit(TransientWaveform{volts(-0.0)}.value_at(seconds(0.0)).value()));
}

TEST_CASE("DC storage conversion retains reversed composite model orientation") {
    auto fixture = test::dc_request::make_fixture();
    const auto definition = fixture.circuit->get(fixture.resistor).definition();
    const auto reversed = fixture.circuit->instantiate_component(
        definition, ComponentInstanceSpec{.reference = ReferenceDesignator{"C_REVERSED"}});
    fixture.circuit->update(
        reversed, SelectLibraryPart{fixture.library, fixture.library.require(PartKey{"modeled"})});
    fixture.circuit->connect(fixture.negative,
                             queries::pin_by_number(*fixture.circuit, reversed, "1").value());
    fixture.circuit->connect(fixture.positive,
                             queries::pin_by_number(*fixture.circuit, reversed, "2").value());
    const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
    const auto compiled = compile_electrical(test::dc_request::complete_request(input));
    REQUIRE(compiled.complete());
    const auto dc = solve_dc(*compiled.model());
    REQUIRE(dc.success());
    const auto copied = initial_state_from(dc);
    REQUIRE(copied.entries().size() == 4);
    const auto reverse_voltage = std::ranges::find_if(copied.entries(), [&](const auto &entry) {
        return entry.occurrence().id() == reversed &&
               entry.kind() == TransientStorageKind::CapacitorVoltage;
    });
    REQUIRE(reverse_voltage != copied.entries().end());
    CHECK(reverse_voltage->value().value() == Catch::Approx(-5.0));
    CHECK(assess_transient_request(composite_request(input, copied)).complete());
}
