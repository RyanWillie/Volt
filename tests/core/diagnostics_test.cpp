#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <volt/core/diagnostics.hpp>
#include <volt/core/ids.hpp>

TEST_CASE("DiagnosticCode rejects empty values") {
    CHECK_THROWS_AS(volt::DiagnosticCode{""}, std::invalid_argument);
}

TEST_CASE("DiagnosticCode stores comparable machine-readable values") {
    const auto code = volt::DiagnosticCode{"UNCONNECTED_PIN"};

    CHECK(code.value() == "UNCONNECTED_PIN");
    CHECK(code == volt::DiagnosticCode{"UNCONNECTED_PIN"});
    CHECK(code < volt::DiagnosticCode{"Z_LATER_CODE"});
}

TEST_CASE("EntityRef preserves logical entity kind and index") {
    const auto component_def = volt::EntityRef::component_def(volt::ComponentDefId{1});
    const auto component = volt::EntityRef::component(volt::ComponentId{2});
    const auto pin_def = volt::EntityRef::pin_def(volt::PinDefId{3});
    const auto pin = volt::EntityRef::pin(volt::PinId{4});
    const auto net = volt::EntityRef::net(volt::NetId{5});

    CHECK(component_def.kind() == volt::EntityKind::ComponentDef);
    CHECK(component_def.index() == 1);

    CHECK(component.kind() == volt::EntityKind::Component);
    CHECK(component.index() == 2);

    CHECK(pin_def.kind() == volt::EntityKind::PinDef);
    CHECK(pin_def.index() == 3);

    CHECK(pin.kind() == volt::EntityKind::Pin);
    CHECK(pin.index() == 4);

    CHECK(net.kind() == volt::EntityKind::Net);
    CHECK(net.index() == 5);
}

TEST_CASE("EntityRef preserves PCB copper primitive references") {
    const auto track = volt::EntityRef::board_track(volt::BoardTrackId{6});
    const auto via = volt::EntityRef::board_via(volt::BoardViaId{7});
    const auto zone = volt::EntityRef::board_zone(volt::BoardZoneId{8});
    const auto keepout = volt::EntityRef::board_keepout(volt::BoardKeepoutId{9});
    const auto room = volt::EntityRef::board_room(volt::BoardRoomId{10});
    const auto text = volt::EntityRef::board_text(volt::BoardTextId{11});

    CHECK(track.kind() == volt::EntityKind::BoardTrack);
    CHECK(track.index() == 6);

    CHECK(via.kind() == volt::EntityKind::BoardVia);
    CHECK(via.index() == 7);

    CHECK(zone.kind() == volt::EntityKind::BoardZone);
    CHECK(zone.index() == 8);

    CHECK(keepout.kind() == volt::EntityKind::BoardKeepout);
    CHECK(keepout.index() == 9);

    CHECK(room.kind() == volt::EntityKind::BoardRoom);
    CHECK(room.index() == 10);

    CHECK(text.kind() == volt::EntityKind::BoardText);
    CHECK(text.index() == 11);
}

TEST_CASE("EntityRef can identify the board projection root") {
    const auto board = volt::EntityRef::board();

    CHECK(board.kind() == volt::EntityKind::Board);
    CHECK(board.index() == 0);
}

