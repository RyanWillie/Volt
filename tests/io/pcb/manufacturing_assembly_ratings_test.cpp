#include "../../support/assembly_ratings_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <limits>
#include <nlohmann/json.hpp>
#include <volt/io/electrical/electrical_input_io.hpp>

namespace {
using namespace volt;
using namespace volt::test;

const ManufacturingCoverageRow &leaf(const ManufacturingCoverage &r, ManufacturingCheck key) {
    return r.coverage()[static_cast<std::size_t>(key)];
}

ManufacturingCoverage evaluate(const CompiledBoard &compiled, bool assembly = true,
                               CplProjectionOptions offsets = {}) {
    return evaluate_manufacturing_coverage(
        compiled, assembly_rating_target(compiled, assembly, std::move(offsets)));
}

ElectricalRecordSpec range(ElectricalObservable observable, ElectricalMeaning meaning, double low,
                           double high) {
    return {rating_subject(), observable, meaning,
            ElectricalValue{QuantityRange::bounded(
                Quantity{observable == ElectricalObservable::Voltage ? UnitDimension::Voltage
                                                                     : UnitDimension::Current,
                         low},
                Quantity{observable == ElectricalObservable::Voltage ? UnitDimension::Voltage
                                                                     : UnitDimension::Current,
                         high})}};
}

ElectricalRecordSpec current(ElectricalMeaning meaning, double value) {
    return current_record(rating_subject(), meaning,
                          ElectricalValue{ContinuousCurrent{rating_amps(value)}});
}

RatingFixtureOptions pair(std::vector<ElectricalRecordSpec> provider,
                          std::vector<ElectricalRecordSpec> consumer) {
    auto options = RatingFixtureOptions{};
    options.occurrences = {RatingOccurrence{.records = std::move(provider)},
                           RatingOccurrence{.records = std::move(consumer), .selected_index = 1}};
    return options;
}
} // namespace

TEST_CASE("Delivered assembly and rating checks retain exact preflight ownership and gaps",
          "[manufacturing][assembly][ratings]") {
    const auto assembly = GENERATE(false, true);
    const auto empty = GENERATE(false, true);
    const auto variant = GENERATE(0, 1, 2, 3, 4, 5, 6);
    if (!assembly && variant == 2)
        return;
    auto options = RatingFixtureOptions{};
    if (empty)
        options.occurrences.clear();
    const auto compiled = assembly_rating_fixture(options);
    auto process = manufacturing_process();
    if (assembly)
        process.assembly = ManufacturingAssembly::SmtAndThroughHole;
    auto applicability = process;
    auto cpl =
        assembly ? std::optional<CplProjectionOptions>{CplProjectionOptions{}} : std::nullopt;
    if (variant == 0)
        process.finish.reset();
    if (variant == 1)
        applicability.finish.reset();
    if (variant == 2)
        cpl.reset();
    if (variant == 3)
        process.finish = ManufacturingFinish::Enig;
    if (variant == 4)
        applicability.finish = ManufacturingFinish::Enig;
    if (variant == 5)
        applicability.dimension_x_mm = 31;
    auto foreign_options = options;
    foreign_options.voltage = 9;
    const auto foreign = assembly_rating_fixture(foreign_options);
    const auto target = ManufacturingTarget{
        variant == 6 ? foreign.identity() : compiled.identity(),
        variant == 6 ? foreign.content_digest() : compiled.content_digest(),
        assembly ? ManufacturingGoal::FabricationAndAssembly : ManufacturingGoal::Fabrication,
        process,
        manufacturing_profile(false, applicability),
        cpl};
    const auto result = evaluate_manufacturing_coverage(compiled, target);
    for (const auto check :
         {ManufacturingCheck::PopulatedQuantities, ManufacturingCheck::DnpQuantities,
          ManufacturingCheck::SelectedAlternates, ManufacturingCheck::CplIdentity,
          ManufacturingCheck::CplSide, ManufacturingCheck::CplRotation,
          ManufacturingCheck::SelectedPartRatings}) {
        CAPTURE(assembly, empty, variant, check);
        const auto &row = leaf(result, check);
        const auto ratings = check == ManufacturingCheck::SelectedPartRatings;
        const auto applicable = assembly || ratings;
        CHECK(row.owner ==
              (ratings ? ManufacturingOwner::NativeRatings : ManufacturingOwner::NativeAssembly));
        CHECK(row.applicable == applicable);
        CHECK(row.state ==
              (applicable ? ManufacturingState::NotEvaluated : ManufacturingState::NotApplicable));
        CHECK(row.blocker ==
              (applicable ? ManufacturingBlocker::MissingInput : ManufacturingBlocker::None));
        CHECK(row.reason.find("not implemented") == std::string::npos);
        CHECK(row.entities == (empty ? std::vector<EntityRef>{}
                                     : std::vector{EntityRef::component(ComponentId{0})}));
        CHECK(row.observations.empty());
    }
    CHECK_FALSE(result.ratings());
    CHECK_FALSE(result.bom());
    CHECK_FALSE(result.cpl());
}

