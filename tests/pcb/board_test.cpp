#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints.hpp>

namespace {

struct ResistorCircuit {
    volt::Circuit circuit;
    volt::ComponentId component;
    volt::PinDefId first_pin_definition;
    volt::PinDefId second_pin_definition;
    volt::PinId first_pin;
    volt::PinId second_pin;
    volt::NetId first_net;
    volt::NetId second_net;
};

[[nodiscard]] ResistorCircuit make_resistor_circuit(bool select_physical_part = true) {
    auto circuit = volt::Circuit{};
    const auto first_pin_definition =
        circuit.add_pin_definition(volt::PinDefinition{"A", "1", volt::PinRole::Passive});
    const auto second_pin_definition =
        circuit.add_pin_definition(volt::PinDefinition{"B", "2", volt::PinRole::Passive});
    const auto component_definition = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", {first_pin_definition, second_pin_definition}});
    const auto component =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"R1"});
    const auto first_pin = circuit.pin_by_definition(component, first_pin_definition).value();
    const auto second_pin = circuit.pin_by_definition(component, second_pin_definition).value();
    const auto first_net = circuit.add_net(volt::Net{volt::NetName{"LEFT"}, volt::NetKind::Signal});
    const auto second_net =
        circuit.add_net(volt::Net{volt::NetName{"RIGHT"}, volt::NetKind::Signal});

    circuit.connect(first_net, first_pin);
    circuit.connect(second_net, second_pin);

    if (select_physical_part) {
        circuit.select_physical_part(
            component, volt::PhysicalPart{
                           volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                           volt::PackageRef{"0603"},
                           volt::FootprintRef{"passives", "R_0603_1608Metric"},
                           std::vector{volt::PinPadMapping{first_pin_definition, "1"},
                                       volt::PinPadMapping{second_pin_definition, "2"}},
                       });
    }

    return ResistorCircuit{std::move(circuit),
                           component,
                           first_pin_definition,
                           second_pin_definition,
                           first_pin,
                           second_pin,
                           first_net,
                           second_net};
}

[[nodiscard]] const volt::Diagnostic *find_diagnostic(const volt::DiagnosticReport &report,
                                                      const std::string &code) {
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code() == volt::DiagnosticCode{code}) {
            return &diagnostic;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("Board stores metadata, layers, outline, features, and placements") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Control"}};

    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{50.0, 30.0}));
    const auto feature = board.add_feature(
        volt::BoardFeature::mounting_hole("MH1", volt::BoardPoint{3.0, 3.0}, 3.2));
    const auto placement = board.place_component(
        volt::ComponentPlacement{fixture.component, volt::BoardPoint{25.0, 15.0},
                                 volt::BoardRotation::degrees(90.0), volt::BoardSide::Top, true});

    CHECK(board.name() == volt::BoardName{"Control"});
    CHECK(board.units() == volt::BoardUnits::Millimeters);
    REQUIRE(board.layer_stack().has_value());
    CHECK(board.layer_stack()->layers() == std::vector{front, back});
    REQUIRE(board.outline().has_value());
    CHECK(board.feature(feature).kind() == volt::BoardFeatureKind::MountingHole);
    CHECK(board.placement(placement).component() == fixture.component);
    CHECK(board.placement(placement).position() == volt::BoardPoint{25.0, 15.0});
    CHECK(board.placement(placement).rotation() == volt::BoardRotation::degrees(90.0));
    CHECK(board.placement(placement).side() == volt::BoardSide::Top);
    CHECK(board.placement(placement).locked());
}

