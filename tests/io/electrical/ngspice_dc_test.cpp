#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <locale>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/errors.hpp>
#include <volt/electrical/dc_solve.hpp>
#include <volt/electrical/ngspice_dc.hpp>
#include <volt/io/electrical/ngspice_dc_io.hpp>

#include "../../../src/circuit/electrical/ngspice_dc_detail.hpp"
#include "support/electrical_compilation_fixture.hpp"

namespace {

using Json = nlohmann::json;
using volt::test::electrical_compilation::Fixture;
using volt::test::electrical_compilation::PartVariant;

class CommaDecimal final : public std::numpunct<char> {
  protected:
    [[nodiscard]] char do_decimal_point() const override { return ','; }
};

class ScopedGlobalLocale final {
  public:
    explicit ScopedGlobalLocale(const std::locale &locale)
        : previous_{std::locale::global(locale)} {}

    ~ScopedGlobalLocale() { std::locale::global(previous_); }

  private:
    std::locale previous_;
};

[[nodiscard]] volt::NgspiceDcAnalysis divider_analysis(std::string source_key = "supply-5v") {
    auto fixture = volt::test::electrical_compilation::make_fixture();
    const auto input = volt::test::electrical_compilation::input(fixture);
    const auto request = volt::DcRequest{
        volt::ElectricalRequestKey{"divider); quit"},
        input,
        input.net(fixture.reference),
        {volt::DcVoltageSource{
            volt::ElectricalSourceKey{std::move(source_key)},
            volt::ElectricalNetPair{input.net(fixture.supply), input.net(fixture.reference)},
            volt::Quantity{volt::UnitDimension::Voltage, 5.0}}}};
    const auto compiled = volt::compile_electrical(request);
    REQUIRE(compiled.model() != nullptr);
    return volt::prepare_ngspice_dc(*compiled.model());
}

[[nodiscard]] std::string machine_output(const volt::NgspiceDcAnalysis &analysis,
                                         std::vector<std::string> values) {
    const auto mapping = volt::detail::ngspice_dc_mapping(analysis);
    auto output = std::string{};
    for (const auto &header : mapping.headers) {
        if (!output.empty()) {
            output += ' ';
        }
        output += header;
    }
    output += '\n';
    for (auto index = std::size_t{0}; index < values.size(); ++index) {
        if (index != 0U) {
            output += ' ';
        }
        output += values[index];
    }
    output += '\n';
    return output;
}

[[nodiscard]] std::string zero_machine_output(const volt::NgspiceDcAnalysis &analysis) {
    const auto field_count = volt::detail::ngspice_dc_mapping(analysis).headers.size();
    return machine_output(analysis, std::vector<std::string>(field_count, "0"));
}

[[nodiscard]] volt::ComponentId add_part(Fixture &fixture, const PartVariant &variant,
                                         std::string reference, volt::NetId from, volt::NetId to) {
    const auto occurrence = fixture.circuit->instantiate_component(
        variant.component,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{std::move(reference)}});
    fixture.circuit->update(
        occurrence,
        volt::SelectLibraryPart{fixture.library, fixture.library.require(variant.part)});
    fixture.circuit->connect(
        from, volt::queries::pin_by_number(*fixture.circuit, occurrence, "1").value());
    fixture.circuit->connect(
        to, volt::queries::pin_by_number(*fixture.circuit, occurrence, "2").value());
    return occurrence;
}

} // namespace

TEST_CASE("ngspice DC preparation is deterministic exact and contains no authored syntax") {
    const auto analysis = divider_analysis("hostile); destroy all");
    const auto again = volt::prepare_ngspice_dc(analysis.model());

    CHECK(analysis.complete());
    CHECK(analysis.diagnostics().empty());
    CHECK(analysis.deck() == again.deck());
    CHECK(analysis.deck_identity() == again.deck_identity());
    CHECK(analysis.mapping_identity() == again.mapping_identity());
    CHECK(analysis.deck_identity() == volt::sha256_content_hash(analysis.deck()));
    CHECK(analysis.deck().find("divider); quit") == std::string::npos);
    CHECK(analysis.deck().find("hostile); destroy all") == std::string::npos);
    CHECK(analysis.deck().find("rshunt") == std::string::npos);
    CHECK(analysis.deck().find("gmin=0 gminsteps=0 srcsteps=0") != std::string::npos);
    CHECK(analysis.deck().find("reltol=1e-12 abstol=1e-15 vntol=1e-12") != std::string::npos);
    CHECK(analysis.deck().find("rb0 n0 n1 1000") != std::string::npos);
    CHECK(analysis.deck().find("rb1 n1 0 1000") != std::string::npos);
    CHECK(analysis.deck().find("vb2 n0 0 DC 5") != std::string::npos);
    CHECK(analysis.deck().find("set wr_singlescale\nset wr_vecnames\nset wr_onespace") !=
          std::string::npos);
    CHECK(analysis.deck().find("option numdgt=16\nop") != std::string::npos);
    CHECK(analysis.deck().find("wrdata volt-dc-output.txt") != std::string::npos);
    CHECK(analysis.deck().ends_with("\nquit\n.endc\n.end\n"));

    const auto report = Json::parse(volt::io::write_ngspice_dc_analysis(analysis));
    CHECK(report.at("request_identity") == analysis.model().request_identity().value());
    CHECK(report.at("mapping").at("branches").at(2).at("origin").at("kind") == "request_source");
    CHECK(report.at("mapping").at("branches").at(2).at("origin").at("key") ==
          "hostile); destroy all");
}

