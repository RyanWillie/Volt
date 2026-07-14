#include "erc_drc_real_board_regression_helpers.hpp"

#include <volt/adapters/kicad/loss_report.hpp>
#include <volt/adapters/kicad/pcb_writer.hpp>

namespace {

[[nodiscard]] volt::FootprintDefinition real_testpoint_footprint() {
    return volt::FootprintDefinition{
        volt::FootprintRef{"regression", "TP_1MM_REAL"},
        std::vector{volt::FootprintPad::surface_mount(
            "1", volt::FootprintPadShape::Circle, volt::FootprintPoint{0.0, 0.0},
            volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::front_smd())},
        rectangle_polygon(0.75, 0.75),
        rectangle_polygon(0.55, 0.55),
    };
}

[[nodiscard]] volt::FootprintDefinition led_footprint_with_mechanical_locator() {
    return volt::FootprintDefinition{
        volt::FootprintRef{"regression", "LED_0603_WITH_LOCATOR"},
        std::vector{
            volt::FootprintPad::surface_mount(
                "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{-1.0, 0.0},
                volt::FootprintSize{0.8, 0.8}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "2", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{1.0, 0.0},
                volt::FootprintSize{0.8, 0.8}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::through_hole(
                "M1", volt::FootprintPadShape::Circle, volt::FootprintPoint{0.0, 1.2},
                volt::FootprintSize{0.7, 0.7}, volt::FootprintLayerSet::mechanical_hole(),
                volt::FootprintDrill{0.45, volt::FootprintPadPlating::NonPlated},
                volt::FootprintPadMechanicalRole::MechanicalSupport),
        },
        rectangle_polygon(1.55, 1.65),
        rectangle_polygon(1.30, 1.40),
    };
}

[[nodiscard]] volt::FootprintLibrary real_board_library_with_test_extensions() {
    auto library = real_board_library();
    library.add(real_testpoint_footprint());
    library.add(led_footprint_with_mechanical_locator());
    return library;
}

void select_testpoint(volt::Circuit &circuit, volt::ComponentId component,
                      volt::PinDefId pin_definition, std::string_view mpn) {
    select_part(circuit, component, mpn, volt::PackageRef{"TP"},
                volt::FootprintRef{"regression", "TP_1MM_REAL"},
                std::vector{volt::PinPadMapping{pin_definition, "1"}});
    set_selected_part_voltage_rating(circuit, component, 30.0);
}

[[nodiscard]] volt::BoardCapabilityProfile make_real_board_track_limit_profile() {
    return volt::BoardCapabilityProfile{
        "Real-board track limit fab",
        volt::BoardCapabilityProvenance{"Regression fixture capability table", "2026-06-16"},
        0.25,
        0.25,
        0.60,
        std::vector{
            volt::BoardClearancePair{volt::BoardClearanceKind::Track,
                                     volt::BoardClearanceKind::Track, 0.10},
        },
    };
}

[[nodiscard]] volt::BoardCapabilityProfile make_real_board_physical_fact_profile() {
    return volt::BoardCapabilityProfile{
        "Real-board physical fact fab",
        volt::BoardCapabilityProvenance{"Regression fixture capability table", "2026-06-16"},
        0.20,
        0.25,
        0.60,
        std::vector{
            volt::BoardClearancePair{volt::BoardClearanceKind::Track,
                                     volt::BoardClearanceKind::Track, 0.10},
        },
        {},
        std::vector{2},
        volt::BoardCapabilityRange{1.6, 2.0},
        std::vector{0.5},
        volt::BoardCapabilityRange{0.40, 0.65},
    };
}

} // namespace

TEST_CASE("Real-board foundation supports selective diagnostic assertions") {
    const auto fixture = make_real_board_fixture();
    const auto layout = make_real_board_layout(fixture);

    const auto report = volt::validate_board(layout.board, real_board_library());

    CHECK(report.empty());
    CHECK(find_diagnostics(report, "PCB_COPPER_CLEARANCE_VIOLATION").empty());
}

TEST_CASE("Real-board ERC matrix covers hierarchy and module-port boundaries") {
    auto fixture = make_real_board_fixture();
    const auto module = fixture.circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"RemoteSensorBlock"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"VCC"}, volt::NetKind::Power}},
        .ports = {volt::ModulePortSpec{volt::PortName{"VCC"}, volt::NetName{"VCC"},
                                       volt::PortRole::PowerInput}},
    });
    const auto port = fixture.circuit.get(module).ports()[0];
    const auto instance = fixture.circuit.instantiate_module(
        module, volt::ModuleInstanceSpec{.name = volt::ModuleInstanceName{"SENSOR_A"}});

    const auto report = volt::validate_circuit(fixture.circuit);

    [[maybe_unused]] const auto diagnostic = require_diagnostic_with_entities(
        report,
        ExpectedDiagnostic{"UNBOUND_REQUIRED_PORT", volt::Severity::Error,
                           volt::DiagnosticCategory{volt::diagnostic_categories::Erc}},
        std::vector{volt::EntityRef::module_instance(instance), volt::EntityRef::module_def(module),
                    volt::EntityRef::port_def(port)});
    check_diagnostic_count(report, "EMPTY_NET", 1);
}