TEST_CASE("Exact assembly inventories preserve selected grouping DNP and alternates",
          "[manufacturing][assembly]") {
    auto options = RatingFixtureOptions{};
    options.occurrences = {RatingOccurrence{.override_selection = true},
                           RatingOccurrence{.side = BoardSide::Bottom, .rotation = -450},
                           RatingOccurrence{.dnp = true, .placed = false},
                           RatingOccurrence{.dnp = true, .selected = false, .placed = false}};
    const auto compiled = assembly_rating_fixture(options);
    const auto before = std::string{compiled.bytes()};
    const auto result = evaluate(compiled);
    for (const auto key :
         {ManufacturingCheck::PopulatedQuantities, ManufacturingCheck::DnpQuantities,
          ManufacturingCheck::SelectedAlternates, ManufacturingCheck::CplIdentity,
          ManufacturingCheck::CplSide, ManufacturingCheck::CplRotation,
          ManufacturingCheck::SelectedPartRatings})
        CHECK(leaf(result, key).state == ManufacturingState::NativePass);
    REQUIRE(result.bom());
    REQUIRE(result.cpl());
    CHECK(result.bom()->components().size() == 4);
    REQUIRE(result.bom()->lines().size() == 2);
    CHECK(result.bom()->lines()[0].quantity() == 2);
    CHECK(result.bom()->lines()[0].references() == std::vector<std::string>{"U0", "U1"});
    CHECK(result.bom()->lines()[0].approved_alternate_mpns() ==
          std::vector<std::string>{"APPROVED-ONLY"});
    CHECK(result.bom()->lines()[0].mpn() == "SELECTED");
    CHECK(result.bom()->lines()[0].selection_override_references() ==
          std::vector<std::string>{"U0"});
    CHECK(result.bom()->lines()[1].quantity() == 0);
    CHECK(result.bom()->lines()[1].references() == std::vector<std::string>{"U2"});
    REQUIRE(result.cpl()->rows().size() == 2);
    CHECK(result.cpl()->rows()[1].side() == BoardSide::Bottom);
    CHECK(result.cpl()->rows()[1].rotation_deg() == 270);
    CHECK(leaf(result, ManufacturingCheck::CplIdentity).observations.size() == 4);
    CHECK(leaf(result, ManufacturingCheck::CplRotation).observations.size() == 6);
    for (const auto key :
         {ManufacturingCheck::PartPackageReview, ManufacturingCheck::LandPatternReview,
          ManufacturingCheck::PinMapReview, ManufacturingCheck::AssemblyOrientation,
          ManufacturingCheck::AssemblyProcess, ManufacturingCheck::ExportFidelity})
        CHECK(leaf(result, key).state == ManufacturingState::NotEvaluated);
    CHECK_FALSE(result.ready());
    CHECK(std::string{compiled.bytes()} == before);
}

