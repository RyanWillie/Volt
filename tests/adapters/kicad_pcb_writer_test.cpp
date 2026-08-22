#include <catch2/catch_test_macros.hpp>

#include "support/circuit_test_helpers.hpp"
#include "support/compiled_board_export_helpers.hpp"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <volt/adapters/kicad/pcb_writer.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints/footprints.hpp>

namespace {

std::string read_fixture(const std::string &name) {
    auto input = std::ifstream{std::string{VOLT_TEST_FIXTURE_DIR} + "/" + name};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::size_t count_occurrences(const std::string &text, std::string_view needle) {
    auto count = std::size_t{0};
    auto position = std::size_t{0};
    while ((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

struct ResistorCircuit {
    volt::Circuit circuit;
    volt::io::PartLibraryBundle parts;
    volt::ComponentId component;
    volt::PinDefId first_pin_definition;
    volt::PinDefId second_pin_definition;
    volt::NetId left_net;
    volt::NetId right_net;
};

[[nodiscard]] volt::FootprintDefinition asymmetric_test_footprint() {
    return volt::FootprintDefinition{
        volt::FootprintRef{"tests", "Asymmetric_Bottom"},
        std::vector{
            volt::FootprintPad::surface_mount(
                "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{-2.0, 1.0},
                volt::FootprintSize{1.2, 0.7}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "2", volt::FootprintPadShape::Oval, volt::FootprintPoint{1.0, -0.5},
                volt::FootprintSize{0.9, 1.3}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::through_hole(
                "MP", volt::FootprintPadShape::Circle, volt::FootprintPoint{-0.5, -2.0},
                volt::FootprintSize{1.4, 1.4}, volt::FootprintLayerSet::through_hole(),
                volt::FootprintDrill{0.8, volt::FootprintPadPlating::Plated},
                volt::FootprintPadMechanicalRole::MechanicalSupport),
            volt::FootprintPad::through_hole(
                "MH", volt::FootprintPadShape::Circle, volt::FootprintPoint{0.75, 2.25},
                volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::mechanical_hole(),
                volt::FootprintDrill{1.0, volt::FootprintPadPlating::NonPlated},
                volt::FootprintPadMechanicalRole::MechanicalSupport),
        }};
}

[[nodiscard]] ResistorCircuit
make_resistor_circuit(volt::FootprintDefinition footprint = volt::passive_0603_footprint()) {
    auto circuit = volt::Circuit{};
    const auto first_pin_spec = volt::PinSpec{"A",
                                              "1",
                                              volt::ConnectionRequirement::Required,
                                              volt::ElectricalTerminalKind::Passive,
                                              volt::ElectricalDirection::Passive,
                                              volt::ElectricalSignalDomain::Unspecified,
                                              volt::ElectricalDriveKind::Passive};
    const auto second_pin_spec = volt::PinSpec{"B",
                                               "2",
                                               volt::ConnectionRequirement::Required,
                                               volt::ElectricalTerminalKind::Passive,
                                               volt::ElectricalDirection::Passive,
                                               volt::ElectricalSignalDomain::Unspecified,
                                               volt::ElectricalDriveKind::Passive};
    const auto component_spec =
        volt::ComponentSpec{.name = "Resistor", .pins = {first_pin_spec, second_pin_spec}};
    const auto component_definition = circuit.define_component(component_spec);
    const auto pin_definitions = circuit.get(component_definition).pins();
    const auto first_pin_definition = pin_definitions[0];
    const auto second_pin_definition = pin_definitions[1];
    const auto component = circuit.instantiate_component(
        component_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    circuit.update(component, volt::SetComponentProperty{volt::PropertyKey{"Value"},
                                                         volt::PropertyValue{"330R"}});

    const auto first_pin =
        volt::queries::pin_by_definition(circuit, component, first_pin_definition).value();
    const auto second_pin =
        volt::queries::pin_by_definition(circuit, component, second_pin_definition).value();
    const auto left_net = circuit.add_net(
        volt::NetSpec{.name = volt::NetName{"LEFT"}, .kind = volt::NetKind::Signal});
    const auto right_net = circuit.add_net(
        volt::NetSpec{.name = volt::NetName{"RIGHT"}, .kind = volt::NetKind::Signal});
    circuit.connect(left_net, first_pin);
    circuit.connect(right_net, second_pin);
    const auto physical =
        volt::PhysicalPart{volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                           volt::PackageRef{"0603"}, footprint.ref(),
                           std::vector{volt::PinPadMapping{first_pin_definition, "1"},
                                       volt::PinPadMapping{second_pin_definition, "2"}}};
    auto library = volt::test::make_export_fixture_library(
        {{component_spec, physical, std::move(footprint), volt::PartKey{"resistor"}}});
    circuit.update(component, volt::SelectLibraryPart{library.bundle,
                                                      library.bundle.require(library.keys[0])});

    return ResistorCircuit{std::move(circuit),
                           std::move(library.bundle),
                           component,
                           first_pin_definition,
                           second_pin_definition,
                           left_net,
                           right_net};
}

[[nodiscard]] volt::ComponentId add_second_resistor(ResistorCircuit &fixture) {
    const auto component_definition = fixture.circuit.get(fixture.component).definition();
    const auto component = fixture.circuit.instantiate_component(
        component_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R2"}});
    fixture.circuit.update(component, volt::SetComponentProperty{volt::PropertyKey{"Value"},
                                                                 volt::PropertyValue{"330R"}});
    fixture.circuit.update(
        component,
        volt::SelectLibraryPart{fixture.parts, fixture.parts.require(volt::PartKey{"resistor"})});
    const auto first_pin =
        volt::queries::pin_by_definition(fixture.circuit, component, fixture.first_pin_definition)
            .value();
    const auto second_pin =
        volt::queries::pin_by_definition(fixture.circuit, component, fixture.second_pin_definition)
            .value();
    fixture.circuit.connect(fixture.left_net, first_pin);
    fixture.circuit.connect(fixture.right_net, second_pin);
    return component;
}

[[nodiscard]] volt::Board make_placement_board(const ResistorCircuit &fixture,
                                               volt::BoardName name) {
    auto board = volt::Board{fixture.circuit, std::move(name)};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{50.0, 30.0}));
    return board;
}

[[nodiscard]] volt::Board make_routed_board(const ResistorCircuit &fixture) {
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Control"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{50.0, 30.0}));
    [[maybe_unused]] const auto feature = board.add_feature(
        volt::BoardFeature::hole("MH1", volt::BoardPoint{3.0, 3.0}, 3.2, false, "mounting"));
    [[maybe_unused]] const auto placement = board.place_component(
        volt::ComponentPlacement{fixture.component, volt::BoardPoint{25.0, 15.0},
                                 volt::BoardRotation::degrees(90.0), volt::BoardSide::Top, true});
    [[maybe_unused]] const auto track = board.add_track(volt::BoardTrack{
        fixture.left_net,
        front,
        std::vector{
            volt::BoardPoint{5.0, 5.0},
            volt::BoardPoint{12.0, 5.0},
            volt::BoardPoint{12.0, 8.0},
        },
        0.25,
    });
    [[maybe_unused]] const auto via = board.add_via(
        volt::BoardVia{fixture.left_net, volt::BoardPoint{12.0, 8.0}, front, back, 0.30, 0.70});
    [[maybe_unused]] const auto text =
        board.add_text(volt::BoardText{"REV A", volt::BoardPoint{5.0, 24.0},
                                       volt::BoardRotation::degrees(90.0), front, 1.2, true});
    return board;
}

struct LedBadgeCircuit {
    volt::Circuit circuit;
    volt::io::PartLibraryBundle parts;
    volt::ComponentId header;
    volt::ComponentId resistor;
    volt::ComponentId led;
    volt::NetId vcc;
    volt::NetId led_a;
    volt::NetId gnd;
};

[[nodiscard]] LedBadgeCircuit make_led_badge_circuit() {
    auto circuit = volt::Circuit{};
    const auto header_one_spec = volt::PinSpec{"1",
                                               "1",
                                               volt::ConnectionRequirement::Required,
                                               volt::ElectricalTerminalKind::Passive,
                                               volt::ElectricalDirection::Passive,
                                               volt::ElectricalSignalDomain::Unspecified,
                                               volt::ElectricalDriveKind::Passive};
    const auto header_two_spec = volt::PinSpec{"2",
                                               "2",
                                               volt::ConnectionRequirement::Required,
                                               volt::ElectricalTerminalKind::Passive,
                                               volt::ElectricalDirection::Passive,
                                               volt::ElectricalSignalDomain::Unspecified,
                                               volt::ElectricalDriveKind::Passive};
    const auto passive_one_spec = volt::PinSpec{"A",
                                                "1",
                                                volt::ConnectionRequirement::Required,
                                                volt::ElectricalTerminalKind::Passive,
                                                volt::ElectricalDirection::Passive,
                                                volt::ElectricalSignalDomain::Unspecified,
                                                volt::ElectricalDriveKind::Passive};
    const auto passive_two_spec = volt::PinSpec{"B",
                                                "2",
                                                volt::ConnectionRequirement::Required,
                                                volt::ElectricalTerminalKind::Passive,
                                                volt::ElectricalDirection::Passive,
                                                volt::ElectricalSignalDomain::Unspecified,
                                                volt::ElectricalDriveKind::Passive};
    const auto led_anode_spec = volt::PinSpec{"A",
                                              "1",
                                              volt::ConnectionRequirement::Required,
                                              volt::ElectricalTerminalKind::Passive,
                                              volt::ElectricalDirection::Passive,
                                              volt::ElectricalSignalDomain::Unspecified,
                                              volt::ElectricalDriveKind::Passive};
    const auto led_cathode_spec = volt::PinSpec{"K",
                                                "2",
                                                volt::ConnectionRequirement::Required,
                                                volt::ElectricalTerminalKind::Passive,
                                                volt::ElectricalDirection::Passive,
                                                volt::ElectricalSignalDomain::Unspecified,
                                                volt::ElectricalDriveKind::Passive};

    const auto header_spec =
        volt::ComponentSpec{.name = "Header", .pins = {header_one_spec, header_two_spec}};
    const auto passive_spec =
        volt::ComponentSpec{.name = "Resistor", .pins = {passive_one_spec, passive_two_spec}};
    const auto led_spec =
        volt::ComponentSpec{.name = "LED", .pins = {led_anode_spec, led_cathode_spec}};
    const auto header_definition = circuit.define_component(header_spec);
    const auto header_pins = circuit.get(header_definition).pins();
    const auto header_one = header_pins[0];
    const auto header_two = header_pins[1];
    const auto passive_definition = circuit.define_component(passive_spec);
    const auto passive_pins = circuit.get(passive_definition).pins();
    const auto passive_one = passive_pins[0];
    const auto passive_two = passive_pins[1];
    const auto led_definition = circuit.define_component(led_spec);
    const auto led_pins = circuit.get(led_definition).pins();
    const auto led_anode = led_pins[0];
    const auto led_cathode = led_pins[1];

    const auto header = circuit.instantiate_component(
        header_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"J1"}});
    const auto resistor = circuit.instantiate_component(
        passive_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    const auto led = circuit.instantiate_component(
        led_definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"D1"}});
    circuit.update(resistor, volt::SetComponentProperty{volt::PropertyKey{"Value"},
                                                        volt::PropertyValue{"330R"}});
    circuit.update(
        led, volt::SetComponentProperty{volt::PropertyKey{"Value"}, volt::PropertyValue{"RED"}});

    const auto vcc =
        circuit.add_net(volt::NetSpec{.name = volt::NetName{"VCC"}, .kind = volt::NetKind::Power});
    const auto led_a = circuit.add_net(
        volt::NetSpec{.name = volt::NetName{"LED_A"}, .kind = volt::NetKind::Signal});
    const auto gnd =
        circuit.add_net(volt::NetSpec{.name = volt::NetName{"GND"}, .kind = volt::NetKind::Ground});

    circuit.connect(vcc, volt::queries::pin_by_definition(circuit, header, header_one).value());
    circuit.connect(vcc, volt::queries::pin_by_definition(circuit, resistor, passive_one).value());
    circuit.connect(led_a,
                    volt::queries::pin_by_definition(circuit, resistor, passive_two).value());
    circuit.connect(led_a, volt::queries::pin_by_definition(circuit, led, led_anode).value());
    circuit.connect(gnd, volt::queries::pin_by_definition(circuit, led, led_cathode).value());
    circuit.connect(gnd, volt::queries::pin_by_definition(circuit, header, header_two).value());

    const auto header_part = volt::PhysicalPart{
        volt::ManufacturerPart{"Generic", "HDR-1x02"}, volt::PackageRef{"2.54mm-1x02"},
        volt::FootprintRef{"connectors", "PinHeader_1x02_P2.54mm_Vertical"},
        std::vector{volt::PinPadMapping{header_one, "1"}, volt::PinPadMapping{header_two, "2"}}};
    const auto resistor_part = volt::PhysicalPart{
        volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"}, volt::PackageRef{"0603"},
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{volt::PinPadMapping{passive_one, "1"}, volt::PinPadMapping{passive_two, "2"}}};
    const auto led_part = volt::PhysicalPart{
        volt::ManufacturerPart{"Lite-On", "LTST-C190KRKT"}, volt::PackageRef{"0603"},
        volt::FootprintRef{"leds", "LED_0603_1608Metric"},
        std::vector{volt::PinPadMapping{led_anode, "1"}, volt::PinPadMapping{led_cathode, "2"}}};
    const auto builtin = volt::builtin_footprint_library();
    auto library = volt::test::make_export_fixture_library(
        {{header_spec, header_part, *builtin.find(header_part.footprint()),
          volt::PartKey{"header"}},
         {passive_spec, resistor_part, *builtin.find(resistor_part.footprint()),
          volt::PartKey{"resistor"}},
         {led_spec, led_part, *builtin.find(led_part.footprint()), volt::PartKey{"led"}}});
    circuit.update(
        header, volt::SelectLibraryPart{library.bundle, library.bundle.require(library.keys[0])});
    circuit.update(
        resistor, volt::SelectLibraryPart{library.bundle, library.bundle.require(library.keys[1])});
    circuit.update(
        led, volt::SelectLibraryPart{library.bundle, library.bundle.require(library.keys[2])});

    return LedBadgeCircuit{
        std::move(circuit), std::move(library.bundle), header, resistor, led, vcc, led_a, gnd};
}

[[nodiscard]] volt::Board make_led_badge_board(const LedBadgeCircuit &fixture) {
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Badge"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    const auto silk = board.add_layer(
        volt::BoardLayer{"F.SilkS", volt::BoardLayerRole::Silkscreen, volt::BoardLayerSide::Top});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{36.0, 18.0}));
    static_cast<void>(board.add_feature(
        volt::BoardFeature::hole("MH1", volt::BoardPoint{4.0, 4.0}, 2.4, false, "mounting")));
    static_cast<void>(board.place_component(
        volt::ComponentPlacement{fixture.header, volt::BoardPoint{6.0, 9.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Top, true}));
    static_cast<void>(board.place_component(
        volt::ComponentPlacement{fixture.resistor, volt::BoardPoint{18.0, 9.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Top, true}));
    static_cast<void>(board.place_component(
        volt::ComponentPlacement{fixture.led, volt::BoardPoint{28.0, 9.0},
                                 volt::BoardRotation::degrees(180.0), volt::BoardSide::Top, true}));
    static_cast<void>(board.add_track(volt::BoardTrack{
        fixture.vcc,
        front,
        std::vector{volt::BoardPoint{6.0, 7.73}, volt::BoardPoint{12.0, 7.73},
                    volt::BoardPoint{17.25, 9.0}},
        0.25,
    }));
    static_cast<void>(board.add_track(volt::BoardTrack{
        fixture.led_a,
        front,
        std::vector{volt::BoardPoint{18.75, 9.0}, volt::BoardPoint{23.0, 5.5},
                    volt::BoardPoint{28.75, 9.0}},
        0.25,
    }));
    static_cast<void>(board.add_via(
        volt::BoardVia{fixture.gnd, volt::BoardPoint{27.25, 9.0}, front, back, 0.35, 0.75}));
    static_cast<void>(board.add_track(volt::BoardTrack{
        fixture.gnd,
        back,
        std::vector{volt::BoardPoint{27.25, 9.0}, volt::BoardPoint{18.0, 14.0},
                    volt::BoardPoint{6.0, 10.27}},
        0.30,
    }));
    static_cast<void>(
        board.add_text(volt::BoardText{"VOL-187", volt::BoardPoint{18.0, 15.5},
                                       volt::BoardRotation::degrees(0.0), silk, 1.0, true}));
    return board;
}

[[nodiscard]] std::size_t exported_segment_count(const volt::Board &board) {
    auto count = std::size_t{0};
    for (std::size_t index = 0; index < board.all<volt::BoardTrackId>().size(); ++index) {
        count += board.get(volt::BoardTrackId{index}).points().size() - 1U;
    }
    return count;
}

[[nodiscard]] std::size_t exported_pad_count(const volt::CompiledBoard &compiled) {
    return compiled.board().all<volt::BoardFeatureId>().size() + compiled.pad_resolutions().size();
}

} // namespace

TEST_CASE("KiCad PCB writer exports a deterministic manufacturable board subset") {
    const auto fixture = make_resistor_circuit();
    const auto board = make_routed_board(fixture);
    const auto compiled = volt::test::compile_export_fixture(fixture.circuit, board, fixture.parts);
    const auto result = volt::adapters::kicad::write_board(compiled);

    CHECK_FALSE(result.loss_report.has_warnings());
    CHECK(result.text == read_fixture("kicad_flat_resistor.kicad_pcb"));
    CHECK(result.text == volt::adapters::kicad::write_board(compiled).text);
}

TEST_CASE("KiCad PCB writer pins a routed multi-net golden board") {
    const auto fixture = make_led_badge_circuit();
    const auto board = make_led_badge_board(fixture);
    const auto compiled = volt::test::compile_export_fixture(fixture.circuit, board, fixture.parts);
    const auto result = volt::adapters::kicad::write_board(compiled);

    CHECK_FALSE(result.loss_report.has_fab_critical_warnings());
    CHECK(result.text == read_fixture("kicad_routed_badge.kicad_pcb"));
    CHECK(result.text == volt::adapters::kicad::write_board(compiled).text);
    CHECK(count_occurrences(result.text, "(footprint ") ==
          board.all<volt::ComponentPlacementId>().size() +
              board.all<volt::BoardFeatureId>().size());
    CHECK(count_occurrences(result.text, "(pad ") == exported_pad_count(compiled));
    CHECK(count_occurrences(result.text, "(segment\n") == exported_segment_count(board));
    CHECK(count_occurrences(result.text, "(via\n") == board.all<volt::BoardViaId>().size());
    CHECK(count_occurrences(result.text, "\n  (net ") ==
          board.circuit().all<volt::NetId>().size() + 1U);
    CHECK(result.text.find("(net 1 \"VCC\")") != std::string::npos);
    CHECK(result.text.find("(net 2 \"LED_A\")") != std::string::npos);
    CHECK(result.text.find("(net 3 \"GND\")") != std::string::npos);
    CHECK(result.text.find("(layer \"F.Cu\")") != std::string::npos);
    CHECK(result.text.find("(layer \"B.Cu\")") != std::string::npos);
}

TEST_CASE("KiCad PCB writer exports an asymmetric bottom-side placement") {
    const auto fixture = make_resistor_circuit(asymmetric_test_footprint());
    auto board = make_placement_board(fixture, volt::BoardName{"Bottom"});
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{20.0, 10.0}, volt::BoardRotation::degrees(30.0),
        volt::BoardSide::Bottom, false}));

    const auto compiled = volt::test::compile_export_fixture(fixture.circuit, board, fixture.parts);
    const auto result = volt::adapters::kicad::write_board(compiled);
    const auto diagnostics = volt::adapters::kicad::fabrication_diagnostics(result.loss_report);

    CHECK_FALSE(result.loss_report.has_warnings());
    CHECK(diagnostics.diagnostics().empty());
    CHECK(result.text == volt::adapters::kicad::write_board(compiled).text);
    CHECK(result.text.find("(footprint \"Asymmetric_Bottom\"\n    (layer \"B.Cu\")") !=
          std::string::npos);
    CHECK(result.text.find("(at 20 10 330)") != std::string::npos);
    CHECK(result.text.find("(48 \"B.Fab\" user)") != std::string::npos);
    CHECK(result.text.find(
              "(property \"Reference\" \"R1\"\n      (at 0 -1.5 0)\n      (layer \"B.Fab\")") !=
          std::string::npos);
    CHECK(count_occurrences(result.text, "(justify left mirror)") == 2);
    CHECK(result.text.find("(pad \"1\" smd rect\n      (at 2 1 330)") != std::string::npos);
    CHECK(result.text.find("(layers \"B.Cu\" \"B.Paste\" \"B.Mask\")") != std::string::npos);
    CHECK(result.text.find("(net 1 \"LEFT\")") != std::string::npos);
    CHECK(result.text.find("(pad \"2\" smd oval\n      (at -1 -0.5 330)") != std::string::npos);
    CHECK(result.text.find("(net 2 \"RIGHT\")") != std::string::npos);
    CHECK(result.text.find("(pad \"MP\" thru_hole circle\n      (at 0.5 -2 330)") !=
          std::string::npos);
    CHECK(result.text.find("(drill 0.8)\n      (layers \"*.Cu\" \"*.Mask\")") != std::string::npos);
    CHECK(result.text.find("(pad \"MH\" np_thru_hole circle\n      (at -0.75 2.25 330)") !=
          std::string::npos);
}

