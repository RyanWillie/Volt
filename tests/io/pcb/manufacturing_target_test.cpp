#include "../../support/manufacturing_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <limits>
#include <nlohmann/json.hpp>
#include <ranges>

namespace {
using namespace volt;
using Json = nlohmann::json;

const ManufacturingCoverageRow &leaf(const ManufacturingCoverage &r, ManufacturingCheck key) {
    return r.coverage()[static_cast<std::size_t>(key)];
}

ManufacturingCoverage changed(const CompiledBoard &compiled, Json doc) {
    return evaluate_manufacturing_coverage(compiled, io::read_manufacturing_target(doc.dump()));
}

Json document(const CompiledBoard &compiled) {
    return Json::parse(io::write_manufacturing_target(test::manufacturing_target(compiled)));
}
} // namespace

TEST_CASE("Manufacturing fixed inventory retains every required deferred leaf", "[manufacturing]") {
    const auto compiled = test::completion_fixture(test::manufacturing_options());
    const auto target = test::manufacturing_target(compiled);
    const auto result = evaluate_manufacturing_coverage(compiled, target);
    CHECK(result.status() == ManufacturingStatus::Blocked);
    CHECK_FALSE(result.ready());
    CHECK(result.coverage().size() == static_cast<std::size_t>(ManufacturingCheck::Count));
    const auto json = Json::parse(io::write_manufacturing_coverage(result));
    CHECK(json["counts"]["required"] == result.coverage().size());
    for (std::size_t i = 0; i < result.coverage().size(); ++i) {
        const auto &row = result.coverage()[i];
        CHECK(row.check == static_cast<ManufacturingCheck>(i));
        CHECK(json["coverage"][i]["check"] == std::string{manufacturing_check_name(row.check)});
        CHECK(json["coverage"][i]["required"] == true);
        CHECK(json["coverage"][i]["entity_count"] == row.entities.size());
        CHECK(json["coverage"][i]["observation_count"] == row.observations.size());
        CHECK(std::ranges::is_sorted(row.entities));
    }
    for (const auto key : {ManufacturingCheck::MaskOpenings,
                           ManufacturingCheck::MaskCopperClearance,
                           ManufacturingCheck::MaskWeb,
                           ManufacturingCheck::MaskSliver,
                           ManufacturingCheck::ViaCovering,
                           ManufacturingCheck::LegendHeight,
                           ManufacturingCheck::LegendStroke,
                           ManufacturingCheck::LegendPadClearance,
                           ManufacturingCheck::LegendMaskClearance,
                           ManufacturingCheck::LegendClipping,
                           ManufacturingCheck::RequiredPinOne,
                           ManufacturingCheck::RequiredPolarity,
                           ManufacturingCheck::SelectedPartRatings,
                           ManufacturingCheck::ExportFidelity,
                           ManufacturingCheck::PthDrillRange,
                           ManufacturingCheck::NpthDrillRange,
                           ManufacturingCheck::DrillSeparation,
                           ManufacturingCheck::PadPthAnnulus,
                           ManufacturingCheck::SlotWidth,
                           ManufacturingCheck::SlotLength,
                           ManufacturingCheck::OutlineClearance,
                           ManufacturingCheck::MechanicalClearance,
                           ManufacturingCheck::MaterialRealization,
                           ManufacturingCheck::FinishRealization})
        CHECK(leaf(result, key).state == ManufacturingState::NotEvaluated);
    for (auto key = static_cast<std::size_t>(ManufacturingCheck::TrackTrackSpacing);
         key <= static_cast<std::size_t>(ManufacturingCheck::ZoneZoneSpacing); ++key)
        CHECK(result.coverage()[key].state == ManufacturingState::NotEvaluated);
    CHECK(leaf(result, ManufacturingCheck::SlotTreatment).state ==
          ManufacturingState::NotApplicable);
    CHECK_FALSE(leaf(result, ManufacturingCheck::SlotTreatment).applicable);
    CHECK(leaf(result, ManufacturingCheck::CopperWidth).state == ManufacturingState::NativePass);
    CHECK(leaf(result, ManufacturingCheck::CopperWidth).observations.size() == 1);
    CHECK(leaf(result, ManufacturingCheck::CopperWidth).entities.size() == 2);
    const auto physical = io::write_board_completion(evaluate_board_completion(compiled));
    CHECK(io::write_board_completion(result.completion()) == physical);
    CHECK_FALSE(json.contains("diagnostics"));
    CHECK(json["completion"] == Json::parse(physical));
    CHECK(json["source"] == json["completion"]["source"]);
    CHECK(json["provenance"] == json["completion"]["provenance"]);
    CHECK(json["profile_digest"] == result.profile_digest().value());
    for (const auto &item : json["coverage"])
        CHECK_FALSE(item.contains("profile_digest"));
    const auto &export_row = leaf(result, ManufacturingCheck::ExportFidelity);
    CHECK(export_row.owner == ManufacturingOwner::DeferredIO);
    CHECK(export_row.blocker == ManufacturingBlocker::MissingCoverage);
    CHECK(export_row.reason == "Required checker or exact evidence is not implemented");
    CHECK(export_row.remediation ==
          "Complete the required native checker or independently reviewed evidence stage");
    CHECK(export_row.entities == std::vector{EntityRef::board()});
}

