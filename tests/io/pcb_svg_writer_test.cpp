#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/queries.hpp>
#include <volt/io/pcb_svg_writer.hpp>
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

struct MultiComponentNetCircuit {
    volt::Circuit circuit;
    std::vector<volt::ComponentId> components;
    volt::PinDefId first_pin_definition;
    volt::PinDefId second_pin_definition;
    volt::NetId shared_net;
};

[[nodiscard]] ResistorCircuit make_resistor_circuit(bool select_physical_part = true) {
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
    const auto component =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"R1"});
    const auto first_pin =
        volt::queries::pin_by_definition(circuit, component, first_pin_definition).value();
    const auto second_pin =
        volt::queries::pin_by_definition(circuit, component, second_pin_definition).value();
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

[[nodiscard]] MultiComponentNetCircuit make_multi_component_net(std::size_t component_count) {
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
    const auto shared_net =
        circuit.add_net(volt::Net{volt::NetName{"SHARED"}, volt::NetKind::Signal});

    auto components = std::vector<volt::ComponentId>{};
    components.reserve(component_count);
    for (std::size_t index = 0; index < component_count; ++index) {
        const auto component = circuit.instantiate_component(
            component_definition, volt::ReferenceDesignator{"R" + std::to_string(index + 1U)});
        circuit.select_physical_part(
            component, volt::PhysicalPart{
                           volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                           volt::PackageRef{"0603"},
                           volt::FootprintRef{"passives", "R_0603_1608Metric"},
                           std::vector{volt::PinPadMapping{first_pin_definition, "1"},
                                       volt::PinPadMapping{second_pin_definition, "2"}},
                       });
        const auto connected_pin_definition =
            index == 0U ? second_pin_definition : first_pin_definition;
        circuit.connect(
            shared_net,
            volt::queries::pin_by_definition(circuit, component, connected_pin_definition).value());
        components.push_back(component);
    }

    return MultiComponentNetCircuit{std::move(circuit), std::move(components), first_pin_definition,
                                    second_pin_definition, shared_net};
}

[[nodiscard]] volt::Board make_preview_board(const ResistorCircuit &fixture) {
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
    return board;
}

[[nodiscard]] std::string read_fixture(const std::string &name) {
    auto input = std::ifstream{std::string{VOLT_TEST_FIXTURE_DIR} + "/" + name};
    REQUIRE(input.is_open());
    auto buffer = std::ostringstream{};
    buffer << input.rdbuf();
    return buffer.str();
}

[[nodiscard]] std::size_t count_occurrences(const std::string &text, const std::string &needle) {
    auto count = std::size_t{0};
    auto position = std::size_t{0};
    while ((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

[[nodiscard]] volt::FootprintDefinition resistor_with_declared_geometry() {
    const auto courtyard = volt::FootprintPolygon{std::vector{
        volt::FootprintPoint{-3.0, -1.5},
        volt::FootprintPoint{3.0, -1.5},
        volt::FootprintPoint{3.0, 1.5},
        volt::FootprintPoint{-3.0, 1.5},
    }};
    const auto body = volt::FootprintPolygon{std::vector{
        volt::FootprintPoint{-1.2, -0.55},
        volt::FootprintPoint{1.2, -0.55},
        volt::FootprintPoint{1.2, 0.55},
        volt::FootprintPoint{-1.2, 0.55},
    }};
    return volt::FootprintDefinition{
        volt::FootprintRef{"test", "DeclaredGeometry"},
        std::vector{
            volt::FootprintPad::surface_mount(
                "1", volt::FootprintPadShape::RoundedRectangle, volt::FootprintPoint{-0.75, 0.0},
                volt::FootprintSize{0.8, 0.95}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "2", volt::FootprintPadShape::RoundedRectangle, volt::FootprintPoint{0.75, 0.0},
                volt::FootprintSize{0.8, 0.95}, volt::FootprintLayerSet::front_smd()),
        },
        courtyard,
        body,
    };
}

} // namespace

TEST_CASE("PCB SVG writer renders a deterministic placement preview") {
    const auto fixture = make_resistor_circuit();
    const auto board = make_preview_board(fixture);

    const auto first = volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library());
    const auto second = volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library());

    CHECK(first == second);
    CHECK(first == read_fixture("pcb_placement_preview.svg"));
}

TEST_CASE("PCB SVG writer exposes stable selectors matching PCB JSON entities") {
    const auto fixture = make_resistor_circuit();
    const auto board = make_preview_board(fixture);

    const auto svg = volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library());

    CHECK(svg.find("class=\"pcb-placement-preview\"") != std::string::npos);
    CHECK(svg.find("data-board=\"board:0\"") != std::string::npos);
    CHECK(svg.find("data-units=\"mm\"") != std::string::npos);
    CHECK(svg.find("class=\"board-outline\" data-board=\"board:0\"") != std::string::npos);
    CHECK(svg.find("data-board-feature=\"board_feature:0\"") != std::string::npos);
    CHECK(svg.find("data-placement=\"component_placement:0\"") != std::string::npos);
    CHECK(svg.find("data-component=\"component:0\"") != std::string::npos);
    CHECK(svg.find("data-footprint-def=\"footprint_def:0\"") != std::string::npos);
    CHECK(svg.find("data-pad-projection=\"pcb_pad:0:0\"") != std::string::npos);
    CHECK(svg.find("data-pad=\"footprint_pad:0\"") != std::string::npos);
    CHECK(svg.find("data-pin=\"pin:0\"") != std::string::npos);
    CHECK(svg.find("data-net=\"net:0\"") != std::string::npos);
}

