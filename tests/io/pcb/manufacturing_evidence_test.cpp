#include "../../support/assembly_ratings_fixture.hpp"
#include "../../support/manufacturing_evidence_fixture.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>

namespace {
using namespace volt;
using Json = nlohmann::json;

const ManufacturingCoverageRow &leaf(const io::ManufacturingEvaluation &r, ManufacturingCheck c) {
    return r.coverage().coverage()[static_cast<std::size_t>(c)];
}

void not_reviewed(const io::ManufacturingEvaluation &r) {
    for (const auto &row : r.coverage().coverage())
        CHECK(row.state != ManufacturingState::ReviewedPass);
}
} // namespace

TEST_CASE("Exact evidence composes actual bytes and finite synthetic assertions",
          "[manufacturing][evidence]") {
    const auto assembly = GENERATE(false, true);
    auto options = test::evidence_options();
    options.explicit_fitted = true;
    const auto compiled = test::completion_fixture(options);
    const auto bytes = std::string{compiled.bytes()};
    const auto target = test::evidence_target(compiled, assembly);
    const auto plain = io::evaluate_manufacturing_export(compiled, target);
    CHECK(leaf(plain, ManufacturingCheck::ExportFidelity).state == ManufacturingState::NativePass);
    const auto input = test::synthetic_evidence(compiled, plain);
    const auto result = io::evaluate_manufacturing_export(compiled, target, input);
    const auto direct = io::write_pcb_fabrication_files(compiled);
    REQUIRE(result.output().files.size() == direct.files.size());
    CHECK(result.output().files.size() == 9);
    for (std::size_t i = 0; i < direct.files.size(); ++i) {
        CHECK(result.output().files[i].text == direct.files[i].text);
        CHECK(result.output().files[i].function == direct.files[i].function);
        CHECK(result.output().files[i].filename == direct.files[i].filename);
        CHECK_FALSE(direct.files[i].text.empty());
    }
    CHECK_FALSE(result.output().loss_report.has_fab_critical_warnings());
    CHECK(result.reviews().size() == (assembly ? 16 : 12));
    for (const auto &receipt : result.reviews()) {
        CHECK(receipt.disposition == io::ManufacturingReviewDisposition::Accepted);
        CHECK(leaf(result, receipt.assertion.requirement.check).state ==
              ManufacturingState::ReviewedPass);
        CHECK_FALSE(receipt.assertion.requirement.scope.empty());
    }
    for (const auto c :
         {ManufacturingCheck::MaterialRealization, ManufacturingCheck::FinishRealization,
          ManufacturingCheck::AssemblyProcess, ManufacturingCheck::DrillSeparation})
        CHECK(leaf(result, c).state == (c == ManufacturingCheck::AssemblyProcess && !assembly
                                            ? ManufacturingState::NotApplicable
                                            : ManufacturingState::NotEvaluated));
    CHECK_FALSE(result.coverage().ready());
    CHECK(result.coverage().coverage().size() == 65);
    CHECK(std::string{compiled.bytes()} == bytes);
    const auto report = Json::parse(io::write_manufacturing_evaluation(result));
    CHECK(report["schema_version"] == 3);
    CHECK(report["counts"]["reviewed_pass"] == result.reviews().size());
    CHECK(report["native_export"]["files"].size() == direct.files.size());
    CHECK(report["source"]["content_digest"] == compiled.content_digest().value());
    const auto native = evaluate_manufacturing_coverage(compiled, target);
    const auto old = Json::parse(io::write_manufacturing_coverage(native));
    CHECK(old["schema_version"] == 2);
    CHECK_FALSE(old["counts"].contains("reviewed_pass"));
    CHECK(report["assembly"] == old["assembly"]);
    CHECK(report["ratings"] == old["ratings"]);
    for (std::size_t i = 0; i < native.coverage().size(); ++i) {
        const auto check = static_cast<ManufacturingCheck>(i);
        if (!manufacturing_review_requirement(compiled, check) &&
            check != ManufacturingCheck::ExportFidelity) {
            auto expected = old["coverage"][i];
            expected["rule"] = report["coverage"][i]["rule"];
            CHECK(report["coverage"][i] == expected);
        }
    }
    const auto reopened = io::open_compiled_board(compiled.bytes());
    const auto reopened_target =
        io::read_manufacturing_target(io::write_manufacturing_target(target));
    auto reopened_input = input;
    reopened_input.assertions =
        io::read_manufacturing_evidence(io::write_manufacturing_evidence(input.assertions));
    CHECK(io::write_manufacturing_evaluation(
              io::evaluate_manufacturing_export(reopened, reopened_target, reopened_input)) ==
          io::write_manufacturing_evaluation(result));
}