TEST_CASE("Diagnostic stores category and overlay geometry") {
    const auto diagnostic = volt::Diagnostic{
        volt::Severity::Warning,
        volt::DiagnosticCode{"PCB_VISUAL_REFERENCE_DESIGNATOR_UNREADABLE"},
        volt::DiagnosticCategory{"pcb.visual"},
        "Reference designator is difficult to read",
        std::vector{volt::EntityRef::board(), volt::EntityRef::board_text(volt::BoardTextId{2})},
        std::vector{
            volt::DiagnosticOverlay::bounding_box(
                volt::DiagnosticPoint{10.0, 20.0}, volt::DiagnosticPoint{14.0, 22.0},
                std::vector{volt::EntityRef::board_text(volt::BoardTextId{2})},
                std::vector{volt::BoardLayerId{0}}),
            volt::DiagnosticOverlay::segment(volt::DiagnosticPoint{10.0, 21.0},
                                             volt::DiagnosticPoint{14.0, 21.0}, {},
                                             std::vector{volt::BoardLayerId{0}}),
        },
    };

    CHECK(diagnostic.category() == volt::DiagnosticCategory{"pcb.visual"});
    REQUIRE(diagnostic.overlays().size() == 2);
    CHECK(diagnostic.overlays()[0].kind() == volt::DiagnosticOverlayKind::BoundingBox);
    CHECK(diagnostic.overlays()[0].points() ==
          std::vector{volt::DiagnosticPoint{10.0, 20.0}, volt::DiagnosticPoint{14.0, 22.0}});
    CHECK(diagnostic.overlays()[0].entities() ==
          std::vector{volt::EntityRef::board_text(volt::BoardTextId{2})});
    CHECK(diagnostic.overlays()[0].layers() == std::vector{volt::BoardLayerId{0}});
    CHECK(diagnostic.overlays()[1].kind() == volt::DiagnosticOverlayKind::Segment);
}

TEST_CASE("Diagnostic carries an optional typed measurement") {
    const auto without = volt::Diagnostic{
        volt::Severity::Error, volt::DiagnosticCode{"PCB_NET_UNROUTED"},
        volt::DiagnosticCategory{"drc"}, "Logical net still has unrouted placed pads"};
    CHECK_FALSE(without.measurement().has_value());

    const auto with = volt::Diagnostic{
        volt::Severity::Error,
        volt::DiagnosticCode{"PCB_TRACK_WIDTH_BELOW_MINIMUM"},
        volt::DiagnosticCategory{"drc"},
        "Track width is below the board minimum",
        std::vector{volt::EntityRef::board_track(volt::BoardTrackId{0})},
        {},
        volt::DiagnosticMeasurement{0.15, 0.2},
    };
    REQUIRE(with.measurement().has_value());
    CHECK(with.measurement().value() == volt::DiagnosticMeasurement{0.15, 0.2});
}

TEST_CASE("Diagnostic carries optional stable rule identity") {
    const auto without = volt::Diagnostic{
        volt::Severity::Error, volt::DiagnosticCode{"PCB_KICAD_FAB_EXPORT_LOSS"},
        volt::DiagnosticCategory{"pcb.fabrication"}, "KiCad PCB export lost a construct"};
    CHECK_FALSE(without.rule().has_value());

    const auto with = volt::Diagnostic{
        volt::Severity::Error,
        volt::DiagnosticCode{"PCB_KICAD_FAB_EXPORT_LOSS"},
        volt::DiagnosticCategory{"pcb.fabrication"},
        "KiCad PCB export lost a construct",
        std::vector{volt::EntityRef::board()},
        {},
        std::nullopt,
        "board.zone",
    };
    REQUIRE(with.rule().has_value());
    CHECK(with.rule().value() == "board.zone");
    CHECK_THROWS_AS((volt::Diagnostic{volt::Severity::Error,
                                      volt::DiagnosticCode{"PCB_KICAD_FAB_EXPORT_LOSS"},
                                      volt::DiagnosticCategory{"pcb.fabrication"},
                                      "KiCad PCB export lost a construct",
                                      {},
                                      {},
                                      std::nullopt,
                                      ""}),
                    std::invalid_argument);
}

TEST_CASE("Diagnostic overlay construction rejects malformed geometry") {
    CHECK_THROWS_AS((volt::DiagnosticPoint{std::numeric_limits<double>::infinity(), 0.0}),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::DiagnosticOverlay::polygon(std::vector{volt::DiagnosticPoint{0.0, 0.0},
                                                                 volt::DiagnosticPoint{1.0, 1.0}}),
                    std::invalid_argument);
}