TEST_CASE("Board outline accepts an explicit closing vertex") {
    auto outline = volt::BoardOutline{std::vector{
        volt::BoardPoint{0.0, 0.0},
        volt::BoardPoint{10.0, 0.0},
        volt::BoardPoint{10.0, 5.0},
        volt::BoardPoint{0.0, 5.0},
        volt::BoardPoint{0.0, 0.0},
    }};

    CHECK(outline.vertices() == std::vector{
                                    volt::BoardPoint{0.0, 0.0},
                                    volt::BoardPoint{10.0, 0.0},
                                    volt::BoardPoint{10.0, 5.0},
                                    volt::BoardPoint{0.0, 5.0},
                                });
    CHECK(outline.contains(volt::BoardPoint{5.0, 2.5}));
}

TEST_CASE("Board rejects structurally invalid board and placement mutations") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});

    CHECK_THROWS_AS(volt::BoardPoint(std::numeric_limits<double>::quiet_NaN(), 0.0),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::BoardRotation::degrees(std::numeric_limits<double>::infinity()),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        volt::BoardOutline(std::vector{volt::BoardPoint{0.0, 0.0}, volt::BoardPoint{1.0, 0.0}}),
        std::invalid_argument);
    CHECK_THROWS_AS(volt::BoardOutline(std::vector{
                        volt::BoardPoint{0.0, 3.0},
                        volt::BoardPoint{1.0, 0.0},
                        volt::BoardPoint{2.0, 3.0},
                        volt::BoardPoint{0.0, 1.0},
                        volt::BoardPoint{2.0, 1.0},
                    }),
                    std::invalid_argument);
    CHECK_THROWS_AS(board.set_layer_stack(volt::LayerStack{{front, volt::BoardLayerId{99}}, 1.6}),
                    std::out_of_range);
    CHECK_THROWS_AS(board.add_layer(volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper,
                                                     volt::BoardLayerSide::Top}),
                    std::logic_error);
    CHECK_THROWS_AS(
        board.place_component(volt::ComponentPlacement{
            volt::ComponentId{99}, volt::BoardPoint{1.0, 1.0}, volt::BoardRotation::degrees(0.0)}),
        std::out_of_range);

    [[maybe_unused]] const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{1.0, 1.0}, volt::BoardRotation::degrees(0.0)});
    CHECK_THROWS_AS(
        board.place_component(volt::ComponentPlacement{
            fixture.component, volt::BoardPoint{2.0, 2.0}, volt::BoardRotation::degrees(0.0)}),
        std::logic_error);

    [[maybe_unused]] const auto footprint =
        board.cache_footprint_definition(volt::passive_0603_footprint());
    CHECK_THROWS_AS(board.cache_footprint_definition(volt::passive_0603_footprint()),
                    std::logic_error);
    CHECK_THROWS_AS(board.footprint_definition(volt::FootprintDefId{99}), std::out_of_range);
}

TEST_CASE("Board resolves placed pads to logical pins and existing nets") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit};
    const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{10.0, 8.0}, volt::BoardRotation::degrees(0.0)});

    const auto resolutions = board.resolve_pads(volt::builtin_footprint_library());

    REQUIRE(resolutions.size() == 2);
    CHECK(resolutions[0].placement() == placement);
    CHECK(resolutions[0].component() == fixture.component);
    CHECK(resolutions[0].pad() == volt::FootprintPadId{0});
    CHECK(resolutions[0].pad_label() == "1");
    CHECK(resolutions[0].pin() == fixture.first_pin);
    CHECK(resolutions[0].net() == fixture.first_net);
    CHECK(resolutions[0].status() == volt::PadResolutionStatus::Connected);
    CHECK(resolutions[1].pad() == volt::FootprintPadId{1});
    CHECK(resolutions[1].pin() == fixture.second_pin);
    CHECK(resolutions[1].net() == fixture.second_net);
}

TEST_CASE("Board pad resolution and validation use cached footprint definitions") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{50.0, 30.0}));
    [[maybe_unused]] const auto footprint =
        board.cache_footprint_definition(volt::passive_0603_footprint());
    [[maybe_unused]] const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{10.0, 8.0}, volt::BoardRotation::degrees(0.0)});

    const auto empty_library = volt::FootprintLibrary{};
    const auto resolutions = board.resolve_pads(empty_library);
    const auto report = volt::validate_board(board, empty_library);

    REQUIRE(resolutions.size() == 2);
    CHECK(resolutions[0].net() == fixture.first_net);
    CHECK(resolutions[1].net() == fixture.second_net);
    CHECK(find_diagnostic(report, "PCB_FOOTPRINT_UNRESOLVED") == nullptr);
}