TEST_CASE("KiCad PCB writer fails closed for a bottom placement without bottom copper") {
    const auto fixture = make_resistor_circuit(asymmetric_test_footprint());
    auto board = volt::Board{fixture.circuit, volt::BoardName{"SingleSided"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_layer_stack(volt::LayerStack{{front}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{50.0, 30.0}));
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{20.0, 10.0}, volt::BoardRotation::degrees(30.0),
        volt::BoardSide::Bottom, false}));

    const auto compiled = volt::test::compile_export_fixture(fixture.circuit, board, fixture.parts);
    const auto result = volt::adapters::kicad::write_board(compiled);
    const auto diagnostics = volt::adapters::kicad::fabrication_diagnostics(result.loss_report);

    REQUIRE(result.loss_report.warnings().size() == 1);
    CHECK(result.loss_report.warnings().front().construct == "component_placement.side");
    CHECK(result.loss_report.warnings().front().fabrication_impact ==
          volt::adapters::kicad::LossFabricationImpact::FabCritical);
    REQUIRE(diagnostics.diagnostics().size() == 1);
    CHECK(diagnostics.diagnostics().front().code() ==
          volt::DiagnosticCode{"PCB_KICAD_FAB_EXPORT_LOSS"});
    CHECK(result.text.find("(footprint \"Asymmetric_Bottom\"") == std::string::npos);
    CHECK(result.text.find("(31 \"B.Cu\" signal)") == std::string::npos);
}