TEST_CASE("Evidence verifies entire reviewed file inventory including conditional files",
          "[manufacturing][evidence]") {
    auto options = test::evidence_options();
    options.interlayer = true;
    options.via = true;
    const auto compiled = test::completion_fixture(options);
    const auto target = test::evidence_target(compiled);
    const auto plain = io::evaluate_manufacturing_export(compiled, target);
    auto input = test::synthetic_evidence(compiled, plain, ManufacturingCheck::MaskOpenings);
    input.reviewed_files = plain.output().files;
    REQUIRE(input.reviewed_files->size() >= 10);
    const auto variant = GENERATE(0, 1, 2, 3, 4, 5, 6);
    if (variant == 0)
        std::ranges::reverse(*input.reviewed_files);
    if (variant == 1)
        input.reviewed_files->front().text += "x";
    if (variant == 2)
        input.reviewed_files->pop_back();
    if (variant == 3)
        input.reviewed_files->push_back(input.reviewed_files->front());
    if (variant == 4)
        input.reviewed_files->clear();
    if (variant == 5)
        input.reviewed_files->front().filename += "x";
    if (variant == 6)
        input.reviewed_files->front().function = input.reviewed_files->back().function;
    const auto result = io::evaluate_manufacturing_export(compiled, target, input);
    CHECK(result.reviews()[0].disposition ==
          (variant == 0 ? io::ManufacturingReviewDisposition::Accepted
                        : io::ManufacturingReviewDisposition::FileInventoryMismatch));
    if (variant != 0)
        not_reviewed(result);
}

TEST_CASE("Evidence never trusts stale bindings, bytes or reduced native scopes",
          "[manufacturing][evidence]") {
    const auto compiled = test::completion_fixture(test::evidence_options());
    const auto target = test::evidence_target(compiled);
    const auto plain = io::evaluate_manufacturing_export(compiled, target);
    auto input = test::synthetic_evidence(compiled, plain, ManufacturingCheck::RequiredPolarity);
    auto &a = input.assertions[0];
    const auto variant = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
    const auto foreign = sha256_content_hash("foreign");
    if (variant == 0)
        a.binding.content_digest = foreign;
    if (variant == 1)
        a.binding.selected_closure_digest = foreign;
    if (variant == 2)
        a.binding.profile_digest = foreign;
    if (variant == 3)
        a.binding.target_digest = foreign;
    if (variant == 4)
        a.binding.source =
            CompiledBoardIdentity{BoardName{"foreign"}, a.binding.source.provenance_digest()};
    if (variant == 5)
        a.binding.files[0].digest = foreign;
    if (variant == 6)
        a.binding.files.pop_back();
    if (variant == 7)
        a.requirement.scope.pop_back();
    if (variant == 8)
        std::ranges::reverse(a.requirement.scope);
    if (variant == 9)
        a.requirement.scope.push_back(a.requirement.scope.back());
    if (variant == 10)
        input.attachments[0].bytes += "x";
    if (variant == 11)
        input.attachments[1].bytes += "x";
    if (variant == 12)
        input.attachments[1].bytes.clear();
    if (variant == 13)
        input.attachments.pop_back();
    if (variant == 14)
        input.options.basename = "different";
    const auto result = io::evaluate_manufacturing_export(compiled, target, input);
    not_reviewed(result);
    CHECK(result.reviews()[0].disposition != io::ManufacturingReviewDisposition::Accepted);
    CHECK(leaf(result, ManufacturingCheck::RequiredPolarity).state ==
          ManufacturingState::NotEvaluated);
}