TEST_CASE("Real-board PCB readiness matrix covers mapping and unplaced-pad boundaries") {
    const auto library = real_board_library_with_test_extensions();

    SECTION("selected part mapping to a non-electrical locator pad is a board-model error") {
        auto fixture = make_real_board_fixture();
        select_part(fixture.circuit, fixture.led, "LTST-C190-LOCATOR", volt::PackageRef{"0603"},
                    volt::FootprintRef{"regression", "LED_0603_WITH_LOCATOR"},
                    std::vector{volt::PinPadMapping{fixture.led_a_pin, "1"},
                                volt::PinPadMapping{fixture.led_k_pin, "2"},
                                volt::PinPadMapping{fixture.led_a_pin, "M1"}});
        const auto layout = make_real_board_layout(fixture);

        const auto report = volt::validate_board(layout.board, library);

        REQUIRE(layout.led_placement.has_value());
        [[maybe_unused]] const auto diagnostic = require_diagnostic_with_entities(
            report,
            ExpectedDiagnostic{"PCB_PAD_MAPPING_NON_ELECTRICAL", volt::Severity::Error,
                               volt::DiagnosticCategory{volt::diagnostic_categories::PcbBoard}},
            std::vector{volt::EntityRef::component(fixture.led),
                        volt::EntityRef::component_placement(layout.led_placement.value())});
    }

    SECTION("connected test pads with selected parts but no placements remain board-diagnosable") {
        auto fixture = make_real_board_fixture();
        const auto first = add_single_pin_component(
            fixture.circuit, "TP2", "PAD", volt::ConnectionRequirement::Required,
            volt::ElectricalTerminalKind::Passive, volt::ElectricalDirection::Passive,
            volt::ElectricalSignalDomain::Unspecified, volt::ElectricalDriveKind::Passive);
        const auto second = add_single_pin_component(
            fixture.circuit, "TP3", "PAD", volt::ConnectionRequirement::Required,
            volt::ElectricalTerminalKind::Passive, volt::ElectricalDirection::Passive,
            volt::ElectricalSignalDomain::Unspecified, volt::ElectricalDriveKind::Passive);
        select_testpoint(fixture.circuit, first.component, first.definition, "TP-1MM-A");
        select_testpoint(fixture.circuit, second.component, second.definition, "TP-1MM-B");
        const auto debug_net = fixture.circuit.add_net(
            volt::NetSpec{volt::NetName{"DEBUG_PAIR"}, volt::NetKind::Signal});
        fixture.circuit.connect(debug_net, first.pin);
        fixture.circuit.connect(debug_net, second.pin);
        const auto layout = make_real_board_layout(fixture);

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_count(report, "PCB_COMPONENT_NOT_PLACED", 2);
        [[maybe_unused]] const auto diagnostic = require_diagnostic_with_entities(
            report,
            ExpectedDiagnostic{"PCB_NET_WITHOUT_PLACED_PADS", volt::Severity::Warning,
                               volt::DiagnosticCategory{volt::diagnostic_categories::PcbBoard}},
            std::vector{volt::EntityRef::net(debug_net)});
    }
}

