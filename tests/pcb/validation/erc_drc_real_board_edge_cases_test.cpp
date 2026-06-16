#include "erc_drc_real_board_regression_helpers.hpp"

namespace {

[[nodiscard]] volt::BoardCapabilityProfile make_physical_edge_capability_profile() {
    return volt::BoardCapabilityProfile{
        "Real-board physical edge fab",
        volt::BoardCapabilityProvenance{"Regression fixture physical table", "2026-06-16"},
        0.19,
        0.29,
        0.69,
        std::vector{
            volt::BoardClearancePair{volt::BoardClearanceKind::Track,
                                     volt::BoardClearanceKind::Track, 0.10},
            volt::BoardClearancePair{volt::BoardClearanceKind::Track, volt::BoardClearanceKind::Pad,
                                     0.10},
            volt::BoardClearancePair{volt::BoardClearanceKind::Pad,
                                     volt::BoardClearanceKind::BoardEdge, 0.10},
        },
        {},
        std::vector{4},
        volt::BoardCapabilityRange{2.0, 3.0},
    };
}

} // namespace

TEST_CASE("Real-board ERC edge cases cover semantic rule boundaries") {
    SECTION("forbidden connector pad tied to reset reports must-not-connect") {
        auto fixture = make_real_board_fixture();
        const auto forbidden = add_single_pin_component(
            fixture.circuit, "J2", "NC", volt::ConnectionRequirement::MustNotConnect,
            volt::ElectricalTerminalKind::NoConnect, volt::ElectricalDirection::Unspecified);
        fixture.circuit.connect(fixture.reset, forbidden.pin);

        const auto report = volt::validate_circuit(fixture.circuit);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PIN_MUST_NOT_CONNECT", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Erc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::pin(forbidden.pin),
                          volt::EntityRef::component(forbidden.component),
                          volt::EntityRef::pin_def(forbidden.definition),
                          volt::EntityRef::net(fixture.reset)});
    }

    SECTION("empty debug net and one-pin testpoint net remain diagnosable warnings") {
        auto fixture = make_real_board_fixture();
        const auto empty = fixture.circuit.add_net(
            volt::Net{volt::NetName{"UNUSED_DEBUG"}, volt::NetKind::Signal});
        const auto testpoint = add_single_pin_component(
            fixture.circuit, "TP1", "PAD", volt::ConnectionRequirement::Required,
            volt::ElectricalTerminalKind::Passive, volt::ElectricalDirection::Passive,
            volt::ElectricalSignalDomain::Unspecified, volt::ElectricalDriveKind::Passive);
        const auto spare =
            fixture.circuit.add_net(volt::Net{volt::NetName{"TP_SPARE"}, volt::NetKind::Signal});
        fixture.circuit.connect(spare, testpoint.pin);

        const auto report = volt::validate_circuit(fixture.circuit);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"EMPTY_NET", volt::Severity::Warning,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Erc}},
             ExpectedDiagnostic{"SINGLE_PIN_NET", volt::Severity::Warning,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Erc}}});
        CHECK(report.diagnostics()[0].entities() == std::vector{volt::EntityRef::net(empty)});
        CHECK(report.diagnostics()[1].entities() ==
              std::vector{volt::EntityRef::net(spare), volt::EntityRef::pin(testpoint.pin)});
    }

    SECTION("passive switched MCU supply rail reports no typed source") {
        auto fixture = make_real_board_fixture();
        REQUIRE(fixture.circuit.disconnect(fixture.mcu_vdd));
        const auto jumper = add_single_pin_component(
            fixture.circuit, "JP1", "1", volt::ConnectionRequirement::Required,
            volt::ElectricalTerminalKind::Passive, volt::ElectricalDirection::Passive,
            volt::ElectricalSignalDomain::Unspecified, volt::ElectricalDriveKind::Passive);
        const auto switched =
            fixture.circuit.add_net(volt::Net{volt::NetName{"SW_3V3"}, volt::NetKind::Power});
        fixture.circuit.connect(switched, fixture.mcu_vdd);
        fixture.circuit.connect(switched, jumper.pin);

        const auto report = volt::validate_circuit(fixture.circuit);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"POWER_INPUT_WITHOUT_SOURCE", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Erc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::net(switched), volt::EntityRef::pin(fixture.mcu_vdd)});
    }

    SECTION("ground pin swapped onto 3V3 reports rail-domain mismatch") {
        auto fixture = make_real_board_fixture();
        REQUIRE(fixture.circuit.disconnect(fixture.mcu_gnd));
        fixture.circuit.connect(fixture.vdd, fixture.mcu_gnd);

        const auto report = volt::validate_circuit(fixture.circuit);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PIN_GROUND_ON_NON_GROUND_NET", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Erc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::net(fixture.vdd), volt::EntityRef::pin(fixture.mcu_gnd),
                          volt::EntityRef::pin_def(fixture.mcu_gnd_pin)});
    }

    SECTION("power input tied to ground reports rail-domain mismatch") {
        auto fixture = make_real_board_fixture();
        const auto load = add_single_pin_component(
            fixture.circuit, "U3", "VDD", volt::ConnectionRequirement::Required,
            volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input);
        fixture.circuit.connect(fixture.ground, load.pin);

        const auto report = volt::validate_circuit(fixture.circuit);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PIN_POWER_ON_GROUND_NET", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Erc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::net(fixture.ground), volt::EntityRef::pin(load.pin),
                          volt::EntityRef::pin_def(load.definition)});
    }

    SECTION("USB rail above assigned net-class voltage limit is an ERC error") {
        auto fixture = make_real_board_fixture();
        auto usb_class = volt::NetClass{volt::NetClassName{"USB_LOW_VOLTAGE_ONLY"}};
        usb_class.set_maximum_net_voltage(volt::Quantity{volt::UnitDimension::Voltage, 3.6});
        assign_net_class(fixture.circuit, fixture.vbus, std::move(usb_class));

        const auto report = volt::validate_circuit(fixture.circuit);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"NET_CLASS_VOLTAGE_EXCEEDED", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Erc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::net(fixture.vbus)});
    }

    SECTION("mixed analog and digital reset inputs without a driver report domain mismatch") {
        auto fixture = make_real_board_fixture();
        const auto analog_reset = add_single_pin_component(
            fixture.circuit, "U3", "AIN_RESET", volt::ConnectionRequirement::Required,
            volt::ElectricalTerminalKind::Signal, volt::ElectricalDirection::Input,
            volt::ElectricalSignalDomain::Analog);
        fixture.circuit.connect(fixture.reset, analog_reset.pin);

        const auto report = volt::validate_circuit(fixture.circuit);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"INPUT_SIGNAL_DOMAIN_MISMATCH", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Erc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{
                  volt::EntityRef::net(fixture.reset), volt::EntityRef::pin(fixture.header_reset),
                  volt::EntityRef::pin(fixture.mcu_reset), volt::EntityRef::pin(analog_reset.pin)});
    }
}