TEST_CASE("Assembly missing intent selection and first last all placements cannot disappear",
          "[manufacturing][assembly]") {
    const auto missing = GENERATE("intent", "selection", "first", "last", "all");
    auto options = pair({voltage_rating()}, {voltage_rating()});
    const auto which = std::string_view{missing};
    if (which == "intent")
        options.occurrences[0].dnp.reset();
    if (which == "selection")
        options.occurrences[1].selected = false;
    if (which == "first" || which == "all")
        options.occurrences[0].placed = false;
    if (which == "last" || which == "all")
        options.occurrences[1].placed = false;
    const auto compiled = assembly_rating_fixture(options);
    const auto result = evaluate(compiled);
    REQUIRE(result.bom());
    CHECK(result.bom()->components().size() == 2);
    if (which == "intent" || which == "selection")
        CHECK(leaf(result, ManufacturingCheck::PopulatedQuantities).state ==
              ManufacturingState::Failed);
    if (which == "selection") {
        CHECK(leaf(result, ManufacturingCheck::SelectedAlternates).state ==
              ManufacturingState::Failed);
        CHECK(leaf(result, ManufacturingCheck::SelectedPartRatings).state ==
              ManufacturingState::NotEvaluated);
        CHECK(leaf(result, ManufacturingCheck::CplIdentity).state == ManufacturingState::Failed);
    }
    if (which == "first" || which == "last" || which == "all")
        for (const auto key : {ManufacturingCheck::CplIdentity, ManufacturingCheck::CplSide,
                               ManufacturingCheck::CplRotation})
            CHECK(leaf(result, key).state == ManufacturingState::Failed);
}

TEST_CASE("Pinned native CPL rotation options change target evidence without mutation",
          "[manufacturing][assembly]") {
    const auto degrees = GENERATE(-450.0, -360.0, -0.0, 0.0, 360.0, 765.0, 1.0e12);
    auto options = RatingFixtureOptions{};
    options.occurrences[0].rotation = degrees;
    const auto compiled = assembly_rating_fixture(options);
    const auto empty = evaluate(compiled);
    const auto corrected = evaluate(
        compiled, true, CplProjectionOptions{{CplRotationOffset{{"test.rating", "subject"}, 90}}});
    CHECK(empty.target_digest() != corrected.target_digest());
    CHECK(empty.cpl()->rows()[0].rotation_offset_deg() == 0);
    CHECK(corrected.cpl()->rows()[0].rotation_offset_deg() == 90);
    CHECK(leaf(corrected, ManufacturingCheck::CplRotation).state == ManufacturingState::NativePass);
    CHECK(corrected.cpl()->rows()[0].rotation_deg() >= 0);
    CHECK(corrected.cpl()->rows()[0].rotation_deg() < 360);
    CHECK(io::write_manufacturing_coverage(empty) != io::write_manufacturing_coverage(corrected));
    CHECK_THROWS_AS(assembly_rating_target(
                        compiled, true,
                        CplProjectionOptions{{CplRotationOffset{{"test.rating", "subject"}, 90},
                                              CplRotationOffset{{"test.rating", "subject"}, 180}}}),
                    KernelArgumentError);
}

TEST_CASE("All four supported native rating families execute inclusive comparisons",
          "[manufacturing][ratings]") {
    const auto family = GENERATE(0, 1, 2, 3);
    const auto delta = GENERATE(-0.01, 0.0, 0.01);
    auto options = RatingFixtureOptions{};
    if (family == 0) {
        options.voltage = 5 + delta;
        options.occurrences[0].records = {voltage_rating(0, 5)};
    } else if (family == 1) {
        options = pair(
            {range(ElectricalObservable::Voltage, ElectricalMeaning::ProvidedRange, 3, 5 + delta)},
            {range(ElectricalObservable::Voltage, ElectricalMeaning::AcceptedRange, 3, 5)});
    } else if (family == 2) {
        options = pair(
            {range(ElectricalObservable::Current, ElectricalMeaning::ProvidedRange, 0, 1 + delta)},
            {range(ElectricalObservable::Current, ElectricalMeaning::AbsoluteLimit, 0, 1)});
    } else
        options = pair({current(ElectricalMeaning::Capability, 1)},
                       {current(ElectricalMeaning::Requirement, 1 + delta)});
    const auto compiled = assembly_rating_fixture(options);
    const auto result = evaluate(compiled, GENERATE(false, true));
    CHECK(leaf(result, ManufacturingCheck::SelectedPartRatings).state ==
          (delta > 0 ? ManufacturingState::Failed : ManufacturingState::NativePass));
    REQUIRE(result.ratings());
    CHECK_FALSE(result.ratings()->subjects.empty());
    for (const auto &subject : result.ratings()->subjects) {
        CHECK_FALSE(subject.observations.empty());
        CHECK(subject.state ==
              (delta > 0 ? SelectedPartRatingState::Failed : SelectedPartRatingState::Pass));
        CHECK(subject.selection);
        CHECK(subject.key);
        CHECK_FALSE(subject.records.empty());
    }
    if (delta > 0) {
        CHECK_FALSE(result.ratings()->diagnostics.diagnostics().empty());
        CHECK(result.ratings()->diagnostics.diagnostics()[0].rule());
    }
}