TEST_CASE("Real-board board-model matrix covers layer-stack and fabrication loss boundaries") {
    const auto library = real_board_library();

    SECTION("reversed physical stack order reports layer-side conflict with stable entities") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        layout.board.set_layer_stack(volt::LayerStack{{layout.back, layout.front}, 1.6});

        const auto report = volt::validate_board(layout.board, library);

        [[maybe_unused]] const auto diagnostic = require_diagnostic_with_entities(
            report,
            ExpectedDiagnostic{"PCB_LAYER_STACK_SIDE_ORDER_CONFLICT", volt::Severity::Error,
                               volt::DiagnosticCategory{volt::diagnostic_categories::PcbBoard}},
            std::vector{volt::EntityRef::board_layer(layout.back),
                        volt::EntityRef::board_layer(layout.front)});
    }

    SECTION("plated board-feature hole produces positive KiCad fab-loss diagnostics") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        [[maybe_unused]] const auto plated_mount = layout.board.add_feature(
            volt::BoardFeature::hole("MH1", volt::BoardPoint{40.0, 4.0}, 1.0, true, "mounting"));

        const auto export_result = volt::adapters::kicad::write_board(layout.board, library);
        const auto diagnostics =
            volt::adapters::kicad::fabrication_diagnostics(export_result.loss_report);

        check_diagnostic_summaries(
            diagnostics, {ExpectedDiagnostic{"PCB_KICAD_FAB_EXPORT_LOSS", volt::Severity::Error,
                                             volt::DiagnosticCategory{
                                                 volt::diagnostic_categories::PcbFabrication}}});
        CHECK(diagnostics.diagnostics()[0].entities() == std::vector{volt::EntityRef::board()});
        REQUIRE(diagnostics.diagnostics()[0].rule().has_value());
        CHECK(diagnostics.diagnostics()[0].rule().value() == "board.feature.hole.plated");
    }
}

TEST_CASE("Real-board DRC matrix covers room, zone, and outline shape variants") {
    const auto library = real_board_library();

    SECTION("fine-pitch room track width override contributes room entity to the DRC") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        auto room = volt::BoardRoom{
            "MCU LED escape",
            volt::BoardOutline::rectangle(volt::BoardPoint{27.0, 12.0}, volt::BoardSize{6.0, 2.0}),
            std::vector{layout.front},
        };
        room.set_track_width_mm(0.50);
        const auto room_id = layout.board.add_room(std::move(room));

        const auto report = volt::validate_board(layout.board, library);

        const auto diagnostic = require_diagnostic_with_entities(
            report,
            ExpectedDiagnostic{"PCB_TRACK_WIDTH_BELOW_NET_CLASS", volt::Severity::Error,
                               volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
            std::vector{volt::EntityRef::board_track(layout.led_drive_route),
                        volt::EntityRef::net(fixture.led_drive),
                        volt::EntityRef::board_layer(layout.front),
                        volt::EntityRef::board_room(room_id)});
        REQUIRE(diagnostic.get().measurement().has_value());
        CHECK(diagnostic.get().measurement()->actual_mm == 0.30);
        CHECK(diagnostic.get().measurement()->required_mm == 0.50);
    }

    SECTION("copper keepout also catches copper zones, not just tracks") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        const auto keepout = layout.board.add_keepout(volt::BoardKeepout{
            std::vector{volt::BoardPoint{15.0, 3.0}, volt::BoardPoint{17.0, 3.0},
                        volt::BoardPoint{17.0, 5.0}, volt::BoardPoint{15.0, 5.0}},
            std::vector{layout.front}, std::vector{volt::BoardKeepoutRestriction::Copper}});
        const auto zone = layout.board.add_zone(volt::BoardZone{
            std::vector{volt::BoardPoint{15.3, 3.3}, volt::BoardPoint{16.7, 3.3},
                        volt::BoardPoint{16.7, 4.7}, volt::BoardPoint{15.3, 4.7}},
            std::vector{layout.front},
        });

        const auto report = volt::validate_board(layout.board, library);

        [[maybe_unused]] const auto diagnostic = require_diagnostic_with_entities(
            report,
            ExpectedDiagnostic{"PCB_KEEPOUT_COPPER_VIOLATION", volt::Severity::Error,
                               volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
            std::vector{volt::EntityRef::board_keepout(keepout), volt::EntityRef::board_zone(zone),
                        volt::EntityRef::board_layer(layout.front)});
    }

    SECTION("via near the outline covers disc-shaped copper outline clearance") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        const auto via = layout.board.add_via(volt::BoardVia{
            fixture.vdd, volt::BoardPoint{43.8, 11.0}, layout.front, layout.back, 0.30, 0.70});

        const auto report = volt::validate_board(layout.board, library);

        const auto diagnostic = require_diagnostic_with_entities(
            report,
            ExpectedDiagnostic{"PCB_COPPER_OUTSIDE_OUTLINE", volt::Severity::Error,
                               volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
            std::vector{volt::EntityRef::board_via(via), volt::EntityRef::net(fixture.vdd),
                        volt::EntityRef::board_layer(layout.front)});
        CHECK_FALSE(diagnostic.get().overlays().empty());
    }
}

