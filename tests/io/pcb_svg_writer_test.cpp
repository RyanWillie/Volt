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
    const auto first_pin_definition =
        circuit.add_pin_definition(volt::PinDefinition{"A", "1", volt::PinRole::Passive});
    const auto second_pin_definition =
        circuit.add_pin_definition(volt::PinDefinition{"B", "2", volt::PinRole::Passive});
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
    const auto first_pin_definition =
        circuit.add_pin_definition(volt::PinDefinition{"A", "1", volt::PinRole::Passive});
    const auto second_pin_definition =
        circuit.add_pin_definition(volt::PinDefinition{"B", "2", volt::PinRole::Passive});
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
    static_cast<void>(
        board.add_feature(volt::BoardFeature::fiducial("FID", volt::BoardPoint{34.0, 4.0}, 1.0)));
    static_cast<void>(board.add_feature(
        volt::BoardFeature::tooling_hole("TH", volt::BoardPoint{4.0, 20.0}, 2.0)));

    const auto svg = volt::io::write_pcb_placement_svg(board, volt::builtin_footprint_library());

    CHECK(svg.find("class=\"board-feature hole\"") != std::string::npos);
    CHECK(svg.find("class=\"board-feature slot\"") != std::string::npos);
    CHECK(svg.find("x1=\"8\" y1=\"4\" x2=\"16\" y2=\"4\" stroke-width=\"1.5\"") !=
          std::string::npos);
    CHECK(svg.find("class=\"board-feature cutout\"") != std::string::npos);
    CHECK(svg.find("points=\"20,4 25,4 25,9 20,9\"") != std::string::npos);
    CHECK(svg.find("class=\"board-feature fiducial top\"") != std::string::npos);
    CHECK(svg.find("class=\"board-feature tooling-hole\"") != std::string::npos);
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
    CHECK(svg.find("<g id=\"pcb-via-0\" class=\"pcb-via\" data-via=\"board_via:0\"") !=
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