TEST_CASE(
    "Manufacturing synthetic profiles use matching immutable artifacts and different outcomes",
    "[manufacturing]") {
    const auto a = test::completion_fixture(test::manufacturing_options());
    const auto b = test::completion_fixture(test::manufacturing_options(true));
    const auto ta = test::manufacturing_target(a);
    const auto tb = test::manufacturing_target(b, true);
    CHECK(ta.digest() != tb.digest());
    CHECK(ta.profile().digest() != tb.profile().digest());
    CHECK(a.identity() != b.identity());
    const auto ra = evaluate_manufacturing_coverage(a, ta);
    const auto rb = evaluate_manufacturing_coverage(b, tb);
    CHECK(leaf(ra, ManufacturingCheck::ArtifactIdentity).state == ManufacturingState::NativePass);
    CHECK(leaf(rb, ManufacturingCheck::ArtifactIdentity).state == ManufacturingState::NativePass);
    CHECK(leaf(ra, ManufacturingCheck::CopperWidth).state == ManufacturingState::NativePass);
    CHECK(leaf(rb, ManufacturingCheck::CopperWidth).state == ManufacturingState::Failed);
    CHECK_FALSE(ra.ready());
    CHECK_FALSE(rb.ready());
    CHECK(
        leaf(evaluate_manufacturing_coverage(a, tb), ManufacturingCheck::ArtifactIdentity).state ==
        ManufacturingState::Failed);
    const auto foreign = ManufacturingTarget{a.identity(), a.content_digest(), ta.goal(),
                                             ta.process(), tb.profile()};
    CHECK(leaf(evaluate_manufacturing_coverage(a, foreign), ManufacturingCheck::ArtifactIdentity)
              .state == ManufacturingState::Failed);
}

TEST_CASE("Manufacturing implemented measurements have positive negative inclusive boundaries",
          "[manufacturing]") {
    const auto key = GENERATE(ManufacturingCheck::CopperWidth, ManufacturingCheck::ViaDrill,
                              ManufacturingCheck::ViaCopperDiameter,
                              ManufacturingCheck::ViaRadialAnnulus, ManufacturingCheck::DimensionX,
                              ManufacturingCheck::DimensionY, ManufacturingCheck::BoardThickness);
    const auto offset = GENERATE(-0.0001, 0.0, 0.0001);
    const auto upper = GENERATE(false, true);
    auto options = test::manufacturing_options();
    if (key == ManufacturingCheck::CopperWidth)
        options.track_width = 0.15 + offset;
    if (key == ManufacturingCheck::ViaDrill)
        options.via_drill = 0.2 + offset;
    if (key == ManufacturingCheck::ViaCopperDiameter)
        options.via_diameter = 0.6 + offset;
    const auto compiled = test::completion_fixture(options);
    auto doc = document(compiled);
    auto expected_fail = offset < 0.0;
    if (key == ManufacturingCheck::ViaRadialAnnulus) {
        doc["profile"]["minimum_via_radial_annulus_mm"] = 0.25 - offset;
    } else if (key == ManufacturingCheck::DimensionX || key == ManufacturingCheck::DimensionY ||
               key == ManufacturingCheck::BoardThickness) {
        auto &range =
            key == ManufacturingCheck::BoardThickness
                ? doc["profile"]["capability"]["profile"]["board_thickness_range_mm"]
                : doc["profile"]
                     [key == ManufacturingCheck::DimensionX ? "dimension_x_mm" : "dimension_y_mm"];
        const auto actual = key == ManufacturingCheck::DimensionX   ? 30.0
                            : key == ManufacturingCheck::DimensionY ? 20.0
                                                                    : 1.6;
        if (key == ManufacturingCheck::BoardThickness) {
            // Compile the matching profile; changing a profile under an artifact is forbidden.
            const auto cap = BoardCapabilityProfile{
                "Synthetic range",
                {"synthetic://range", "2026-10-19"},
                0.15,
                0.2,
                0.6,
                {},
                {},
                {2},
                BoardCapabilityRange{upper ? 1.0 : actual - offset, upper ? actual + offset : 2.0},
                {1.0},
                BoardCapabilityRange{0.2, 2.0}};
            options.capability = cap;
            const auto matching = test::completion_fixture(options);
            const auto profile = ManufacturingProfile{cap,
                                                      sha256_content_hash("synthetic range"),
                                                      test::manufacturing_process(),
                                                      BoardCapabilityRange{10, 40},
                                                      BoardCapabilityRange{10, 30},
                                                      0.15};
            const auto result = evaluate_manufacturing_coverage(
                matching, ManufacturingTarget{matching.identity(), matching.content_digest(),
                                              ManufacturingGoal::Fabrication,
                                              test::manufacturing_process(), profile});
            CHECK(leaf(result, key).state ==
                  (expected_fail ? ManufacturingState::Failed : ManufacturingState::NativePass));
            return;
        }
        range[upper ? "maximum_mm" : "minimum_mm"] = upper ? actual + offset : actual - offset;
    }
    const auto result = changed(compiled, doc);
    CHECK(leaf(result, key).state ==
          (expected_fail ? ManufacturingState::Failed : ManufacturingState::NativePass));
    REQUIRE(leaf(result, key).observations.size() == 1);
    CHECK(leaf(result, key).observations[0].state == leaf(result, key).state);
    CHECK(leaf(result, key).observations[0].unit == ManufacturingUnit::Millimetres);
}