TEST_CASE("KiCad PCB writer keeps mixed top and bottom placements ordered and oriented") {
    auto fixture = make_resistor_circuit(asymmetric_test_footprint());
    const auto second_component = add_second_resistor(fixture);
    auto board = make_placement_board(fixture, volt::BoardName{"Mixed"});
    static_cast<void>(board.place_component(
        volt::ComponentPlacement{fixture.component, volt::BoardPoint{10.0, 8.0},
                                 volt::BoardRotation::degrees(17.0), volt::BoardSide::Top, false}));
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        second_component, volt::BoardPoint{30.0, 12.0}, volt::BoardRotation::degrees(137.0),
        volt::BoardSide::Bottom, false}));

    const auto compiled = volt::test::compile_export_fixture(fixture.circuit, board, fixture.parts);
    const auto result = volt::adapters::kicad::write_board(compiled);

    CHECK_FALSE(result.loss_report.has_warnings());
    CHECK(result.text == volt::adapters::kicad::write_board(compiled).text);
    CHECK(count_occurrences(result.text, "(footprint \"Asymmetric_Bottom\"") == 2);
    CHECK(count_occurrences(result.text, "    (layer \"F.Cu\")") == 1);
    CHECK(count_occurrences(result.text, "    (layer \"B.Cu\")") == 1);
    const auto top_reference = result.text.find("(property \"Reference\" \"R1\"");
    const auto bottom_reference = result.text.find("(property \"Reference\" \"R2\"");
    REQUIRE(top_reference != std::string::npos);
    REQUIRE(bottom_reference != std::string::npos);
    CHECK(top_reference < bottom_reference);
    CHECK(result.text.find("(at -2 1 343)") != std::string::npos);
    CHECK(result.text.find("(at 2 1 223)") != std::string::npos);
    CHECK(result.text.find("(at 30 12 223)") != std::string::npos);
}