TEST_CASE("PCB visual diagnostic codes are stable constants") {
    CHECK(std::string{volt::pcb_visual_diagnostic_codes::PlacementOverlap} ==
          "PCB_VISUAL_PLACEMENT_OVERLAP");
    CHECK(std::string{volt::pcb_visual_diagnostic_codes::ReferenceDesignatorUnreadable} ==
          "PCB_VISUAL_REFERENCE_DESIGNATOR_UNREADABLE");
    CHECK(std::string{volt::pcb_visual_diagnostic_codes::LabelOverlap} ==
          "PCB_VISUAL_LABEL_OVERLAP");
    CHECK(std::string{volt::pcb_visual_diagnostic_codes::LabelOutsideBoard} ==
          "PCB_VISUAL_LABEL_OUTSIDE_BOARD");
    CHECK(std::string{volt::diagnostic_categories::PcbVisual} == "pcb.visual");
}

TEST_CASE("PCB fabrication diagnostic codes are stable constants") {
    CHECK(std::string{volt::diagnostic_categories::PcbFabrication} == "pcb.fabrication");
    CHECK(std::string{volt::pcb_fabrication_diagnostic_codes::KiCadFabExportLoss} ==
          "PCB_KICAD_FAB_EXPORT_LOSS");
    CHECK(std::string{volt::pcb_fabrication_diagnostic_codes::NativeFabMissingGeometry} ==
          "PCB_NATIVE_FAB_MISSING_GEOMETRY");
    CHECK(std::string{volt::pcb_fabrication_diagnostic_codes::NativeFabUnsupportedGeometry} ==
          "PCB_NATIVE_FAB_UNSUPPORTED_GEOMETRY");
    CHECK(std::string{volt::pcb_fabrication_diagnostic_codes::NativeFabUnsupportedLayer} ==
          "PCB_NATIVE_FAB_UNSUPPORTED_LAYER");
    CHECK(std::string{volt::pcb_fabrication_diagnostic_codes::NativeFabLossyGeometry} ==
          "PCB_NATIVE_FAB_LOSSY_GEOMETRY");
    CHECK(std::vector<std::string>{volt::diagnostic_code_catalogs::PcbFabrication.begin(),
                                   volt::diagnostic_code_catalogs::PcbFabrication.end()} ==
          std::vector<std::string>{"PCB_KICAD_FAB_EXPORT_LOSS", "PCB_NATIVE_FAB_MISSING_GEOMETRY",
                                   "PCB_NATIVE_FAB_UNSUPPORTED_GEOMETRY",
                                   "PCB_NATIVE_FAB_UNSUPPORTED_LAYER",
                                   "PCB_NATIVE_FAB_LOSSY_GEOMETRY"});
}

TEST_CASE("Part lineup diagnostic codes are stable constants") {
    CHECK(std::string{volt::diagnostic_categories::PartLineup} == "part.lineup");
    CHECK(std::string{volt::part_lineup_diagnostic_codes::PinWithoutPad} == "PART_PIN_WITHOUT_PAD");
    CHECK(std::string{volt::part_lineup_diagnostic_codes::PadWithoutPin} == "PART_PAD_WITHOUT_PIN");
    CHECK(std::string{volt::part_lineup_diagnostic_codes::PadOverlap} == "PART_PAD_OVERLAP");
    CHECK(std::string{volt::part_lineup_diagnostic_codes::PadRowPitchInconsistent} ==
          "PART_PAD_ROW_PITCH_INCONSISTENT");
    CHECK(std::vector<std::string>{volt::diagnostic_code_catalogs::PartLineup.begin(),
                                   volt::diagnostic_code_catalogs::PartLineup.end()} ==
          std::vector<std::string>{
              "PART_PIN_WITHOUT_PAD",
              "PART_PAD_WITHOUT_PIN",
              "PART_PAD_OVERLAP",
              "PART_PAD_ROW_PITCH_INCONSISTENT",
          });
}

TEST_CASE("BOM diagnostic codes are stable constants") {
    CHECK(std::string{volt::diagnostic_categories::Bom} == "bom");
    CHECK(std::string{volt::bom_diagnostic_codes::ComponentMissingSelectedPart} ==
          "BOM_COMPONENT_MISSING_SELECTED_PART");
    CHECK(std::string{volt::bom_diagnostic_codes::ComponentImplicitDnp} ==
          "BOM_COMPONENT_IMPLICIT_DNP");
    CHECK(std::string{volt::bom_diagnostic_codes::ApprovedAlternateDuplicatesPrimary} ==
          "BOM_APPROVED_ALTERNATE_DUPLICATES_PRIMARY");
    CHECK(std::vector<std::string>{volt::diagnostic_code_catalogs::Bom.begin(),
                                   volt::diagnostic_code_catalogs::Bom.end()} ==
          std::vector<std::string>{"BOM_COMPONENT_MISSING_SELECTED_PART",
                                   "BOM_COMPONENT_IMPLICIT_DNP",
                                   "BOM_APPROVED_ALTERNATE_DUPLICATES_PRIMARY"});
}