TEST_CASE("Manufacturing exact board selections and missing native metadata cannot pass",
          "[manufacturing]") {
    const auto variant = GENERATE("thickness", "top", "bottom", "width", "height", "stack",
                                  "outline", "weight", "extra", "disabled", "slot");
    auto options = test::manufacturing_options();
    auto key = ManufacturingCheck::BoardThickness;
    auto expected = ManufacturingState::Failed;
    const auto v = std::string_view{variant};
    if (v == "thickness")
        options.thickness = 1.7;
    if (v == "top") {
        options.top_weight = 2.0;
        key = ManufacturingCheck::TopCopperWeight;
    }
    if (v == "bottom") {
        options.bottom_weight = 2.0;
        key = ManufacturingCheck::BottomCopperWeight;
    }
    if (v == "width") {
        options.width = 31;
        key = ManufacturingCheck::DimensionX;
    }
    if (v == "height") {
        options.height = 21;
        key = ManufacturingCheck::DimensionY;
    }
    if (v == "stack") {
        options.no_stack = true;
        key = ManufacturingCheck::CopperLayerCount;
        expected = ManufacturingState::NotEvaluated;
    }
    if (v == "outline") {
        options.no_outline = true;
        key = ManufacturingCheck::DimensionX;
        expected = ManufacturingState::NotEvaluated;
    }
    if (v == "weight") {
        options.top_weight.reset();
        key = ManufacturingCheck::TopCopperWeight;
        expected = ManufacturingState::NotEvaluated;
    }
    if (v == "extra") {
        options.extra_copper = true;
        key = ManufacturingCheck::CopperLayerCount;
    }
    if (v == "disabled") {
        options.disabled_top = true;
        key = ManufacturingCheck::CopperLayerCount;
    }
    if (v == "slot") {
        options.fab_critical_slot = true;
        key = ManufacturingCheck::SlotTreatment;
    }
    const auto compiled = test::completion_fixture(options);
    const auto result =
        evaluate_manufacturing_coverage(compiled, test::manufacturing_target(compiled));
    CHECK(leaf(result, key).state == expected);
    if (expected == ManufacturingState::NotEvaluated)
        CHECK_FALSE(leaf(result, key).entities.empty());
    CHECK_FALSE(result.ready());
    if (v == "slot") {
        CHECK(result.status() == ManufacturingStatus::UnsupportedTarget);
        for (const auto check :
             {ManufacturingCheck::CopperWidth, ManufacturingCheck::ViaDrill,
              ManufacturingCheck::ViaCopperDiameter, ManufacturingCheck::ViaRadialAnnulus,
              ManufacturingCheck::DimensionX, ManufacturingCheck::DimensionY,
              ManufacturingCheck::CopperLayerCount, ManufacturingCheck::BoardThickness,
              ManufacturingCheck::TopCopperWeight, ManufacturingCheck::BottomCopperWeight})
            CHECK(leaf(result, check).state == ManufacturingState::NotEvaluated);
    }
}

