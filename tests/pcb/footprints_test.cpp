#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <volt/circuit/parts.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/ids.hpp>
#include <volt/pcb/footprints.hpp>

namespace {

[[nodiscard]] volt::PhysicalPart same_numbered_part(volt::FootprintRef ref, std::size_t pin_count) {
    auto mappings = std::vector<volt::PinPadMapping>{};
    mappings.reserve(pin_count);
    for (std::size_t index = 0; index < pin_count; ++index) {
        mappings.emplace_back(volt::PinDefId{index}, std::to_string(index + 1U));
    }
    return volt::PhysicalPart{volt::ManufacturerPart{"Volt", "fixture"},
                              volt::PackageRef{"fixture"}, std::move(ref), std::move(mappings)};
}

void expect_pad_labels(const volt::FootprintDefinition &definition,
                       const std::vector<std::string> &labels) {
    REQUIRE(definition.pad_count() == labels.size());
    for (std::size_t index = 0; index < labels.size(); ++index) {
        const auto pad_id = definition.pad_id(labels[index]);
        REQUIRE(pad_id.has_value());
        CHECK(*pad_id == volt::FootprintPadId{index});
        CHECK(definition.pad(volt::FootprintPadId{index}).label() == labels[index]);
    }
}

} // namespace

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

    CHECK(library.size() == 20);
    CHECK(library.find(volt::FootprintRef{"passives", "R_0603_1608Metric"}) != nullptr);
    CHECK(library.find(volt::FootprintRef{"passives", "C_0603_1608Metric"}) != nullptr);
    CHECK(library.find(volt::FootprintRef{"passives", "L_0603_1608Metric"}) != nullptr);
    CHECK(library.find(volt::FootprintRef{"leds", "LED_0603_1608Metric"}) != nullptr);
    CHECK(library.find(volt::FootprintRef{"diodes", "D_SOD-123"}) != nullptr);
    CHECK(library.find(volt::FootprintRef{"connectors", "PinHeader_1x02_P2.54mm_Vertical"}) !=
          nullptr);
    CHECK(library.find(volt::FootprintRef{"connectors", "PinHeader_1x04_P2.54mm_Vertical"}) !=
          nullptr);
    CHECK(library.find(volt::FootprintRef{"connectors", "PinHeader_2x05_P1.27mm_Vertical"}) !=
          nullptr);
    CHECK(library.find(volt::FootprintRef{"connectors", "TerminalBlock_1x02_P5.08mm"}) != nullptr);
    CHECK(library.find(volt::FootprintRef{"connectors", "USB_Micro-B_Receptacle"}) != nullptr);
    CHECK(library.find(volt::FootprintRef{"Package_TO_SOT_SMD", "SOT-23"}) != nullptr);
    CHECK(library.find(volt::FootprintRef{"Package_TO_SOT_SMD", "SOT-23-6"}) != nullptr);
    CHECK(library.find(volt::FootprintRef{"Package_TO_SOT_SMD", "SOT-223-3_TabPin2"}) != nullptr);
    CHECK(library.find(volt::FootprintRef{"ics", "SOIC-8_3.9x4.9mm_P1.27mm"}) != nullptr);
    CHECK(library.find(volt::FootprintRef{"Package_SO", "SOIC-8_3.9x4.9mm_P1.27mm"}) != nullptr);
    CHECK(library.find(volt::FootprintRef{"Package_SO", "TSSOP-14_4.4x5mm_P0.65mm"}) != nullptr);
    CHECK(library.find(volt::FootprintRef{"Package_QFP", "LQFP-64_10x10mm_P0.5mm"}) != nullptr);
}