TEST_CASE("PCB SVG writer renders declared footprint body and courtyard polygons") {
    auto fixture = make_resistor_circuit(false);
    fixture.circuit.select_physical_part(
        fixture.component, volt::PhysicalPart{
                               volt::ManufacturerPart{"Volt", "DeclaredGeometry"},
                               volt::PackageRef{"DeclaredGeometry"},
                               volt::FootprintRef{"test", "DeclaredGeometry"},
                               std::vector{volt::PinPadMapping{fixture.first_pin_definition, "1"},
                                           volt::PinPadMapping{fixture.second_pin_definition, "2"}},
                           });
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Declared Geometry"}};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    [[maybe_unused]] const auto footprint =
        board.cache_footprint_definition(resistor_with_declared_geometry());
    [[maybe_unused]] const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{15.0, 10.0}, volt::BoardRotation::degrees(0.0)});

    const auto svg = volt::io::write_pcb_placement_svg(board, volt::FootprintLibrary{});

    CHECK(svg.find("<polygon class=\"footprint-courtyard declared\" "
                   "data-placement=\"component_placement:0\" data-component=\"component:0\" "
                   "points=\"12,8.5 18,8.5 18,11.5 12,11.5\"/>") != std::string::npos);
    CHECK(svg.find("<polygon class=\"footprint-body declared\" "
                   "data-placement=\"component_placement:0\" data-component=\"component:0\" "
                   "points=\"13.8,9.45 16.2,9.45 16.2,10.55 13.8,10.55\"/>") != std::string::npos);
    CHECK(svg.find("class=\"footprint-envelope synthetic\"") == std::string::npos);
}

TEST_CASE("PCB SVG writer marks pad-derived footprint envelopes as synthetic") {
    const auto fixture = make_resistor_circuit();
    const auto board = make_preview_board(fixture);

    const auto svg = volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library());

    CHECK(svg.find(".footprint-envelope.synthetic{fill:#fff8db;fill-opacity:0.24;"
                   "stroke:#8a6a16;stroke-width:0.18;stroke-dasharray:0.9 0.55}") !=
          std::string::npos);
    CHECK(svg.find("<rect class=\"footprint-envelope synthetic\" x=\"-1.65\" y=\"-0.975\" "
                   "width=\"3.3\" height=\"1.95\"/>") != std::string::npos);
}

TEST_CASE("PCB SVG writer keeps reference designators upright for rotated placements") {
    auto fixture = make_multi_component_net(8);
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Reference Orientation"}};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{60.0, 24.0}));
    [[maybe_unused]] const auto r1 = board.place_component(volt::ComponentPlacement{
        fixture.components[0], volt::BoardPoint{10.0, 8.0}, volt::BoardRotation::degrees(0.0)});
    [[maybe_unused]] const auto r2 = board.place_component(volt::ComponentPlacement{
        fixture.components[1], volt::BoardPoint{20.0, 8.0}, volt::BoardRotation::degrees(90.0)});
    [[maybe_unused]] const auto r3 = board.place_component(volt::ComponentPlacement{
        fixture.components[2], volt::BoardPoint{30.0, 8.0}, volt::BoardRotation::degrees(180.0)});
    [[maybe_unused]] const auto r4 = board.place_component(volt::ComponentPlacement{
        fixture.components[3], volt::BoardPoint{40.0, 8.0}, volt::BoardRotation::degrees(270.0)});
    [[maybe_unused]] const auto r5 = board.place_component(
        volt::ComponentPlacement{fixture.components[4], volt::BoardPoint{50.0, 10.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Bottom});
    [[maybe_unused]] const auto r6 = board.place_component(
        volt::ComponentPlacement{fixture.components[5], volt::BoardPoint{10.0, 17.0},
                                 volt::BoardRotation::degrees(90.0), volt::BoardSide::Bottom});
    [[maybe_unused]] const auto r7 = board.place_component(
        volt::ComponentPlacement{fixture.components[6], volt::BoardPoint{20.0, 17.0},
                                 volt::BoardRotation::degrees(180.0), volt::BoardSide::Bottom});
    [[maybe_unused]] const auto r8 = board.place_component(
        volt::ComponentPlacement{fixture.components[7], volt::BoardPoint{30.0, 17.0},
                                 volt::BoardRotation::degrees(270.0), volt::BoardSide::Bottom});

    const auto svg = volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library());

    CHECK(svg.find("transform=\"translate(50 10) rotate(0) scale(-1 1)\"") != std::string::npos);
    CHECK(svg.find("transform=\"translate(10 17) rotate(90) scale(-1 1)\"") != std::string::npos);
    CHECK(svg.find("transform=\"translate(20 17) rotate(180) scale(-1 1)\"") != std::string::npos);
    CHECK(svg.find("transform=\"translate(30 17) rotate(270) scale(-1 1)\"") != std::string::npos);
    CHECK(svg.find("<text class=\"reference-designator\" data-placement=\"component_placement:0\" "
                   "data-component=\"component:0\" x=\"10\" y=\"6.025\" "
                   "text-anchor=\"middle\">R1</text>") != std::string::npos);
    CHECK(svg.find("<text class=\"reference-designator\" data-placement=\"component_placement:1\" "
                   "data-component=\"component:1\" x=\"21.975\" y=\"8\" "
                   "text-anchor=\"middle\">R2</text>") != std::string::npos);
    CHECK(svg.find("<text class=\"reference-designator\" data-placement=\"component_placement:2\" "
                   "data-component=\"component:2\" x=\"30\" y=\"9.975\" "
                   "text-anchor=\"middle\">R3</text>") != std::string::npos);
    CHECK(svg.find("<text class=\"reference-designator\" data-placement=\"component_placement:3\" "
                   "data-component=\"component:3\" x=\"38.025\" y=\"8\" "
                   "text-anchor=\"middle\">R4</text>") != std::string::npos);
    CHECK(svg.find("<text class=\"reference-designator\" data-placement=\"component_placement:4\" "
                   "data-component=\"component:4\" x=\"50\" y=\"8.025\" "
                   "text-anchor=\"middle\">R5</text>") != std::string::npos);
    CHECK(svg.find("<text class=\"reference-designator\" data-placement=\"component_placement:5\" "
                   "data-component=\"component:5\" x=\"11.975\" y=\"17\" "
                   "text-anchor=\"middle\">R6</text>") != std::string::npos);
    CHECK(svg.find("<text class=\"reference-designator\" data-placement=\"component_placement:6\" "
                   "data-component=\"component:6\" x=\"20\" y=\"18.975\" "
                   "text-anchor=\"middle\">R7</text>") != std::string::npos);
    CHECK(svg.find("<text class=\"reference-designator\" data-placement=\"component_placement:7\" "
                   "data-component=\"component:7\" x=\"28.025\" y=\"17\" "
                   "text-anchor=\"middle\">R8</text>") != std::string::npos);
    CHECK(count_occurrences(svg, "class=\"reference-designator\"") == 8U);
    CHECK(svg.find("class=\"reference-designator\" transform=") == std::string::npos);
}