TEST_CASE("Real-board PCB readiness catches selected-part and pad-mapping edge cases") {
    const auto library = real_board_library();

    SECTION("placed LED without a selected part stays a readiness and board-model error") {
        auto fixture = make_real_board_fixture(false);
        const auto layout = make_real_board_layout(fixture);

        const auto readiness = volt::validate_for_pcb(fixture.circuit);
        const auto board = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            readiness,
            {ExpectedDiagnostic{"PHYSICAL_PART_REQUIRED", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::General}}});
        check_diagnostic_summaries(
            board,
            {ExpectedDiagnostic{"PCB_COMPONENT_MISSING_SELECTED_PART", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::PcbBoard}}});
        CHECK(board.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::component(fixture.led)});
    }

    SECTION("selected LED footprint typo is reported before DRC geometry") {
        auto fixture = make_real_board_fixture();
        select_part(fixture.circuit, fixture.led, "LTST-C190-MISSING", volt::PackageRef{"0603"},
                    volt::FootprintRef{"regression", "LED_DOES_NOT_EXIST"},
                    std::vector{volt::PinPadMapping{fixture.led_a_pin, "1"},
                                volt::PinPadMapping{fixture.led_k_pin, "2"}});
        const auto layout = make_real_board_layout(fixture);

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_FOOTPRINT_UNRESOLVED", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::PcbBoard}}});
        REQUIRE(layout.led_placement.has_value());
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::component(fixture.led),
                          volt::EntityRef::component_placement(layout.led_placement.value())});
    }

    SECTION(
        "selected LED mapping to an unknown pad also reports the first missing electrical pad") {
        auto fixture = make_real_board_fixture();
        select_part(fixture.circuit, fixture.led, "LTST-C190-BADPAD", volt::PackageRef{"0603"},
                    volt::FootprintRef{"regression", "LED_0603_REAL"},
                    std::vector{volt::PinPadMapping{fixture.led_a_pin, "99"},
                                volt::PinPadMapping{fixture.led_k_pin, "2"}});
        const auto layout = make_real_board_layout(fixture);

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_PAD_MAPPING_UNKNOWN_PAD", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::PcbBoard}},
             ExpectedDiagnostic{"PCB_PAD_MAPPING_MISSING_PIN", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::PcbBoard}}});
        REQUIRE(layout.led_placement.has_value());
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::component(fixture.led),
                          volt::EntityRef::component_placement(layout.led_placement.value())});
        CHECK(report.diagnostics()[1].entities() ==
              std::vector{volt::EntityRef::component(fixture.led),
                          volt::EntityRef::component_placement(layout.led_placement.value())});
    }

    SECTION(
        "selected LED mapping to a pin from another component is rejected at mutation boundary") {
        auto fixture = make_real_board_fixture();

        CHECK_THROWS(select_part(fixture.circuit, fixture.led, "LTST-C190-WRONGPIN",
                                 volt::PackageRef{"0603"},
                                 volt::FootprintRef{"regression", "LED_0603_REAL"},
                                 std::vector{volt::PinPadMapping{fixture.mcu_vdd_pin, "1"},
                                             volt::PinPadMapping{fixture.led_k_pin, "2"}}));
    }
}

