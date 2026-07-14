#include <catch2/catch_test_macros.hpp>

#include "support/circuit_test_helpers.hpp"

#include <fstream>
#include <iterator>
#include <set>
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

[[nodiscard]] std::vector<std::string> extract_uuids(const std::string &text) {
    auto uuids = std::vector<std::string>{};
    const auto marker = std::string{"(uuid \""};
    auto position = std::size_t{0};
    while ((position = text.find(marker, position)) != std::string::npos) {
        const auto start = position + marker.size();
        const auto end = text.find('"', start);
        REQUIRE(end != std::string::npos);
        uuids.push_back(text.substr(start, end - start));
        position = end;
    }
    return uuids;
}

struct ResistorCircuit {
    volt::Circuit circuit;
    volt::ComponentId component;
    volt::PinDefId first_pin_definition;
    volt::PinDefId second_pin_definition;
    volt::NetId left_net;
    volt::NetId right_net;
};

[[nodiscard]] ResistorCircuit make_resistor_circuit() {
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
    const auto component_definition = volt::test::define_component(
        circuit, "Resistor", std::vector{first_pin_spec, second_pin_spec});
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
    circuit.update(component, volt::SelectPhysicalPart{volt::PhysicalPart{
                                  volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                                  volt::PackageRef{"0603"},
                                  volt::FootprintRef{"passives", "R_0603_1608Metric"},
                                  std::vector{volt::PinPadMapping{first_pin_definition, "1"},
                                              volt::PinPadMapping{second_pin_definition, "2"}},
                              }});

    return ResistorCircuit{std::move(circuit),    component, first_pin_definition,
                           second_pin_definition, left_net,  right_net};
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
    [[maybe_unused]] const auto footprint =
        board.cache_footprint_definition(volt::passive_0603_footprint());
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

    const auto header_definition = volt::test::define_component(
        circuit, "Header", std::vector{header_one_spec, header_two_spec});
    const auto header_pins = circuit.get(header_definition).pins();
    const auto header_one = header_pins[0];
    const auto header_two = header_pins[1];
    const auto passive_definition = volt::test::define_component(
        circuit, "Resistor", std::vector{passive_one_spec, passive_two_spec});
    const auto passive_pins = circuit.get(passive_definition).pins();
    const auto passive_one = passive_pins[0];
    const auto passive_two = passive_pins[1];
    const auto led_definition =
        volt::test::define_component(circuit, "LED", std::vector{led_anode_spec, led_cathode_spec});
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

    circuit.update(header, volt::SelectPhysicalPart{volt::PhysicalPart{
                               volt::ManufacturerPart{"Generic", "HDR-1x02"},
                               volt::PackageRef{"2.54mm-1x02"},
                               volt::FootprintRef{"connectors", "PinHeader_1x02_P2.54mm_Vertical"},
                               std::vector{volt::PinPadMapping{header_one, "1"},
                                           volt::PinPadMapping{header_two, "2"}},
                           }});
    circuit.update(resistor, volt::SelectPhysicalPart{volt::PhysicalPart{
                                 volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                                 volt::PackageRef{"0603"},
                                 volt::FootprintRef{"passives", "R_0603_1608Metric"},
                                 std::vector{volt::PinPadMapping{passive_one, "1"},
                                             volt::PinPadMapping{passive_two, "2"}},
                             }});
    circuit.update(led, volt::SelectPhysicalPart{volt::PhysicalPart{
                            volt::ManufacturerPart{"Lite-On", "LTST-C190KRKT"},
                            volt::PackageRef{"0603"},
                            volt::FootprintRef{"leds", "LED_0603_1608Metric"},
                            std::vector{volt::PinPadMapping{led_anode, "1"},
                                        volt::PinPadMapping{led_cathode, "2"}},
                        }});

    return LedBadgeCircuit{std::move(circuit), header, resistor, led, vcc, led_a, gnd};
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
    for (std::size_t index = 0; index < board.track_count(); ++index) {
        count += board.track(volt::BoardTrackId{index}).points().size() - 1U;
    }
    return count;
}