TEST_CASE("PCB SVG writer exposes deterministic layer filename tokens") {
    CHECK(volt::io::detail::pcb_svg_layer_filename_token("F.Cu") == "F_Cu");
    CHECK(volt::io::detail::pcb_svg_layer_filename_token("B.Cu") == "B_Cu");
    CHECK(volt::io::detail::pcb_svg_layer_filename_token("F.SilkS") == "F_SilkS");
    CHECK(volt::io::detail::pcb_svg_layer_filename_token("user mask+paste") == "user_mask_paste");
    CHECK(volt::io::detail::pcb_svg_layer_filename_token("***") == "layer");

    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit, volt::BoardName{"Token Collisions"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto duplicate = board.add_layer(
        volt::BoardLayer{"F Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto empty = board.add_layer(
        volt::BoardLayer{"***", volt::BoardLayerRole::Mechanical, volt::BoardLayerSide::None});
    const auto duplicate_empty = board.add_layer(
        volt::BoardLayer{"___", volt::BoardLayerRole::Mechanical, volt::BoardLayerSide::None});

    CHECK(volt::io::detail::pcb_svg_layer_token(board, front) == "F_Cu");
    CHECK(volt::io::detail::pcb_svg_layer_token(board, duplicate) == "F_Cu_2");
    CHECK(volt::io::detail::pcb_svg_layer_token(board, empty) == "layer");
    CHECK(volt::io::detail::pcb_svg_layer_token(board, duplicate_empty) == "layer_2");
}

TEST_CASE("PCB SVG writer groups composite output by board layer") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Layer Groups"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    const auto silk = board.add_layer(
        volt::BoardLayer{"F.SilkS", volt::BoardLayerRole::Silkscreen, volt::BoardLayerSide::Top});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    [[maybe_unused]] const auto front_track = board.add_track(volt::BoardTrack{
        fixture.first_net, front,
        std::vector{volt::BoardPoint{4.0, 5.0}, volt::BoardPoint{10.0, 5.0}}, 0.25});
    [[maybe_unused]] const auto back_track = board.add_track(volt::BoardTrack{
        fixture.second_net, back,
        std::vector{volt::BoardPoint{4.0, 10.0}, volt::BoardPoint{10.0, 10.0}}, 0.25});
    [[maybe_unused]] const auto text = board.add_text(volt::BoardText{
        "REV A", volt::BoardPoint{5.0, 15.0}, volt::BoardRotation::degrees(0.0), silk, 1.2});

    const auto svg = volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library());

    CHECK(svg.find("id=\"pcb-layer-F_Cu\"") != std::string::npos);
    CHECK(svg.find("class=\"pcb-layer board-layer layer-F_Cu\"") != std::string::npos);
    CHECK(svg.find("data-layer=\"board_layer:0\"") != std::string::npos);
    CHECK(svg.find("data-layer-name=\"F.Cu\"") != std::string::npos);
    CHECK(svg.find("id=\"pcb-layer-B_Cu\"") != std::string::npos);
    CHECK(svg.find("id=\"pcb-layer-F_SilkS\"") != std::string::npos);
}

