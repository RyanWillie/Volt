#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints/footprints.hpp>

namespace {

struct PlacementFixture {
    volt::Circuit circuit;
    std::vector<volt::ComponentId> components;
};

const volt::Diagnostic *find_diagnostic(const volt::DiagnosticReport &report,
                                        const std::string &code) {
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code().value() == code) {
            return &diagnostic;
        }
    }
    return nullptr;
}

[[nodiscard]] std::vector<const volt::Diagnostic *>
find_diagnostics(const volt::DiagnosticReport &report, const std::string &code) {
    auto matches = std::vector<const volt::Diagnostic *>{};
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code().value() == code) {
            matches.push_back(&diagnostic);
        }
    }
    return matches;
}

[[nodiscard]] volt::FootprintRef square_package_ref() {
    return volt::FootprintRef{"test", "SquarePackage"};
}

[[nodiscard]] volt::FootprintDefinition square_package_footprint() {
    const auto outline = volt::FootprintPolygon{std::vector{
        volt::FootprintPoint{-0.5, -0.5},
        volt::FootprintPoint{0.5, -0.5},
        volt::FootprintPoint{0.5, 0.5},
        volt::FootprintPoint{-0.5, 0.5},
    }};
    return volt::FootprintDefinition{
        square_package_ref(),
        std::vector{
            volt::FootprintPad::surface_mount(
                "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{-0.2, 0.0},
                volt::FootprintSize{0.2, 0.2}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "2", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.2, 0.0},
                volt::FootprintSize{0.2, 0.2}, volt::FootprintLayerSet::front_smd()),
        },
        volt::FootprintPackageGeometry{
            outline,
            outline,
            outline,
            outline,
            std::vector{volt::FootprintMarking{
                volt::FootprintMarkingKind::Silkscreen,
                volt::FootprintPolygon{std::vector{
                    volt::FootprintPoint{-0.45, 0.35},
                    volt::FootprintPoint{0.45, 0.35},
                    volt::FootprintPoint{0.45, 0.45},
                    volt::FootprintPoint{-0.45, 0.45},
                }},
            }},
        },
    };
}

[[nodiscard]] volt::FootprintLibrary square_package_library() {
    auto library = volt::FootprintLibrary{};
    library.add(square_package_footprint());
    return library;
}

[[nodiscard]] PlacementFixture
make_placed_resistors(std::size_t count, volt::FootprintRef footprint = volt::FootprintRef{
                                             "passives", "R_0603_1608Metric"}) {
    auto circuit = volt::Circuit{};
    const auto first_pin_definition = circuit.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin_definition = circuit.add_pin_definition(volt::PinDefinition{
        "B", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_definition = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", {first_pin_definition, second_pin_definition}});

    auto components = std::vector<volt::ComponentId>{};
    components.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto component = circuit.instantiate_component(
            component_definition, volt::ReferenceDesignator{"R" + std::to_string(index + 1U)});
        circuit.select_physical_part(
            component, volt::PhysicalPart{
                           volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                           volt::PackageRef{"0603"},
                           footprint,
                           std::vector{volt::PinPadMapping{first_pin_definition, "1"},
                                       volt::PinPadMapping{second_pin_definition, "2"}},
                       });
        components.push_back(component);
    }

    return PlacementFixture{std::move(circuit), std::move(components)};
}

[[nodiscard]] volt::Board make_visual_board(const PlacementFixture &fixture) {
    auto board = volt::Board{fixture.circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{50.0, 30.0}));
    return board;
}

} // namespace

TEST_CASE("Board validation diagnoses layer stack side order conflicts") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{10.0, 10.0}));
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_layer_stack(volt::LayerStack{{back, front}, 1.6});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *order = find_diagnostic(report, "PCB_LAYER_STACK_SIDE_ORDER_CONFLICT");
    REQUIRE(order != nullptr);
    CHECK(order->severity() == volt::Severity::Error);
    CHECK(order->entities() ==
          std::vector{volt::EntityRef::board_layer(back), volt::EntityRef::board_layer(front)});
}

