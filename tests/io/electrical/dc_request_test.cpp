#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>
#include <volt/io/electrical/dc_request_io.hpp>

#include "support/dc_request_fixture.hpp"

namespace {

using namespace volt;
using volt::test::dc_request::complete_request;
using volt::test::dc_request::make_fixture;

[[nodiscard]] std::map<std::size_t, DcCoverageStatus>
coverage_by_occurrence(const DcRequestAssessment &assessment) {
    auto result = std::map<std::size_t, DcCoverageStatus>{};
    for (const auto &entry : assessment.coverage()) {
        result.emplace(entry.occurrence().id().index(), entry.status());
    }
    return result;
}

[[nodiscard]] DcRequest request_with(const DcInput &input, std::optional<DcNetRef> reference,
                                     std::vector<DcSource> sources,
                                     std::vector<DcProbe> probes = {},
                                     std::vector<DcOccurrenceExclusion> exclusions = {}) {
    return DcRequest{DcRequestKey{"test"}, input,
                     std::move(reference), std::move(sources),
                     std::move(probes),    std::move(exclusions)};
}

[[nodiscard]] std::vector<DcOccurrenceExclusion> exclude_unselected(const DcInput &input) {
    return {DcOccurrenceExclusion{input.occurrence(ComponentId{2}), DcOutsideAnalysisExclusion{}},
            DcOccurrenceExclusion{input.occurrence(ComponentId{3}), DcNonElectricalExclusion{}}};
}

} // namespace

TEST_CASE("complete DC request retains all typed participation and probe variants") {
    const auto fixture = make_fixture();
    const auto input = io::prepare_dc_input(*fixture.circuit, fixture.library);
    const auto request = complete_request(input);
    const auto assessment = assess_dc_request(request);

    CHECK(assessment.complete());
    CHECK(assessment.input() == input.identity());
    CHECK(assessment.diagnostics().empty());
    CHECK(request.sources().size() == 2U);
    CHECK(request.probes().size() == 3U);
    CHECK(request.exclusions().size() == 3U);
    CHECK(std::holds_alternative<DcVoltageProbe>(request.probes()[0]));
    CHECK(std::holds_alternative<DcModelElementCurrentProbe>(request.probes()[1]));
    CHECK(std::holds_alternative<DcSourceCurrentProbe>(request.probes()[2]));
    CHECK(std::holds_alternative<DcReplacedByStimulusExclusion>(request.exclusions()[0].reason()));
    CHECK(std::holds_alternative<DcOutsideAnalysisExclusion>(request.exclusions()[1].reason()));
    CHECK(std::holds_alternative<DcNonElectricalExclusion>(request.exclusions()[2].reason()));
    CHECK(coverage_by_occurrence(assessment) ==
          std::map<std::size_t, DcCoverageStatus>{{0, DcCoverageStatus::Supported},
                                                  {1, DcCoverageStatus::Excluded},
                                                  {2, DcCoverageStatus::Excluded},
                                                  {3, DcCoverageStatus::Excluded}});
    CHECK(fixture.circuit->get(fixture.resistor).dnp().value());
}

TEST_CASE("a stimulus does not fabricate coverage for a selected model-absent supply") {
    const auto fixture = make_fixture();
    const auto input = io::prepare_dc_input(*fixture.circuit, fixture.library);
    const auto positive = input.net(fixture.positive);
    const auto negative = input.net(fixture.negative);
    const auto request =
        request_with(input, negative,
                     {DcVoltageSource{DcSourceKey{"drive"}, DcNetPair{positive, negative},
                                      Quantity{UnitDimension::Voltage, 5.0}}},
                     {}, exclude_unselected(input));

    const auto assessment = assess_dc_request(request);
    CHECK_FALSE(assessment.complete());
    CHECK(coverage_by_occurrence(assessment).at(fixture.absent.index()) ==
          DcCoverageStatus::ModelAbsent);
    CHECK(std::ranges::any_of(assessment.diagnostics(), [](const Diagnostic &diagnostic) {
        return diagnostic.code().value() == analysis_diagnostic_codes::DcOccurrenceModelAbsent;
    }));
}

TEST_CASE("assessment reports missing reference and every ordinary uncovered occurrence") {
    const auto fixture = make_fixture();
    const auto input = io::prepare_dc_input(*fixture.circuit, fixture.library);
    const auto assessment = assess_dc_request(request_with(input, std::nullopt, {}));
    const auto coverage = coverage_by_occurrence(assessment);

    CHECK_FALSE(assessment.complete());
    CHECK(coverage.at(fixture.resistor.index()) == DcCoverageStatus::Supported);
    CHECK(coverage.at(fixture.absent.index()) == DcCoverageStatus::ModelAbsent);
    CHECK(coverage.at(fixture.outside.index()) == DcCoverageStatus::Unselected);
    CHECK(coverage.at(fixture.non_electrical.index()) == DcCoverageStatus::Unselected);
    CHECK(std::ranges::any_of(assessment.diagnostics(), [](const Diagnostic &diagnostic) {
        return diagnostic.code().value() == analysis_diagnostic_codes::DcReferenceMissing;
    }));
}