TEST_CASE("PCB SVG writer filters layer-owned content for a selected layer") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Layer Filter"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    const auto silk = board.add_layer(
        volt::BoardLayer{"F.SilkS", volt::BoardLayerRole::Silkscreen, volt::BoardLayerSide::Top});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    [[maybe_unused]] const auto front_track = board.add_track(volt::BoardTrack{
        fixture.first_net, front,
        std::vector{volt::BoardPoint{4.0, 5.0}, volt::BoardPoint{10.0, 5.0}}, 0.25});
    [[maybe_unused]] const auto back_track = board.add_track(volt::BoardTrack{
        fixture.second_net, back,
        std::vector{volt::BoardPoint{4.0, 10.0}, volt::BoardPoint{10.0, 10.0}}, 0.25});
    [[maybe_unused]] const auto via = board.add_via(
        volt::BoardVia{fixture.first_net, volt::BoardPoint{10.0, 5.0}, front, back, 0.30, 0.70});
    [[maybe_unused]] const auto text = board.add_text(volt::BoardText{
        "REV A", volt::BoardPoint{5.0, 15.0}, volt::BoardRotation::degrees(0.0), silk, 1.2});
    [[maybe_unused]] const auto placement = board.place_component(
        volt::ComponentPlacement{fixture.component, volt::BoardPoint{18.0, 10.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Top});

    const auto front_svg =
        volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library(),
                                          volt::io::PcbPlacementSvgOptions{.layer_filter = front});
    const auto silk_svg =
        volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library(),
                                          volt::io::PcbPlacementSvgOptions{.layer_filter = silk});

    CHECK(front_svg.find("id=\"pcb-layer-F_Cu\"") != std::string::npos);
    CHECK(front_svg.find("id=\"pcb-layer-B_Cu\"") == std::string::npos);
    CHECK(front_svg.find("id=\"pcb-layer-F_SilkS\"") == std::string::npos);
    CHECK(front_svg.find("data-track=\"board_track:0\"") != std::string::npos);
    CHECK(front_svg.find("data-track=\"board_track:1\"") == std::string::npos);
    CHECK(front_svg.find("data-via=\"board_via:0\"") != std::string::npos);
    CHECK(front_svg.find("data-text=\"board_text:0\"") == std::string::npos);
    CHECK(front_svg.find("data-pad-projection=\"pcb_pad:0:0\"") != std::string::npos);

    CHECK(silk_svg.find("id=\"pcb-layer-F_SilkS\"") != std::string::npos);
    CHECK(silk_svg.find("data-track=\"board_track:0\"") == std::string::npos);
    CHECK(silk_svg.find("data-track=\"board_track:1\"") == std::string::npos);
    CHECK(silk_svg.find("data-via=\"board_via:0\"") == std::string::npos);
    CHECK(silk_svg.find("data-text=\"board_text:0\"") != std::string::npos);
    CHECK(silk_svg.find("data-pad-projection=") == std::string::npos);
}

TEST_CASE("PCB SVG writer preserves through-hole pad copper on non-placement-side layers") {
    auto fixture = make_resistor_circuit(false);
    fixture.circuit.select_physical_part(
        fixture.component, volt::PhysicalPart{
                               volt::ManufacturerPart{"Generic", "PinHeader_1x02"},
                               volt::PackageRef{"1x02"},
                               volt::FootprintRef{"connectors", "PinHeader_1x02_P2.54mm_Vertical"},
                               std::vector{volt::PinPadMapping{fixture.first_pin_definition, "1"},
                                           volt::PinPadMapping{fixture.second_pin_definition, "2"}},
                           });
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Through Hole Layers"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto inner = board.add_layer(
        volt::BoardLayer{"In1.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Inner});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, inner, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    [[maybe_unused]] const auto placement = board.place_component(
        volt::ComponentPlacement{fixture.component, volt::BoardPoint{15.0, 10.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Top});

    const auto inner_svg =
        volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library(),
                                          volt::io::PcbPlacementSvgOptions{.layer_filter = inner});
    const auto back_svg =
        volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library(),
                                          volt::io::PcbPlacementSvgOptions{.layer_filter = back});

    CHECK(inner_svg.find("<circle class=\"footprint-pad circle connected\" "
                         "data-pad-projection=\"pcb_pad:0:0\"") != std::string::npos);
    CHECK(back_svg.find("<circle class=\"footprint-pad circle connected\" "
                        "data-pad-projection=\"pcb_pad:0:0\"") != std::string::npos);
    CHECK(inner_svg.find("class=\"footprint-body\"") == std::string::npos);
    CHECK(back_svg.find("class=\"footprint-body\"") == std::string::npos);
}

TEST_CASE("PCB SVG writer keeps repeated layer object IDs unique") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Repeated Layer Objects"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    [[maybe_unused]] const auto via = board.add_via(
        volt::BoardVia{fixture.first_net, volt::BoardPoint{10.0, 5.0}, front, back, 0.30, 0.70});
    [[maybe_unused]] const auto zone = board.add_zone(volt::BoardZone{
        std::vector{
            volt::BoardPoint{2.0, 2.0},
            volt::BoardPoint{10.0, 2.0},
            volt::BoardPoint{10.0, 7.0},
            volt::BoardPoint{2.0, 7.0},
        },
        std::vector{front, back},
        fixture.first_net,
    });
    [[maybe_unused]] const auto keepout = board.add_keepout(volt::BoardKeepout{
        std::vector{
            volt::BoardPoint{12.0, 2.0},
            volt::BoardPoint{16.0, 2.0},
            volt::BoardPoint{16.0, 6.0},
            volt::BoardPoint{12.0, 6.0},
        },
        std::vector{front, back},
        std::vector{volt::BoardKeepoutRestriction::Copper},
    });

    const auto svg = volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library());

    CHECK(count_occurrences(svg, "id=\"pcb-via-0-layer-F_Cu\"") == 1U);
    CHECK(count_occurrences(svg, "id=\"pcb-via-0-layer-B_Cu\"") == 1U);
    CHECK(count_occurrences(svg, "id=\"pcb-zone-0-layer-F_Cu\"") == 1U);
    CHECK(count_occurrences(svg, "id=\"pcb-zone-0-layer-B_Cu\"") == 1U);
    CHECK(count_occurrences(svg, "id=\"pcb-keepout-0-layer-F_Cu\"") == 1U);
    CHECK(count_occurrences(svg, "id=\"pcb-keepout-0-layer-B_Cu\"") == 1U);
    CHECK(svg.find("id=\"pcb-via-0\"") == std::string::npos);
    CHECK(svg.find("id=\"pcb-zone-0\"") == std::string::npos);
    CHECK(svg.find("id=\"pcb-keepout-0\"") == std::string::npos);
}