TEST_CASE("Every eligible leaf requires its exact method and declared review outcome",
          "[manufacturing][evidence]") {
    auto options = test::evidence_options();
    options.explicit_fitted = true;
    const auto compiled = test::completion_fixture(options);
    const auto target = test::evidence_target(compiled, true);
    const auto plain = io::evaluate_manufacturing_export(compiled, target);
    const auto failed = GENERATE(false, true);
    auto input = test::synthetic_evidence(compiled, plain);
    for (auto &a : input.assertions) {
        if (failed)
            a.outcome = ManufacturingReviewOutcome::Fail;
        else
            a.requirement.method =
                a.requirement.method == ManufacturingReviewMethod::EmittedFabricationInspection
                    ? ManufacturingReviewMethod::IndependentAssemblyInspection
                    : ManufacturingReviewMethod::EmittedFabricationInspection;
    }
    const auto result = io::evaluate_manufacturing_export(compiled, target, input);
    for (const auto &a : input.assertions) {
        CHECK(leaf(result, a.requirement.check).state ==
              (failed ? ManufacturingState::Failed : ManufacturingState::NotEvaluated));
        CHECK(leaf(plain, a.requirement.check).state == ManufacturingState::NotEvaluated);
    }
    std::ranges::reverse(input.assertions);
    CHECK(io::write_manufacturing_evaluation(io::evaluate_manufacturing_export(
              compiled, target, input)) == io::write_manufacturing_evaluation(result));
}

TEST_CASE("Native failures and missing physical truth cannot be waived by valid-looking reviews",
          "[manufacturing][evidence]") {
    auto options = test::evidence_options();
    const auto variant = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    if (variant == 0)
        options.placements = 0;
    if (variant == 1)
        options.selected = false;
    if (variant == 2)
        options.omit_mapping = true;
    if (variant == 3) {
        options.different_nets = true;
        options.netless_zone = true;
    }
    if (variant == 4) {
        options.track_width = 0.1;
        options.tracks = true;
    }
    if (variant == 5)
        options.shape = FootprintPadShape::Oval;
    if (variant == 6)
        options.shape = FootprintPadShape::RoundedRectangle;
    if (variant == 7)
        options.disabled_top = true;
    if (variant == 8)
        options.first_x = 10.0000004;
    if (variant == 9)
        options.no_outline = true;
    if (variant == 10)
        options.outline_shift = 1e6;
    if (variant == 11)
        options.connected = false;
    if (variant == 2) {
        CHECK_THROWS_AS(test::completion_fixture(options), KernelArgumentError);
        return;
    }
    const auto compiled = test::completion_fixture(options);
    const auto target = test::evidence_target(compiled);
    const auto plain = io::evaluate_manufacturing_export(compiled, target);
    const auto input = test::synthetic_evidence(compiled, plain);
    const auto result = io::evaluate_manufacturing_export(compiled, target, input);
    not_reviewed(result);
    for (const auto &r : result.reviews())
        CHECK(r.disposition == io::ManufacturingReviewDisposition::NativePrerequisite);
    CHECK(io::write_board_completion(result.coverage().completion()) ==
          io::write_board_completion(plain.coverage().completion()));
    if (variant == 10)
        CHECK(result.output().files.empty());
}

TEST_CASE("Evidence typed boundaries reject duplicates, conflicts, invalid criteria and malformed "
          "documents",
          "[manufacturing][evidence]") {
    const auto compiled = test::completion_fixture(test::evidence_options());
    const auto target = test::evidence_target(compiled);
    const auto plain = io::evaluate_manufacturing_export(compiled, target);
    auto input = test::synthetic_evidence(compiled, plain, ManufacturingCheck::LegendHeight);
    const auto variant = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    if (variant <= 1) {
        input.assertions.push_back(input.assertions.front());
        if (variant == 1)
            input.assertions.back().outcome = ManufacturingReviewOutcome::Fail;
    }
    if (variant == 2)
        input.attachments.push_back(input.attachments.front());
    if (variant == 3)
        input.attachments.push_back({"unused", "x"});
    if (variant == 4)
        input.assertions[0].minimum_mm.reset();
    if (variant == 5)
        input.assertions[0].minimum_mm = std::numeric_limits<double>::infinity();
    if (variant == 6)
        input.assertions[0].reviewer = " ";
    if (variant == 7)
        input.assertions[0].criterion.clear();
    if (variant == 8)
        input.assertions[0].requirement.check = static_cast<ManufacturingCheck>(999);
    CHECK_THROWS_AS(io::evaluate_manufacturing_export(compiled, target, input),
                    KernelArgumentError);
}