TEST_CASE("Real-board manufacturability matrix covers limits, warnings, and physical facts") {
    const auto library = real_board_library();

    SECTION("board rule exactly at capability minimum stays a warning") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        layout.board.set_capability_profile(make_real_board_track_limit_profile());

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_RULE_AT_CAPABILITY_MINIMUM", volt::Severity::Warning,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        CHECK(report.diagnostics()[0].entities() == std::vector{volt::EntityRef::board()});
    }

    SECTION("physical stackup facts cover thickness, copper weight, and drill range boundaries") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        layout.board.set_capability_profile(make_real_board_physical_fact_profile());
        const auto drill_at_limit = layout.board.add_via(volt::BoardVia{
            fixture.vdd, volt::BoardPoint{18.0, 18.0}, layout.front, layout.back, 0.40, 0.70});
        const auto drill_outside = layout.board.add_via(volt::BoardVia{
            fixture.vdd, volt::BoardPoint{20.0, 18.0}, layout.front, layout.back, 0.30, 0.70});

        const auto report = volt::validate_board(layout.board, library);

        [[maybe_unused]] const auto thickness = require_diagnostic_with_entities(
            report,
            ExpectedDiagnostic{"PCB_BOARD_THICKNESS_AT_CAPABILITY_LIMIT", volt::Severity::Warning,
                               volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
            std::vector{volt::EntityRef::board()});
        [[maybe_unused]] const auto front_weight = require_diagnostic_with_entities(
            report,
            ExpectedDiagnostic{"PCB_COPPER_WEIGHT_OUTSIDE_CAPABILITY", volt::Severity::Error,
                               volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
            std::vector{volt::EntityRef::board_layer(layout.front)});
        [[maybe_unused]] const auto back_weight = require_diagnostic_with_entities(
            report,
            ExpectedDiagnostic{"PCB_COPPER_WEIGHT_OUTSIDE_CAPABILITY", volt::Severity::Error,
                               volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
            std::vector{volt::EntityRef::board_layer(layout.back)});
        [[maybe_unused]] const auto drill_limit = require_diagnostic_with_entities(
            report,
            ExpectedDiagnostic{"PCB_DRILL_DIAMETER_AT_CAPABILITY_LIMIT", volt::Severity::Warning,
                               volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
            std::vector{volt::EntityRef::board_via(drill_at_limit)});
        [[maybe_unused]] const auto drill_range = require_diagnostic_with_entities(
            report,
            ExpectedDiagnostic{"PCB_DRILL_DIAMETER_OUTSIDE_CAPABILITY", volt::Severity::Error,
                               volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
            std::vector{volt::EntityRef::board_via(drill_outside)});
        check_diagnostic_count(report, "PCB_COPPER_WEIGHT_OUTSIDE_CAPABILITY", 2);
    }
}