TEST_CASE("ngspice DC divider pins the independent deck and mapping contract") {
    const auto analysis = divider_analysis();
    const auto expected_deck =
        std::string{"* Volt ngspice DC adapter contract 1\n"
                    ".options gmin=0 gminsteps=0 srcsteps=0 reltol=1e-12 abstol=1e-15 "
                    "vntol=1e-12\n"
                    "rb0 n0 n1 1000\n"
                    "rb1 n1 0 1000\n"
                    "vb2 n0 0 DC 5\n"
                    ".control\n"
                    "set wr_singlescale\n"
                    "set wr_vecnames\n"
                    "set wr_onespace\n"
                    "option numdgt=16\n"
                    "op\n"
                    "let volt_mapping_"
                    "dfe391c3593e091bb3997b2b8117d864b8c50cba5656f06e3f86f137809308e9 = 0\n"
                    "let volt_scale = 0\n"
                    "setscale volt_scale\n"
                    "wrdata volt-dc-output.txt volt_mapping_"
                    "dfe391c3593e091bb3997b2b8117d864b8c50cba5656f06e3f86f137809308e9 "
                    "v(n0) v(n1) i(vb2)\n"
                    "quit\n"
                    ".endc\n"
                    ".end\n"};

    CHECK(analysis.mapping_identity().value() ==
          "sha256:dfe391c3593e091bb3997b2b8117d864b8c50cba5656f06e3f86f137809308e9");
    CHECK(analysis.deck_identity().value() ==
          "sha256:85533c95f524253259a573f210b816c1f411c10d96caedc7f9ecde1bd61c7572");
    CHECK(analysis.deck() == expected_deck);
}

TEST_CASE("ngspice DC report exposes native exact mapping and output contract") {
    const auto analysis = divider_analysis();
    const auto report = Json::parse(volt::io::write_ngspice_dc_analysis(analysis));

    CHECK(report.at("format") == "volt.ngspice-dc-analysis");
    CHECK(report.at("complete") == true);
    CHECK(report.at("backend").at("name") == "ngspice");
    CHECK(report.at("backend").at("version") == "46");
    CHECK(report.at("backend").at("settings") == std::string{volt::NgspiceDcAnalysis::settings()});
    CHECK(report.at("projection").at("kind") == "exact_dc");
    CHECK(report.at("projection").at("losses").empty());
    CHECK(report.at("mapping").at("identity") == analysis.mapping_identity().value());
    CHECK(report.at("mapping").at("output").at("filename") == "volt-dc-output.txt");
    CHECK(report.at("mapping").at("output").at("maximum_bytes") == 1024U * 1024U);
    CHECK(
        report.at("mapping").at("output").at("headers") ==
        Json::array({"volt_scale", "volt_mapping_" + analysis.mapping_identity().value().substr(7),
                     "v(n0)", "v(n1)", "i(vb2)"}));
    CHECK(report.at("mapping").at("branches").at(0).at("current_projection") ==
          "derived_resistance");
    CHECK(report.at("mapping").at("branches").at(2).at("current_projection") == "returned");
}