TEST_CASE("Strict evidence codec pins versions, counts, hashes, methods and full shape",
          "[manufacturing][evidence]") {
    const auto compiled = test::completion_fixture(test::evidence_options());
    const auto plain = io::evaluate_manufacturing_export(compiled, test::evidence_target(compiled));
    const auto input = test::synthetic_evidence(compiled, plain);
    const auto text = io::write_manufacturing_evidence(input.assertions);
    CHECK(io::write_manufacturing_evidence(io::read_manufacturing_evidence(text)) == text);
    auto doc = Json::parse(text);
    const auto variant = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
    if (variant == 0)
        doc["schema_version"] = 2;
    if (variant == 1)
        doc["assertions"][0]["binding"]["policy_version"] = 2;
    if (variant == 2)
        doc["assertions"][0]["binding"]["evaluator"] = "volt.manufacturing-coverage/2";
    if (variant == 3)
        doc["assertions"][0]["binding"]["exporter_revision"] = "volt.native_fabrication/2";
    if (variant == 4)
        doc["assertions"][0]["scope_count"] = 999;
    if (variant == 5)
        doc["assertions"][0]["check"] = "unknown";
    if (variant == 6)
        doc["assertions"][0]["method"] = "unknown";
    if (variant == 7)
        doc["assertions"][0]["outcome"] = "unknown";
    if (variant == 8)
        doc["unknown"] = 1;
    if (variant == 9)
        doc["assertions"][0].erase("criterion");
    if (variant == 10)
        doc["assertions"][0]["evidence_digest"] = "sha256:bad";
    if (variant == 11)
        doc["assertions"][0]["scope"][0][0]["index"] = true;
    if (variant == 12)
        doc["assertions"].push_back(doc["assertions"][0]);
    if (variant == 13)
        doc["assertions"][0]["binding"]["digest"] = sha256_content_hash("different").value();
    try {
        (void)io::read_manufacturing_evidence(doc.dump());
        FAIL("Expected typed rejection");
    } catch (const io::ManufacturingEvidenceReadError &e) {
        CHECK(e.code() == (variant <= 3 ? io::ManufacturingEvidenceReadErrorCode::UnsupportedVersion
                                        : io::ManufacturingEvidenceReadErrorCode::Malformed));
    }
    CHECK_THROWS_AS(io::read_manufacturing_evidence("{\"format\":1,\"format\":2}"),
                    io::ManufacturingEvidenceReadError);
}

TEST_CASE("Target-only CPL corrections and additional same-code entities invalidate prior evidence",
          "[manufacturing][evidence]") {
    auto options = test::evidence_options();
    options.explicit_fitted = true;
    const auto compiled = test::completion_fixture(options);
    const auto target = test::evidence_target(compiled, true);
    const auto plain = io::evaluate_manufacturing_export(compiled, target);
    auto input = test::synthetic_evidence(compiled, plain);
    const auto changed_target = test::evidence_target(
        compiled, true,
        CplProjectionOptions{{CplRotationOffset{{"test.completion", "contact"}, 90}}});
    CHECK(changed_target.digest() != target.digest());
    not_reviewed(io::evaluate_manufacturing_export(compiled, changed_target, input));
    options.review_text = true;
    const auto changed = test::completion_fixture(options);
    const auto fresh =
        io::evaluate_manufacturing_export(changed, test::evidence_target(changed, true));
    not_reviewed(
        io::evaluate_manufacturing_export(changed, test::evidence_target(changed, true), input));
    // Rebinding the identity alone cannot conceal the newly demanded native scope.
    for (auto &a : input.assertions)
        a.binding = fresh.binding();
    const auto scoped =
        io::evaluate_manufacturing_export(changed, test::evidence_target(changed, true), input);
    CHECK(scoped.reviews()[0].disposition == io::ManufacturingReviewDisposition::ScopeMismatch);
}

TEST_CASE("Ineligible deferred/native leaves and fabrication assembly reviews stay nonpassing",
          "[manufacturing][evidence]") {
    const auto compiled = test::completion_fixture(test::evidence_options());
    const auto target = test::evidence_target(compiled);
    const auto plain = io::evaluate_manufacturing_export(compiled, target);
    for (std::size_t i = 0; i < 65; ++i) {
        const auto check = static_cast<ManufacturingCheck>(i);
        if (manufacturing_review_requirement(compiled, check) &&
            check < ManufacturingCheck::PartPackageReview)
            continue;
        auto input = test::synthetic_evidence(compiled, plain, ManufacturingCheck::MaskOpenings);
        input.assertions[0].requirement.check = check;
        const auto result = io::evaluate_manufacturing_export(compiled, target, input);
        CHECK(result.reviews()[0].disposition == io::ManufacturingReviewDisposition::Ineligible);
        CHECK(leaf(result, check).state == leaf(plain, check).state);
    }
}