TEST_CASE("Incomplete supported ratings remain explicit instead of passing empty loops",
          "[manufacturing][ratings]") {
    const auto variant = GENERATE("absent", "characteristic", "unknown", "conditional", "mixed",
                                  "conflict", "unresolved", "current-accepted", "unpaired",
                                  "partial-provider", "dnp-source", "dnp-all", "empty");
    const auto which = std::string_view{variant};
    auto options = RatingFixtureOptions{};
    auto &records = options.occurrences[0].records;
    if (which == "absent")
        records.clear();
    if (which == "characteristic")
        records = {voltage_record(rating_subject(), ElectricalMeaning::Characteristic,
                                  ElectricalValue{rating_volts(3.3)})};
    if (which == "unknown" || which == "mixed") {
        if (which == "unknown")
            records.clear();
        records.push_back(voltage_record(rating_subject(), ElectricalMeaning::AbsoluteLimit,
                                         ElectricalValue{UnknownElectricalValue{}}));
    }
    if (which == "conditional")
        records[0].conditions = {
            ElectricalCondition::equal(rating_subject(), ElectricalObservable::Voltage,
                                       ElectricalValueExpression::literal(rating_volts(3.3)))};
    if (which == "conflict")
        records.push_back(voltage_rating(6, 8));
    if (which == "unresolved")
        options.occurrences[0].connected = false;
    if (which == "current-accepted")
        records = {range(ElectricalObservable::Current, ElectricalMeaning::AcceptedRange, 0, 1)};
    if (which == "unpaired")
        records = {range(ElectricalObservable::Voltage, ElectricalMeaning::ProvidedRange, 3, 4)};
    if (which == "partial-provider") {
        options =
            pair({voltage_record(rating_subject(), ElectricalMeaning::ProvidedRange,
                                 ElectricalValue{QuantityRange::minimum(rating_volts(3))})},
                 {range(ElectricalObservable::Voltage, ElectricalMeaning::AcceptedRange, 2, 5)});
    }
    if (which == "dnp-source") {
        options = pair({current(ElectricalMeaning::Capability, 1)},
                       {current(ElectricalMeaning::Requirement, 0.1)});
        options.occurrences[0].dnp = true;
    }
    if (which == "dnp-all")
        options.occurrences[0].dnp = true;
    if (which == "empty")
        options.occurrences.clear();
    const auto compiled = assembly_rating_fixture(options);
    const auto result = evaluate(compiled);
    CHECK(leaf(result, ManufacturingCheck::SelectedPartRatings).state ==
          ManufacturingState::NotEvaluated);
    REQUIRE(result.ratings());
    if (which != "empty")
        CHECK_FALSE(result.ratings()->subjects.empty());
    if (which == "dnp-source" || which == "dnp-all")
        CHECK(result.ratings()->subjects[0].state == SelectedPartRatingState::NotApplicable);
}

TEST_CASE("Native rating repeated current claims use the maximum per occurrence",
          "[manufacturing][ratings]") {
    auto first = current(ElectricalMeaning::Requirement, 0.2);
    auto alias = current(ElectricalMeaning::Requirement, 0.3);

    auto options = pair({current(ElectricalMeaning::Capability, 0.3)}, {first, alias});
    const auto compiled = assembly_rating_fixture(options);
    const auto result = evaluate(compiled);
    CHECK(leaf(result, ManufacturingCheck::SelectedPartRatings).state ==
          ManufacturingState::NativePass);
    REQUIRE(result.ratings());
    CHECK(result.ratings()->subjects.size() == 2);
    for (const auto &subject : result.ratings()->subjects)
        CHECK(subject.observations[0].value == 0.3);
}