TEST_CASE("ngspice DC preparation refuses a model without an operating-point circuit") {
    auto fixture = volt::test::electrical_compilation::make_fixture();
    const auto input = volt::test::electrical_compilation::input(fixture);
    const auto request =
        volt::DcRequest{volt::ElectricalRequestKey{"reference-only"},
                        input,
                        input.net(fixture.reference),
                        {},
                        {},
                        {volt::DcOccurrenceExclusion{input.occurrence(fixture.upper_resistor),
                                                     volt::DcOutsideAnalysisExclusion{}},
                         volt::DcOccurrenceExclusion{input.occurrence(fixture.lower_resistor),
                                                     volt::DcOutsideAnalysisExclusion{}}}};
    const auto compiled = volt::compile_electrical(request);
    REQUIRE(compiled.model() != nullptr);
    REQUIRE(compiled.model()->branches().empty());

    const auto analysis = volt::prepare_ngspice_dc(*compiled.model());
    CHECK_FALSE(analysis.complete());
    REQUIRE(analysis.diagnostics().size() == 1U);
    CHECK(analysis.diagnostics().front().code().value() == "DC_NGSPICE_MODEL_EMPTY");
    CHECK_THROWS_AS(volt::detail::read_ngspice_dc_coordinates(analysis, "ignored"),
                    volt::KernelArgumentError);
}

TEST_CASE("ngspice DC parser returns canonical node then all-branch coordinates") {
    const auto analysis = divider_analysis();
    const auto output = machine_output(analysis, {"0", "0", "5", "2.5", "-2.5e-3"});
    const auto coordinates = volt::detail::read_ngspice_dc_coordinates(analysis, output);

    REQUIRE(coordinates.size() == 5U);
    CHECK(coordinates[0] == Catch::Approx(5.0));
    CHECK(coordinates[1] == Catch::Approx(2.5));
    CHECK(coordinates[2] == Catch::Approx(0.0025));
    CHECK(coordinates[3] == Catch::Approx(0.0025));
    CHECK(coordinates[4] == Catch::Approx(-0.0025));
}

TEST_CASE("ngspice DC parser accepts decimal and exponent fields independent of global locale") {
    const auto analysis = divider_analysis();
    const auto locale = ScopedGlobalLocale{std::locale{std::locale::classic(), new CommaDecimal{}}};
    const auto output = machine_output(analysis, {"0", "0", "5.", ".25e1", "-2.5E-3"});
    const auto coordinates = volt::detail::read_ngspice_dc_coordinates(analysis, output);

    REQUIRE(coordinates.size() == 5U);
    CHECK(coordinates[0] == Catch::Approx(5.0));
    CHECK(coordinates[1] == Catch::Approx(2.5));
    CHECK(coordinates[4] == Catch::Approx(-0.0025));
    CHECK_THROWS_AS(volt::detail::read_ngspice_dc_coordinates(
                        analysis, machine_output(analysis, {"0", "0", "5", "2,5", "-2.5e-3"})),
                    volt::KernelArgumentError);
}

TEST_CASE("ngspice DC parser retains representable subnormals and signed zero") {
    const auto analysis = divider_analysis();
    const auto output =
        machine_output(analysis, {"0", "0", "5", "1e-310", "-4.9406564584124654e-324"});
    const auto coordinates = volt::detail::read_ngspice_dc_coordinates(analysis, output);

    REQUIRE(coordinates.size() == 5U);
    CHECK(coordinates[1] == 1e-310);
    CHECK(coordinates[4] == -std::numeric_limits<double>::denorm_min());

    const auto signed_zero = volt::detail::read_ngspice_dc_coordinates(
        analysis, machine_output(analysis, {"0", "0", "5", "2.5", "-0"}));
    CHECK(signed_zero[4] == 0.0);
    CHECK(std::signbit(signed_zero[4]));

    const auto zero_exponents = volt::detail::read_ngspice_dc_coordinates(
        analysis, machine_output(analysis, {"0e-9999", "0", "5", "2.5", "-0.000e+9999"}));
    CHECK(zero_exponents[4] == 0.0);
    CHECK(std::signbit(zero_exponents[4]));
}