TEST_CASE("Board validation checks mechanical opening extents against the outline") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{10.0, 10.0}));
    const auto clipped_hole = board.add_feature(
        volt::BoardFeature::hole("MH", volt::BoardPoint{0.5, 5.0}, 2.0, false, "mounting"));

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *outside_outline = find_diagnostic(report, "PCB_BOARD_FEATURE_OUTSIDE_OUTLINE");
    REQUIRE(outside_outline != nullptr);
    CHECK(outside_outline->severity() == volt::Severity::Error);
    CHECK(outside_outline->entities() == std::vector{volt::EntityRef::board_feature(clipped_hole)});
}

TEST_CASE("Board validation checks cutout edges against concave outlines") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    board.set_outline(volt::BoardOutline{std::vector{
        volt::BoardPoint{0.0, 0.0},
        volt::BoardPoint{10.0, 0.0},
        volt::BoardPoint{10.0, 4.0},
        volt::BoardPoint{4.0, 4.0},
        volt::BoardPoint{4.0, 10.0},
        volt::BoardPoint{0.0, 10.0},
    }});
    const auto clipped_cutout = board.add_feature(volt::BoardFeature::cutout(
        "CUT",
        std::vector{volt::BoardPoint{3.0, 3.0}, volt::BoardPoint{9.0, 3.0},
                    volt::BoardPoint{3.0, 9.0}},
        "access"));

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *outside_outline = find_diagnostic(report, "PCB_BOARD_FEATURE_OUTSIDE_OUTLINE");
    REQUIRE(outside_outline != nullptr);
    CHECK(outside_outline->severity() == volt::Severity::Error);
    CHECK(outside_outline->entities() ==
          std::vector{volt::EntityRef::board_feature(clipped_cutout)});
}

TEST_CASE("Board validation rejects zero-clearance polygon edges crossing concave outlines") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    board.set_outline(volt::BoardOutline{std::vector{
        volt::BoardPoint{0.0, 0.0},
        volt::BoardPoint{10.0, 0.0},
        volt::BoardPoint{10.0, 10.0},
        volt::BoardPoint{8.0, 10.0},
        volt::BoardPoint{8.0, 2.0},
        volt::BoardPoint{6.0, 2.0},
        volt::BoardPoint{6.0, 10.0},
        volt::BoardPoint{4.0, 10.0},
        volt::BoardPoint{4.0, 2.0},
        volt::BoardPoint{2.0, 2.0},
        volt::BoardPoint{2.0, 10.0},
        volt::BoardPoint{0.0, 10.0},
    }});
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_layer_stack(volt::LayerStack{{front}, 1.6});
    const auto zone = board.add_zone(volt::BoardZone{
        std::vector{
            volt::BoardPoint{1.0, 5.0},
            volt::BoardPoint{9.0, 5.0},
            volt::BoardPoint{9.0, 6.0},
            volt::BoardPoint{1.0, 6.0},
        },
        std::vector{front},
    });

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *outside_outline = find_diagnostic(report, "PCB_COPPER_OUTSIDE_OUTLINE");
    REQUIRE(outside_outline != nullptr);
    CHECK(outside_outline->severity() == volt::Severity::Error);
    CHECK(outside_outline->category() ==
          volt::DiagnosticCategory{volt::diagnostic_categories::Drc});
    CHECK(outside_outline->entities() ==
          std::vector{volt::EntityRef::board_zone(zone), volt::EntityRef::board_layer(front)});
}