TEST_CASE("KiCad PCB writer reports unsupported out-of-subset board constructs") {
    const auto fixture = make_resistor_circuit();
    auto board = make_routed_board(fixture);
    [[maybe_unused]] const auto zone = board.add_zone(volt::BoardZone{
        std::vector{volt::BoardPoint{2.0, 2.0}, volt::BoardPoint{12.0, 2.0},
                    volt::BoardPoint{12.0, 8.0}, volt::BoardPoint{2.0, 8.0}},
        std::vector{volt::BoardLayerId{0}},
        fixture.left_net,
    });
    [[maybe_unused]] const auto keepout = board.add_keepout(volt::BoardKeepout{
        std::vector{volt::BoardPoint{15.0, 2.0}, volt::BoardPoint{18.0, 2.0},
                    volt::BoardPoint{18.0, 6.0}, volt::BoardPoint{15.0, 6.0}},
        std::vector{volt::BoardLayerId{0}, volt::BoardLayerId{1}},
        std::vector{volt::BoardKeepoutRestriction::Copper,
                    volt::BoardKeepoutRestriction::Placement},
    });
    static_cast<void>(board.add_feature(volt::BoardFeature::slot(
        "SLOT", volt::BoardPoint{20.0, 2.0}, volt::BoardPoint{24.0, 2.0}, 1.2, false, "mounting")));
    static_cast<void>(board.add_feature(volt::BoardFeature::cutout(
        "CUT",
        std::vector{volt::BoardPoint{30.0, 2.0}, volt::BoardPoint{34.0, 2.0},
                    volt::BoardPoint{34.0, 6.0}, volt::BoardPoint{30.0, 6.0}},
        "access")));
    static_cast<void>(board.add_feature(volt::BoardFeature::circle(
        "FID", volt::BoardPoint{42.0, 2.0}, 1.0, volt::BoardSide::Top, "fiducial")));

    const auto compiled = volt::test::compile_export_fixture(fixture.circuit, board, fixture.parts);
    const auto result = volt::adapters::kicad::write_board(compiled);

    REQUIRE(result.loss_report.warnings().size() == 4);
    CHECK(result.loss_report.warnings().at(0).kind ==
          volt::adapters::kicad::LossKind::UnsupportedConstruct);
    CHECK(result.loss_report.warnings().at(0).construct == "board.keepout");
    CHECK(result.loss_report.warnings().at(1).kind ==
          volt::adapters::kicad::LossKind::UnsupportedConstruct);
    CHECK(result.loss_report.warnings().at(1).construct == "board.feature.slot");
    CHECK(result.loss_report.warnings().at(2).construct == "board.feature.cutout");
    CHECK(result.loss_report.warnings().at(3).construct == "board.feature.circle");
    CHECK(result.text.find("(zone") != std::string::npos);
    CHECK(result.text.find("(net_name \"LEFT\")") != std::string::npos);
}