TEST_CASE("ngspice DC parser rejects malformed or unassociated machine output") {
    const auto analysis = divider_analysis();
    const auto valid = machine_output(analysis, {"0", "0", "5", "2.5", "-2.5e-3"});

    SECTION("wrong retained-model marker") {
        auto changed = valid;
        changed.replace(changed.find("volt_mapping_"), std::string{"volt_mapping_"}.size(),
                        "wrong_mapping_");
        CHECK_THROWS_AS(volt::detail::read_ngspice_dc_coordinates(analysis, changed),
                        volt::KernelArgumentError);
    }
    SECTION("missing vector") {
        CHECK_THROWS_AS(volt::detail::read_ngspice_dc_coordinates(
                            analysis, machine_output(analysis, {"0", "0", "5", "2.5"})),
                        volt::KernelArgumentError);
    }
    SECTION("unexpected vector") {
        auto changed = valid;
        changed.insert(changed.find('\n'), " surprise");
        CHECK_THROWS_AS(volt::detail::read_ngspice_dc_coordinates(analysis, changed),
                        volt::KernelArgumentError);
    }
    SECTION("nonfinite vector") {
        CHECK_THROWS_AS(volt::detail::read_ngspice_dc_coordinates(
                            analysis, machine_output(analysis, {"0", "0", "5", "nan", "-2.5e-3"})),
                        volt::KernelArgumentError);
    }
    SECTION("infinite vector") {
        CHECK_THROWS_AS(volt::detail::read_ngspice_dc_coordinates(
                            analysis, machine_output(analysis, {"0", "0", "5", "inf", "-2.5e-3"})),
                        volt::KernelArgumentError);
    }
    SECTION("trailing numeric text") {
        CHECK_THROWS_AS(
            volt::detail::read_ngspice_dc_coordinates(
                analysis, machine_output(analysis, {"0", "0", "5", "2.5junk", "-2.5e-3"})),
            volt::KernelArgumentError);
    }
    SECTION("incomplete exponent") {
        CHECK_THROWS_AS(
            volt::detail::read_ngspice_dc_coordinates(
                analysis, machine_output(analysis, {"0", "0", "5", "2.5e+", "-2.5e-3"})),
            volt::KernelArgumentError);
    }
    SECTION("leading plus") {
        CHECK_THROWS_AS(volt::detail::read_ngspice_dc_coordinates(
                            analysis, machine_output(analysis, {"0", "0", "5", "+2.5", "-2.5e-3"})),
                        volt::KernelArgumentError);
    }
    SECTION("hexadecimal floating point") {
        CHECK_THROWS_AS(
            volt::detail::read_ngspice_dc_coordinates(
                analysis, machine_output(analysis, {"0", "0", "5", "0x1p2", "-2.5e-3"})),
            volt::KernelArgumentError);
        CHECK_THROWS_AS(
            volt::detail::read_ngspice_dc_coordinates(
                analysis, machine_output(analysis, {"0", "0", "5", "0X1.2P3", "-2.5e-3"})),
            volt::KernelArgumentError);
    }
    SECTION("overflow") {
        CHECK_THROWS_AS(
            volt::detail::read_ngspice_dc_coordinates(
                analysis, machine_output(analysis, {"0", "0", "5", "1e9999", "-2.5e-3"})),
            volt::KernelArgumentError);
    }
    SECTION("positive and negative underflow to zero") {
        CHECK_THROWS_AS(
            volt::detail::read_ngspice_dc_coordinates(
                analysis, machine_output(analysis, {"0", "0", "5", "1e-9999", "-2.5e-3"})),
            volt::KernelArgumentError);
        CHECK_THROWS_AS(
            volt::detail::read_ngspice_dc_coordinates(
                analysis, machine_output(analysis, {"0", "0", "5", "-1e-9999", "-2.5e-3"})),
            volt::KernelArgumentError);
    }
    SECTION("truncated row") {
        CHECK_THROWS_AS(
            volt::detail::read_ngspice_dc_coordinates(analysis, valid.substr(0, valid.rfind(' '))),
            volt::KernelArgumentError);
    }
    SECTION("duplicate operating point") {
        const auto duplicate = valid + valid.substr(valid.find('\n') + 1U);
        CHECK_THROWS_AS(volt::detail::read_ngspice_dc_coordinates(analysis, duplicate),
                        volt::KernelArgumentError);
    }
    SECTION("nonzero association marker") {
        CHECK_THROWS_AS(volt::detail::read_ngspice_dc_coordinates(
                            analysis, machine_output(analysis, {"0", "1", "5", "2.5", "-2.5e-3"})),
                        volt::KernelArgumentError);
    }
    SECTION("oversized output") {
        CHECK_THROWS_AS(
            volt::detail::read_ngspice_dc_coordinates(
                analysis, std::string(volt::NgspiceDcAnalysis::maximum_output_bytes() + 1U, 'x')),
            volt::KernelArgumentError);
    }
}