TEST_CASE("Manufacturing missing required selections and unsupported choices block",
          "[manufacturing]") {
    const auto compiled = test::completion_fixture(test::manufacturing_options());
    const auto original = document(compiled);
    for (const auto &entry : original.at("process").items()) {
        auto doc = original;
        doc["process"][entry.key()] = nullptr;
        const auto result = changed(compiled, doc);
        CHECK(leaf(result, ManufacturingCheck::ProcessSelections).state ==
              ManufacturingState::NotEvaluated);
        CHECK(leaf(result, ManufacturingCheck::CopperWidth).state ==
              ManufacturingState::NotEvaluated);
        doc = original;
        doc["profile"]["applicability"][entry.key()] = nullptr;
        CHECK(leaf(changed(compiled, doc), ManufacturingCheck::ProfileApplicability).state ==
              ManufacturingState::NotEvaluated);
    }
    for (const auto &[field, value] :
         std::vector<std::pair<std::string, Json>>{{"family", "Flex"},
                                                   {"material", "OtherFr4"},
                                                   {"copper_layers", 4},
                                                   {"finish", "Enig"},
                                                   {"mask_sides", "SingleSide"},
                                                   {"mask_sides", "NoMask"},
                                                   {"mask_colour", "OtherColour"},
                                                   {"via_treatment", "Tented"},
                                                   {"via_treatment", "Filled"},
                                                   {"drilling", "Laser"},
                                                   {"holes", "BlindBuried"},
                                                   {"slots", "Routed"},
                                                   {"slots", "Plated"},
                                                   {"assembly", "OtherAssembly"}}) {
        auto doc = original;
        doc["process"][field] = value;
        const auto result = changed(compiled, doc);
        CHECK(result.status() == ManufacturingStatus::UnsupportedTarget);
        CHECK(leaf(result, ManufacturingCheck::ProcessSelections).state ==
              ManufacturingState::Failed);
        CHECK(leaf(result, ManufacturingCheck::CopperWidth).state ==
              ManufacturingState::NotEvaluated);
    }
    auto doc = original;
    doc["profile"]["applicability"]["thickness_mm"] = 1.7;
    CHECK(leaf(changed(compiled, doc), ManufacturingCheck::ProfileApplicability).state ==
          ManufacturingState::Failed);
}

TEST_CASE(
    "Manufacturing target codecs reject malformed data unsupported versions and policy injection",
    "[manufacturing]") {
    const auto compiled = test::completion_fixture(test::manufacturing_options());
    const auto original = document(compiled);
    for (const auto *field :
         {"coverage", "waivers", "manual_evidence", "not_applicable", "required_rows"}) {
        auto doc = original;
        doc[field] = Json::array();
        CHECK_THROWS_AS(io::read_manufacturing_target(doc.dump()),
                        io::ManufacturingTargetReadError);
    }
    for (const auto *field : {"schema_version", "policy_version"}) {
        auto doc = original;
        doc[field] = 2;
        try {
            static_cast<void>(io::read_manufacturing_target(doc.dump()));
            FAIL("Version accepted");
        } catch (const io::ManufacturingTargetReadError &error) {
            CHECK(error.code() == io::ManufacturingTargetReadErrorCode::UnsupportedVersion);
        }
    }
    auto embedded = original;
    embedded["profile"]["capability"]["version"] = 2;
    try {
        static_cast<void>(io::read_manufacturing_target(embedded.dump()));
        FAIL("Embedded capability version accepted");
    } catch (const io::ManufacturingTargetReadError &error) {
        CHECK(error.code() == io::ManufacturingTargetReadErrorCode::UnsupportedVersion);
    }
    for (const auto &bad : std::vector<Json>{true, "1", -1, 1.5}) {
        auto doc = original;
        doc["process"]["copper_layers"] = bad;
        CHECK_THROWS_AS(io::read_manufacturing_target(doc.dump()),
                        io::ManufacturingTargetReadError);
    }
    for (const auto *field : {"thickness_mm", "top_copper_weight_oz", "bottom_copper_weight_oz",
                              "dimension_x_mm", "dimension_y_mm"}) {
        auto doc = original;
        doc["process"][field] = 0;
        CHECK_THROWS_AS(io::read_manufacturing_target(doc.dump()),
                        io::ManufacturingTargetReadError);
    }
    for (const auto &count :
         std::vector<Json>{4294967298ULL, 18446744073709551615ULL, -4294967294LL, 2147483648ULL}) {
        auto count_doc = original;
        count_doc["profile"]["capability"]["profile"]["supported_copper_layer_counts"] =
            Json::array({count});
        CHECK_THROWS_AS(io::read_manufacturing_target(count_doc.dump()),
                        io::ManufacturingTargetReadError);
    }
    auto doc = original;
    doc["process"]["finish"] = "Unknown";
    CHECK_THROWS_AS(io::read_manufacturing_target(doc.dump()), io::ManufacturingTargetReadError);
    doc = original;
    doc["source"]["content_digest"] = "sha256:bad";
    CHECK_THROWS_AS(io::read_manufacturing_target(doc.dump()), io::ManufacturingTargetReadError);
    doc = original;
    doc["profile"]["dimension_x_mm"]["maximum_mm"] = 1;
    CHECK_THROWS_AS(io::read_manufacturing_target(doc.dump()), io::ManufacturingTargetReadError);
    doc = original;
    doc["profile"]["capability"]["profile"]["unknown"] = 1;
    CHECK_THROWS_AS(io::read_manufacturing_target(doc.dump()), io::ManufacturingTargetReadError);
    auto duplicate = original.dump();
    duplicate.insert(1, "\"schema_version\":1,");
    CHECK_THROWS_AS(io::read_manufacturing_target(duplicate), io::ManufacturingTargetReadError);
    CHECK_THROWS_AS(io::read_manufacturing_target("{broken"), io::ManufacturingTargetReadError);
    auto process = test::manufacturing_process();
    process.thickness_mm = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS((ManufacturingTarget{compiled.identity(), compiled.content_digest(),
                                         ManufacturingGoal::Fabrication, process,
                                         test::manufacturing_profile()}),
                    KernelArgumentError);
    process = test::manufacturing_process();
    process.finish = static_cast<ManufacturingFinish>(999);
    CHECK_THROWS_AS((ManufacturingTarget{compiled.identity(), compiled.content_digest(),
                                         ManufacturingGoal::Fabrication, process,
                                         test::manufacturing_profile()}),
                    KernelArgumentError);
}