TEST_CASE("PCB SVG writer projects through-stack vias onto inner copper layers") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Inner Via"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto inner = board.add_layer(
        volt::BoardLayer{"In1.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Inner});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, inner, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    [[maybe_unused]] const auto via = board.add_via(
        volt::BoardVia{fixture.first_net, volt::BoardPoint{10.0, 5.0}, front, back, 0.30, 0.70});

    const auto inner_svg =
        volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library(),
                                          volt::io::PcbPlacementSvgOptions{.layer_filter = inner});

    CHECK(inner_svg.find("id=\"pcb-layer-In1_Cu\"") != std::string::npos);
    CHECK(inner_svg.find("id=\"pcb-layer-F_Cu\"") == std::string::npos);
    CHECK(inner_svg.find("data-via=\"board_via:0\"") != std::string::npos);
    CHECK(inner_svg.find("id=\"pcb-via-0-layer-In1_Cu\"") != std::string::npos);
}

TEST_CASE("PCB SVG writer renders generic board feature primitives") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit, volt::BoardName{"Features"}};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{40.0, 24.0}));
    static_cast<void>(board.add_feature(
        volt::BoardFeature::hole("MH1", volt::BoardPoint{4.0, 4.0}, 3.2, false, "mounting")));
    static_cast<void>(board.add_feature(volt::BoardFeature::slot(
        "SLOT", volt::BoardPoint{8.0, 4.0}, volt::BoardPoint{16.0, 4.0}, 1.5, false, "mounting")));
    static_cast<void>(board.add_feature(volt::BoardFeature::cutout(
        "CUT",
        std::vector{volt::BoardPoint{20.0, 4.0}, volt::BoardPoint{25.0, 4.0},
                    volt::BoardPoint{25.0, 9.0}, volt::BoardPoint{20.0, 9.0}},
        "access")));
    static_cast<void>(board.add_feature(volt::BoardFeature::circle(
        "FID", volt::BoardPoint{34.0, 4.0}, 1.0, volt::BoardSide::Top, "fiducial")));
    static_cast<void>(board.add_feature(
        volt::BoardFeature::hole("TH", volt::BoardPoint{4.0, 20.0}, 2.0, false, "tooling")));

    const auto svg = volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library());

    CHECK(svg.find("class=\"board-feature hole\"") != std::string::npos);
    CHECK(svg.find("class=\"board-feature slot\"") != std::string::npos);
    CHECK(svg.find("x1=\"8\" y1=\"4\" x2=\"16\" y2=\"4\" stroke-width=\"1.5\"") !=
          std::string::npos);
    CHECK(svg.find("class=\"board-feature cutout\"") != std::string::npos);
    CHECK(svg.find("points=\"20,4 25,4 25,9 20,9\"") != std::string::npos);
    CHECK(svg.find("class=\"board-feature circle top\"") != std::string::npos);
}

TEST_CASE("PCB SVG writer renders stable ratsnest selectors for placed multi-pad nets") {
    auto fixture = make_multi_component_net(2);
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Ratsnest"}};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    [[maybe_unused]] const auto first_placement = board.place_component(volt::ComponentPlacement{
        fixture.components[0], volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    [[maybe_unused]] const auto second_placement = board.place_component(volt::ComponentPlacement{
        fixture.components[1], volt::BoardPoint{20.0, 10.0}, volt::BoardRotation::degrees(0.0)});

    const auto svg = volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library());

    CHECK(svg.find("<line id=\"ratsnest-edge-net-0-0\" class=\"ratsnest ratsnest-edge\"") !=
          std::string::npos);
    CHECK(svg.find("data-ratsnest-edge=\"ratsnest:0:0\"") != std::string::npos);
    CHECK(svg.find("data-net=\"net:0\"") != std::string::npos);
    CHECK(svg.find("data-from-pad=\"pcb_pad:0:1\"") != std::string::npos);
    CHECK(svg.find("data-to-pad=\"pcb_pad:1:0\"") != std::string::npos);
    CHECK(svg.find("x1=\"10.75\" y1=\"10\" x2=\"19.25\" y2=\"10\"") != std::string::npos);

    const auto without_ratsnest = volt::io::write_pcb_placement_svg(
        board, volt::builtin_footprint_library(),
        volt::io::PcbPlacementSvgOptions{.ratsnest_edges = false});
    CHECK(without_ratsnest.find("data-ratsnest-edge=") == std::string::npos);
}