TEST_CASE("KiCad PCB writer classifies fab-critical and informational losses") {
    const auto fixture = make_resistor_circuit();
    auto board = make_routed_board(fixture);
    [[maybe_unused]] const auto zone = board.add_zone(volt::BoardZone{
        std::vector{volt::BoardPoint{2.0, 2.0}, volt::BoardPoint{12.0, 2.0},
                    volt::BoardPoint{12.0, 8.0}, volt::BoardPoint{2.0, 8.0}},
        std::vector{volt::BoardLayerId{0}},
        fixture.left_net,
    });
    const auto documentation_layer = board.add_layer(volt::BoardLayer{
        "Documentation", volt::BoardLayerRole::Mechanical, volt::BoardLayerSide::None});
    [[maybe_unused]] const auto text = board.add_text(
        volt::BoardText{"ASSEMBLY NOTE", volt::BoardPoint{2.0, 20.0},
                        volt::BoardRotation::degrees(0.0), documentation_layer, 1.0, true});
    const auto fabrication_layer = board.add_layer(
        volt::BoardLayer{"FabNotes", volt::BoardLayerRole::Fabrication, volt::BoardLayerSide::Top});
    [[maybe_unused]] const auto fab_text = board.add_text(
        volt::BoardText{"FAB NOTE", volt::BoardPoint{2.0, 22.0}, volt::BoardRotation::degrees(0.0),
                        fabrication_layer, 1.0, true});

    const auto compiled = volt::test::compile_export_fixture(fixture.circuit, board, fixture.parts);
    const auto result = volt::adapters::kicad::write_board(compiled);

    REQUIRE(result.loss_report.warnings().size() == 2);
    CHECK(result.loss_report.warnings().at(0).construct == "board.text.layer");
    CHECK(result.loss_report.warnings().at(0).severity ==
          volt::adapters::kicad::LossSeverity::Info);
    CHECK(result.loss_report.warnings().at(0).fabrication_impact ==
          volt::adapters::kicad::LossFabricationImpact::Informational);
    CHECK(result.loss_report.warnings().at(1).construct == "board.text.layer");
    CHECK(result.loss_report.warnings().at(1).severity ==
          volt::adapters::kicad::LossSeverity::Warning);
    CHECK(result.loss_report.warnings().at(1).fabrication_impact ==
          volt::adapters::kicad::LossFabricationImpact::FabCritical);
    CHECK(result.text.find("(zone") != std::string::npos);
    CHECK(result.loss_report.has_fab_critical_warnings());
}