TEST_CASE("ERC and DRC diagnostic categories and code catalogs are stable") {
    CHECK(std::string{volt::diagnostic_categories::Erc} == "erc");
    CHECK(std::string{volt::diagnostic_categories::Drc} == "drc");

    CHECK(std::string{volt::erc_diagnostic_codes::UnconnectedRequiredPin} ==
          "UNCONNECTED_REQUIRED_PIN");
    CHECK(std::string{volt::erc_diagnostic_codes::SinglePinNet} == "SINGLE_PIN_NET");
    CHECK(std::string{volt::erc_diagnostic_codes::PowerInputWithoutSource} ==
          "POWER_INPUT_WITHOUT_SOURCE");
    CHECK(std::string{volt::erc_diagnostic_codes::MultipleOutputsOnNet} ==
          "MULTIPLE_OUTPUTS_ON_NET");
    CHECK(std::string{volt::erc_diagnostic_codes::InputSignalDomainMismatch} ==
          "INPUT_SIGNAL_DOMAIN_MISMATCH");
    CHECK(std::string{volt::erc_diagnostic_codes::SelectedPartVoltageBelowAcceptedRange} ==
          "SELECTED_PART_VOLTAGE_BELOW_ACCEPTED_RANGE");
    CHECK(std::string{volt::erc_diagnostic_codes::SelectedPartVoltageAboveAcceptedRange} ==
          "SELECTED_PART_VOLTAGE_ABOVE_ACCEPTED_RANGE");
    CHECK(std::string{volt::erc_diagnostic_codes::SelectedPartVoltageAbsoluteLimitViolation} ==
          "SELECTED_PART_VOLTAGE_ABSOLUTE_LIMIT_VIOLATION");
    CHECK(std::string{volt::erc_diagnostic_codes::SelectedPartCurrentAbsoluteLimitViolation} ==
          "SELECTED_PART_CURRENT_ABSOLUTE_LIMIT_VIOLATION");
    CHECK(std::string{volt::erc_diagnostic_codes::SelectedPartCurrentCapabilityInsufficient} ==
          "SELECTED_PART_CURRENT_CAPABILITY_INSUFFICIENT");
    CHECK(std::string{volt::erc_diagnostic_codes::SelectedPartCurrentBudgetUnknown} ==
          "SELECTED_PART_CURRENT_BUDGET_UNKNOWN");
    CHECK(std::string{volt::erc_diagnostic_codes::SelectedPartElectricalSubjectUnresolved} ==
          "SELECTED_PART_ELECTRICAL_SUBJECT_UNRESOLVED");

    CHECK(std::string{volt::drc_diagnostic_codes::TrackWidthBelowMinimum} ==
          "PCB_TRACK_WIDTH_BELOW_MINIMUM");
    CHECK(std::string{volt::drc_diagnostic_codes::CopperClearanceViolation} ==
          "PCB_COPPER_CLEARANCE_VIOLATION");
    CHECK(std::string{volt::drc_diagnostic_codes::ComponentBodyOverlap} ==
          "PCB_COMPONENT_BODY_OVERLAP");
    CHECK(std::string{volt::drc_diagnostic_codes::ComponentCourtyardOverlap} ==
          "PCB_COMPONENT_COURTYARD_OVERLAP");
    CHECK(std::string{volt::drc_diagnostic_codes::KeepoutPlacementViolation} ==
          "PCB_KEEPOUT_PLACEMENT_VIOLATION");
    CHECK(std::string{volt::drc_diagnostic_codes::RuleBelowCapability} ==
          "PCB_RULE_BELOW_CAPABILITY");
    CHECK(std::string{volt::drc_diagnostic_codes::RuleAtCapabilityMinimum} ==
          "PCB_RULE_AT_CAPABILITY_MINIMUM");
    CHECK(std::string{volt::drc_diagnostic_codes::CopperLayerCountOutsideCapability} ==
          "PCB_COPPER_LAYER_COUNT_OUTSIDE_CAPABILITY");
    CHECK(std::string{volt::drc_diagnostic_codes::BoardThicknessOutsideCapability} ==
          "PCB_BOARD_THICKNESS_OUTSIDE_CAPABILITY");
    CHECK(std::string{volt::drc_diagnostic_codes::BoardThicknessAtCapabilityLimit} ==
          "PCB_BOARD_THICKNESS_AT_CAPABILITY_LIMIT");
    CHECK(std::string{volt::drc_diagnostic_codes::CopperWeightOutsideCapability} ==
          "PCB_COPPER_WEIGHT_OUTSIDE_CAPABILITY");
    CHECK(std::string{volt::drc_diagnostic_codes::DrillDiameterOutsideCapability} ==
          "PCB_DRILL_DIAMETER_OUTSIDE_CAPABILITY");
    CHECK(std::string{volt::drc_diagnostic_codes::DrillDiameterAtCapabilityLimit} ==
          "PCB_DRILL_DIAMETER_AT_CAPABILITY_LIMIT");

    CHECK(std::vector<std::string>{volt::diagnostic_code_catalogs::Erc.begin(),
                                   volt::diagnostic_code_catalogs::Erc.end()} ==
          std::vector<std::string>{
              "PIN_MUST_NOT_CONNECT",
              "PIN_INTENTIONAL_NO_CONNECT_IS_CONNECTED",
              "UNCONNECTED_REQUIRED_PIN",
              "EMPTY_NET",
              "SINGLE_PIN_NET",
              "UNBOUND_REQUIRED_PORT",
              "PIN_GROUND_ON_NON_GROUND_NET",
              "PIN_POWER_ON_GROUND_NET",
              "POWER_INPUT_WITHOUT_SOURCE",
              "SELECTED_PART_VOLTAGE_RATING_EXCEEDED",
              "PIN_VOLTAGE_RANGE_VIOLATION",
              "NET_CLASS_VOLTAGE_EXCEEDED",
              "MULTIPLE_OUTPUTS_ON_NET",
              "INPUT_SIGNAL_DOMAIN_MISMATCH",
              "SELECTED_PART_VOLTAGE_BELOW_ACCEPTED_RANGE",
              "SELECTED_PART_VOLTAGE_ABOVE_ACCEPTED_RANGE",
              "SELECTED_PART_VOLTAGE_ABSOLUTE_LIMIT_VIOLATION",
              "SELECTED_PART_CURRENT_ABSOLUTE_LIMIT_VIOLATION",
              "SELECTED_PART_CURRENT_CAPABILITY_INSUFFICIENT",
              "SELECTED_PART_CURRENT_BUDGET_UNKNOWN",
              "SELECTED_PART_ELECTRICAL_SUBJECT_UNRESOLVED",
          });

    CHECK(std::vector<std::string>{volt::diagnostic_code_catalogs::Drc.begin(),
                                   volt::diagnostic_code_catalogs::Drc.end()} ==
          std::vector<std::string>{
              "PCB_TRACK_WIDTH_BELOW_MINIMUM",
              "PCB_VIA_DRILL_BELOW_MINIMUM",
              "PCB_VIA_ANNULAR_BELOW_MINIMUM",
              "PCB_COPPER_OUTSIDE_OUTLINE",
              "PCB_COPPER_CLEARANCE_VIOLATION",
              "PCB_COMPONENT_BODY_OVERLAP",
              "PCB_COMPONENT_COURTYARD_OVERLAP",
              "PCB_COMPONENT_ASSEMBLY_CLEARANCE_WARNING",
              "PCB_COMPONENT_BOARD_EDGE_CLEARANCE_VIOLATION",
              "PCB_KEEPOUT_COPPER_VIOLATION",
              "PCB_KEEPOUT_VIA_VIOLATION",
              "PCB_KEEPOUT_PLACEMENT_VIOLATION",
              "PCB_NET_UNROUTED",
              "PCB_TRACK_WIDTH_BELOW_NET_CLASS",
              "PCB_VIA_DRILL_BELOW_NET_CLASS",
              "PCB_VIA_DIAMETER_BELOW_NET_CLASS",
              "PCB_COPPER_ON_DISALLOWED_LAYER",
              "PCB_RULE_BELOW_CAPABILITY",
              "PCB_RULE_AT_CAPABILITY_MINIMUM",
              "PCB_COPPER_LAYER_COUNT_OUTSIDE_CAPABILITY",
              "PCB_BOARD_THICKNESS_OUTSIDE_CAPABILITY",
              "PCB_BOARD_THICKNESS_AT_CAPABILITY_LIMIT",
              "PCB_COPPER_WEIGHT_OUTSIDE_CAPABILITY",
              "PCB_DRILL_DIAMETER_OUTSIDE_CAPABILITY",
              "PCB_DRILL_DIAMETER_AT_CAPABILITY_LIMIT",
          });
}

