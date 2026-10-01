#include <catch2/catch_test_macros.hpp>

#include "support/dc_request_fixture.hpp"
#include <cmath>
#include <limits>
#include <numbers>
#include <volt/electrical/compiled_electrical_model.hpp>
#include <volt/electrical/dc_solve.hpp>
#include <volt/electrical/ngspice_dc.hpp>
#include <volt/io/electrical/ac_request_io.hpp>
#include <volt/io/electrical/compiled_electrical_model_io.hpp>

namespace {
using namespace volt;

Quantity hz(double value) { return Quantity{UnitDimension::Frequency, value}; }

AcRequest request(const ElectricalInput &input, double amplitude = 1.0, double phase = 0.0,
                  double frequency = 100.0) {
    const auto dc = test::dc_request::complete_request(input);
    return AcRequest{ElectricalRequestKey{"ac"},
                     input,
                     dc.reference(),
                     AcFrequencySweep{{hz(frequency)}},
                     {AcVoltageSource{ElectricalSourceKey{"drive"},
                                      ElectricalNetPair{input.net(NetId{0}), input.net(NetId{1})},
                                      Quantity{UnitDimension::Voltage, amplitude}, phase}},
                     {},
                     dc.exclusions()};
}
} // namespace

TEST_CASE("AC frequency sweeps enforce typed finite positive strictly ascending samples") {
    CHECK_THROWS(AcFrequencySweep{{}});
    CHECK_THROWS(AcFrequencySweep{{hz(0.0)}});
    CHECK_THROWS(AcFrequencySweep{{hz(-1.0)}});
    CHECK_THROWS(AcFrequencySweep{{Quantity{UnitDimension::Voltage, 1.0}}});
    CHECK_THROWS(AcFrequencySweep{{hz(1.0), hz(1.0)}});
    CHECK_THROWS(AcFrequencySweep{{hz(2.0), hz(1.0)}});
    CHECK_THROWS(AcFrequencySweep{{hz(std::numeric_limits<double>::max())}});
    CHECK_NOTHROW(AcFrequencySweep{{hz(std::numeric_limits<double>::denorm_min())}});
    CHECK_NOTHROW(AcFrequencySweep{{hz(1.0)}});
    CHECK_THROWS(AcFrequencySweep::linear(hz(1.0), hz(2.0), 1));
    CHECK_THROWS(AcFrequencySweep::logarithmic(hz(1.0), hz(1.0), 2));
    CHECK_THROWS(AcFrequencySweep::linear(hz(1.0), hz(std::nextafter(1.0, 2.0)), 3));
    CHECK_THROWS(AcFrequencySweep::logarithmic(hz(1.0), hz(std::nextafter(1.0, 2.0)), 3));
    const auto linear = AcFrequencySweep::linear(hz(1.0), hz(5.0), 3);
    CHECK(linear.frequencies() == std::vector{hz(1.0), hz(3.0), hz(5.0)});
    const auto logarithmic = AcFrequencySweep::logarithmic(hz(0.01), hz(1e100), 7);
    CHECK(logarithmic.frequencies().front() == hz(0.01));
    CHECK(logarithmic.frequencies().back() == hz(1e100));
}

TEST_CASE("AC sources use explicit peak amplitudes finite radian phases and canonical zero") {
    const auto fixture = test::dc_request::make_fixture();
    const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
    const auto nets = ElectricalNetPair{input.net(fixture.positive), input.net(fixture.negative)};
    const auto source =
        AcVoltageSource{ElectricalSourceKey{"drive"}, nets, Quantity{UnitDimension::Voltage, 2.0},
                        std::numbers::pi / 2.0};
    CHECK(std::abs(source.phasor() - std::complex<double>{0.0, 2.0}) < 1e-14);
    const auto zero = AcVoltageSource{ElectricalSourceKey{"zero"}, nets,
                                      Quantity{UnitDimension::Voltage, -0.0}, 9.0};
    CHECK(zero.phase() == 0.0);
    CHECK_FALSE(std::signbit(zero.amplitude().value()));
    CHECK(zero.phasor() == std::complex<double>{});
    CHECK_THROWS(AcVoltageSource{ElectricalSourceKey{"bad"}, nets,
                                 Quantity{UnitDimension::Current, 1.0}, 0.0});
    CHECK_THROWS(AcCurrentSource{ElectricalSourceKey{"bad"}, nets,
                                 Quantity{UnitDimension::Current, -1.0}, 0.0});
    CHECK_THROWS(AcVoltageSource{ElectricalSourceKey{"bad"}, nets,
                                 Quantity{UnitDimension::Voltage, 0.0},
                                 std::numeric_limits<double>::infinity()});
    CHECK_THROWS(
        AcVoltageSource{ElectricalSourceKey{"bad"},
                        ElectricalNetPair{input.net(fixture.positive), input.net(fixture.positive)},
                        Quantity{UnitDimension::Voltage, 1.0}, 0.0});
    for (const auto amplitude :
         {std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::max()}) {
        const auto value = AcVoltageSource{ElectricalSourceKey{"extreme"}, nets,
                                           Quantity{UnitDimension::Voltage, amplitude}, 1e300}
                               .phasor();
        CHECK(std::isfinite(value.real()));
        CHECK(std::isfinite(value.imag()));
    }
}

