#include "erc_drc_real_board_regression_helpers.hpp"

TEST_CASE("Real-board ERC and DRC regression accepts a fully routed status-controller board") {
    const auto fixture = make_real_board_fixture();
    const auto layout = make_real_board_layout(fixture);
    const auto library = real_board_library();

    const auto erc = volt::validate_circuit(fixture.circuit);
    const auto pcb_readiness = volt::validate_for_pcb(fixture.circuit);
    const auto board = volt::validate_board(layout.board, library);

    CHECK(erc.empty());
    CHECK(pcb_readiness.empty());
    INFO("actual board diagnostic codes: " << diagnostic_code_list(board));
    CHECK(board.empty());
}

TEST_CASE("Real-board regression keeps ERC, PCB readiness, DRC, visual, and fab boundaries clear") {
    auto fixture = make_real_board_fixture(false);
    auto layout = make_real_board_layout(fixture, {}, false);
    const auto library = real_board_library();

    const auto erc = volt::validate_circuit(fixture.circuit);
    const auto pcb_readiness = volt::validate_for_pcb(fixture.circuit);
    auto board = volt::validate_board(layout.board, library);

    CHECK(erc.empty());
    check_diagnostic_summaries(
        pcb_readiness,
        {ExpectedDiagnostic{"PHYSICAL_PART_REQUIRED", volt::Severity::Error,
                            volt::DiagnosticCategory{volt::diagnostic_categories::General}}});
    check_diagnostic_summaries(
        board,
        {ExpectedDiagnostic{"PCB_COMPONENT_NOT_PLACED", volt::Severity::Error,
                            volt::DiagnosticCategory{volt::diagnostic_categories::PcbBoard}},
         ExpectedDiagnostic{"PCB_COMPONENT_MISSING_SELECTED_PART", volt::Severity::Error,
                            volt::DiagnosticCategory{volt::diagnostic_categories::PcbBoard}}});
    CHECK(board.diagnostics()[0].entities() ==
          std::vector{volt::EntityRef::component(fixture.led)});
    CHECK(board.diagnostics()[1].entities() ==
          std::vector{volt::EntityRef::component(fixture.led)});

    [[maybe_unused]] const auto text = layout.board.add_text(
        volt::BoardText{"REV A", volt::BoardPoint{42.0, 1.0}, volt::BoardRotation::degrees(0.0),
                        layout.front, 2.0});
    board = volt::validate_board(layout.board, library);
    const auto *visual =
        find_diagnostic(board, volt::pcb_visual_diagnostic_codes::LabelOutsideBoard);
    REQUIRE(visual != nullptr);
    CHECK(visual->severity() == volt::Severity::Warning);
    CHECK(visual->category() == volt::DiagnosticCategory{volt::diagnostic_categories::PcbVisual});

    layout.board.set_capability_profile(make_manufacturing_prereq_profile());
    board = volt::validate_board(layout.board, library);
    const auto *capability =
        find_diagnostic(board, volt::drc_diagnostic_codes::RuleBelowCapability);
    REQUIRE(capability != nullptr);
    CHECK(capability->severity() == volt::Severity::Error);
    CHECK(capability->category() == volt::DiagnosticCategory{volt::diagnostic_categories::Drc});
    CHECK(find_diagnostic(board, volt::pcb_fabrication_diagnostic_codes::KiCadFabExportLoss) ==
          nullptr);
}

TEST_CASE("Real-board ERC regression locks intentionally broken logical variants") {
    SECTION("required MCU power pin left unconnected") {
        auto fixture = make_real_board_fixture();
        REQUIRE(fixture.circuit.disconnect(fixture.mcu_vdd));

        const auto report = volt::validate_circuit(fixture.circuit);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"UNCONNECTED_REQUIRED_PIN", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Erc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::pin(fixture.mcu_vdd),
                          volt::EntityRef::component(fixture.mcu),
                          volt::EntityRef::pin_def(fixture.mcu_vdd_pin)});
    }

    SECTION("authored no-connect MCU boot pin is accidentally tied to reset") {
        auto fixture = make_real_board_fixture();
        fixture.circuit.connect(fixture.reset, fixture.mcu_boot);

        const auto report = volt::validate_circuit(fixture.circuit);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PIN_INTENTIONAL_NO_CONNECT_IS_CONNECTED", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Erc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::pin(fixture.mcu_boot),
                          volt::EntityRef::component(fixture.mcu),
                          volt::EntityRef::pin_def(fixture.mcu_boot_pin),
                          volt::EntityRef::net(fixture.reset)});
    }

    SECTION("3V3 rail overdrives the MCU pin voltage range") {
        auto fixture = make_real_board_fixture();
        set_net_voltage(fixture.circuit, fixture.vdd, 5.0);

        const auto report = volt::validate_circuit(fixture.circuit);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PIN_VOLTAGE_RANGE_VIOLATION", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Erc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::net(fixture.vdd), volt::EntityRef::pin(fixture.mcu_vdd),
                          volt::EntityRef::pin_def(fixture.mcu_vdd_pin)});
    }

    SECTION("selected MCU part voltage rating is below the authored 3V3 rail") {
        auto fixture = make_real_board_fixture();
        set_selected_part_voltage_rating(fixture.circuit, fixture.mcu, 2.5);

        const auto report = volt::validate_circuit(fixture.circuit);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"SELECTED_PART_VOLTAGE_RATING_EXCEEDED", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Erc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::net(fixture.vdd), volt::EntityRef::pin(fixture.mcu_vdd),
                          volt::EntityRef::component(fixture.mcu)});
    }

    SECTION("two physical supply outputs are shorted onto VBUS") {
        auto fixture = make_real_board_fixture();
        REQUIRE(fixture.circuit.disconnect(fixture.regulator_vout));
        fixture.circuit.connect(fixture.vbus, fixture.regulator_vout);

        const auto report = volt::validate_circuit(fixture.circuit);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"MULTIPLE_OUTPUTS_ON_NET", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Erc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::net(fixture.vbus),
                          volt::EntityRef::pin(fixture.header_vbus),
                          volt::EntityRef::pin(fixture.regulator_vout)});
    }
}