TEST_CASE("PCB SVG writer renders stable selectors for copper tracks and vias") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Copper"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    [[maybe_unused]] const auto track = board.add_track(volt::BoardTrack{
        fixture.first_net,
        front,
        std::vector{
            volt::BoardPoint{5.0, 5.0},
            volt::BoardPoint{12.0, 5.0},
            volt::BoardPoint{12.0, 8.0},
        },
        0.25,
    });
    [[maybe_unused]] const auto via = board.add_via(
        volt::BoardVia{fixture.first_net, volt::BoardPoint{12.0, 8.0}, front, back, 0.30, 0.70});

    const auto svg = volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library());

    CHECK(svg.find("<polyline id=\"pcb-track-0\" class=\"pcb-track\"") != std::string::npos);
    CHECK(svg.find("data-track=\"board_track:0\"") != std::string::npos);
    CHECK(svg.find("data-layer=\"board_layer:0\"") != std::string::npos);
    CHECK(svg.find("data-net=\"net:0\"") != std::string::npos);
    CHECK(svg.find("points=\"5,5 12,5 12,8\"") != std::string::npos);
    CHECK(svg.find("stroke-width=\"0.25\"") != std::string::npos);
    CHECK(svg.find("<g id=\"pcb-via-0-layer-F_Cu\" class=\"pcb-via\" data-via=\"board_via:0\"") !=
          std::string::npos);
    CHECK(svg.find("<g id=\"pcb-via-0-layer-B_Cu\" class=\"pcb-via\" data-via=\"board_via:0\"") !=
          std::string::npos);
    CHECK(svg.find("data-start-layer=\"board_layer:0\"") != std::string::npos);
    CHECK(svg.find("data-end-layer=\"board_layer:1\"") != std::string::npos);
    CHECK(svg.find("<circle class=\"pcb-via-annular\" cx=\"12\" cy=\"8\" r=\"0.35\"") !=
          std::string::npos);
    CHECK(svg.find("<circle class=\"pcb-via-drill\" cx=\"12\" cy=\"8\" r=\"0.15\"") !=
          std::string::npos);
}

TEST_CASE("PCB SVG writer renders stable selectors for zones, keepouts, and board text") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Annotations"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto silk = board.add_layer(
        volt::BoardLayer{"F.SilkS", volt::BoardLayerRole::Silkscreen, volt::BoardLayerSide::Top});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    [[maybe_unused]] const auto zone = board.add_zone(volt::BoardZone{
        std::vector{
            volt::BoardPoint{2.0, 2.0},
            volt::BoardPoint{10.0, 2.0},
            volt::BoardPoint{10.0, 7.0},
            volt::BoardPoint{2.0, 7.0},
        },
        std::vector{front},
        fixture.first_net,
    });
    [[maybe_unused]] const auto keepout = board.add_keepout(volt::BoardKeepout{
        std::vector{
            volt::BoardPoint{12.0, 2.0},
            volt::BoardPoint{16.0, 2.0},
            volt::BoardPoint{16.0, 6.0},
            volt::BoardPoint{12.0, 6.0},
        },
        std::vector{front},
        std::vector{volt::BoardKeepoutRestriction::Copper},
    });
    [[maybe_unused]] const auto text = board.add_text(volt::BoardText{
        "REV A", volt::BoardPoint{5.0, 15.0}, volt::BoardRotation::degrees(90.0), silk, 1.2, true});

    const auto svg = volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library());

    CHECK(svg.find("<polygon id=\"pcb-zone-0\" class=\"pcb-zone fill-solid\"") !=
          std::string::npos);
    CHECK(svg.find("data-zone=\"board_zone:0\"") != std::string::npos);
    CHECK(svg.find("data-layer=\"board_layer:0\"") != std::string::npos);
    CHECK(svg.find("data-net=\"net:0\"") != std::string::npos);
    CHECK(svg.find("points=\"2,2 10,2 10,7 2,7\"") != std::string::npos);
    CHECK(svg.find("<polygon id=\"pcb-keepout-0\" class=\"pcb-keepout copper\"") !=
          std::string::npos);
    CHECK(svg.find("data-keepout=\"board_keepout:0\"") != std::string::npos);
    CHECK(svg.find("data-restrictions=\"copper\"") != std::string::npos);
    CHECK(svg.find("<text id=\"pcb-text-0\" class=\"board-text locked\"") != std::string::npos);
    CHECK(svg.find("data-text=\"board_text:0\"") != std::string::npos);
    CHECK(svg.find("transform=\"rotate(90 5 15)\"") != std::string::npos);
    CHECK(svg.find(">REV A</text>") != std::string::npos);
}

TEST_CASE("PCB SVG writer surfaces board diagnostics without mutating projection state") {
    auto fixture = make_resistor_circuit(false);
    auto board = make_preview_board(fixture);

    const auto placements_before = board.placement_count();
    const auto svg = volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library());

    CHECK(board.placement_count() == placements_before);
    CHECK(svg.find("data-diagnostic-code=\"PCB_COMPONENT_MISSING_SELECTED_PART\"") !=
          std::string::npos);
    CHECK(svg.find("data-entities=\"component:0\"") != std::string::npos);
    CHECK(svg.find("class=\"diagnostic-label error\"") != std::string::npos);
}