TEST_CASE("Board validation reports design issues without owning connectivity") {
    auto fixture = make_resistor_circuit(false);
    auto board = volt::Board{fixture.circuit};

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *missing_outline = find_diagnostic(report, "PCB_BOARD_OUTLINE_MISSING");
    REQUIRE(missing_outline != nullptr);
    CHECK(missing_outline->severity() == volt::Severity::Error);
    CHECK(missing_outline->entities().empty());

    const auto *unplaced = find_diagnostic(report, "PCB_COMPONENT_NOT_PLACED");
    REQUIRE(unplaced != nullptr);
    CHECK(unplaced->severity() == volt::Severity::Error);
    REQUIRE(unplaced->entities().size() == 1);
    CHECK(unplaced->entities()[0] == volt::EntityRef::component(fixture.component));

    const auto *missing_part = find_diagnostic(report, "PCB_COMPONENT_MISSING_SELECTED_PART");
    REQUIRE(missing_part != nullptr);
    CHECK(missing_part->severity() == volt::Severity::Error);
    REQUIRE(missing_part->entities().size() == 1);
    CHECK(missing_part->entities()[0] == volt::EntityRef::component(fixture.component));
}

TEST_CASE("Board validation checks transformed pad bodies against the outline") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{5.0, 5.0}));
    const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{1.1, 2.5}, volt::BoardRotation::degrees(0.0)});

    const auto report = volt::validate_board(board, volt::builtin_footprint_library());

    const auto *outside_outline = find_diagnostic(report, "PCB_PLACEMENT_OUTSIDE_OUTLINE");
    REQUIRE(outside_outline != nullptr);
    CHECK(outside_outline->severity() == volt::Severity::Error);
    REQUIRE(outside_outline->entities().size() == 2);
    CHECK(outside_outline->entities()[0] == volt::EntityRef::component(fixture.component));
    CHECK(outside_outline->entities()[1] == volt::EntityRef::component_placement(placement));
}

TEST_CASE("Board validation detects pad edges crossing a concave outline") {
    auto fixture = make_resistor_circuit(false);
    fixture.circuit.select_physical_part(
        fixture.component, volt::PhysicalPart{
                               volt::ManufacturerPart{"Volt", "wide-pad-fixture"},
                               volt::PackageRef{"fixture"},
                               volt::FootprintRef{"test", "WidePad"},
                               std::vector{volt::PinPadMapping{fixture.first_pin_definition, "1"},
                                           volt::PinPadMapping{fixture.second_pin_definition, "2"}},
                           });

    auto library = volt::FootprintLibrary{};
    library.add(volt::FootprintDefinition{
        volt::FootprintRef{"test", "WidePad"},
        std::vector{
            volt::FootprintPad::surface_mount(
                "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, 0.0},
                volt::FootprintSize{3.0, 0.4}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "2", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, -1.0},
                volt::FootprintSize{0.3, 0.3}, volt::FootprintLayerSet::front_smd()),
        },
    });

    auto board = volt::Board{fixture.circuit};
    board.set_outline(volt::BoardOutline{std::vector{
        volt::BoardPoint{0.0, 0.0},
        volt::BoardPoint{6.0, 0.0},
        volt::BoardPoint{6.0, 6.0},
        volt::BoardPoint{4.0, 6.0},
        volt::BoardPoint{4.0, 2.0},
        volt::BoardPoint{2.0, 2.0},
        volt::BoardPoint{2.0, 6.0},
        volt::BoardPoint{0.0, 6.0},
    }});
    const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{3.0, 1.9}, volt::BoardRotation::degrees(0.0)});

    const auto report = volt::validate_board(board, library);

    const auto *outside_outline = find_diagnostic(report, "PCB_PLACEMENT_OUTSIDE_OUTLINE");
    REQUIRE(outside_outline != nullptr);
    CHECK(outside_outline->severity() == volt::Severity::Error);
    REQUIRE(outside_outline->entities().size() == 2);
    CHECK(outside_outline->entities()[0] == volt::EntityRef::component(fixture.component));
    CHECK(outside_outline->entities()[1] == volt::EntityRef::component_placement(placement));
}