TEST_CASE(
    "Manufacturing assembly CPL identity is canonical and never satisfies deferred assembly checks",
    "[manufacturing]") {
    const auto compiled = test::completion_fixture(test::manufacturing_options());
    auto doc = document(compiled);
    doc["goal"] = "FabricationAndAssembly";
    doc["process"]["assembly"] = "SmtAndThroughHole";
    doc["profile"]["applicability"]["assembly"] = "SmtAndThroughHole";
    CHECK(leaf(changed(compiled, doc), ManufacturingCheck::ProcessSelections).state ==
          ManufacturingState::NotEvaluated);
    doc["cpl_options"] = {{"rotation_offsets", Json::array()}};
    const auto empty = io::read_manufacturing_target(doc.dump());
    const auto result = evaluate_manufacturing_coverage(compiled, empty);
    CHECK(leaf(result, ManufacturingCheck::ProcessSelections).state ==
          ManufacturingState::NativePass);
    CHECK(leaf(result, ManufacturingCheck::SlotTreatment).state ==
          ManufacturingState::NotApplicable);
    CHECK_FALSE(leaf(result, ManufacturingCheck::SlotTreatment).applicable);
    for (const auto key :
         {ManufacturingCheck::PartPackageReview, ManufacturingCheck::LandPatternReview,
          ManufacturingCheck::PinMapReview, ManufacturingCheck::AssemblyOrientation,
          ManufacturingCheck::SelectedPartRatings, ManufacturingCheck::AssemblyProcess})
        CHECK(leaf(result, key).state == ManufacturingState::NotEvaluated);
    CHECK(leaf(result, ManufacturingCheck::PopulatedQuantities).state ==
          ManufacturingState::Failed);
    for (const auto key : {ManufacturingCheck::DnpQuantities,
                           ManufacturingCheck::SelectedAlternates, ManufacturingCheck::CplIdentity,
                           ManufacturingCheck::CplSide, ManufacturingCheck::CplRotation})
        CHECK(leaf(result, key).state == ManufacturingState::NativePass);
    doc["cpl_options"]["rotation_offsets"] =
        Json::array({{{"library", "z"}, {"name", "b"}, {"rotation_deg", 90}},
                     {{"library", "a"}, {"name", "a"}, {"rotation_deg", 0}}});
    const auto offsets = io::read_manufacturing_target(doc.dump());
    CHECK(offsets.digest() != empty.digest());
    std::reverse(doc["cpl_options"]["rotation_offsets"].begin(),
                 doc["cpl_options"]["rotation_offsets"].end());
    CHECK(io::read_manufacturing_target(doc.dump()).digest() == offsets.digest());
    doc["cpl_options"]["rotation_offsets"][0]["rotation_deg"] = -0.0;
    CHECK(io::read_manufacturing_target(doc.dump()).digest() == offsets.digest());
    CHECK(io::write_manufacturing_target(io::read_manufacturing_target(doc.dump())) ==
          io::write_manufacturing_target(offsets));
    doc["cpl_options"]["rotation_offsets"].push_back(doc["cpl_options"]["rotation_offsets"][0]);
    CHECK_THROWS_AS(io::read_manufacturing_target(doc.dump()), io::ManufacturingTargetReadError);
    doc["cpl_options"]["rotation_offsets"][2]["rotation_deg"] = 180;
    CHECK_THROWS_AS(io::read_manufacturing_target(doc.dump()), io::ManufacturingTargetReadError);
}