TEST_CASE("PCB SVG writer exposes placement entity references for board diagnostics") {
    const auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Control"}};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{5.0, 5.0}));
    [[maybe_unused]] const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});

    const auto svg = volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library());

    CHECK(svg.find("data-diagnostic-code=\"PCB_PLACEMENT_OUTSIDE_OUTLINE\"") != std::string::npos);
    CHECK(svg.find("data-entities=\"component:0 component_placement:0\"") != std::string::npos);
    CHECK(svg.find("class=\"diagnostic-marker error\"") != std::string::npos);
}

TEST_CASE("PCB SVG writer renders PCB visual diagnostic overlay geometry") {
    auto fixture = make_multi_component_net(2);
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Visual Diagnostics"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        fixture.components[0], volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)}));
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        fixture.components[1], volt::BoardPoint{10.5, 10.0}, volt::BoardRotation::degrees(0.0)}));

    const auto svg = volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library());

    CHECK(svg.find("<rect id=\"diagnostic-overlay-0-0\" "
                   "class=\"diagnostic-overlay warning bounding-box\" "
                   "data-diagnostic-code=\"PCB_VISUAL_PLACEMENT_OVERLAP\" "
                   "data-diagnostic-index=\"0\" data-overlay-index=\"0\" "
                   "data-entities=\"component:0 component_placement:0 component:1 "
                   "component_placement:1\" "
                   "data-overlay-entities=\"component:0 component_placement:0\" "
                   "data-layers=\"board_layer:0\" x=\"8.85\" y=\"9.525\" width=\"2.3\" "
                   "height=\"0.95\"/>") != std::string::npos);
    CHECK(svg.find("<rect id=\"diagnostic-overlay-0-1\" "
                   "class=\"diagnostic-overlay warning bounding-box\" "
                   "data-diagnostic-code=\"PCB_VISUAL_PLACEMENT_OVERLAP\" "
                   "data-diagnostic-index=\"0\" data-overlay-index=\"1\" "
                   "data-entities=\"component:0 component_placement:0 component:1 "
                   "component_placement:1\" "
                   "data-overlay-entities=\"component:1 component_placement:1\" "
                   "data-layers=\"board_layer:0\" x=\"9.35\" y=\"9.525\" width=\"2.3\" "
                   "height=\"0.95\"/>") != std::string::npos);
}

TEST_CASE("PCB SVG writer filters diagnostic overlay geometry by selected layer") {
    auto fixture = make_multi_component_net(2);
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Layered Diagnostics"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        fixture.components[0], volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)}));
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        fixture.components[1], volt::BoardPoint{10.5, 10.0}, volt::BoardRotation::degrees(0.0)}));

    const auto front_svg =
        volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library(),
                                          volt::io::PcbPlacementSvgOptions{.layer_filter = front});
    const auto back_svg =
        volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library(),
                                          volt::io::PcbPlacementSvgOptions{.layer_filter = back});

    CHECK(front_svg.find("id=\"diagnostic-overlay-0-0\"") != std::string::npos);
    CHECK(front_svg.find("data-diagnostic-code=\"PCB_VISUAL_PLACEMENT_OVERLAP\"") !=
          std::string::npos);
    CHECK(back_svg.find("diagnostic-overlay") == std::string::npos);
    CHECK(back_svg.find("PCB_VISUAL_PLACEMENT_OVERLAP") == std::string::npos);
}

TEST_CASE("PCB SVG writer renders DRC diagnostic overlay geometry") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"DRC Diagnostics"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    board.set_design_rules(volt::BoardDesignRules{0.15, 0.25, 0.30, 0.70, 0.0});
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{20.0, 10.0}, volt::BoardRotation::degrees(0.0)}));
    static_cast<void>(board.add_track(volt::BoardTrack{
        fixture.first_net,
        front,
        std::vector{volt::BoardPoint{5.0, 5.0}, volt::BoardPoint{12.0, 5.0}},
        0.10,
    }));
    static_cast<void>(board.add_via(
        volt::BoardVia{fixture.first_net, volt::BoardPoint{12.0, 8.0}, front, back, 0.20, 0.50}));

    const auto svg = volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library());

    CHECK(svg.find("<line id=\"diagnostic-overlay-0-0\" "
                   "class=\"diagnostic-overlay error segment\" "
                   "data-diagnostic-code=\"PCB_TRACK_WIDTH_BELOW_MINIMUM\" "
                   "data-diagnostic-index=\"0\" data-overlay-index=\"0\" "
                   "data-entities=\"board_track:0 net:0 board_layer:0\" "
                   "data-overlay-entities=\"board_track:0\" data-layers=\"board_layer:0\" "
                   "x1=\"5\" y1=\"5\" x2=\"12\" y2=\"5\"/>") != std::string::npos);
    CHECK(svg.find("<circle id=\"diagnostic-overlay-1-0\" "
                   "class=\"diagnostic-overlay error point\" "
                   "data-diagnostic-code=\"PCB_VIA_DRILL_BELOW_MINIMUM\" "
                   "data-diagnostic-index=\"1\" data-overlay-index=\"0\" "
                   "data-entities=\"board_via:0 net:0\" data-overlay-entities=\"board_via:0\" "
                   "data-layers=\"board_layer:0 board_layer:1\" cx=\"12\" cy=\"8\" "
                   "r=\"0.45\"/>") != std::string::npos);
}

