#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <volt/circuit/parts.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/ids.hpp>
#include <volt/pcb/footprints.hpp>

TEST_CASE("FootprintPad stores normalized pad geometry") {
    const auto pad = volt::FootprintPad::surface_mount(
        "1", volt::FootprintPadShape::RoundedRectangle, volt::FootprintPoint{-0.75, 0.0},
        volt::FootprintSize{0.80, 0.95}, volt::FootprintLayerSet::front_smd());

    CHECK(pad.label() == "1");
    CHECK(pad.kind() == volt::FootprintPadKind::SurfaceMount);
    CHECK(pad.shape() == volt::FootprintPadShape::RoundedRectangle);
    CHECK(pad.position() == volt::FootprintPoint{-0.75, 0.0});
    CHECK(pad.size() == volt::FootprintSize{0.80, 0.95});
    CHECK(pad.layers() == volt::FootprintLayerSet::front_smd());
    CHECK_FALSE(pad.drill().has_value());
    CHECK_FALSE(pad.mechanical_role().has_value());
}

TEST_CASE("FootprintPad stores through-hole drill and mechanical role") {
    const auto pad = volt::FootprintPad::through_hole(
        "M1", volt::FootprintPadShape::Circle, volt::FootprintPoint{0.0, 0.0},
        volt::FootprintSize{1.8, 1.8}, volt::FootprintLayerSet::mechanical_hole(),
        volt::FootprintDrill{0.95, volt::FootprintPadPlating::NonPlated},
        volt::FootprintPadMechanicalRole::Mounting);

    CHECK(pad.kind() == volt::FootprintPadKind::ThroughHole);
    CHECK(pad.layers() == volt::FootprintLayerSet::mechanical_hole());
    REQUIRE(pad.drill().has_value());
    CHECK(pad.drill()->diameter_mm() == 0.95);
    CHECK(pad.drill()->plating() == volt::FootprintPadPlating::NonPlated);
    REQUIRE(pad.mechanical_role().has_value());
    CHECK(*pad.mechanical_role() == volt::FootprintPadMechanicalRole::Mounting);
}

TEST_CASE("Footprint geometry rejects invalid structural values") {
    CHECK_THROWS_AS(volt::FootprintPoint(std::numeric_limits<double>::quiet_NaN(), 0.0),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::FootprintSize(0.0, 1.0), std::invalid_argument);
    CHECK_THROWS_AS(volt::FootprintDrill(0.0, volt::FootprintPadPlating::Plated),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::FootprintLayerSet(std::vector<volt::FootprintLayer>{}),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::FootprintLayerSet(std::vector{volt::FootprintLayer::FrontCopper,
                                                        volt::FootprintLayer::FrontCopper}),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::FootprintPad::surface_mount(
                        "", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, 0.0},
                        volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::front_smd()),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::FootprintPad::surface_mount(
                        "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, 0.0},
                        volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::through_hole()),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::FootprintPad::through_hole(
                        "1", volt::FootprintPadShape::Circle, volt::FootprintPoint{0.0, 0.0},
                        volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::front_smd(),
                        volt::FootprintDrill{0.5, volt::FootprintPadPlating::Plated}),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::FootprintPad::through_hole(
                        "1", volt::FootprintPadShape::Circle, volt::FootprintPoint{0.0, 0.0},
                        volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::mechanical_hole(),
                        volt::FootprintDrill{0.5, volt::FootprintPadPlating::Plated}),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::FootprintPad::through_hole(
                        "1", volt::FootprintPadShape::Circle, volt::FootprintPoint{0.0, 0.0},
                        volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::mechanical_hole(),
                        volt::FootprintDrill{0.5, volt::FootprintPadPlating::NonPlated}),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::FootprintDefinition(volt::FootprintRef{"test", "Empty"},
                                              std::vector<volt::FootprintPad>{}),
                    std::invalid_argument);
}