[[nodiscard]] std::size_t exported_pad_count(const volt::Board &board) {
    auto count = board.feature_count();
    const auto library = volt::builtin_footprint_library();
    for (std::size_t index = 0; index < board.placement_count(); ++index) {
        const auto &placement = board.placement(volt::ComponentPlacementId{index});
        const auto &part =
            volt::queries::selected_physical_part(board.circuit(), placement.component()).value();
        const auto *definition = library.find(part.footprint());
        REQUIRE(definition != nullptr);
        count += definition->pad_count();
    }
    return count;
}

[[nodiscard]] volt::FootprintDefinition make_large_footprint(std::size_t pad_count) {
    auto pads = std::vector<volt::FootprintPad>{};
    pads.reserve(pad_count);
    for (std::size_t index = 0; index < pad_count; ++index) {
        pads.push_back(volt::FootprintPad::surface_mount(
            std::to_string(index + 1U), volt::FootprintPadShape::Rectangle,
            volt::FootprintPoint{static_cast<double>(index) * 0.1, 0.0},
            volt::FootprintSize{0.05, 0.05}, volt::FootprintLayerSet::front_smd()));
    }
    return volt::FootprintDefinition{volt::FootprintRef{"test", "LargePadArray"}, std::move(pads)};
}

struct TwoLargeFootprintComponents {
    volt::Circuit circuit;
    volt::ComponentId first_component;
    volt::ComponentId second_component;
};