TEST_CASE("PCB SVG writer filters board layer diagnostics by selected layer") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit, volt::BoardName{"Layer Diagnostic"}};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{10.0, 10.0}));
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto silk = board.add_layer(
        volt::BoardLayer{"F.SilkS", volt::BoardLayerRole::Silkscreen, volt::BoardLayerSide::Top});
    board.set_layer_stack(volt::LayerStack{{back, front}, 1.6});

    const auto back_svg =
        volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library(),
                                          volt::io::PcbPlacementSvgOptions{.layer_filter = back});
    const auto silk_svg =
        volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library(),
                                          volt::io::PcbPlacementSvgOptions{.layer_filter = silk});

    CHECK(back_svg.find("PCB_LAYER_STACK_SIDE_ORDER_CONFLICT") != std::string::npos);
    CHECK(silk_svg.find("PCB_LAYER_STACK_SIDE_ORDER_CONFLICT") == std::string::npos);
    CHECK(back_svg.find("viewBox=\"0 0 18 21\"") != std::string::npos);
    CHECK(silk_svg.find("viewBox=\"0 0 18 18\"") != std::string::npos);
}

TEST_CASE("PCB SVG writer exposes copper entity references for DRC diagnostics") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Copper DRC"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    board.set_design_rules(volt::BoardDesignRules{0.15, 0.25, 0.30, 0.70, 0.0});
    [[maybe_unused]] const auto track = board.add_track(volt::BoardTrack{
        fixture.first_net,
        front,
        std::vector{volt::BoardPoint{5.0, 5.0}, volt::BoardPoint{12.0, 5.0}},
        0.10,
    });
    [[maybe_unused]] const auto via = board.add_via(
        volt::BoardVia{fixture.first_net, volt::BoardPoint{12.0, 8.0}, front, back, 0.20, 0.50});

    const auto svg = volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library());

    CHECK(svg.find("data-diagnostic-code=\"PCB_TRACK_WIDTH_BELOW_MINIMUM\"") != std::string::npos);
    CHECK(svg.find("data-track=\"board_track:0\"") != std::string::npos);
    CHECK(svg.find("data-diagnostic-code=\"PCB_VIA_DRILL_BELOW_MINIMUM\"") != std::string::npos);
    CHECK(svg.find("data-via=\"board_via:0\"") != std::string::npos);
}

TEST_CASE("PCB SVG writer exposes keepout entity references for DRC diagnostics") {
    auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Keepout DRC"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));
    [[maybe_unused]] const auto keepout = board.add_keepout(volt::BoardKeepout{
        std::vector{
            volt::BoardPoint{4.0, 4.0},
            volt::BoardPoint{8.0, 4.0},
            volt::BoardPoint{8.0, 8.0},
            volt::BoardPoint{4.0, 8.0},
        },
        std::vector{front},
        std::vector{volt::BoardKeepoutRestriction::Copper},
    });
    [[maybe_unused]] const auto track = board.add_track(volt::BoardTrack{
        fixture.first_net,
        front,
        std::vector{volt::BoardPoint{2.0, 6.0}, volt::BoardPoint{10.0, 6.0}},
        0.25,
    });

    const auto svg = volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library());

    CHECK(svg.find("data-diagnostic-code=\"PCB_KEEPOUT_COPPER_VIOLATION\"") != std::string::npos);
    CHECK(svg.find("data-keepout=\"board_keepout:0\"") != std::string::npos);
    CHECK(svg.find("data-track=\"board_track:0\"") != std::string::npos);
}

TEST_CASE("PCB SVG writer omits diagnostic layout when overlays are disabled") {
    auto fixture = make_resistor_circuit(false);
    auto board = make_preview_board(fixture);

    const auto svg = volt::io::write_pcb_placement_svg(
        board, volt::builtin_footprint_library(),
        volt::io::PcbPlacementSvgOptions{.pad_net_overlays = true, .diagnostic_overlays = false});

    CHECK(svg.find("data-diagnostic-code=") == std::string::npos);
    CHECK(svg.find("viewBox=\"0 0 58 38\"") != std::string::npos);
}

TEST_CASE("PCB SVG writer expands bounds to include off-board placements") {
    const auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Control"}};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{50.0, 30.0}));
    [[maybe_unused]] const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{70.0, 15.0}, volt::BoardRotation::degrees(0.0)});

    const auto svg = volt::io::write_pcb_placement_svg(
        board, volt::builtin_footprint_library(),
        volt::io::PcbPlacementSvgOptions{.pad_net_overlays = true, .diagnostic_overlays = false});

    CHECK(svg.find("viewBox=\"0 0 79.65 38\"") != std::string::npos);
    CHECK(svg.find("data-placement=\"component_placement:0\"") != std::string::npos);
}

TEST_CASE("PCB SVG writer uses board-cached footprint definitions") {
    const auto fixture = make_resistor_circuit();
    auto board = make_preview_board(fixture);
    [[maybe_unused]] const auto footprint =
        board.cache_footprint_definition(volt::passive_0603_footprint());
    const auto empty_library = volt::FootprintLibrary{};

    const auto svg = volt::io::write_pcb_placement_svg(board, empty_library);

    CHECK(svg.find("data-pad-projection=\"pcb_pad:0:0\"") != std::string::npos);
    CHECK(svg.find("data-net=\"net:0\"") != std::string::npos);
    CHECK(svg.find("PCB_FOOTPRINT_UNRESOLVED") == std::string::npos);
}