TEST_CASE("Board validation diagnoses footprint resolution and pad geometry issues") {
    auto fixture = make_resistor_circuit(false);
    fixture.circuit.select_physical_part(
        fixture.component, volt::PhysicalPart{
                               volt::ManufacturerPart{"Acme", "NOPE"},
                               volt::PackageRef{"custom"},
                               volt::FootprintRef{"missing", "NotARealFootprint"},
                               std::vector{volt::PinPadMapping{fixture.first_pin_definition, "99"},
                                           volt::PinPadMapping{fixture.second_pin_definition, "2"}},
                           });
    auto board = volt::Board{fixture.circuit};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{5.0, 5.0}));
    const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});

    auto report = volt::validate_board(board, volt::builtin_footprint_library());
    const auto *unresolved_footprint = find_diagnostic(report, "PCB_FOOTPRINT_UNRESOLVED");
    REQUIRE(unresolved_footprint != nullptr);
    CHECK(unresolved_footprint->severity() == volt::Severity::Error);
    REQUIRE(unresolved_footprint->entities().size() == 2);
    CHECK(unresolved_footprint->entities()[0] == volt::EntityRef::component(fixture.component));
    CHECK(unresolved_footprint->entities()[1] == volt::EntityRef::component_placement(placement));

    fixture.circuit.select_physical_part(
        fixture.component, volt::PhysicalPart{
                               volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                               volt::PackageRef{"0603"},
                               volt::FootprintRef{"passives", "R_0603_1608Metric"},
                               std::vector{volt::PinPadMapping{fixture.first_pin_definition, "99"},
                                           volt::PinPadMapping{fixture.second_pin_definition, "2"}},
                           });

    report = volt::validate_board(board, volt::builtin_footprint_library());
    const auto *unknown_pad = find_diagnostic(report, "PCB_PAD_MAPPING_UNKNOWN_PAD");
    REQUIRE(unknown_pad != nullptr);
    CHECK(unknown_pad->severity() == volt::Severity::Error);
    REQUIRE(unknown_pad->entities().size() == 2);
    CHECK(unknown_pad->entities()[0] == volt::EntityRef::component(fixture.component));
    CHECK(unknown_pad->entities()[1] == volt::EntityRef::component_placement(placement));

    const auto *missing_pad_mapping = find_diagnostic(report, "PCB_PAD_MAPPING_MISSING_PIN");
    REQUIRE(missing_pad_mapping != nullptr);
    CHECK(missing_pad_mapping->severity() == volt::Severity::Error);
    REQUIRE(missing_pad_mapping->entities().size() == 2);
    CHECK(missing_pad_mapping->entities()[0] == volt::EntityRef::component(fixture.component));
    CHECK(missing_pad_mapping->entities()[1] == volt::EntityRef::component_placement(placement));

    const auto *outside_outline = find_diagnostic(report, "PCB_PLACEMENT_OUTSIDE_OUTLINE");
    REQUIRE(outside_outline != nullptr);
    CHECK(outside_outline->severity() == volt::Severity::Error);
    REQUIRE(outside_outline->entities().size() == 2);
    CHECK(outside_outline->entities()[0] == volt::EntityRef::component(fixture.component));
    CHECK(outside_outline->entities()[1] == volt::EntityRef::component_placement(placement));
}