[[nodiscard]] TwoLargeFootprintComponents make_two_large_footprint_components() {
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
    const auto component_definition = volt::test::define_component(
        circuit, "LargePackage", std::vector{first_pin_spec, second_pin_spec});
    const auto pin_definitions = circuit.get(component_definition).pins();
    const auto first_pin_definition = pin_definitions[0];
    const auto second_pin_definition = pin_definitions[1];
    const auto first_component = circuit.instantiate_component(
        component_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto second_component = circuit.instantiate_component(
        component_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U2"}});

    for (const auto component : {first_component, second_component}) {
        circuit.update(component, volt::SelectPhysicalPart{volt::PhysicalPart{
                                      volt::ManufacturerPart{"Test", "LargePadArray"},
                                      volt::PackageRef{"LARGE"},
                                      volt::FootprintRef{"test", "LargePadArray"},
                                      std::vector{volt::PinPadMapping{first_pin_definition, "1"},
                                                  volt::PinPadMapping{second_pin_definition, "2"}},
                                  }});
    }

    return TwoLargeFootprintComponents{std::move(circuit), first_component, second_component};
}

} // namespace

TEST_CASE("KiCad PCB writer exports a deterministic manufacturable board subset") {
    const auto fixture = make_resistor_circuit();
    const auto board = make_routed_board(fixture);

    const auto result =
        volt::adapters::kicad::write_board(board, volt::builtin_footprint_library());

    CHECK_FALSE(result.loss_report.has_warnings());
    CHECK(result.text == read_fixture("kicad_flat_resistor.kicad_pcb"));
    CHECK(result.text ==
          volt::adapters::kicad::write_board(board, volt::builtin_footprint_library()).text);
}

TEST_CASE("KiCad PCB writer pins a routed multi-net golden board") {
    const auto fixture = make_led_badge_circuit();
    const auto board = make_led_badge_board(fixture);

    const auto result =
        volt::adapters::kicad::write_board(board, volt::builtin_footprint_library());

    CHECK_FALSE(result.loss_report.has_fab_critical_warnings());
    CHECK(result.text == read_fixture("kicad_routed_badge.kicad_pcb"));
    CHECK(result.text ==
          volt::adapters::kicad::write_board(board, volt::builtin_footprint_library()).text);
    CHECK(count_occurrences(result.text, "(footprint ") ==
          board.placement_count() + board.feature_count());
    CHECK(count_occurrences(result.text, "(pad ") == exported_pad_count(board));
    CHECK(count_occurrences(result.text, "(segment\n") == exported_segment_count(board));
    CHECK(count_occurrences(result.text, "(via\n") == board.via_count());
    CHECK(count_occurrences(result.text, "\n  (net ") ==
          board.circuit().all<volt::NetId>().size() + 1U);
    CHECK(result.text.find("(net 1 \"VCC\")") != std::string::npos);
    CHECK(result.text.find("(net 2 \"LED_A\")") != std::string::npos);
    CHECK(result.text.find("(net 3 \"GND\")") != std::string::npos);
    CHECK(result.text.find("(layer \"F.Cu\")") != std::string::npos);
    CHECK(result.text.find("(layer \"B.Cu\")") != std::string::npos);
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

    const auto result =
        volt::adapters::kicad::write_board(board, volt::builtin_footprint_library());

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

    const auto result =
        volt::adapters::kicad::write_board(board, volt::builtin_footprint_library());

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

TEST_CASE("KiCad PCB writer keeps generated footprint metadata DRC-neutral") {
    const auto fixture = make_resistor_circuit();
    const auto board = make_routed_board(fixture);

    const auto result =
        volt::adapters::kicad::write_board(board, volt::builtin_footprint_library());

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

    const auto result =
        volt::adapters::kicad::write_board(board, volt::builtin_footprint_library());

    CHECK(count_occurrences(result.text, "(segment\n") == 2);
    REQUIRE(result.loss_report.warnings().size() == 2);
    CHECK(result.loss_report.warnings().at(0).construct == "board.layer.mapping");
    CHECK(result.loss_report.warnings().at(1).construct == "board.track.layer");
}

TEST_CASE("KiCad PCB writer generates unique UUIDs without footprint pad range contracts") {
    auto fixture = make_two_large_footprint_components();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"LargeFootprints"}};
    [[maybe_unused]] const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    [[maybe_unused]] const auto first_placement = board.place_component(
        volt::ComponentPlacement{fixture.first_component, volt::BoardPoint{0.0, 0.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Top, true});
    [[maybe_unused]] const auto second_placement = board.place_component(
        volt::ComponentPlacement{fixture.second_component, volt::BoardPoint{200.0, 0.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Top, true});

    auto footprints = volt::FootprintLibrary{};
    footprints.add(make_large_footprint(950U));

    const auto result = volt::adapters::kicad::write_board(board, footprints);
    const auto uuids = extract_uuids(result.text);
    const auto unique_uuids = std::set<std::string>{uuids.begin(), uuids.end()};

    CHECK(uuids.size() == unique_uuids.size());
}

TEST_CASE("KiCad PCB writer reports invalid pad resolutions before omitting pad nets") {
    auto fixture = make_resistor_circuit();
    fixture.circuit.update(fixture.component,
                           volt::SelectPhysicalPart{volt::PhysicalPart{
                               volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                               volt::PackageRef{"0603"},
                               volt::FootprintRef{"passives", "R_0603_1608Metric"},
                               std::vector{volt::PinPadMapping{fixture.first_pin_definition, "99"},
                                           volt::PinPadMapping{fixture.second_pin_definition, "2"}},
                           }});
    const auto board = make_routed_board(fixture);

    const auto result =
        volt::adapters::kicad::write_board(board, volt::builtin_footprint_library());

    REQUIRE(result.loss_report.warnings().size() == 1);
    const auto &warning = result.loss_report.warnings().at(0);
    CHECK(warning.kind == volt::adapters::kicad::LossKind::IncompleteConstruct);
    CHECK(warning.construct == "pad_resolution");
    CHECK(warning.message.find("R1") != std::string::npos);
    CHECK(warning.message.find("without a net") != std::string::npos);
}