TEST_CASE("Manufacturing fresh reopened reports and deterministic codecs preserve full payload",
          "[manufacturing]") {
    const auto compiled = test::completion_fixture(test::manufacturing_options());
    const auto before = std::string{compiled.bytes()};
    const auto target = test::manufacturing_target(compiled);
    const auto text = io::write_manufacturing_target(target);
    const auto reopened_target = io::read_manufacturing_target(text);
    const auto reopened = io::open_compiled_board(compiled.bytes());
    CHECK(io::write_manufacturing_target(reopened_target) == text);
    CHECK(target.digest() == reopened_target.digest());
    auto reordered = nlohmann::ordered_json::object();
    const auto doc = Json::parse(text);
    for (auto it = doc.rbegin(); it != doc.rend(); ++it)
        reordered[it.key()] = it.value();
    CHECK(io::read_manufacturing_target(reordered.dump()).digest() == target.digest());
    CHECK(io::write_manufacturing_coverage(evaluate_manufacturing_coverage(compiled, target)) ==
          io::write_manufacturing_coverage(
              evaluate_manufacturing_coverage(reopened, reopened_target)));
    CHECK(std::string{compiled.bytes()} == before);
}

TEST_CASE("Manufacturing missing native profile limits cannot become unchecked passes",
          "[manufacturing]") {
    const auto missing = GENERATE("counts", "thickness", "weights", "refinement-weight");
    const auto which = std::string_view{missing};
    const auto capability = BoardCapabilityProfile{
        "Synthetic missing limits",
        {"synthetic://missing", "2026-10-19"},
        0.15,
        0.2,
        0.6,
        {},
        which == "refinement-weight"
            ? std::vector{BoardCapabilityCopperWeightRefinement{1, 0.3, 0.2}}
            : std::vector<BoardCapabilityCopperWeightRefinement>{},
        which == "counts" ? std::vector<int>{} : std::vector{2},
        which == "thickness" ? std::nullopt : std::optional{BoardCapabilityRange{1.0, 2.0}},
        which == "weights" ? std::vector<double>{} : std::vector{1.0},
        BoardCapabilityRange{0.2, 2.0}};
    auto options = test::manufacturing_options();
    options.capability = capability;
    if (which == "refinement-weight")
        options.top_weight.reset();
    const auto compiled = test::completion_fixture(options);
    const auto profile = ManufacturingProfile{capability,
                                              sha256_content_hash("Synthetic missing limits"),
                                              test::manufacturing_process(),
                                              BoardCapabilityRange{10, 40},
                                              BoardCapabilityRange{10, 30},
                                              0.15};
    const auto result = evaluate_manufacturing_coverage(
        compiled, ManufacturingTarget{compiled.identity(), compiled.content_digest(),
                                      ManufacturingGoal::Fabrication, test::manufacturing_process(),
                                      profile});
    const auto key = which == "counts"      ? ManufacturingCheck::CopperLayerCount
                     : which == "thickness" ? ManufacturingCheck::BoardThickness
                     : which == "weights"   ? ManufacturingCheck::TopCopperWeight
                                            : ManufacturingCheck::CopperWidth;
    CHECK(leaf(result, key).state == ManufacturingState::NotEvaluated);
}

TEST_CASE("Manufacturing weight refinements constrain actual track widths", "[manufacturing]") {
    const auto capability = BoardCapabilityProfile{"Synthetic refinement",
                                                   {"synthetic://refinement", "2026-10-19"},
                                                   0.15,
                                                   0.2,
                                                   0.6,
                                                   {},
                                                   {{1.0, 0.25, 0.2}},
                                                   {2},
                                                   BoardCapabilityRange{1.0, 2.0},
                                                   {1.0, 2.0}};
    auto options = test::manufacturing_options();
    options.capability = capability;
    const auto width = GENERATE(0.2499, 0.25, 0.2501);
    options.track_width = width;
    const auto compiled = test::completion_fixture(options);
    const auto profile = ManufacturingProfile{capability,
                                              sha256_content_hash("Synthetic refinement"),
                                              test::manufacturing_process(),
                                              BoardCapabilityRange{10, 40},
                                              BoardCapabilityRange{10, 30},
                                              0.15};
    const auto result = evaluate_manufacturing_coverage(
        compiled, ManufacturingTarget{compiled.identity(), compiled.content_digest(),
                                      ManufacturingGoal::Fabrication, test::manufacturing_process(),
                                      profile});
    CHECK(leaf(result, ManufacturingCheck::CopperWidth).state ==
          (width < 0.25 ? ManufacturingState::Failed : ManufacturingState::NativePass));
    CHECK(leaf(result, ManufacturingCheck::CopperWidth).observations[0].minimum == 0.25);
}