TEST_CASE("Real-board DRC edge cases cover net-class, placement, zone, and feature geometry") {
    const auto library = real_board_library();

    SECTION("stricter LED net class rejects otherwise board-legal track and via sizes") {
        auto fixture = make_real_board_fixture();
        auto led_rules = volt::NetClass{volt::NetClassName{"LED_HIGH_CURRENT"}};
        led_rules.set_track_width_mm(0.50);
        led_rules.set_via_size_mm(0.40, 0.80);
        assign_net_class(fixture.circuit, fixture.led_drive, std::move(led_rules));
        auto layout = make_real_board_layout(fixture);
        const auto via =
            layout.board.add_via(volt::BoardVia{fixture.led_drive, volt::BoardPoint{30.0, 18.0},
                                                layout.front, layout.back, 0.30, 0.70});

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_TRACK_WIDTH_BELOW_NET_CLASS", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_VIA_DRILL_BELOW_NET_CLASS", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_VIA_DIAMETER_BELOW_NET_CLASS", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::board_track(layout.led_drive_route),
                          volt::EntityRef::net(fixture.led_drive),
                          volt::EntityRef::board_layer(layout.front)});
        CHECK(
            report.diagnostics()[1].entities() ==
            std::vector{volt::EntityRef::board_via(via), volt::EntityRef::net(fixture.led_drive)});
        CHECK(
            report.diagnostics()[2].entities() ==
            std::vector{volt::EntityRef::board_via(via), volt::EntityRef::net(fixture.led_drive)});
    }

    SECTION("placed LED crossing the outline stays a board placement error") {
        const auto fixture = make_real_board_fixture();
        const auto layout = make_real_board_layout(
            fixture, BoardOptions{.led_position = volt::BoardPoint{43.8, 13.0}});

        const auto report = volt::validate_board(layout.board, library);

        const auto *outside = find_diagnostic(report, "PCB_PLACEMENT_OUTSIDE_OUTLINE");
        REQUIRE(outside != nullptr);
        CHECK(outside->severity() == volt::Severity::Error);
        CHECK(outside->category() ==
              volt::DiagnosticCategory{volt::diagnostic_categories::PcbBoard});
        REQUIRE(layout.led_placement.has_value());
        CHECK(outside->entities() ==
              std::vector{volt::EntityRef::component(fixture.led),
                          volt::EntityRef::component_placement(layout.led_placement.value())});
    }

    SECTION("mounting slot without role and tooling hole outside outline stay board diagnostics") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        const auto missing_role = layout.board.add_feature(volt::BoardFeature::slot(
            "JTAG_SLOT", volt::BoardPoint{15.0, 3.0}, volt::BoardPoint{18.0, 3.0}, 0.8));
        const auto outside = layout.board.add_feature(
            volt::BoardFeature::hole("FID2", volt::BoardPoint{46.0, 18.0}, 1.0, false, "fiducial"));

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_BOARD_FEATURE_ROLE_MISSING", volt::Severity::Warning,
                                volt::DiagnosticCategory{volt::diagnostic_categories::PcbBoard}},
             ExpectedDiagnostic{"PCB_BOARD_FEATURE_OUTSIDE_OUTLINE", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::PcbBoard}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::board_feature(missing_role)});
        CHECK(report.diagnostics()[1].entities() ==
              std::vector{volt::EntityRef::board_feature(outside)});
    }

    SECTION("same-side assembly labels overlap as visual warnings") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        const auto revision = layout.board.add_text(
            volt::BoardText{"REV A", volt::BoardPoint{7.0, 2.0}, volt::BoardRotation::degrees(0.0),
                            layout.front, 1.0});
        const auto lot = layout.board.add_text(volt::BoardText{"LOT42", volt::BoardPoint{7.4, 2.0},
                                                               volt::BoardRotation::degrees(0.0),
                                                               layout.front, 1.0});

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_VISUAL_LABEL_OVERLAP", volt::Severity::Warning,
                                volt::DiagnosticCategory{volt::diagnostic_categories::PcbVisual}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::board_text(revision), volt::EntityRef::board_text(lot)});
    }

    SECTION("top-only LED copper pour on bottom layer reports zone layer violation") {
        auto fixture = make_real_board_fixture();
        auto led_rules = volt::NetClass{volt::NetClassName{"TOP_ONLY_LED_ZONE"}};
        led_rules.set_layer_scope(volt::NetClassLayerScope::TopOnly);
        assign_net_class(fixture.circuit, fixture.led_drive, std::move(led_rules));
        auto layout = make_real_board_layout(fixture);
        const auto zone = layout.board.add_zone(volt::BoardZone{
            std::vector{volt::BoardPoint{29.0, 18.0}, volt::BoardPoint{31.0, 18.0},
                        volt::BoardPoint{31.0, 20.0}, volt::BoardPoint{29.0, 20.0}},
            std::vector{layout.back},
            fixture.led_drive,
        });

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_COPPER_ON_DISALLOWED_LAYER", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::board_zone(zone),
                          volt::EntityRef::net(fixture.led_drive),
                          volt::EntityRef::board_layer(layout.back)});
    }

    SECTION("netless copper pour clipped by the board outline reports zone outline violation") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        const auto zone = layout.board.add_zone(volt::BoardZone{
            std::vector{volt::BoardPoint{42.0, 18.0}, volt::BoardPoint{46.0, 18.0},
                        volt::BoardPoint{46.0, 21.0}, volt::BoardPoint{42.0, 21.0}},
            std::vector{layout.front},
        });

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_COPPER_OUTSIDE_OUTLINE", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::board_zone(zone),
                          volt::EntityRef::board_layer(layout.front)});
    }
}

TEST_CASE("Real-board manufacturability edge cases cover physical capability facts") {
    const auto fixture = make_real_board_fixture();
    auto layout = make_real_board_layout(fixture);
    layout.board.set_capability_profile(make_physical_edge_capability_profile());

    const auto report = volt::validate_board(layout.board, real_board_library());

    check_diagnostic_summaries(
        report,
        {ExpectedDiagnostic{"PCB_COPPER_LAYER_COUNT_OUTSIDE_CAPABILITY", volt::Severity::Error,
                            volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
         ExpectedDiagnostic{"PCB_BOARD_THICKNESS_OUTSIDE_CAPABILITY", volt::Severity::Error,
                            volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
    CHECK(report.diagnostics()[0].entities() == std::vector{volt::EntityRef::board()});
    CHECK(report.diagnostics()[1].entities() == std::vector{volt::EntityRef::board()});
}