TEST_CASE("AC request assesses missing assignments and models without fabricated DC excitation") {
    const auto fixture = test::dc_request::make_fixture();
    const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
    const auto incomplete = AcRequest{ElectricalRequestKey{"incomplete"}, input, std::nullopt,
                                      AcFrequencySweep{{hz(1.0)}}};
    const auto assessment = assess_ac_request(incomplete);
    CHECK_FALSE(assessment.complete());
    CHECK(assessment.diagnostics().size() == 5);
    CHECK(assess_ac_request(request(input)).complete());
    CHECK(assess_ac_request(request(input, 0.0)).complete());
    const auto foreign_fixture = test::dc_request::make_fixture();
    static_cast<void>(foreign_fixture.circuit->add_net(NetSpec{.name = NetName{"extra"}}));
    const auto foreign =
        io::prepare_electrical_input(*foreign_fixture.circuit, foreign_fixture.library);
    CHECK_THROWS(AcRequest{ElectricalRequestKey{"foreign"}, input, foreign.net(NetId{1}),
                           AcFrequencySweep{{hz(1.0)}}});
    const auto source = std::get<AcVoltageSource>(request(input).sources().front());
    CHECK_THROWS(AcRequest{ElectricalRequestKey{"duplicate"},
                           input,
                           input.net(NetId{1}),
                           AcFrequencySweep{{hz(1.0)}},
                           {source, source}});
}

TEST_CASE("AC uses one graph preserving RCL laws origins and true request identities") {
    const auto fixture = test::dc_request::make_fixture();
    const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
    const auto compiled = compile_electrical(request(input));
    REQUIRE(compiled.complete());
    const auto &model = *compiled.model();
    CHECK(model.ac_request() != nullptr);
    CHECK_THROWS(model.request());
    const auto rejects_ac = [](const auto &operation) {
        try {
            operation();
            FAIL("A DC consumer accepted an AC model");
        } catch (const KernelArgumentError &error) {
            CHECK(error.code() == ErrorCode::InvalidArgument);
        }
    };
    rejects_ac([&] { static_cast<void>(solve_dc(model)); });
    rejects_ac([&] { static_cast<void>(prepare_ngspice_dc(model)); });
    CHECK(model.branches().size() == 4);
    CHECK(model.storage().size() == 2);
    CHECK(std::holds_alternative<InductanceElement>(model.branches()[0].law));
    CHECK(std::holds_alternative<ResistanceElement>(model.branches()[1].law));
    CHECK(std::holds_alternative<CapacitanceElement>(model.branches()[2].law));
    CHECK(std::get<AcVoltageSource>(model.branches()[3].law).amplitude().value() == 1.0);
    const auto identity = [](const AcRequest &value) {
        const auto result = compile_electrical(value);
        REQUIRE(result.complete());
        return result.model()->identity();
    };
    CHECK(identity(request(input, 2.0)) != model.identity());
    CHECK(identity(request(input, 1.0, 1.0)) != model.identity());
    CHECK(identity(request(input, 1.0, 0.0, 200.0)) != model.identity());
    CHECK(identity(request(input)) == model.identity());
    CHECK(io::write_compiled_electrical_model(model).find("ac_voltage_source") !=
          std::string::npos);
}

TEST_CASE("AC derived probes validate dimensions target keys and stimulus readiness") {
    const auto fixture = test::dc_request::make_fixture();
    const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
    const auto base = request(input);
    const auto nets = ElectricalNetPair{input.net(NetId{0}), input.net(NetId{1})};
    const auto make = [&](std::vector<DcProbe> probes, std::vector<AcGainProbe> gains,
                          std::vector<AcImpedanceProbe> impedances = {}) {
        return AcRequest{base.key(),           input,
                         base.reference(),     base.sweep(),
                         base.sources(),       std::move(probes),
                         base.exclusions(),    std::move(gains),
                         std::move(impedances)};
    };
    const std::vector<DcProbe> probes{
        DcVoltageProbe{ElectricalProbeKey{"v"}, nets},
        DcSourceCurrentProbe{ElectricalProbeKey{"i"}, ElectricalSourceKey{"drive"}}};
    CHECK_THROWS(make(probes, {AcGainProbe{ElectricalProbeKey{"gain"}, ElectricalProbeKey{"v"},
                                           ElectricalProbeKey{"i"}}}));
    CHECK_THROWS(
        make(probes, {AcGainProbe{ElectricalProbeKey{"gain"}, ElectricalProbeKey{"missing"},
                                  ElectricalProbeKey{"v"}}}));
    CHECK_THROWS(make(probes, {AcGainProbe{ElectricalProbeKey{"v"}, ElectricalProbeKey{"v"},
                                           ElectricalProbeKey{"v"}}}));
    CHECK_NOTHROW(make(probes, {AcGainProbe{ElectricalProbeKey{"gain"}, ElectricalProbeKey{"v"},
                                            ElectricalProbeKey{"v"}}}));
    CHECK_THROWS(
        make(probes, {},
             {AcImpedanceProbe{ElectricalProbeKey{"z"}, nets, ElectricalSourceKey{"drive"}}}));
    const auto zero_gain =
        AcRequest{base.key(),
                  input,
                  base.reference(),
                  base.sweep(),
                  request(input, 0.0).sources(),
                  probes,
                  base.exclusions(),
                  {AcGainProbe{ElectricalProbeKey{"gain"}, ElectricalProbeKey{"v"},
                               ElectricalProbeKey{"v"}}}};
    CHECK_FALSE(assess_ac_request(zero_gain).complete());
    const auto impedance =
        AcRequest{base.key(),
                  input,
                  base.reference(),
                  base.sweep(),
                  {AcCurrentSource{ElectricalSourceKey{"drive"}, nets,
                                   Quantity{UnitDimension::Current, 1.0}, 0.0}},
                  {},
                  base.exclusions(),
                  {},
                  {AcImpedanceProbe{ElectricalProbeKey{"z"}, nets, ElectricalSourceKey{"drive"}}}};
    CHECK(assess_ac_request(impedance).complete());
}