TEST_CASE("Built-in footprint library keeps unique references and stable pad labels") {
    const auto library = volt::builtin_footprint_library();
    auto refs = std::vector<std::string>{};
    refs.reserve(library.size());
    for (const auto &definition : library.definitions()) {
        refs.push_back(definition.ref().library() + ":" + definition.ref().name());
    }
    std::sort(refs.begin(), refs.end());
    CHECK(std::adjacent_find(refs.begin(), refs.end()) == refs.end());

    const auto *resistor = library.find(volt::FootprintRef{"passives", "R_0402_1005Metric"});
    REQUIRE(resistor != nullptr);
    expect_pad_labels(*resistor, {"1", "2"});

    const auto *header =
        library.find(volt::FootprintRef{"connectors", "PinHeader_2x05_P1.27mm_Vertical"});
    REQUIRE(header != nullptr);
    expect_pad_labels(*header, {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"});

    const auto *usb = library.find(volt::FootprintRef{"connectors", "USB_Micro-B_Receptacle"});
    REQUIRE(usb != nullptr);
    expect_pad_labels(*usb, {"1", "2", "3", "4", "5", "6", "M1", "M2"});
    CHECK_FALSE(usb->pad(volt::FootprintPadId{6}).requires_pin_mapping());
    CHECK_FALSE(usb->pad(volt::FootprintPadId{7}).requires_pin_mapping());

    const auto *regulator =
        library.find(volt::FootprintRef{"Package_TO_SOT_SMD", "SOT-223-3_TabPin2"});
    REQUIRE(regulator != nullptr);
    expect_pad_labels(*regulator, {"1", "2", "3", "4"});
    CHECK(regulator->pad(volt::FootprintPadId{3}).position() == volt::FootprintPoint{0.00, 2.05});

    const auto *badge_soic = library.find(volt::FootprintRef{"ics", "SOIC-8_3.9x4.9mm_P1.27mm"});
    REQUIRE(badge_soic != nullptr);
    CHECK(badge_soic->pad(volt::FootprintPadId{0}).size() == volt::FootprintSize{1.55, 0.60});

    const auto *soic = library.find(volt::FootprintRef{"Package_SO", "SOIC-8_3.9x4.9mm_P1.27mm"});
    REQUIRE(soic != nullptr);
    CHECK(soic->pad(volt::FootprintPadId{0}).size() == volt::FootprintSize{1.55, 0.60});

    const auto *tssop = library.find(volt::FootprintRef{"Package_SO", "TSSOP-14_4.4x5mm_P0.65mm"});
    REQUIRE(tssop != nullptr);
    CHECK(tssop->pad(volt::FootprintPadId{0}).size() == volt::FootprintSize{1.10, 0.45});

    const auto *mcu = library.find(volt::FootprintRef{"Package_QFP", "LQFP-64_10x10mm_P0.5mm"});
    REQUIRE(mcu != nullptr);
    REQUIRE(mcu->pad_count() == 64);
    REQUIRE(mcu->pad_id("45").has_value());
    CHECK(*mcu->pad_id("45") == volt::FootprintPadId{44});
    CHECK(mcu->pad(volt::FootprintPadId{44}).label() == "45");
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

TEST_CASE("Footprint resolver binds expanded built-in library families") {
    const auto library = volt::builtin_footprint_library();

    SECTION("SOT-23 transistor package") {
        const auto part =
            same_numbered_part(volt::FootprintRef{"Package_TO_SOT_SMD", "SOT-23"}, 3U);
        const auto resolution = volt::resolve_footprint(part, library);

        REQUIRE(resolution.ok());
        REQUIRE(resolution.definition() != nullptr);
        CHECK(resolution.definition()->ref() == part.footprint());
        REQUIRE(resolution.pad_bindings().size() == 3);
        CHECK(resolution.pad_bindings()[2].pad() == volt::FootprintPadId{2});
        CHECK(resolution.pad_bindings()[2].pin() == volt::PinDefId{2});
    }

    SECTION("SOIC and TSSOP IC packages") {
        const auto soic =
            same_numbered_part(volt::FootprintRef{"Package_SO", "SOIC-8_3.9x4.9mm_P1.27mm"}, 8U);
        const auto soic_resolution = volt::resolve_footprint(soic, library);
        REQUIRE(soic_resolution.ok());
        REQUIRE(soic_resolution.pad_bindings().size() == 8);
        CHECK(soic_resolution.pad_bindings()[7].pad() == volt::FootprintPadId{7});
        CHECK(soic_resolution.pad_bindings()[7].pin() == volt::PinDefId{7});

        const auto tssop =
            same_numbered_part(volt::FootprintRef{"Package_SO", "TSSOP-14_4.4x5mm_P0.65mm"}, 14U);
        const auto tssop_resolution = volt::resolve_footprint(tssop, library);
        REQUIRE(tssop_resolution.ok());
        REQUIRE(tssop_resolution.pad_bindings().size() == 14);
        CHECK(tssop_resolution.pad_bindings()[13].pad() == volt::FootprintPadId{13});
        CHECK(tssop_resolution.pad_bindings()[13].pin() == volt::PinDefId{13});
    }

    SECTION("micro USB connector with mechanical support holes") {
        const auto usb =
            same_numbered_part(volt::FootprintRef{"connectors", "USB_Micro-B_Receptacle"}, 6U);
        const auto resolution = volt::resolve_footprint(usb, library);

        REQUIRE(resolution.ok());
        REQUIRE(resolution.definition() != nullptr);
        CHECK(resolution.definition()->pad_count() == 8);
        REQUIRE(resolution.pad_bindings().size() == 6);
        CHECK(resolution.pad_bindings()[5].pad() == volt::FootprintPadId{5});
        CHECK(resolution.pad_bindings()[5].pin() == volt::PinDefId{5});
    }

    SECTION("SOT-223 regulator with tab tied to pin 2") {
        const auto regulator = volt::PhysicalPart{
            volt::ManufacturerPart{"Diodes Incorporated", "AP1117E15G-13"},
            volt::PackageRef{"SOT-223-3"},
            volt::FootprintRef{"Package_TO_SOT_SMD", "SOT-223-3_TabPin2"},
            std::vector{
                volt::PinPadMapping{volt::PinDefId{0}, "1"},
                volt::PinPadMapping{volt::PinDefId{1}, "2"},
                volt::PinPadMapping{volt::PinDefId{1}, "4"},
                volt::PinPadMapping{volt::PinDefId{2}, "3"},
            },
        };
        const auto resolution = volt::resolve_footprint(regulator, library);

        REQUIRE(resolution.ok());
        REQUIRE(resolution.definition() != nullptr);
        REQUIRE(resolution.pad_bindings().size() == 4);
        CHECK(resolution.pad_bindings()[1].pad() == volt::FootprintPadId{1});
        CHECK(resolution.pad_bindings()[1].pin() == volt::PinDefId{1});
        CHECK(resolution.pad_bindings()[3].pad() == volt::FootprintPadId{3});
        CHECK(resolution.pad_bindings()[3].pin() == volt::PinDefId{1});
    }

    SECTION("STM32 benchmark MCU package") {
        const auto stm32 =
            same_numbered_part(volt::FootprintRef{"Package_QFP", "LQFP-64_10x10mm_P0.5mm"}, 64U);
        const auto resolution = volt::resolve_footprint(stm32, library);

        REQUIRE(resolution.ok());
        REQUIRE(resolution.definition() != nullptr);
        CHECK(resolution.definition()->pad_count() == 64);
        REQUIRE(resolution.pad_bindings().size() == 64);
        CHECK(resolution.pad_bindings()[44].pad() == volt::FootprintPadId{44});
        CHECK(resolution.pad_bindings()[44].pin() == volt::PinDefId{44});
        CHECK(resolution.definition()->pad(volt::FootprintPadId{44}).label() == "45");
    }
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