TEST_CASE("Board footprint DRC reports measured assembly comfort gaps") {
    const auto library = square_package_library();
    const auto fixture = make_placed_resistors(3, square_package_ref());
    auto board = make_visual_board(fixture);
    const auto first = board.place_component(volt::ComponentPlacement{
        fixture.components[0], volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    const auto second = board.place_component(volt::ComponentPlacement{
        fixture.components[1], volt::BoardPoint{11.15, 10.0}, volt::BoardRotation::degrees(0.0)});
    [[maybe_unused]] const auto clean = board.place_component(volt::ComponentPlacement{
        fixture.components[2], volt::BoardPoint{12.45, 10.0}, volt::BoardRotation::degrees(0.0)});

    const auto report = volt::validate_board(board, library);

    const auto warnings = find_diagnostics(report, "PCB_COMPONENT_ASSEMBLY_CLEARANCE_WARNING");
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0]->severity() == volt::Severity::Warning);
    CHECK(warnings[0]->category() == volt::DiagnosticCategory{volt::diagnostic_categories::Drc});
    CHECK(warnings[0]->rule() == "component-body-to-body-assembly-clearance");
    CHECK(warnings[0]->measurement() == volt::DiagnosticMeasurement{0.15, 0.25});
    CHECK(warnings[0]->entities() ==
          std::vector{volt::EntityRef::component_placement(first),
                      volt::EntityRef::component_placement(second),
                      volt::EntityRef::component(fixture.components[0]),
                      volt::EntityRef::component(fixture.components[1])});
    REQUIRE(warnings[0]->overlays().size() == 2);
    CHECK(warnings[0]->overlays()[0].kind() == volt::DiagnosticOverlayKind::Polygon);
    CHECK(warnings[0]->overlays()[0].layers() == std::vector{volt::BoardLayerId{0}});
    CHECK(warnings[1]->rule() == "component-courtyard-to-courtyard-assembly-clearance");
    CHECK(warnings[1]->measurement() == volt::DiagnosticMeasurement{0.15, 0.25});
}

TEST_CASE("Board footprint DRC distinguishes hard package overlap failures") {
    const auto library = square_package_library();
    const auto fixture = make_placed_resistors(2, square_package_ref());
    auto board = make_visual_board(fixture);
    const auto first = board.place_component(volt::ComponentPlacement{
        fixture.components[0], volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    const auto second = board.place_component(volt::ComponentPlacement{
        fixture.components[1], volt::BoardPoint{10.9, 10.0}, volt::BoardRotation::degrees(0.0)});

    const auto report = volt::validate_board(board, library);

    const auto *body = find_diagnostic(report, "PCB_COMPONENT_BODY_OVERLAP");
    REQUIRE(body != nullptr);
    CHECK(body->severity() == volt::Severity::Error);
    CHECK(body->rule() == "component-body-overlap");
    CHECK(body->measurement() == volt::DiagnosticMeasurement{0.0, 0.0});
    CHECK(body->entities() == std::vector{volt::EntityRef::component_placement(first),
                                          volt::EntityRef::component_placement(second),
                                          volt::EntityRef::component(fixture.components[0]),
                                          volt::EntityRef::component(fixture.components[1])});

    const auto *courtyard = find_diagnostic(report, "PCB_COMPONENT_COURTYARD_OVERLAP");
    REQUIRE(courtyard != nullptr);
    CHECK(courtyard->severity() == volt::Severity::Error);
    CHECK(courtyard->rule() == "component-courtyard-overlap");
    CHECK(courtyard->measurement() == volt::DiagnosticMeasurement{0.0, 0.0});
}

TEST_CASE("Board footprint DRC reports body and courtyard board-edge clearance") {
    const auto library = square_package_library();
    const auto fixture = make_placed_resistors(1, square_package_ref());
    auto board = make_visual_board(fixture);
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{10.0, 10.0}));
    board.set_design_rules(volt::BoardDesignRules{0.15, 0.15, 0.20, 0.45, 0.25});
    const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.components[0], volt::BoardPoint{0.6, 5.0}, volt::BoardRotation::degrees(0.0)});

    const auto report = volt::validate_board(board, library);

    const auto edge = find_diagnostics(report, "PCB_COMPONENT_BOARD_EDGE_CLEARANCE_VIOLATION");
    REQUIRE(edge.size() == 2);
    CHECK(edge[0]->severity() == volt::Severity::Error);
    CHECK(edge[0]->category() == volt::DiagnosticCategory{volt::diagnostic_categories::Drc});
    CHECK(edge[0]->rule() == "component-body-to-board-edge-clearance");
    CHECK(edge[0]->measurement() == volt::DiagnosticMeasurement{0.1, 0.25});
    CHECK(edge[0]->entities() == std::vector{volt::EntityRef::board(),
                                             volt::EntityRef::component_placement(placement),
                                             volt::EntityRef::component(fixture.components[0])});
    REQUIRE(edge[0]->overlays().size() == 1);
    CHECK(edge[0]->overlays()[0].kind() == volt::DiagnosticOverlayKind::Polygon);
    CHECK(edge[1]->rule() == "component-courtyard-to-board-edge-clearance");
    CHECK(edge[1]->measurement() == volt::DiagnosticMeasurement{0.1, 0.25});
}