TEST_CASE("Rating semantic key groups retain all source records and failure dominates gaps",
          "[manufacturing][ratings]") {
    auto options = RatingFixtureOptions{};
    options.occurrences[0].records = {voltage_rating(0, 5), voltage_rating(1, 4)};
    const auto compiled = assembly_rating_fixture(options);
    const auto result = evaluate(compiled);
    REQUIRE(result.ratings());
    REQUIRE(result.ratings()->subjects.size() == 1);
    CHECK(result.ratings()->subjects[0].records.size() == 2);
    CHECK(leaf(result, ManufacturingCheck::SelectedPartRatings).state ==
          ManufacturingState::NativePass);
    options.voltage = 6;
    options.occurrences.push_back(RatingOccurrence{.records = {}, .selected_index = 1});
    const auto invalid = assembly_rating_fixture(options);
    CHECK(leaf(evaluate(invalid), ManufacturingCheck::SelectedPartRatings).state ==
          ManufacturingState::Failed);
}

TEST_CASE("Compiled rating subjects preserve original identity continuity and voltage gaps",
          "[manufacturing][ratings]") {
    auto options = RatingFixtureOptions{};
    options.modules = true;
    const auto variant = GENERATE("supported", "noncanonical", "contradictory");
    options.noncanonical_voltage = std::string_view{variant} == "noncanonical";
    options.contradictory_voltage = std::string_view{variant} == "contradictory";
    const auto compiled = assembly_rating_fixture(options);
    const auto result = evaluate(compiled);
    CHECK(leaf(result, ManufacturingCheck::SelectedPartRatings).state ==
          (std::string_view{variant} == "supported" ? ManufacturingState::NativePass
                                                    : ManufacturingState::NotEvaluated));
    CHECK_THROWS_AS(
        io::prepare_electrical_input(compiled.board().circuit(), compiled.part_definitions()),
        KernelLogicError);
    const auto bytes = std::string{compiled.bytes()};
    const auto target = io::write_manufacturing_target(assembly_rating_target(compiled));
    const auto reopened = io::open_compiled_board(bytes);
    CHECK(io::write_manufacturing_coverage(result) ==
          io::write_manufacturing_coverage(
              evaluate_manufacturing_coverage(reopened, io::read_manufacturing_target(target))));
}

TEST_CASE("Ratings preserve native framed relation and supply orientation",
          "[manufacturing][ratings]") {
    const auto kind = GENERATE(0, 1, 2);
    auto options = RatingFixtureOptions{};
    if (kind == 0)
        options.occurrences[0].records[0].subject =
            ElectricalSubject::framed_pin(ElectricalPinIndex{0}, ElectricalPinIndex{1});
    if (kind == 1)
        options.occurrences[0].records[0].subject =
            ElectricalSubject::directed_relation(ElectricalPinIndex{0}, ElectricalPinIndex{1});
    const auto compiled = assembly_rating_fixture(options);
    CHECK(leaf(evaluate(compiled), ManufacturingCheck::SelectedPartRatings).state ==
          ManufacturingState::NativePass);
}

TEST_CASE("Current budget missing multiple conditional and overflowing sources never pass",
          "[manufacturing][ratings]") {
    const auto variant =
        GENERATE("missing", "multiple", "conditional", "mixed", "unknown", "overflow");
    const auto which = std::string_view{variant};
    auto options = pair({current(ElectricalMeaning::Capability, 1)},
                        {current(ElectricalMeaning::Requirement, 0.5)});
    if (which == "missing")
        options.occurrences[0].records.clear();
    if (which == "multiple")
        options.occurrences.push_back(RatingOccurrence{
            .records = {current(ElectricalMeaning::Capability, 1)}, .selected_index = 2});
    if (which == "conditional" || which == "mixed") {
        auto conditional = current(ElectricalMeaning::Requirement, 0.25);
        conditional.conditions = {
            ElectricalCondition::equal(rating_subject(), ElectricalObservable::Voltage,
                                       ElectricalValueExpression::literal(rating_volts(3.3)))};
        if (which == "conditional")
            options.occurrences[1].records.clear();
        options.occurrences[1].records.push_back(conditional);
    }
    if (which == "unknown")
        options.occurrences[1].records.push_back(
            current_record(rating_subject(), ElectricalMeaning::Requirement,
                           ElectricalValue{UnknownElectricalValue{}}));
    if (which == "overflow") {
        options.occurrences[0].records = {
            current(ElectricalMeaning::Capability, std::numeric_limits<double>::max())};
        options.occurrences[1].records = {
            current(ElectricalMeaning::Requirement, std::numeric_limits<double>::max())};
        options.occurrences.push_back(
            RatingOccurrence{.records = options.occurrences[1].records, .selected_index = 2});
    }
    const auto compiled = assembly_rating_fixture(options);
    const auto result = evaluate(compiled);
    CHECK(leaf(result, ManufacturingCheck::SelectedPartRatings).state ==
          ManufacturingState::NotEvaluated);
    for (const auto &subject : result.ratings()->subjects)
        CHECK(subject.observations.empty());
}