TEST_CASE("unavailable exact resolver remains unresolved rather than becoming an open model") {
    const auto fixture = make_fixture();
    const auto input =
        io::prepare_dc_input(*fixture.circuit, volt::test::dc_request::UnavailableParts{});
    const auto request =
        request_with(input, input.net(fixture.negative), {}, {}, exclude_unselected(input));
    const auto assessment = assess_dc_request(request);

    CHECK_FALSE(assessment.complete());
    CHECK(coverage_by_occurrence(assessment).at(fixture.resistor.index()) ==
          DcCoverageStatus::Unresolved);
    CHECK(coverage_by_occurrence(assessment).at(fixture.absent.index()) ==
          DcCoverageStatus::Unresolved);
    CHECK(std::ranges::none_of(assessment.coverage(), [](const DcOccurrenceCoverage &entry) {
        return entry.status() == DcCoverageStatus::Unsupported;
    }));
}

TEST_CASE("request construction rejects foreign references and invalid local relationships") {
    const auto fixture = make_fixture();
    const auto input = io::prepare_dc_input(*fixture.circuit, fixture.library);
    auto changed = make_fixture();
    static_cast<void>(changed.circuit->add_net(NetSpec{.name = NetName{"identity-change"}}));
    const auto foreign = io::prepare_dc_input(*changed.circuit, changed.library);

    CHECK_THROWS_AS((DcNetPair{input.net(fixture.positive), foreign.net(changed.negative)}),
                    KernelLogicError);
    CHECK_THROWS_AS(
        (DcVoltageSource{DcSourceKey{"same"},
                         DcNetPair{input.net(fixture.positive), input.net(fixture.positive)},
                         Quantity{UnitDimension::Voltage, 1.0}}),
        KernelArgumentError);
    CHECK_THROWS_AS(input.net(NetId{99}), KernelRangeError);
    CHECK_THROWS_AS(input.occurrence(ComponentId{99}), KernelRangeError);
    CHECK_THROWS_AS(
        (DcVoltageSource{DcSourceKey{"wrong"},
                         DcNetPair{input.net(fixture.positive), input.net(fixture.negative)},
                         Quantity{UnitDimension::Current, 1.0}}),
        KernelArgumentError);
    CHECK_THROWS_AS((Quantity{UnitDimension::Voltage, std::numeric_limits<double>::infinity()}),
                    std::invalid_argument);
}

TEST_CASE("request rejects duplicate keys and invalid probe or replacement targets") {
    const auto fixture = make_fixture();
    const auto input = io::prepare_dc_input(*fixture.circuit, fixture.library);
    const auto pair = DcNetPair{input.net(fixture.positive), input.net(fixture.negative)};
    const auto drive =
        DcVoltageSource{DcSourceKey{"drive"}, pair, Quantity{UnitDimension::Voltage, 5.0}};

    CHECK_THROWS_AS(request_with(input, input.net(fixture.negative), {drive, drive}),
                    KernelArgumentError);
    CHECK_THROWS_AS(request_with(input, input.net(fixture.negative), {drive},
                                 {DcVoltageProbe{DcProbeKey{"p"}, pair},
                                  DcSourceCurrentProbe{DcProbeKey{"p"}, DcSourceKey{"drive"}}}),
                    KernelArgumentError);
    CHECK_THROWS_AS(
        request_with(input, input.net(fixture.negative), {drive},
                     {DcSourceCurrentProbe{DcProbeKey{"missing"}, DcSourceKey{"unknown"}}}),
        KernelRangeError);
    CHECK_THROWS_AS(request_with(input, input.net(fixture.negative), {drive},
                                 {DcModelElementCurrentProbe{DcProbeKey{"missing"},
                                                             input.occurrence(fixture.resistor),
                                                             ModelElementKey{"unknown"}}}),
                    KernelLogicError);
    CHECK_THROWS_AS(request_with(input, input.net(fixture.negative), {drive}, {},
                                 {DcOccurrenceExclusion{
                                     input.occurrence(fixture.absent),
                                     DcReplacedByStimulusExclusion{{DcSourceKey{"unknown"}}}}}),
                    KernelRangeError);
    CHECK_THROWS_AS(
        request_with(
            input, input.net(fixture.negative), {drive}, {},
            {DcOccurrenceExclusion{input.occurrence(fixture.outside), DcOutsideAnalysisExclusion{}},
             DcOccurrenceExclusion{input.occurrence(fixture.outside), DcNonElectricalExclusion{}}}),
        KernelArgumentError);
}