TEST_CASE("ngspice lowering covers exact DC storage constraints and source projections") {
    auto fixture = volt::test::electrical_compilation::make_fixture();
    const auto extra = fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"extra"}});
    static_cast<void>(
        add_part(fixture, fixture.zero_resistor, "zero); quit", fixture.supply, extra));
    static_cast<void>(add_part(fixture, fixture.capacitor, "capacitor", extra, fixture.reference));
    static_cast<void>(add_part(fixture, fixture.inductor, "inductor", fixture.midpoint, extra));
    const auto input = volt::test::electrical_compilation::input(fixture);
    const auto request = volt::DcRequest{
        volt::ElectricalRequestKey{"all-laws"},
        input,
        input.net(fixture.reference),
        {volt::DcVoltageSource{
             volt::ElectricalSourceKey{"voltage"},
             volt::ElectricalNetPair{input.net(fixture.supply), input.net(fixture.reference)},
             volt::Quantity{volt::UnitDimension::Voltage, 5.0}},
         volt::DcCurrentSource{
             volt::ElectricalSourceKey{"current"},
             volt::ElectricalNetPair{input.net(extra), input.net(fixture.reference)},
             volt::Quantity{volt::UnitDimension::Current, 0.001}}}};
    const auto compiled = volt::compile_electrical(request);
    REQUIRE(compiled.model() != nullptr);
    const auto analysis = volt::prepare_ngspice_dc(*compiled.model());
    const auto report = Json::parse(volt::io::write_ngspice_dc_analysis(analysis));
    const auto &branches = report.at("mapping").at("branches");

    const auto projection_count = [&](std::string_view name) {
        return std::ranges::count_if(
            branches, [&](const Json &branch) { return branch.at("current_projection") == name; });
    };
    CHECK(projection_count("derived_resistance") == 2);
    CHECK(projection_count("exact_capacitor_open") == 1);
    CHECK(projection_count("independent_source_nominal") == 1);
    CHECK(projection_count("returned") == 3);
    CHECK(analysis.deck().find("zero); quit") == std::string::npos);
    CHECK(analysis.deck().find(" DC 0\n") != std::string::npos);
}

TEST_CASE("ngspice candidates cannot bypass native uniqueness and conditioning gates") {
    using namespace volt::test::electrical_compilation;

    SECTION("rank deficient") {
        auto fixture = make_fixture();
        const auto first =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"floating-a"}});
        const auto second =
            fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"floating-b"}});
        static_cast<void>(add_part(fixture, fixture.resistor, "floating", first, second));
        const auto input = volt::test::electrical_compilation::input(fixture);
        const auto compiled = volt::compile_electrical(divider_request(fixture, input));
        REQUIRE(compiled.model() != nullptr);
        const auto analysis = volt::prepare_ngspice_dc(*compiled.model());

        const auto report = volt::solve_ngspice_dc(analysis, zero_machine_output(analysis));
        CHECK(report.outcome() == volt::DcSolveOutcome::RankDeficient);
        CHECK(report.solution() == nullptr);
    }

    SECTION("inconsistent") {
        auto fixture = make_fixture();
        const auto input = volt::test::electrical_compilation::input(fixture);
        const auto request = volt::DcRequest{
            volt::ElectricalRequestKey{"inconsistent-loop"},
            input,
            input.net(fixture.reference),
            {volt::DcVoltageSource{
                 volt::ElectricalSourceKey{"supply-midpoint"},
                 volt::ElectricalNetPair{input.net(fixture.supply), input.net(fixture.midpoint)},
                 volt::Quantity{volt::UnitDimension::Voltage, 2.0}},
             volt::DcVoltageSource{
                 volt::ElectricalSourceKey{"midpoint-reference"},
                 volt::ElectricalNetPair{input.net(fixture.midpoint), input.net(fixture.reference)},
                 volt::Quantity{volt::UnitDimension::Voltage, 2.0}},
             volt::DcVoltageSource{
                 volt::ElectricalSourceKey{"supply-reference"},
                 volt::ElectricalNetPair{input.net(fixture.supply), input.net(fixture.reference)},
                 volt::Quantity{volt::UnitDimension::Voltage, 5.0}}}};
        const auto compiled = volt::compile_electrical(request);
        REQUIRE(compiled.model() != nullptr);
        const auto analysis = volt::prepare_ngspice_dc(*compiled.model());

        const auto report = volt::solve_ngspice_dc(analysis, zero_machine_output(analysis));
        CHECK(report.outcome() == volt::DcSolveOutcome::Inconsistent);
        CHECK(report.solution() == nullptr);
    }

    SECTION("ill conditioned") {
        const auto analysis = divider_analysis();
        const auto options = volt::DcSolveOptions{1e-12, 0.999999, 1e-9};
        const auto report =
            volt::solve_ngspice_dc(analysis, zero_machine_output(analysis), options);
        CHECK(report.outcome() == volt::DcSolveOutcome::IllConditioned);
        CHECK(report.solution() == nullptr);
    }
}