TEST_CASE("Rating arithmetic and maximum finite CPL correction retain truthful outcomes",
          "[manufacturing][ratings]") {
    auto options = RatingFixtureOptions{};
    options.voltage = std::numeric_limits<double>::max();
    options.return_voltage = -std::numeric_limits<double>::max();
    const auto compiled = assembly_rating_fixture(options);
    const auto result =
        evaluate(compiled, true,
                 CplProjectionOptions{{CplRotationOffset{{"test.rating", "subject"},
                                                         std::numeric_limits<double>::max()}}});
    CHECK(leaf(result, ManufacturingCheck::SelectedPartRatings).state ==
          ManufacturingState::NotEvaluated);
    CHECK(result.ratings()->subjects[0].observations.empty());
    CHECK(leaf(result, ManufacturingCheck::CplRotation).state == ManufacturingState::NativePass);
    CHECK(std::isfinite(result.cpl()->rows()[0].rotation_deg()));
}

TEST_CASE("Changed selection requires a different immutable artifact despite approved MPN",
          "[manufacturing][assembly]") {
    auto options = pair({voltage_rating(0, 5)}, {voltage_rating(0, 2)});
    options.occurrences[0].alternates = {"ALTERNATE"};
    options.occurrences[1].mpn = "ALTERNATE";
    options.occurrences[1].dnp = true;
    const auto selected = assembly_rating_fixture(options);
    CHECK(leaf(evaluate(selected), ManufacturingCheck::SelectedPartRatings).state ==
          ManufacturingState::NativePass);
    options.occurrences[0].selected_index = 1;
    options.occurrences[0].override_selection = true;
    const auto substitute = assembly_rating_fixture(options);
    CHECK(selected.identity() != substitute.identity());
    const auto result = evaluate(substitute);
    CHECK(leaf(result, ManufacturingCheck::SelectedPartRatings).state ==
          ManufacturingState::Failed);
    CHECK(result.bom()->lines()[0].mpn() == "ALTERNATE");
    const auto foreign =
        evaluate_manufacturing_coverage(substitute, assembly_rating_target(selected));
    CHECK(leaf(foreign, ManufacturingCheck::ArtifactIdentity).state == ManufacturingState::Failed);
    CHECK_FALSE(foreign.ratings());
    CHECK_FALSE(foreign.bom());
    CHECK_FALSE(foreign.cpl());
}

TEST_CASE("Conditional rating selectors retain original cross-group context without revalidation",
          "[manufacturing][ratings]") {
    const auto characteristic = voltage_record(rating_subject(), ElectricalMeaning::Characteristic,
                                               ElectricalValue{rating_volts(3.3)});
    auto conditional = voltage_rating();
    conditional.conditions = {ElectricalCondition::equal(
        rating_subject(), ElectricalObservable::Voltage,
        ElectricalValueExpression::scaled_reference(
            ElectricalRecordSelector{rating_subject(), ElectricalObservable::Voltage,
                                     ElectricalMeaning::Characteristic},
            1))};
    auto options = RatingFixtureOptions{};
    options.occurrences[0].records = {characteristic, conditional};
    const auto compiled = assembly_rating_fixture(options);
    const auto result = evaluate(compiled);
    CHECK(leaf(result, ManufacturingCheck::SelectedPartRatings).state ==
          ManufacturingState::NotEvaluated);
    REQUIRE(result.ratings());
    REQUIRE(result.ratings()->subjects.size() == 1);
    CHECK(result.ratings()->subjects[0].records.size() == 1);
    CHECK(result.ratings()->subjects[0].records[0].conditions() == conditional.conditions);
    const auto text = io::write_manufacturing_coverage(result);
    const auto document = nlohmann::json::parse(text);
    CHECK(document["ratings"]["subjects"][0]["record_count"] == 1);
    CHECK_FALSE(document["ratings"]["subjects"][0]["records"][0]["conditions"].empty());
}