TEST_CASE("KiCad PCB writer preserves plated board-hole diagnostic identity") {
    const auto fixture = make_resistor_circuit();
    auto board = make_routed_board(fixture);
    static_cast<void>(board.add_feature(
        volt::BoardFeature::hole("MH2", volt::BoardPoint{42.0, 4.0}, 1.0, true, "mounting")));

    const auto compiled = volt::test::compile_export_fixture(fixture.circuit, board, fixture.parts);
    const auto result = volt::adapters::kicad::write_board(compiled);
    const auto diagnostics = volt::adapters::kicad::fabrication_diagnostics(result.loss_report);

    REQUIRE(diagnostics.diagnostics().size() == 1);
    const auto &diagnostic = diagnostics.diagnostics().front();
    CHECK(diagnostic.code() == volt::DiagnosticCode{"PCB_KICAD_FAB_EXPORT_LOSS"});
    CHECK(diagnostic.severity() == volt::Severity::Error);
    CHECK(diagnostic.category() ==
          volt::DiagnosticCategory{volt::diagnostic_categories::PcbFabrication});
    CHECK(diagnostic.entities() == std::vector{volt::EntityRef::board()});
    REQUIRE(diagnostic.rule().has_value());
    CHECK(diagnostic.rule().value() == "board.feature.hole.plated");
}

