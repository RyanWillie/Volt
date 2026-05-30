#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <volt/circuit/circuit.hpp>
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
        volt::BoardFeature::mounting_hole("MH1", volt::BoardPoint{3.0, 3.0}, 3.2));
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