TEST_CASE("Board visual validation accepts spaced component footprint extents") {
    const auto fixture = make_placed_resistors(2);
    auto board = make_visual_board(fixture);
    [[maybe_unused]] const auto first = board.place_component(volt::ComponentPlacement{
        fixture.components[0], volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    [[maybe_unused]] const auto second = board.place_component(volt::ComponentPlacement{
        fixture.components[1], volt::BoardPoint{15.0, 10.0}, volt::BoardRotation::degrees(0.0)});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    CHECK(find_diagnostic(
              report, std::string{volt::pcb_visual_diagnostic_codes::PlacementOverlap}) == nullptr);
}

TEST_CASE("Board visual validation treats board sides as separate placement surfaces") {
    const auto fixture = make_placed_resistors(2);
    auto board = make_visual_board(fixture);
    [[maybe_unused]] const auto top = board.place_component(
        volt::ComponentPlacement{fixture.components[0], volt::BoardPoint{10.0, 10.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Top});
    [[maybe_unused]] const auto bottom = board.place_component(
        volt::ComponentPlacement{fixture.components[1], volt::BoardPoint{10.0, 10.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Bottom});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    CHECK(find_diagnostic(
              report, std::string{volt::pcb_visual_diagnostic_codes::PlacementOverlap}) == nullptr);
}

TEST_CASE("Board visual validation reports overlapping placement footprint extents") {
    const auto fixture = make_placed_resistors(3);
    auto board = make_visual_board(fixture);
    const auto first = board.place_component(volt::ComponentPlacement{
        fixture.components[0], volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    const auto second = board.place_component(volt::ComponentPlacement{
        fixture.components[1], volt::BoardPoint{10.5, 10.0}, volt::BoardRotation::degrees(0.0)});
    const auto third = board.place_component(volt::ComponentPlacement{
        fixture.components[2], volt::BoardPoint{14.0, 10.0}, volt::BoardRotation::degrees(0.0)});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    auto visual_overlaps = std::vector<const volt::Diagnostic *>{};
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code().value() == volt::pcb_visual_diagnostic_codes::PlacementOverlap) {
            visual_overlaps.push_back(&diagnostic);
        }
    }

    REQUIRE(visual_overlaps.size() == 1);
    const auto &overlap = *visual_overlaps.front();
    CHECK(overlap.severity() == volt::Severity::Warning);
    CHECK(overlap.category() == volt::DiagnosticCategory{volt::diagnostic_categories::PcbVisual});
    CHECK(overlap.message() == "Placed footprint extents for R1 and R2 overlap");
    CHECK(overlap.entities() == std::vector{volt::EntityRef::component(fixture.components[0]),
                                            volt::EntityRef::component_placement(first),
                                            volt::EntityRef::component(fixture.components[1]),
                                            volt::EntityRef::component_placement(second)});
    REQUIRE(overlap.overlays().size() == 2);
    CHECK(overlap.overlays()[0].kind() == volt::DiagnosticOverlayKind::BoundingBox);
    CHECK(overlap.overlays()[0].points() ==
          std::vector{volt::DiagnosticPoint{8.85, 9.525}, volt::DiagnosticPoint{11.15, 10.475}});
    CHECK(overlap.overlays()[0].entities() ==
          std::vector{volt::EntityRef::component(fixture.components[0]),
                      volt::EntityRef::component_placement(first)});
    CHECK(overlap.overlays()[0].layers() == std::vector{volt::BoardLayerId{0}});
    CHECK(overlap.overlays()[1].kind() == volt::DiagnosticOverlayKind::BoundingBox);
    CHECK(overlap.overlays()[1].points() ==
          std::vector{volt::DiagnosticPoint{9.35, 9.525}, volt::DiagnosticPoint{11.65, 10.475}});
    CHECK(overlap.overlays()[1].entities() ==
          std::vector{volt::EntityRef::component(fixture.components[1]),
                      volt::EntityRef::component_placement(second)});
    CHECK(overlap.overlays()[1].layers() == std::vector{volt::BoardLayerId{0}});
    CHECK(
        find_diagnostic(report, std::string{volt::pcb_visual_diagnostic_codes::PlacementOverlap}) ==
        visual_overlaps.front());
    CHECK(board.placement(third).component() == fixture.components[2]);
}

TEST_CASE("Board visual validation orders placement overlaps by placement pair") {
    const auto fixture = make_placed_resistors(3);
    auto board = make_visual_board(fixture);
    const auto first = board.place_component(volt::ComponentPlacement{
        fixture.components[0], volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    const auto second = board.place_component(volt::ComponentPlacement{
        fixture.components[1], volt::BoardPoint{10.5, 10.0}, volt::BoardRotation::degrees(0.0)});
    const auto third = board.place_component(volt::ComponentPlacement{
        fixture.components[2], volt::BoardPoint{11.0, 10.0}, volt::BoardRotation::degrees(0.0)});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    auto visual_overlaps = std::vector<const volt::Diagnostic *>{};
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code().value() == volt::pcb_visual_diagnostic_codes::PlacementOverlap) {
            visual_overlaps.push_back(&diagnostic);
        }
    }

    REQUIRE(visual_overlaps.size() == 3);
    CHECK(visual_overlaps[0]->entities() ==
          std::vector{volt::EntityRef::component(fixture.components[0]),
                      volt::EntityRef::component_placement(first),
                      volt::EntityRef::component(fixture.components[1]),
                      volt::EntityRef::component_placement(second)});
    CHECK(visual_overlaps[1]->entities() ==
          std::vector{volt::EntityRef::component(fixture.components[0]),
                      volt::EntityRef::component_placement(first),
                      volt::EntityRef::component(fixture.components[2]),
                      volt::EntityRef::component_placement(third)});
    CHECK(visual_overlaps[2]->entities() ==
          std::vector{volt::EntityRef::component(fixture.components[1]),
                      volt::EntityRef::component_placement(second),
                      volt::EntityRef::component(fixture.components[2]),
                      volt::EntityRef::component_placement(third)});
}

TEST_CASE("Board visual validation reports same-side board text overlaps") {
    const auto fixture = make_placed_resistors(0);
    auto board = make_visual_board(fixture);
    const auto first = board.add_text(volt::BoardText{"REV A", volt::BoardPoint{5.0, 5.0},
                                                      volt::BoardRotation::degrees(0.0),
                                                      volt::BoardLayerId{0}, 1.0});
    const auto second = board.add_text(volt::BoardText{"DATES", volt::BoardPoint{5.5, 5.0},
                                                       volt::BoardRotation::degrees(0.0),
                                                       volt::BoardLayerId{0}, 1.0});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto overlaps =
        find_diagnostics(report, std::string{volt::pcb_visual_diagnostic_codes::LabelOverlap});
    REQUIRE(overlaps.size() == 1);
    CHECK(overlaps[0]->severity() == volt::Severity::Warning);
    CHECK(overlaps[0]->category() ==
          volt::DiagnosticCategory{volt::diagnostic_categories::PcbVisual});
    CHECK(overlaps[0]->message() == "Board text 'REV A' overlaps board text 'DATES'");
    CHECK(overlaps[0]->entities() ==
          std::vector{volt::EntityRef::board_text(first), volt::EntityRef::board_text(second)});
    REQUIRE(overlaps[0]->overlays().size() == 2);
    CHECK(overlaps[0]->overlays()[0].kind() == volt::DiagnosticOverlayKind::BoundingBox);
    CHECK(overlaps[0]->overlays()[0].points() ==
          std::vector{volt::DiagnosticPoint{5.0, 4.0}, volt::DiagnosticPoint{8.0, 5.0}});
    CHECK(overlaps[0]->overlays()[0].entities() == std::vector{volt::EntityRef::board_text(first)});
    CHECK(overlaps[0]->overlays()[0].layers() == std::vector{volt::BoardLayerId{0}});
    CHECK(overlaps[0]->overlays()[1].points() ==
          std::vector{volt::DiagnosticPoint{5.5, 4.0}, volt::DiagnosticPoint{8.5, 5.0}});
    CHECK(overlaps[0]->overlays()[1].entities() ==
          std::vector{volt::EntityRef::board_text(second)});
}

TEST_CASE("Board visual validation treats board text sides as separate visual surfaces") {
    const auto fixture = make_placed_resistors(0);
    auto board = make_visual_board(fixture);
    [[maybe_unused]] const auto top = board.add_text(
        volt::BoardText{"REV A", volt::BoardPoint{5.0, 5.0}, volt::BoardRotation::degrees(0.0),
                        volt::BoardLayerId{0}, 1.0});
    [[maybe_unused]] const auto bottom = board.add_text(
        volt::BoardText{"DATE", volt::BoardPoint{5.0, 5.0}, volt::BoardRotation::degrees(0.0),
                        volt::BoardLayerId{1}, 1.0});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    CHECK(find_diagnostic(report, std::string{volt::pcb_visual_diagnostic_codes::LabelOverlap}) ==
          nullptr);
}

TEST_CASE("Board visual validation reports board text outside the outline") {
    const auto fixture = make_placed_resistors(0);
    auto board = make_visual_board(fixture);
    const auto text = board.add_text(volt::BoardText{"REV A", volt::BoardPoint{-1.0, 5.0},
                                                     volt::BoardRotation::degrees(0.0),
                                                     volt::BoardLayerId{0}, 1.0});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto outside =
        find_diagnostics(report, std::string{volt::pcb_visual_diagnostic_codes::LabelOutsideBoard});
    REQUIRE(outside.size() == 1);
    CHECK(outside[0]->severity() == volt::Severity::Warning);
    CHECK(outside[0]->category() ==
          volt::DiagnosticCategory{volt::diagnostic_categories::PcbVisual});
    CHECK(outside[0]->message() == "Board text 'REV A' is outside the board outline");
    CHECK(outside[0]->entities() == std::vector{volt::EntityRef::board_text(text)});
    REQUIRE(outside[0]->overlays().size() == 1);
    CHECK(outside[0]->overlays()[0].kind() == volt::DiagnosticOverlayKind::BoundingBox);
    CHECK(outside[0]->overlays()[0].points() ==
          std::vector{volt::DiagnosticPoint{-1.0, 4.0}, volt::DiagnosticPoint{2.0, 5.0}});
    CHECK(outside[0]->overlays()[0].entities() == std::vector{volt::EntityRef::board_text(text)});
    CHECK(outside[0]->overlays()[0].layers() == std::vector{volt::BoardLayerId{0}});
}

TEST_CASE("Board visual validation reports board text obstructing pads and package geometry") {
    const auto library = square_package_library();
    const auto fixture = make_placed_resistors(1, square_package_ref());
    auto board = make_visual_board(fixture);
    const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.components[0], volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    const auto text = board.add_text(volt::BoardText{"T", volt::BoardPoint{9.7, 10.2},
                                                     volt::BoardRotation::degrees(0.0),
                                                     volt::BoardLayerId{0}, 0.5});

    const auto report = volt::validate_board(board, library);

    const auto obstructions =
        find_diagnostics(report, std::string{volt::pcb_visual_diagnostic_codes::LabelObstruction});
    REQUIRE(obstructions.size() == 3);
    CHECK(obstructions[0]->severity() == volt::Severity::Warning);
    CHECK(obstructions[0]->category() ==
          volt::DiagnosticCategory{volt::diagnostic_categories::PcbVisual});
    CHECK(obstructions[0]->rule() == "board-text-over-pad");
    CHECK(obstructions[0]->entities() ==
          std::vector{volt::EntityRef::board_text(text),
                      volt::EntityRef::component_placement(placement),
                      volt::EntityRef::component(fixture.components[0]),
                      volt::EntityRef::footprint_pad(volt::FootprintPadId{0})});
    REQUIRE(obstructions[0]->overlays().size() == 2);
    CHECK(obstructions[0]->overlays()[0].kind() == volt::DiagnosticOverlayKind::BoundingBox);
    CHECK(obstructions[0]->overlays()[1].kind() == volt::DiagnosticOverlayKind::Polygon);
    CHECK(obstructions[1]->rule() == "board-text-over-package-body");
    CHECK(obstructions[2]->rule() == "board-text-over-package-courtyard");
}

TEST_CASE("Board visual validation reports board text obstructing board holes") {
    const auto fixture = make_placed_resistors(0);
    auto board = make_visual_board(fixture);
    const auto hole = board.add_feature(
        volt::BoardFeature::hole("MH", volt::BoardPoint{8.0, 8.0}, 1.0, false, "mounting"));
    const auto text = board.add_text(volt::BoardText{"H1", volt::BoardPoint{7.8, 8.2},
                                                     volt::BoardRotation::degrees(0.0),
                                                     volt::BoardLayerId{0}, 0.5});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto obstructions =
        find_diagnostics(report, std::string{volt::pcb_visual_diagnostic_codes::LabelObstruction});
    REQUIRE(obstructions.size() == 1);
    CHECK(obstructions[0]->rule() == "board-text-over-hole");
    CHECK(obstructions[0]->entities() ==
          std::vector{volt::EntityRef::board_text(text), volt::EntityRef::board_feature(hole)});
    REQUIRE(obstructions[0]->overlays().size() == 2);
    CHECK(obstructions[0]->overlays()[1].kind() == volt::DiagnosticOverlayKind::Point);
}

TEST_CASE("Board visual validation reports obstructed default reference designators") {
    const auto library = square_package_library();
    const auto fixture = make_placed_resistors(2, square_package_ref());
    auto board = make_visual_board(fixture);
    const auto first = board.place_component(volt::ComponentPlacement{
        fixture.components[0], volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    const auto second = board.place_component(volt::ComponentPlacement{
        fixture.components[1], volt::BoardPoint{10.0, 8.5}, volt::BoardRotation::degrees(0.0)});

    const auto report = volt::validate_board(board, library);

    const auto hidden = find_diagnostics(
        report, std::string{volt::pcb_visual_diagnostic_codes::ReferenceDesignatorHidden});
    REQUIRE(hidden.size() == 1);
    CHECK(hidden[0]->severity() == volt::Severity::Warning);
    CHECK(hidden[0]->rule() == "default-reference-designator-over-package");
    CHECK(hidden[0]->entities() == std::vector{volt::EntityRef::component(fixture.components[0]),
                                               volt::EntityRef::component_placement(first),
                                               volt::EntityRef::component(fixture.components[1]),
                                               volt::EntityRef::component_placement(second)});
    REQUIRE(hidden[0]->overlays().size() == 2);
    CHECK(hidden[0]->overlays()[0].kind() == volt::DiagnosticOverlayKind::BoundingBox);
    CHECK(hidden[0]->overlays()[1].kind() == volt::DiagnosticOverlayKind::Polygon);
}

TEST_CASE("Board visual validation detects board text crossing concave outline voids") {
    const auto fixture = make_placed_resistors(0);
    auto board = make_visual_board(fixture);
    board.set_outline(volt::BoardOutline{std::vector{
        volt::BoardPoint{0.0, 0.0},
        volt::BoardPoint{10.0, 0.0},
        volt::BoardPoint{10.0, 10.0},
        volt::BoardPoint{6.0, 10.0},
        volt::BoardPoint{6.0, 6.0},
        volt::BoardPoint{4.0, 6.0},
        volt::BoardPoint{4.0, 10.0},
        volt::BoardPoint{0.0, 10.0},
    }});
    const auto text = board.add_text(volt::BoardText{"ABCDEFGHIJ", volt::BoardPoint{2.0, 8.0},
                                                     volt::BoardRotation::degrees(0.0),
                                                     volt::BoardLayerId{0}, 1.0});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto outside =
        find_diagnostics(report, std::string{volt::pcb_visual_diagnostic_codes::LabelOutsideBoard});
    REQUIRE(outside.size() == 1);
    CHECK(outside[0]->entities() == std::vector{volt::EntityRef::board_text(text)});
    REQUIRE(outside[0]->overlays().size() == 1);
    CHECK(outside[0]->overlays()[0].points() ==
          std::vector{volt::DiagnosticPoint{2.0, 7.0}, volt::DiagnosticPoint{8.0, 8.0}});
}