TEST_CASE("Diagnostic stores severity code message and related entities") {
    const auto diagnostic = volt::Diagnostic{
        volt::Severity::Warning,
        volt::DiagnosticCode{"SINGLE_PIN_NET"},
        "Net LED_A has only one connected pin",
        std::vector{volt::EntityRef::net(volt::NetId{7})},
    };

    CHECK(diagnostic.severity() == volt::Severity::Warning);
    CHECK(diagnostic.code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
    CHECK(diagnostic.message() == "Net LED_A has only one connected pin");
    REQUIRE(diagnostic.entities().size() == 1);
    CHECK(diagnostic.entities().front() == volt::EntityRef::net(volt::NetId{7}));
}

TEST_CASE("DiagnosticReport starts empty") {
    const volt::DiagnosticReport report;

    CHECK(report.empty());
    CHECK(report.count() == 0);
    CHECK_FALSE(report.has_errors());
}

TEST_CASE("DiagnosticReport preserves insertion order") {
    volt::DiagnosticReport report;

    report.add(volt::Diagnostic{
        volt::Severity::Info,
        volt::DiagnosticCode{"FIRST"},
        "first diagnostic",
    });
    report.add(volt::Diagnostic{
        volt::Severity::Warning,
        volt::DiagnosticCode{"SECOND"},
        "second diagnostic",
    });

    REQUIRE(report.diagnostics().size() == 2);
    CHECK(report.diagnostics()[0].code() == volt::DiagnosticCode{"FIRST"});
    CHECK(report.diagnostics()[1].code() == volt::DiagnosticCode{"SECOND"});
}

TEST_CASE("DiagnosticReport has_errors only reports error severity") {
    volt::DiagnosticReport report;

    report.add(volt::Diagnostic{
        volt::Severity::Info,
        volt::DiagnosticCode{"NOTE"},
        "informational note",
    });
    report.add(volt::Diagnostic{
        volt::Severity::Warning,
        volt::DiagnosticCode{"WARNING"},
        "warning note",
    });

    CHECK_FALSE(report.has_errors());

    report.add(volt::Diagnostic{
        volt::Severity::Error,
        volt::DiagnosticCode{"ERROR"},
        "error note",
    });

    CHECK(report.has_errors());
}

TEST_CASE("DiagnosticReport counts diagnostics by severity") {
    volt::DiagnosticReport report;

    report.add(volt::Diagnostic{volt::Severity::Info, volt::DiagnosticCode{"I1"}, "info"});
    report.add(volt::Diagnostic{volt::Severity::Warning, volt::DiagnosticCode{"W1"}, "warning"});
    report.add(volt::Diagnostic{volt::Severity::Warning, volt::DiagnosticCode{"W2"}, "warning"});
    report.add(volt::Diagnostic{volt::Severity::Error, volt::DiagnosticCode{"E1"}, "error"});

    CHECK(report.count() == 4);
    CHECK(report.count(volt::Severity::Info) == 1);
    CHECK(report.count(volt::Severity::Warning) == 2);
    CHECK(report.count(volt::Severity::Error) == 1);
}