TEST_CASE("Real-board DRC regression locks broken copper and placement variants") {
    const auto library = real_board_library();

    SECTION("narrow routed LED drive track fails the board minimum") {
        const auto fixture = make_real_board_fixture();
        const auto layout =
            make_real_board_layout(fixture, BoardOptions{.narrow_led_drive_route = true});

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_TRACK_WIDTH_BELOW_MINIMUM", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::board_track(layout.led_drive_route),
                          volt::EntityRef::net(fixture.led_drive),
                          volt::EntityRef::board_layer(layout.front)});
        REQUIRE(report.diagnostics()[0].measurement().has_value());
        CHECK(report.diagnostics()[0].measurement()->actual_mm == 0.10);
        CHECK(report.diagnostics()[0].measurement()->required_mm == 0.25);
    }

    SECTION("undersized via locks drill and annular diagnostics before later DRC rules") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        const auto via = layout.board.add_via(volt::BoardVia{
            fixture.vdd, volt::BoardPoint{18.0, 13.0}, layout.front, layout.back, 0.20, 0.50});

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_VIA_DRILL_BELOW_MINIMUM", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_VIA_ANNULAR_BELOW_MINIMUM", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::board_via(via), volt::EntityRef::net(fixture.vdd)});
        CHECK(report.diagnostics()[1].entities() ==
              std::vector{volt::EntityRef::board_via(via), volt::EntityRef::net(fixture.vdd)});
    }

    SECTION("clearance failure between two extra routed nets preserves copper entity ordering") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        const auto vbus_island = layout.board.add_track(volt::BoardTrack{
            fixture.vbus, layout.front,
            std::vector{volt::BoardPoint{10.0, 20.0}, volt::BoardPoint{20.0, 20.0}}, 0.30});
        const auto reset_island = layout.board.add_track(volt::BoardTrack{
            fixture.reset, layout.front,
            std::vector{volt::BoardPoint{10.0, 20.25}, volt::BoardPoint{20.0, 20.25}}, 0.30});

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_COPPER_CLEARANCE_VIOLATION", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::board_track(vbus_island),
                          volt::EntityRef::board_track(reset_island),
                          volt::EntityRef::net(fixture.vbus), volt::EntityRef::net(fixture.reset),
                          volt::EntityRef::board_layer(layout.front)});
        REQUIRE(report.diagnostics()[0].measurement().has_value());
        CHECK(report.diagnostics()[0].measurement()->actual_mm <
              report.diagnostics()[0].measurement()->required_mm);
    }

    SECTION("copper outside the outline stays a DRC error") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        const auto outside = layout.board.add_track(volt::BoardTrack{
            fixture.reset, layout.front,
            std::vector{volt::BoardPoint{43.9, 18.0}, volt::BoardPoint{46.0, 18.0}}, 0.30});

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_COPPER_OUTSIDE_OUTLINE", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::board_track(outside),
                          volt::EntityRef::net(fixture.reset),
                          volt::EntityRef::board_layer(layout.front)});
    }

    SECTION("keepouts catch copper, vias, and placements on realistic board geometry") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        const auto copper_keepout = layout.board.add_keepout(volt::BoardKeepout{
            std::vector{volt::BoardPoint{7.0, 9.75}, volt::BoardPoint{11.0, 9.75},
                        volt::BoardPoint{11.0, 10.25}, volt::BoardPoint{7.0, 10.25}},
            std::vector{layout.front}, std::vector{volt::BoardKeepoutRestriction::Copper}});
        const auto via_keepout = layout.board.add_keepout(volt::BoardKeepout{
            std::vector{volt::BoardPoint{17.0, 12.0}, volt::BoardPoint{19.0, 12.0},
                        volt::BoardPoint{19.0, 14.0}, volt::BoardPoint{17.0, 14.0}},
            std::vector{layout.front, layout.back},
            std::vector{volt::BoardKeepoutRestriction::Via}});
        const auto placement_keepout = layout.board.add_keepout(volt::BoardKeepout{
            std::vector{volt::BoardPoint{10.5, 8.5}, volt::BoardPoint{13.5, 8.5},
                        volt::BoardPoint{13.5, 11.5}, volt::BoardPoint{10.5, 11.5}},
            std::vector{layout.front}, std::vector{volt::BoardKeepoutRestriction::Placement}});
        const auto via = layout.board.add_via(volt::BoardVia{
            fixture.vdd, volt::BoardPoint{18.0, 13.0}, layout.front, layout.back, 0.30, 0.70});

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_KEEPOUT_COPPER_VIOLATION", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_KEEPOUT_VIA_VIOLATION", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_KEEPOUT_PLACEMENT_VIOLATION", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::board_keepout(copper_keepout),
                          volt::EntityRef::board_track(layout.vbus_route),
                          volt::EntityRef::net(fixture.vbus),
                          volt::EntityRef::board_layer(layout.front)});
        CHECK(report.diagnostics()[1].entities() ==
              std::vector{volt::EntityRef::board_keepout(via_keepout),
                          volt::EntityRef::board_via(via), volt::EntityRef::net(fixture.vdd),
                          volt::EntityRef::board_layer(layout.front)});
        CHECK(report.diagnostics()[2].entities() ==
              std::vector{volt::EntityRef::board_keepout(placement_keepout),
                          volt::EntityRef::component_placement(layout.regulator_placement),
                          volt::EntityRef::component(fixture.regulator)});
    }

    SECTION("overlapping placed components report visual and DRC footprint-geometry diagnostics") {
        const auto fixture = make_real_board_fixture();
        const auto layout =
            make_real_board_layout(fixture, BoardOptions{.overlap_led_with_resistor = true});

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_VISUAL_PLACEMENT_OVERLAP", volt::Severity::Warning,
                                volt::DiagnosticCategory{volt::diagnostic_categories::PcbVisual}},
             ExpectedDiagnostic{"PCB_COPPER_CLEARANCE_VIOLATION", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_COPPER_CLEARANCE_VIOLATION", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_COPPER_CLEARANCE_VIOLATION", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_COPPER_CLEARANCE_VIOLATION", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_COMPONENT_BODY_OVERLAP", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_COMPONENT_COURTYARD_OVERLAP", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_NET_UNROUTED", volt::Severity::Warning,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_NET_UNROUTED", volt::Severity::Warning,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        REQUIRE(layout.led_placement.has_value());
        CHECK(report.diagnostics()[5].entities() ==
              std::vector{volt::EntityRef::component_placement(layout.resistor_placement),
                          volt::EntityRef::component_placement(layout.led_placement.value()),
                          volt::EntityRef::component(fixture.resistor),
                          volt::EntityRef::component(fixture.led)});
    }

    SECTION("omitted LED anode route reports the stable unrouted ratsnest endpoint") {
        const auto fixture = make_real_board_fixture();
        const auto layout =
            make_real_board_layout(fixture, BoardOptions{.omit_led_anode_route = true});

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_NET_UNROUTED", volt::Severity::Warning,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        REQUIRE(layout.led_placement.has_value());
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::net(fixture.led_anode),
                          volt::EntityRef::component_placement(layout.resistor_placement),
                          volt::EntityRef::footprint_pad(volt::FootprintPadId{1}),
                          volt::EntityRef::component_placement(layout.led_placement.value()),
                          volt::EntityRef::footprint_pad(volt::FootprintPadId{0})});
    }
}