TEST_CASE("FootprintDefinition assigns stable pad identities by label") {
    const auto footprint = volt::FootprintDefinition{
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{
            volt::FootprintPad::surface_mount(
                "1", volt::FootprintPadShape::RoundedRectangle, volt::FootprintPoint{-0.75, 0.0},
                volt::FootprintSize{0.80, 0.95}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "2", volt::FootprintPadShape::RoundedRectangle, volt::FootprintPoint{0.75, 0.0},
                volt::FootprintSize{0.80, 0.95}, volt::FootprintLayerSet::front_smd()),
        },
    };

    REQUIRE(footprint.pad_count() == 2);
    const auto first_pad = footprint.pad_id("1");
    const auto second_pad = footprint.pad_id("2");
    REQUIRE(first_pad.has_value());
    REQUIRE(second_pad.has_value());
    CHECK(*first_pad == volt::FootprintPadId{0});
    CHECK(*second_pad == volt::FootprintPadId{1});
    CHECK(footprint.pad(*first_pad).label() == "1");
    CHECK(footprint.pad(*second_pad).position() == volt::FootprintPoint{0.75, 0.0});
    CHECK_FALSE(footprint.pad_id("3").has_value());
}

TEST_CASE("FootprintDefinition rejects duplicate pad labels") {
    CHECK_THROWS_AS(
        volt::FootprintDefinition(
            volt::FootprintRef{"passives", "R_0603_1608Metric"},
            std::vector{
                volt::FootprintPad::surface_mount(
                    "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{-0.75, 0.0},
                    volt::FootprintSize{0.80, 0.95}, volt::FootprintLayerSet::front_smd()),
                volt::FootprintPad::surface_mount(
                    "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.75, 0.0},
                    volt::FootprintSize{0.80, 0.95}, volt::FootprintLayerSet::front_smd()),
            }),
        std::invalid_argument);
}

TEST_CASE("Built-in footprint library provides first board fixtures") {
    const auto library = volt::builtin_footprint_library();

    CHECK(library.find(volt::FootprintRef{"passives", "R_0603_1608Metric"}) != nullptr);
    CHECK(library.find(volt::FootprintRef{"leds", "LED_0603_1608Metric"}) != nullptr);
    CHECK(library.find(volt::FootprintRef{"connectors", "PinHeader_1x02_P2.54mm_Vertical"}) !=
          nullptr);
    CHECK(library.find(volt::FootprintRef{"ics", "SOIC-8_3.9x4.9mm_P1.27mm"}) != nullptr);
}

TEST_CASE("Footprint library rejects duplicate footprint references") {
    auto library = volt::FootprintLibrary{};
    library.add(volt::passive_0603_footprint());

    CHECK_THROWS_AS(library.add(volt::passive_0603_footprint()), std::invalid_argument);
}

TEST_CASE("Footprint resolver binds selected-part pad mappings to footprint pads") {
    const auto library = volt::builtin_footprint_library();
    const auto part = volt::PhysicalPart{
        volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
        volt::PackageRef{"0603"},
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{
            volt::PinPadMapping{volt::PinDefId{10}, "1"},
            volt::PinPadMapping{volt::PinDefId{11}, "2"},
        },
    };

    const auto resolution = volt::resolve_footprint(part, library);

    REQUIRE(resolution.ok());
    REQUIRE(resolution.definition() != nullptr);
    CHECK(resolution.definition()->ref() == part.footprint());
    CHECK(resolution.diagnostics().empty());
    REQUIRE(resolution.pad_bindings().size() == 2);
    CHECK(resolution.pad_bindings()[0].pad() == volt::FootprintPadId{0});
    CHECK(resolution.pad_bindings()[0].pin() == volt::PinDefId{10});
    CHECK(resolution.pad_bindings()[1].pad() == volt::FootprintPadId{1});
    CHECK(resolution.pad_bindings()[1].pin() == volt::PinDefId{11});
}

TEST_CASE("Footprint resolver owns resolved footprint definitions") {
    const auto part = volt::PhysicalPart{
        volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
        volt::PackageRef{"0603"},
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{
            volt::PinPadMapping{volt::PinDefId{10}, "1"},
            volt::PinPadMapping{volt::PinDefId{11}, "2"},
        },
    };
    const auto library = volt::builtin_footprint_library();
    const auto *library_definition = library.find(part.footprint());

    const auto resolution = volt::resolve_footprint(part, library);

    REQUIRE(resolution.definition() != nullptr);
    CHECK(resolution.definition() != library_definition);

    const auto temporary_resolution =
        volt::resolve_footprint(part, volt::builtin_footprint_library());
    REQUIRE(temporary_resolution.definition() != nullptr);
    CHECK(temporary_resolution.definition()->ref() == part.footprint());
}