TEST_CASE("only unequal ideal voltage constraints on the same oriented pair contradict") {
    const auto fixture = make_fixture();
    const auto input = io::prepare_dc_input(*fixture.circuit, fixture.library);
    const auto positive = input.net(fixture.positive);
    const auto negative = input.net(fixture.negative);
    const auto assessment = [&](std::vector<DcSource> sources) {
        return assess_dc_request(
            request_with(input, negative, std::move(sources), {}, exclude_unselected(input)));
    };

    const auto direct = assessment({DcVoltageSource{DcSourceKey{"a"}, DcNetPair{positive, negative},
                                                    Quantity{UnitDimension::Voltage, 5.0}},
                                    DcVoltageSource{DcSourceKey{"b"}, DcNetPair{positive, negative},
                                                    Quantity{UnitDimension::Voltage, 4.0}}});
    CHECK(std::ranges::any_of(direct.diagnostics(), [](const Diagnostic &diagnostic) {
        return diagnostic.code().value() ==
               analysis_diagnostic_codes::DcContradictoryVoltageSources;
    }));

    const auto reversed =
        assessment({DcVoltageSource{DcSourceKey{"a"}, DcNetPair{positive, negative},
                                    Quantity{UnitDimension::Voltage, 5.0}},
                    DcVoltageSource{DcSourceKey{"b"}, DcNetPair{negative, positive},
                                    Quantity{UnitDimension::Voltage, 5.0}}});
    CHECK(std::ranges::any_of(reversed.diagnostics(), [](const Diagnostic &diagnostic) {
        return diagnostic.code().value() ==
               analysis_diagnostic_codes::DcContradictoryVoltageSources;
    }));

    const auto currents =
        assessment({DcCurrentSource{DcSourceKey{"a"}, DcNetPair{positive, negative},
                                    Quantity{UnitDimension::Current, 0.01}},
                    DcCurrentSource{DcSourceKey{"b"}, DcNetPair{positive, negative},
                                    Quantity{UnitDimension::Current, 0.01}}});
    CHECK(std::ranges::none_of(currents.diagnostics(), [](const Diagnostic &diagnostic) {
        return diagnostic.code().value() ==
               analysis_diagnostic_codes::DcContradictoryVoltageSources;
    }));
}

TEST_CASE("standalone request JSON is deterministic strict and exact-input bound") {
    const auto fixture = make_fixture();
    const auto input = io::prepare_dc_input(*fixture.circuit, fixture.library);
    const auto request = complete_request(input);
    const auto bytes = io::write_dc_request(request);
    const auto reopened = io::read_dc_request(bytes, input);

    CHECK(io::write_dc_request(reopened) == bytes);
    CHECK(reopened.input().identity() == input.identity());
    CHECK(reopened.exclusions().size() == 3U);

    auto stale = nlohmann::json::parse(bytes);
    stale["input"]["logical"] = sha256_content_hash("other").value();
    CHECK_THROWS_AS(io::read_dc_request(stale.dump(), input), KernelArgumentError);
    auto dangling = nlohmann::json::parse(bytes);
    dangling["reference"] = "net:99";
    CHECK_THROWS_AS(io::read_dc_request(dangling.dump(), input), KernelRangeError);
    auto unknown = nlohmann::json::parse(bytes);
    unknown["extra"] = true;
    CHECK_THROWS_AS(io::read_dc_request(unknown.dump(), input), KernelArgumentError);
    auto duplicate = std::string{"{\"format\":\"volt.dc-request\",\"format\":"
                                 "\"volt.dc-request\"}"};
    CHECK_THROWS_AS(io::read_dc_request(duplicate, input), KernelArgumentError);
}

TEST_CASE("prepared input owns immutable logical and Part bytes after authoring mutation") {
    auto fixture = make_fixture();
    const auto input = io::prepare_dc_input(*fixture.circuit, fixture.library);
    const auto identity = input.identity();
    const auto part_identity = input.part(fixture.resistor)->content_identity();
    const auto bytes = io::write_dc_request(complete_request(input));
    static_cast<void>(fixture.circuit->add_net(NetSpec{.name = NetName{"later"}}));

    CHECK(input.circuit().all<NetId>().size() == 2U);
    CHECK(input.identity() == identity);
    REQUIRE(input.part(fixture.resistor) != nullptr);
    CHECK(input.part(fixture.resistor)->content_identity() == part_identity);
    CHECK(io::write_dc_request(complete_request(input)) == bytes);
}

TEST_CASE("DC input rejects a resolver returning another exact selected Part") {
    const auto fixture = make_fixture();

    class WrongPart final : public PartDefinitionResolver {
      public:
        explicit WrongPart(const PartDefinition &part) : part_{part} {}

        const PartDefinition &resolve(const LibraryPartRef &) const & override { return part_; }

      private:
        const PartDefinition &part_;
    };

    const auto wrong =
        WrongPart{fixture.library.resolve(fixture.library.require(PartKey{"absent"}))};
    CHECK_THROWS_AS(io::prepare_dc_input(*fixture.circuit, wrong), KernelLogicError);
}