TEST_CASE("Real-board DRC regression covers net-class and manufacturability prerequisites") {
    const auto library = real_board_library();

    SECTION("bottom-layer copper violates a top-only signal net class") {
        auto fixture = make_real_board_fixture();
        auto signal_class = volt::NetClass{volt::NetClassName{"TOP_ONLY_LED"}};
        signal_class.set_layer_scope(volt::NetClassLayerScope::TopOnly);
        const auto class_id = fixture.circuit.net_classes().add_net_class(std::move(signal_class));
        REQUIRE(fixture.circuit.net_classes().assign_net_class(fixture.led_drive, class_id));
        auto layout = make_real_board_layout(fixture);
        const auto bottom_track = layout.board.add_track(volt::BoardTrack{
            fixture.led_drive, layout.back,
            std::vector{volt::BoardPoint{28.0, 18.0}, volt::BoardPoint{31.0, 18.0}}, 0.30});

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_COPPER_ON_DISALLOWED_LAYER", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::board_track(bottom_track),
                          volt::EntityRef::net(fixture.led_drive),
                          volt::EntityRef::board_layer(layout.back)});
    }

    SECTION("fab capability profile rejects board rules below manufacturing prerequisites") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        layout.board.set_capability_profile(make_manufacturing_prereq_profile());

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_RULE_BELOW_CAPABILITY", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_RULE_BELOW_CAPABILITY", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_RULE_BELOW_CAPABILITY", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        CHECK(report.diagnostics()[0].entities() == std::vector{volt::EntityRef::board()});
        CHECK(report.diagnostics()[1].entities() == std::vector{volt::EntityRef::board()});
        CHECK(report.diagnostics()[2].entities() == std::vector{volt::EntityRef::board()});
    }
}