TEST_CASE("Unsupported process and foreign target identity cannot authorize evidence",
          "[manufacturing][evidence]") {
    const auto compiled = test::completion_fixture(test::evidence_options());
    const auto target = test::evidence_target(compiled);
    auto doc = Json::parse(io::write_manufacturing_target(target));
    const auto variant = GENERATE(0, 1, 2);
    if (variant == 0) {
        doc["process"]["via_treatment"] = "Tented";
        doc["profile"]["applicability"]["via_treatment"] = "Tented";
    }
    if (variant == 1)
        doc["source"]["content_digest"] = sha256_content_hash("foreign").value();
    if (variant == 2)
        doc["profile"]["capability"]["profile"]["name"] = "Foreign profile";
    const auto changed = io::read_manufacturing_target(doc.dump());
    const auto plain = io::evaluate_manufacturing_export(compiled, changed);
    const auto input = test::synthetic_evidence(compiled, plain);
    const auto result = io::evaluate_manufacturing_export(compiled, changed, input);
    not_reviewed(result);
    CHECK(result.coverage().status() ==
          (variant == 0 ? ManufacturingStatus::UnsupportedTarget : ManufacturingStatus::Blocked));
}

TEST_CASE("Native scope preserves placement-local pads and demands newly added occurrences",
          "[manufacturing][evidence]") {
    auto options = test::evidence_options();
    const auto compiled = test::completion_fixture(options);
    const auto target = test::evidence_target(compiled);
    const auto plain = io::evaluate_manufacturing_export(compiled, target);
    auto input = test::synthetic_evidence(compiled, plain, ManufacturingCheck::MaskOpenings);
    const auto &scope = input.assertions[0].requirement.scope;
    CHECK(std::ranges::find(
              scope, std::vector{EntityRef::component_placement(ComponentPlacementId{0}),
                                 EntityRef::footprint_pad(FootprintPadId{0})}) != scope.end());
    CHECK(std::ranges::find(
              scope, std::vector{EntityRef::component_placement(ComponentPlacementId{1}),
                                 EntityRef::footprint_pad(FootprintPadId{0})}) != scope.end());
    options.components = 3;
    options.placements = 3;
    const auto added = test::completion_fixture(options);
    const auto added_target = test::evidence_target(added);
    const auto fresh = io::evaluate_manufacturing_export(added, added_target);
    CHECK(manufacturing_review_requirement(added, ManufacturingCheck::MaskOpenings)->scope.size() >
          scope.size());
    input.assertions[0].binding = fresh.binding();
    const auto result = io::evaluate_manufacturing_export(added, added_target, input);
    CHECK(result.reviews()[0].disposition == io::ManufacturingReviewDisposition::ScopeMismatch);
}

TEST_CASE("Empty geometry still demands explicit board-wide reviews and remaining native coverage",
          "[manufacturing][evidence]") {
    auto options = test::evidence_options();
    options.components = 0;
    options.placements = 0;
    options.zone = false;
    const auto compiled = test::completion_fixture(options);
    const auto target = test::evidence_target(compiled);
    const auto plain = io::evaluate_manufacturing_export(compiled, target);
    CHECK(plain.reviews().empty());
    CHECK(leaf(plain, ManufacturingCheck::RequiredPinOne).state ==
          ManufacturingState::NotEvaluated);
    const auto input = test::synthetic_evidence(compiled, plain);
    const auto result = io::evaluate_manufacturing_export(compiled, target, input);
    CHECK(result.reviews().size() == 12);
    for (const auto &r : result.reviews())
        CHECK_FALSE(r.assertion.requirement.scope.empty());
    CHECK_FALSE(result.coverage().ready());
    CHECK(leaf(result, ManufacturingCheck::SelectedPartRatings).state ==
          ManufacturingState::NotEvaluated);
}

TEST_CASE("Export binding token encoding has a portable semantic golden",
          "[manufacturing][evidence]") {
    const auto h = sha256_content_hash("synthetic");
    const auto binding = io::ManufacturingExportBinding{
        CompiledBoardIdentity{BoardName{"Synthetic"}, h},
        h,
        h,
        h,
        h,
        {},
        {},
        {{"example.GTL", "copper-top", h}, {"example.GBL", "copper-bottom", h}}};
    CHECK(binding.digest().value() ==
          "sha256:f73e0f04a81f52a55a7d8e9583412694c99da93266dabd9ff46b79611d590dc6");
    auto reversed = binding;
    std::ranges::reverse(reversed.files);
    CHECK(reversed.digest() == binding.digest());
    reversed.basename = "example";
    CHECK(reversed.digest() != binding.digest());
}