TEST_CASE("Footprint resolver diagnoses missing footprint definitions") {
    const auto library = volt::builtin_footprint_library();
    const auto part = volt::PhysicalPart{
        volt::ManufacturerPart{"Acme", "NOPE"},
        volt::PackageRef{"custom"},
        volt::FootprintRef{"missing", "NotARealFootprint"},
        std::vector{volt::PinPadMapping{volt::PinDefId{0}, "1"}},
    };

    const auto resolution = volt::resolve_footprint(part, library);

    CHECK_FALSE(resolution.ok());
    CHECK(resolution.definition() == nullptr);
    REQUIRE(resolution.diagnostics().count() == 1);
    CHECK(resolution.diagnostics().diagnostics()[0].code() ==
          volt::DiagnosticCode{"PCB_FOOTPRINT_UNRESOLVED"});
    CHECK(resolution.diagnostics().diagnostics()[0].message().find("missing:NotARealFootprint") !=
          std::string::npos);
}

TEST_CASE("Footprint resolver diagnoses invalid selected-part pad mappings") {
    const auto library = volt::builtin_footprint_library();
    const auto unknown_pad_part = volt::PhysicalPart{
        volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
        volt::PackageRef{"0603"},
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{volt::PinPadMapping{volt::PinDefId{0}, "99"}},
    };

    const auto unknown_pad = volt::resolve_footprint(unknown_pad_part, library);

    CHECK_FALSE(unknown_pad.ok());
    REQUIRE(unknown_pad.diagnostics().count() == 2);
    CHECK(unknown_pad.diagnostics().diagnostics()[0].code() ==
          volt::DiagnosticCode{"PCB_PAD_MAPPING_UNKNOWN_PAD"});
    CHECK(unknown_pad.diagnostics().diagnostics()[0].message().find("99") != std::string::npos);
    CHECK(unknown_pad.diagnostics().diagnostics()[0].message().find("pin_def:0") !=
          std::string::npos);
    CHECK(unknown_pad.diagnostics().diagnostics()[1].code() ==
          volt::DiagnosticCode{"PCB_PAD_MAPPING_MISSING_PIN"});
    CHECK(unknown_pad.diagnostics().diagnostics()[1].message().find("1") != std::string::npos);

    const auto incomplete_part = volt::PhysicalPart{
        volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
        volt::PackageRef{"0603"},
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{volt::PinPadMapping{volt::PinDefId{0}, "1"}},
    };

    const auto incomplete = volt::resolve_footprint(incomplete_part, library);

    CHECK_FALSE(incomplete.ok());
    REQUIRE(incomplete.diagnostics().count() == 1);
    CHECK(incomplete.diagnostics().diagnostics()[0].code() ==
          volt::DiagnosticCode{"PCB_PAD_MAPPING_MISSING_PIN"});
    CHECK(incomplete.diagnostics().diagnostics()[0].message().find("2") != std::string::npos);
}

TEST_CASE("Footprint resolver diagnoses mappings to mechanical pads") {
    auto library = volt::FootprintLibrary{};
    library.add(volt::FootprintDefinition{
        volt::FootprintRef{"mechanical", "MountingHole"},
        std::vector{volt::FootprintPad::through_hole(
            "M1", volt::FootprintPadShape::Circle, volt::FootprintPoint{0.0, 0.0},
            volt::FootprintSize{1.8, 1.8}, volt::FootprintLayerSet::mechanical_hole(),
            volt::FootprintDrill{0.95, volt::FootprintPadPlating::NonPlated},
            volt::FootprintPadMechanicalRole::Mounting)},
    });
    const auto part = volt::PhysicalPart{
        volt::ManufacturerPart{"Volt", "fixture"},
        volt::PackageRef{"mounting"},
        volt::FootprintRef{"mechanical", "MountingHole"},
        std::vector{volt::PinPadMapping{volt::PinDefId{0}, "M1"}},
    };

    const auto resolution = volt::resolve_footprint(part, library);

    CHECK_FALSE(resolution.ok());
    REQUIRE(resolution.diagnostics().count() == 1);
    CHECK(resolution.diagnostics().diagnostics()[0].code() ==
          volt::DiagnosticCode{"PCB_PAD_MAPPING_NON_ELECTRICAL"});
    CHECK(resolution.diagnostics().diagnostics()[0].message().find("M1") != std::string::npos);
    CHECK(resolution.diagnostics().diagnostics()[0].message().find("pin_def:0") !=
          std::string::npos);
}