TEST_CASE("Manufacturing discrete copper counts and weights use pinned availability",
          "[manufacturing]") {
    const auto unsupported_count = GENERATE(false, true);
    const auto unsupported_weight = GENERATE(false, true);
    const auto capability = BoardCapabilityProfile{"Synthetic discrete availability",
                                                   {"synthetic://discrete", "2026-10-19"},
                                                   0.15,
                                                   0.2,
                                                   0.6,
                                                   {},
                                                   {},
                                                   {unsupported_count ? 4 : 2},
                                                   BoardCapabilityRange{1.0, 2.0},
                                                   {unsupported_weight ? 2.0 : 1.0}};
    auto options = test::manufacturing_options();
    options.capability = capability;
    const auto compiled = test::completion_fixture(options);
    const auto profile =
        ManufacturingProfile{capability,
                             sha256_content_hash("Synthetic discrete availability"),
                             test::manufacturing_process(),
                             BoardCapabilityRange{10, 40},
                             BoardCapabilityRange{10, 30},
                             0.15};
    const auto result = evaluate_manufacturing_coverage(
        compiled, ManufacturingTarget{compiled.identity(), compiled.content_digest(),
                                      ManufacturingGoal::Fabrication, test::manufacturing_process(),
                                      profile});
    CHECK(leaf(result, ManufacturingCheck::CopperLayerCount).state ==
          (unsupported_count ? ManufacturingState::Failed : ManufacturingState::NativePass));
    for (const auto key :
         {ManufacturingCheck::TopCopperWeight, ManufacturingCheck::BottomCopperWeight}) {
        CHECK(leaf(result, key).state ==
              (unsupported_weight ? ManufacturingState::Failed : ManufacturingState::NativePass));
        CHECK(leaf(result, key).observations[0].unit == ManufacturingUnit::OuncesPerSquareFoot);
        CHECK(leaf(result, key).entities.size() == 1);
    }
}

TEST_CASE("Manufacturing empty inventories require known limits and preserve closed deferred scope",
          "[manufacturing]") {
    auto options = test::manufacturing_options();
    options.components = 0;
    options.placements = 0;
    options.via = false;
    options.tracks = false;
    options.zone = false;
    const auto compiled = test::completion_fixture(options);
    auto doc = document(compiled);
    const auto result = changed(compiled, doc);
    for (const auto key :
         {ManufacturingCheck::CopperWidth, ManufacturingCheck::ViaDrill,
          ManufacturingCheck::ViaCopperDiameter, ManufacturingCheck::ViaRadialAnnulus}) {
        CHECK(leaf(result, key).state == ManufacturingState::NativePass);
        CHECK(leaf(result, key).observations.empty());
    }
    doc["profile"]["minimum_via_radial_annulus_mm"] = nullptr;
    CHECK(leaf(changed(compiled, doc), ManufacturingCheck::ViaRadialAnnulus).state ==
          ManufacturingState::NotEvaluated);
    CHECK(leaf(result, ManufacturingCheck::SelectedPartRatings).state ==
          ManufacturingState::NotEvaluated);
    CHECK(leaf(result, ManufacturingCheck::SelectedPartRatings).entities.empty());
}

TEST_CASE("Manufacturing profile and target semantic encodings have fixed golden digests",
          "[manufacturing]") {
    const auto profile = test::manufacturing_profile();
    const auto target = ManufacturingTarget{
        CompiledBoardIdentity{BoardName{"Golden"}, sha256_content_hash("provenance")},
        sha256_content_hash("artifact"), ManufacturingGoal::Fabrication,
        test::manufacturing_process(), profile};
    CHECK(profile.digest().value() ==
          "sha256:adf8243344c1dd27d89d173f16576d62474fcb2aa20bae600faa8ed5dfd3b4a3");
    CHECK(target.digest().value() ==
          "sha256:bde999d2e7909951e0a8842b334412c5e0fe527de384a11c33d77fbd34eef6bb");
}