TEST_CASE("Distinct rating supply aliases aggregate max demand and min capability per occurrence",
          "[manufacturing][ratings]") {
    const auto variant = GENERATE("boundary", "violation", "dnp");
    const auto which = std::string_view{variant};
    auto capability_alias = current(ElectricalMeaning::Capability, 0.5);
    auto requirement_alias = current(ElectricalMeaning::Requirement, 0.3);
    const auto alias = ElectricalSubject::supply_domain(
        {ElectricalPinIndex{0}, ElectricalPinIndex{2}}, {ElectricalPinIndex{1}});
    capability_alias.subject = alias;
    requirement_alias.subject = alias;
    auto options = pair({current(ElectricalMeaning::Capability, 1), capability_alias},
                        {current(ElectricalMeaning::Requirement, 0.2), requirement_alias});
    options.occurrences.push_back(RatingOccurrence{
        .records = {current(ElectricalMeaning::Requirement, which == "boundary" ? 0.2 : 0.201)},
        .dnp = which == "dnp",
        .selected_index = 2});
    const auto compiled = assembly_rating_fixture(options);
    const auto result = evaluate(compiled);
    CHECK(leaf(result, ManufacturingCheck::SelectedPartRatings).state ==
          (which == "violation" ? ManufacturingState::Failed : ManufacturingState::NativePass));
    REQUIRE(result.ratings());
    REQUIRE(result.ratings()->subjects.size() == 5);
    for (const auto &subject : result.ratings()->subjects)
        if (subject.state != SelectedPartRatingState::NotApplicable) {
            REQUIRE(subject.observations.size() == 1);
            CHECK(subject.observations[0].value == (which == "boundary"    ? 0.5
                                                    : which == "violation" ? 0.501
                                                                           : 0.3));
        }
}

TEST_CASE(
    "Voltage rating checks every populated provider and cannot replace accepted range with nominal",
    "[manufacturing][ratings]") {
    const auto variant = GENERATE("single", "multiple", "dnp", "accepted-nominal");
    const auto which = std::string_view{variant};
    auto options =
        pair({range(ElectricalObservable::Voltage, ElectricalMeaning::ProvidedRange, 0, 4)},
             {voltage_rating(0, 5)});
    options.voltage.reset();
    if (which == "multiple" || which == "dnp")
        options.occurrences.push_back(
            RatingOccurrence{.records = {range(ElectricalObservable::Voltage,
                                               ElectricalMeaning::ProvidedRange, 0, 6)},
                             .dnp = which == "dnp",
                             .selected_index = 2});
    if (which == "accepted-nominal") {
        options.occurrences = {
            RatingOccurrence{.records = {range(ElectricalObservable::Voltage,
                                               ElectricalMeaning::AcceptedRange, 0, 5)}}};
        options.voltage = 3.3;
    }
    const auto compiled = assembly_rating_fixture(options);
    CHECK(leaf(evaluate(compiled), ManufacturingCheck::SelectedPartRatings).state ==
          (which == "multiple"           ? ManufacturingState::Failed
           : which == "accepted-nominal" ? ManufacturingState::NotEvaluated
                                         : ManufacturingState::NativePass));
}

TEST_CASE("Unselected footprint cannot certify the native zero CPL correction fallback",
          "[manufacturing][assembly]") {
    auto options = RatingFixtureOptions{};
    options.occurrences[0].selected = false;
    options.occurrences[0].rotation = 45;
    const auto compiled = assembly_rating_fixture(options);
    const auto result = evaluate(
        compiled, true, CplProjectionOptions{{CplRotationOffset{{"test.rating", "subject"}, 90}}});
    CHECK(leaf(result, ManufacturingCheck::CplIdentity).state == ManufacturingState::Failed);
    CHECK(leaf(result, ManufacturingCheck::CplSide).state == ManufacturingState::NativePass);
    CHECK(leaf(result, ManufacturingCheck::CplRotation).state == ManufacturingState::Failed);
    CHECK(leaf(result, ManufacturingCheck::CplRotation).observations.empty());
    REQUIRE(result.cpl());
    REQUIRE(result.cpl()->rows().size() == 1);
    CHECK_FALSE(result.cpl()->rows()[0].footprint());
    CHECK(result.cpl()->rows()[0].rotation_offset_deg() == 0);
    CHECK(result.cpl()->rows()[0].rotation_deg() == 45);
    CHECK_FALSE(result.cpl()->diagnostics().diagnostics().empty());
}