TEST_CASE("KiCad PCB writer keeps generated footprint metadata DRC-neutral") {
    const auto fixture = make_resistor_circuit();
    const auto board = make_routed_board(fixture);

    const auto compiled = volt::test::compile_export_fixture(fixture.circuit, board, fixture.parts);
    const auto result = volt::adapters::kicad::write_board(compiled);

    CHECK(result.text.find("(footprint \"passives:") == std::string::npos);
    CHECK(result.text.find("(footprint \"Volt:") == std::string::npos);
    CHECK(result.text.find("(footprint \"R_0603_1608Metric\"") != std::string::npos);
    CHECK(result.text.find("(footprint \"BoardHole_NPTH\"") != std::string::npos);
    CHECK(result.text.find("(49 \"F.Fab\" user)") != std::string::npos);
    CHECK(result.text.find(
              "(property \"Reference\" \"R1\"\n      (at 0 -1.5 0)\n      (layer \"F.Fab\")") !=
          std::string::npos);
    CHECK(result.text.find(
              "(property \"Value\" \"330R\"\n      (at 0 1.5 0)\n      (layer \"F.Fab\")") !=
          std::string::npos);
}

TEST_CASE("KiCad PCB writer does not collapse distinct Volt layers onto one KiCad layer") {
    const auto fixture = make_resistor_circuit();
    auto board = make_routed_board(fixture);
    const auto auxiliary_front = board.add_layer(
        volt::BoardLayer{"TopAux", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    [[maybe_unused]] const auto ambiguous_track = board.add_track(volt::BoardTrack{
        fixture.right_net,
        auxiliary_front,
        std::vector{volt::BoardPoint{2.0, 2.0}, volt::BoardPoint{6.0, 2.0}},
        0.25,
    });

    const auto compiled = volt::test::compile_export_fixture(fixture.circuit, board, fixture.parts);
    const auto result = volt::adapters::kicad::write_board(compiled);

    CHECK(count_occurrences(result.text, "(segment\n") == 2);
    REQUIRE(result.loss_report.warnings().size() == 2);
    CHECK(result.loss_report.warnings().at(0).construct == "board.layer.mapping");
    CHECK(result.loss_report.warnings().at(1).construct == "board.track.layer");
}