TEST_CASE("Manufacturing uses the native highest applicable copper weight refinement",
          "[manufacturing]") {
    const auto capability = BoardCapabilityProfile{"Synthetic nonmonotonic refinement",
                                                   {"synthetic://nonmonotonic", "2026-10-19"},
                                                   0.15,
                                                   0.2,
                                                   0.6,
                                                   {},
                                                   {{1.0, 0.30, 0.2}, {2.0, 0.20, 0.2}},
                                                   {2},
                                                   BoardCapabilityRange{1.0, 2.0},
                                                   {1.0, 2.0}};
    auto options = test::manufacturing_options();
    options.capability = capability;
    options.top_weight = 2.0;
    options.bottom_weight = 2.0;
    options.track_width = 0.25;
    auto process = test::manufacturing_process();
    process.top_copper_weight_oz = 2.0;
    process.bottom_copper_weight_oz = 2.0;
    const auto compiled = test::completion_fixture(options);
    const auto profile = ManufacturingProfile{capability,
                                              sha256_content_hash("Synthetic nonmonotonic"),
                                              process,
                                              BoardCapabilityRange{10, 40},
                                              BoardCapabilityRange{10, 30},
                                              0.15};
    const auto result = evaluate_manufacturing_coverage(
        compiled, ManufacturingTarget{compiled.identity(), compiled.content_digest(),
                                      ManufacturingGoal::Fabrication, process, profile});
    CHECK(leaf(result, ManufacturingCheck::CopperWidth).state == ManufacturingState::NativePass);
    CHECK(leaf(result, ManufacturingCheck::CopperWidth).observations[0].minimum == 0.20);
}

TEST_CASE("Manufacturing foreign artifact slots cannot classify the requested process",
          "[manufacturing]") {
    const auto source = test::completion_fixture(test::manufacturing_options());
    auto options = test::manufacturing_options();
    options.fab_critical_slot = true;
    const auto foreign = test::completion_fixture(options);
    const auto result =
        evaluate_manufacturing_coverage(foreign, test::manufacturing_target(source));
    CHECK(leaf(result, ManufacturingCheck::ArtifactIdentity).state == ManufacturingState::Failed);
    CHECK(result.status() == ManufacturingStatus::Blocked);
    CHECK(leaf(result, ManufacturingCheck::ProcessSelections).state ==
          ManufacturingState::NativePass);
    CHECK(leaf(result, ManufacturingCheck::SlotTreatment).state ==
          ManufacturingState::NotEvaluated);
    CHECK(leaf(result, ManufacturingCheck::SlotTreatment).entities ==
          std::vector{EntityRef::board()});
    CHECK(leaf(result, ManufacturingCheck::CopperWidth).state == ManufacturingState::NotEvaluated);
}

TEST_CASE("Manufacturing number errors identify their field", "[manufacturing]") {
    const auto compiled = test::completion_fixture(test::manufacturing_options());
    const auto field = GENERATE("copper_layers", "thickness_mm", "dimension_x_mm");
    auto doc = document(compiled);
    doc["process"][field] = true;
    try {
        static_cast<void>(io::read_manufacturing_target(doc.dump()));
        FAIL("Boolean manufacturing number accepted");
    } catch (const io::ManufacturingTargetReadError &error) {
        CHECK(error.code() == io::ManufacturingTargetReadErrorCode::Malformed);
        CHECK(std::string{error.what()}.find(field) != std::string::npos);
    }
}

TEST_CASE("Manufacturing layer topology failures retain their specific reason", "[manufacturing]") {
    auto options = test::manufacturing_options();
    options.disabled_top = true;
    const auto capability = BoardCapabilityProfile{"Synthetic unsupported layer count",
                                                   {"synthetic://layers", "2026-10-19"},
                                                   0.15,
                                                   0.2,
                                                   0.6,
                                                   {},
                                                   {},
                                                   {4},
                                                   BoardCapabilityRange{1.0, 2.0},
                                                   {1.0}};
    options.capability = capability;
    const auto compiled = test::completion_fixture(options);
    const auto profile = ManufacturingProfile{capability, sha256_content_hash("layers"),
                                              test::manufacturing_process()};
    const auto result = evaluate_manufacturing_coverage(
        compiled, ManufacturingTarget{compiled.identity(), compiled.content_digest(),
                                      ManufacturingGoal::Fabrication, test::manufacturing_process(),
                                      profile});
    const auto &row = leaf(result, ManufacturingCheck::CopperLayerCount);
    CHECK(row.state == ManufacturingState::Failed);
    CHECK(row.reason ==
          "Actual copper layers must be exactly the enabled top/bottom pair in stack order");
}
