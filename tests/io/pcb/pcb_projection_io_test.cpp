#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>

#include "support/circuit_test_helpers.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/errors.hpp>
#include <volt/io/pcb/pcb_reader.hpp>
#include <volt/io/pcb/pcb_writer.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints/footprints.hpp>

namespace {

struct ResistorCircuit {
    volt::Circuit circuit;
    volt::ComponentDefId component_definition;
    volt::ComponentId component;
    volt::PinDefId first_pin_definition;
    volt::PinDefId second_pin_definition;
    volt::PinId first_pin;
    volt::PinId second_pin;
    volt::NetId first_net;
    volt::NetId second_net;
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
    const auto first_pin =
        volt::queries::pin_by_definition(circuit, component, first_pin_definition).value();
    const auto second_pin =
        volt::queries::pin_by_definition(circuit, component, second_pin_definition).value();
    const auto first_net = circuit.add_net(
        volt::NetSpec{.name = volt::NetName{"LEFT"}, .kind = volt::NetKind::Signal});
    const auto second_net = circuit.add_net(
        volt::NetSpec{.name = volt::NetName{"RIGHT"}, .kind = volt::NetKind::Signal});

    circuit.connect(first_net, first_pin);
    circuit.connect(second_net, second_pin);
    circuit.update(component, volt::SelectPhysicalPart{volt::PhysicalPart{
                                  volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                                  volt::PackageRef{"0603"},
                                  volt::FootprintRef{"passives", "R_0603_1608Metric"},
                                  std::vector{volt::PinPadMapping{first_pin_definition, "1"},
                                              volt::PinPadMapping{second_pin_definition, "2"}},
                              }});

    return ResistorCircuit{std::move(circuit),
                           component_definition,
                           component,
                           first_pin_definition,
                           second_pin_definition,
                           first_pin,
                           second_pin,
                           first_net,
                           second_net};
}

[[nodiscard]] volt::Board make_viewer_ready_board(const ResistorCircuit &fixture) {
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
    return board;
}

[[nodiscard]] nlohmann::json make_board_json(const ResistorCircuit &fixture) {
    const auto board = make_viewer_ready_board(fixture);
    return nlohmann::json::parse(
        volt::io::write_pcb_board(board, volt::builtin_footprint_library()));
}

} // namespace

TEST_CASE("PCB writer exposes declared entity reference helper symbols") {
    CHECK(volt::io::detail::entity_ref_id(volt::EntityRef::board()) == "board:0");
    CHECK(volt::io::detail::entity_ref_id(volt::EntityRef::net(volt::NetId{2})) == "net:2");
}

TEST_CASE("PCB projection writer emits deterministic product-viewer-ready JSON") {
    const auto fixture = make_resistor_circuit();
    const auto board = make_viewer_ready_board(fixture);

    const auto first = volt::io::write_pcb_board(board, volt::builtin_footprint_library());
    const auto second = volt::io::write_pcb_board(board, volt::builtin_footprint_library());
    const auto document = nlohmann::json::parse(first);

    CHECK(first == second);
    CHECK(document["format"] == "volt.pcb");
    CHECK(document["version"] == 3);
    CHECK(document["board"]["id"] == "board:0");
    CHECK(document["board"]["name"] == "Control");
    CHECK(document["board"]["units"] == "mm");
    CHECK(document["board"]["rules"]["copper_clearance_mm"] == 0.15);
    CHECK(document["board"]["rules"]["minimum_track_width_mm"] == 0.15);
    CHECK(document["board"]["rules"]["minimum_via_drill_diameter_mm"] == 0.20);
    CHECK(document["board"]["rules"]["minimum_via_annular_diameter_mm"] == 0.45);
    CHECK(document["board"]["rules"]["board_outline_clearance_mm"] == 0.0);
    CHECK(document["board"]["rules"]["package_assembly_clearance_mm"] == 0.25);
    CHECK_FALSE(document["board"].contains("capability_profile"));

    REQUIRE(document["board"]["layers"].size() == 2);
    CHECK(document["board"]["layers"][0]["id"] == "board_layer:0");
    CHECK(document["board"]["layers"][0]["name"] == "F.Cu");
    CHECK(document["board"]["layers"][0]["role"] == "copper");
    CHECK(document["board"]["layers"][0]["side"] == "top");
    CHECK(document["board"]["layers"][0]["thickness_mm"] == 0.0);
    CHECK(document["board"]["layers"][0]["enabled"] == true);
    CHECK(document["board"]["layer_stack"]["board_thickness_mm"] == 1.6);
    CHECK(document["board"]["layer_stack"]["layers"] ==
          nlohmann::json::array({"board_layer:0", "board_layer:1"}));

    CHECK(document["board"]["outline"]["kind"] == "polygon");
    CHECK(document["board"]["outline"]["vertices"][2] == nlohmann::json::array({50.0, 30.0}));
    CHECK(document["board"]["features"][0]["id"] == "board_feature:0");
    CHECK(document["board"]["features"][0]["kind"] == "hole");
    CHECK(document["board"]["features"][0]["position"] == nlohmann::json::array({3.0, 3.0}));
    CHECK(document["board"]["features"][0]["drill_diameter_mm"] == 3.2);
    CHECK(document["board"]["features"][0]["plated"] == false);
    CHECK(document["board"]["features"][0]["role"] == "mounting");

    REQUIRE(document["board"]["footprint_definitions"].size() == 1);
    CHECK(document["board"]["footprint_definitions"][0]["id"] == "footprint_def:0");
    CHECK(document["board"]["footprint_definitions"][0]["ref"]["library"] == "passives");
    CHECK(document["board"]["footprint_definitions"][0]["pads"][0]["id"] == "footprint_pad:0");
    CHECK(document["board"]["footprint_definitions"][0]["pads"][0]["kind"] == "surface_mount");
    CHECK(document["board"]["footprint_definitions"][0]["pads"][0]["shape"] == "rounded_rectangle");
    CHECK(document["board"]["footprint_definitions"][0]["pads"][0]["position"] ==
          nlohmann::json::array({-0.75, 0.0}));
    CHECK(document["board"]["footprint_definitions"][0]["pads"][0]["size"] ==
          nlohmann::json::array({0.80, 0.95}));
    CHECK(document["board"]["footprint_definitions"][0]["pads"][0]["drill"] == nullptr);
    CHECK(document["board"]["footprint_definitions"][0]["pads"][0]["mechanical_role"] == nullptr);

    REQUIRE(document["board"]["placements"].size() == 1);
    CHECK(document["board"]["placements"][0]["id"] == "component_placement:0");
    CHECK(document["board"]["placements"][0]["component"] == "component:0");
    CHECK(document["board"]["placements"][0]["footprint"] == "footprint_def:0");
    CHECK(document["board"]["placements"][0]["position"] == nlohmann::json::array({25.0, 15.0}));
    CHECK(document["board"]["placements"][0]["rotation_deg"] == 90.0);
    CHECK(document["board"]["placements"][0]["side"] == "top");
    CHECK(document["board"]["placements"][0]["locked"] == true);

    REQUIRE(document["viewer"]["pad_resolutions"].size() == 2);
    CHECK(document["viewer"]["pad_resolutions"][0]["id"] == "pcb_pad:0:0");
    CHECK(document["viewer"]["pad_resolutions"][0]["placement"] == "component_placement:0");
    CHECK(document["viewer"]["pad_resolutions"][0]["component"] == "component:0");
    CHECK(document["viewer"]["pad_resolutions"][0]["footprint"] == "footprint_def:0");
    CHECK(document["viewer"]["pad_resolutions"][0]["pad"] == "footprint_pad:0");
    CHECK(document["viewer"]["pad_resolutions"][0]["label"] == "1");
    CHECK(document["viewer"]["pad_resolutions"][0]["position"] ==
          nlohmann::json::array({25.0, 14.25}));
    CHECK(document["viewer"]["pad_resolutions"][0]["pin"] == "pin:0");
    CHECK(document["viewer"]["pad_resolutions"][0]["net"] == "net:0");
    CHECK(document["viewer"]["pad_resolutions"][0]["status"] == "connected");
    CHECK(document["viewer"]["pad_resolutions"][0]["geometry"]["shape"] == "rounded_rectangle");
    CHECK(document["viewer"]["pad_resolutions"][0]["geometry"]["layers"] ==
          nlohmann::json::array({"front_copper", "front_solder_mask", "front_paste"}));
    REQUIRE(document["viewer"]["layers"].size() == 9);
    CHECK(document["viewer"]["layers"][0]["id"] == "viewer_layer:board_outline");
    CHECK(document["viewer"]["layers"][0]["kind"] == "board_outline");
    CHECK(document["viewer"]["layers"][1]["id"] == "viewer_layer:copper");
    CHECK(document["viewer"]["layers"][2]["id"] == "viewer_layer:pads");
    CHECK(document["viewer"]["layers"][3]["id"] == "viewer_layer:package_bodies");
    CHECK(document["viewer"]["layers"][4]["id"] == "viewer_layer:package_courtyards");
    CHECK(document["viewer"]["layers"][5]["id"] == "viewer_layer:package_fabrication");
    CHECK(document["viewer"]["layers"][6]["id"] == "viewer_layer:package_assembly");
    CHECK(document["viewer"]["layers"][7]["id"] == "viewer_layer:annotations");
    CHECK(document["viewer"]["layers"][8]["id"] == "viewer_layer:diagnostics");
    CHECK(document["viewer"]["diagnostics"] == nlohmann::json::array());
}

TEST_CASE("PCB projection writer and reader round-trip footprint package geometry") {
    const auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Control"}};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{50.0, 30.0}));
    const auto footprint = volt::FootprintDefinition{
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{
            volt::FootprintPad::surface_mount(
                "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{-0.75, 0.0},
                volt::FootprintSize{0.8, 0.95}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "2", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.75, 0.0},
                volt::FootprintSize{0.8, 0.95}, volt::FootprintLayerSet::front_smd()),
        },
        volt::FootprintPackageGeometry{
            volt::FootprintPolygon{std::vector{
                volt::FootprintPoint{-1.2, -0.8},
                volt::FootprintPoint{1.2, -0.8},
                volt::FootprintPoint{1.2, 0.8},
                volt::FootprintPoint{-1.2, 0.8},
            }},
            volt::FootprintPolygon{std::vector{
                volt::FootprintPoint{-0.9, -0.5},
                volt::FootprintPoint{0.9, -0.5},
                volt::FootprintPoint{0.9, 0.5},
                volt::FootprintPoint{-0.9, 0.5},
            }},
            volt::FootprintPolygon{std::vector{
                volt::FootprintPoint{-0.8, -0.4},
                volt::FootprintPoint{0.8, -0.4},
                volt::FootprintPoint{0.8, 0.4},
                volt::FootprintPoint{-0.8, 0.4},
            }},
            volt::FootprintPolygon{std::vector{
                volt::FootprintPoint{-1.0, -0.6},
                volt::FootprintPoint{1.0, -0.6},
                volt::FootprintPoint{1.0, 0.6},
                volt::FootprintPoint{-1.0, 0.6},
            }},
            std::vector{
                volt::FootprintMarking{volt::FootprintMarkingKind::Silkscreen,
                                       volt::FootprintPolygon{std::vector{
                                           volt::FootprintPoint{-1.1, -0.7},
                                           volt::FootprintPoint{1.1, -0.7},
                                           volt::FootprintPoint{1.1, -0.55},
                                           volt::FootprintPoint{-1.1, -0.55},
                                       }}},
                volt::FootprintMarking{volt::FootprintMarkingKind::PinOne,
                                       volt::FootprintPolygon{std::vector{
                                           volt::FootprintPoint{-1.0, -0.5},
                                           volt::FootprintPoint{-0.85, -0.5},
                                           volt::FootprintPoint{-1.0, -0.35},
                                       }}},
            },
        },
    };
    static_cast<void>(board.cache_footprint_definition(footprint));
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{25.0, 15.0}, volt::BoardRotation::degrees(0.0)}));

    const auto empty_library = volt::FootprintLibrary{};
    const auto text = volt::io::write_pcb_board(board, empty_library);
    const auto document = nlohmann::json::parse(text);
    const auto &definition = document["board"]["footprint_definitions"][0];

    CHECK(definition["courtyard"] ==
          nlohmann::json::array(
              {nlohmann::json::array({-1.2, -0.8}), nlohmann::json::array({1.2, -0.8}),
               nlohmann::json::array({1.2, 0.8}), nlohmann::json::array({-1.2, 0.8})}));
    CHECK(definition["body"] ==
          nlohmann::json::array(
              {nlohmann::json::array({-0.9, -0.5}), nlohmann::json::array({0.9, -0.5}),
               nlohmann::json::array({0.9, 0.5}), nlohmann::json::array({-0.9, 0.5})}));
    CHECK(definition["fabrication_outline"] ==
          nlohmann::json::array(
              {nlohmann::json::array({-0.8, -0.4}), nlohmann::json::array({0.8, -0.4}),
               nlohmann::json::array({0.8, 0.4}), nlohmann::json::array({-0.8, 0.4})}));
    CHECK(definition["assembly_outline"] ==
          nlohmann::json::array(
              {nlohmann::json::array({-1.0, -0.6}), nlohmann::json::array({1.0, -0.6}),
               nlohmann::json::array({1.0, 0.6}), nlohmann::json::array({-1.0, 0.6})}));
    REQUIRE(definition["markings"].size() == 2);
    CHECK(definition["markings"][0]["id"] == "footprint_marking:0");
    CHECK(definition["markings"][0]["kind"] == "silkscreen");
    CHECK(definition["markings"][0]["polygon"][1] == nlohmann::json::array({1.1, -0.7}));
    CHECK(definition["markings"][1]["id"] == "footprint_marking:1");
    CHECK(definition["markings"][1]["kind"] == "pin_1");
    CHECK(definition["markings"][1]["polygon"][2] == nlohmann::json::array({-1.0, -0.35}));

    const auto restored = volt::io::read_pcb_board_text(fixture.circuit, text);

    CHECK(volt::io::write_pcb_board(restored, empty_library) == text);
    REQUIRE(restored.footprint_definition(volt::FootprintDefId{0}).courtyard().has_value());
    CHECK(restored.footprint_definition(volt::FootprintDefId{0}).courtyard()->vertices()[1] ==
          volt::FootprintPoint{1.2, -0.8});
    REQUIRE(
        restored.footprint_definition(volt::FootprintDefId{0}).fabrication_outline().has_value());
    CHECK(restored.footprint_definition(volt::FootprintDefId{0})
              .fabrication_outline()
              ->vertices()[2] == volt::FootprintPoint{0.8, 0.4});
    REQUIRE(restored.footprint_definition(volt::FootprintDefId{0}).markings().size() == 2);
    CHECK(restored.footprint_definition(volt::FootprintDefId{0}).markings()[1].kind() ==
          volt::FootprintMarkingKind::PinOne);

    auto closed_courtyard = document;
    closed_courtyard["board"]["footprint_definitions"][0]["courtyard"].push_back(
        closed_courtyard["board"]["footprint_definitions"][0]["courtyard"][0]);
    CHECK_THROWS_AS(volt::io::read_pcb_board_text(fixture.circuit, closed_courtyard.dump()),
                    std::invalid_argument);

    auto closed_marking = document;
    closed_marking["board"]["footprint_definitions"][0]["markings"][1]["polygon"].push_back(
        closed_marking["board"]["footprint_definitions"][0]["markings"][1]["polygon"][0]);
    CHECK_THROWS_AS(volt::io::read_pcb_board_text(fixture.circuit, closed_marking.dump()),
                    std::invalid_argument);

    auto non_sequential_marking = document;
    non_sequential_marking["board"]["footprint_definitions"][0]["markings"][1]["id"] =
        "footprint_marking:4";
    CHECK_THROWS_AS(volt::io::read_pcb_board_text(fixture.circuit, non_sequential_marking.dump()),
                    std::logic_error);
}

TEST_CASE("PCB projection writer and reader round-trip generic board features") {
    const auto fixture = make_resistor_circuit();
    auto board = make_viewer_ready_board(fixture);
    static_cast<void>(board.add_feature(
        volt::BoardFeature::hole("DRILL", volt::BoardPoint{36.0, 4.0}, 1.0, false, "fixture")));
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
        volt::BoardFeature::hole("TH", volt::BoardPoint{4.0, 24.0}, 2.0, false, "tooling")));

    const auto text = volt::io::write_pcb_board(board, volt::builtin_footprint_library());
    const auto document = nlohmann::json::parse(text);

    REQUIRE(document["board"]["features"].size() == 6);
    CHECK(document["board"]["features"][1]["kind"] == "hole");
    CHECK(document["board"]["features"][1]["role"] == "fixture");
    CHECK(document["board"]["features"][2]["kind"] == "slot");
    CHECK(document["board"]["features"][2]["start"] == nlohmann::json::array({8.0, 4.0}));
    CHECK(document["board"]["features"][2]["end"] == nlohmann::json::array({16.0, 4.0}));
    CHECK(document["board"]["features"][2]["width_mm"] == 1.5);
    CHECK(document["board"]["features"][3]["kind"] == "cutout");
    CHECK(document["board"]["features"][3]["role"] == "access");
    CHECK(document["board"]["features"][4]["kind"] == "circle");
    CHECK(document["board"]["features"][4]["side"] == "top");
    CHECK(document["board"]["features"][4]["role"] == "fiducial");
    CHECK(document["board"]["features"][5]["kind"] == "hole");
    CHECK(document["board"]["features"][5]["role"] == "tooling");

    const auto restored = volt::io::read_pcb_board_text(fixture.circuit, text);
    CHECK(volt::io::write_pcb_board(restored, volt::builtin_footprint_library()) == text);
    CHECK(restored.feature(volt::BoardFeatureId{1}).hole().drill_diameter_mm() == 1.0);
    CHECK(restored.feature(volt::BoardFeatureId{2}).slot().width_mm() == 1.5);
    CHECK(restored.feature(volt::BoardFeatureId{3}).cutout().outline().size() == 4);
    CHECK(restored.feature(volt::BoardFeatureId{4}).circle().diameter_mm() == 1.0);
    CHECK(restored.feature(volt::BoardFeatureId{5}).hole().drill_diameter_mm() == 2.0);
}

TEST_CASE("PCB projection JSON emits deterministic bare-board 3D geometry") {
    const auto fixture = make_resistor_circuit();
    auto board = make_viewer_ready_board(fixture);
    static_cast<void>(board.add_feature(volt::BoardFeature::slot(
        "SLOT", volt::BoardPoint{8.0, 4.0}, volt::BoardPoint{16.0, 4.0}, 1.5, true, "mounting")));
    static_cast<void>(board.add_feature(volt::BoardFeature::cutout(
        "CUT",
        std::vector{volt::BoardPoint{20.0, 4.0}, volt::BoardPoint{25.0, 4.0},
                    volt::BoardPoint{25.0, 9.0}, volt::BoardPoint{20.0, 9.0}},
        "access")));
    static_cast<void>(board.add_feature(volt::BoardFeature::circle(
        "FID", volt::BoardPoint{34.0, 4.0}, 1.0, volt::BoardSide::Top, "fiducial")));

    const auto first = volt::io::write_pcb_board(board, volt::builtin_footprint_library());
    const auto second = volt::io::write_pcb_board(board, volt::builtin_footprint_library());
    const auto document = nlohmann::json::parse(first);

    REQUIRE(document["board"].contains("geometry"));
    const auto &geometry = document["board"]["geometry"];

    CHECK(first == second);
    CHECK(geometry["units"] == "mm");
    CHECK(geometry["thickness_mm"] == 1.6);
    CHECK(geometry["outline"]["kind"] == "polygon");
    CHECK(geometry["outline"]["vertices"][2] == nlohmann::json::array({50.0, 30.0}));

    REQUIRE(geometry["stackup"].size() == 2);
    CHECK(geometry["stackup"][0]["layer"] == "board_layer:0");
    CHECK(geometry["stackup"][0]["name"] == "F.Cu");
    CHECK(geometry["stackup"][0]["role"] == "copper");
    CHECK(geometry["stackup"][0]["side"] == "top");
    CHECK(geometry["stackup"][0]["z_mm"] == 0.8);
    CHECK(geometry["stackup"][1]["layer"] == "board_layer:1");
    CHECK(geometry["stackup"][1]["side"] == "bottom");
    CHECK(geometry["stackup"][1]["z_mm"] == -0.8);

    REQUIRE(geometry["openings"].size() == 2);
    CHECK(geometry["openings"][0]["id"] == "board_feature:0");
    CHECK(geometry["openings"][0]["kind"] == "hole");
    CHECK(geometry["openings"][0]["center"] == nlohmann::json::array({3.0, 3.0}));
    CHECK(geometry["openings"][0]["drill_diameter_mm"] == 3.2);
    CHECK(geometry["openings"][0]["finished_diameter_mm"] == nullptr);
    CHECK(geometry["openings"][0]["plated"] == false);
    CHECK(geometry["openings"][0]["side"] == "through_board");
    CHECK(geometry["openings"][1]["kind"] == "slot");
    CHECK(geometry["openings"][1]["start"] == nlohmann::json::array({8.0, 4.0}));
    CHECK(geometry["openings"][1]["end"] == nlohmann::json::array({16.0, 4.0}));
    CHECK(geometry["openings"][1]["width_mm"] == 1.5);
    CHECK(geometry["openings"][1]["plated"] == true);
    CHECK(geometry["openings"][1]["side"] == "through_board");

    REQUIRE(geometry["cutouts"].size() == 1);
    CHECK(geometry["cutouts"][0]["id"] == "board_feature:2");
    CHECK(geometry["cutouts"][0]["kind"] == "cutout");
    CHECK(geometry["cutouts"][0]["outline"][2] == nlohmann::json::array({25.0, 9.0}));
    CHECK(geometry["cutouts"][0]["side"] == "through_board");

    REQUIRE(geometry["surface_features"].size() == 1);
    CHECK(geometry["surface_features"][0]["id"] == "board_feature:3");
    CHECK(geometry["surface_features"][0]["kind"] == "circle");
    CHECK(geometry["surface_features"][0]["center"] == nlohmann::json::array({34.0, 4.0}));
    CHECK(geometry["surface_features"][0]["diameter_mm"] == 1.0);
    CHECK(geometry["surface_features"][0]["side"] == "top");

    const auto restored = volt::io::read_pcb_board_text(fixture.circuit, first);
    CHECK(volt::io::write_pcb_board(restored, volt::builtin_footprint_library()) == first);
}

TEST_CASE("PCB projection reader recomputes bare-board geometry from canonical fields") {
    const auto fixture = make_resistor_circuit();
    auto document = make_board_json(fixture);
    document["board"].erase("geometry");

    const auto restored = volt::io::read_pcb_board_text(fixture.circuit, document.dump());
    const auto rewritten = nlohmann::json::parse(
        volt::io::write_pcb_board(restored, volt::builtin_footprint_library()));

    REQUIRE(rewritten["board"].contains("geometry"));
    CHECK(rewritten["board"]["geometry"]["thickness_mm"] == 1.6);
    CHECK(rewritten["board"]["geometry"]["openings"][0]["id"] == "board_feature:0");
    CHECK(rewritten["board"]["geometry"]["stackup"][0]["z_mm"] == 0.8);
}

TEST_CASE("PCB projection JSON contains renderer-ready geometry without SVG") {
    const auto fixture = make_resistor_circuit();
    const auto document = make_board_json(fixture);

    REQUIRE(document["board"]["outline"]["vertices"].size() == 4);
    CHECK(document["board"]["layer_stack"]["board_thickness_mm"] == 1.6);

    const auto &placement = document["board"]["placements"][0];
    const auto &footprint = document["board"]["footprint_definitions"][0];
    const auto &pad = footprint["pads"][0];
    const auto &pad_projection = document["viewer"]["pad_resolutions"][0];

    CHECK(placement["footprint"] == footprint["id"]);
    CHECK(pad_projection["placement"] == placement["id"]);
    CHECK(pad_projection["footprint"] == footprint["id"]);
    CHECK(pad_projection["pad"] == pad["id"]);

    CHECK(pad["shape"] == "rounded_rectangle");
    CHECK(pad["position"] == nlohmann::json::array({-0.75, 0.0}));
    CHECK(pad["size"] == nlohmann::json::array({0.80, 0.95}));
    CHECK(pad["layers"] ==
          nlohmann::json::array({"front_copper", "front_solder_mask", "front_paste"}));

    const auto placement_x = placement["position"][0].get<double>();
    const auto placement_y = placement["position"][1].get<double>();
    const auto pad_x = pad["position"][0].get<double>();
    const auto pad_y = pad["position"][1].get<double>();
    CHECK(placement["rotation_deg"] == 90.0);
    CHECK(placement["side"] == "top");
    CHECK(pad_projection["position"] ==
          nlohmann::json::array({placement_x - pad_y, placement_y + pad_x}));
    CHECK(pad_projection["pin"] == "pin:0");
    CHECK(pad_projection["net"] == "net:0");
}

TEST_CASE("PCB projection reader round-trips board metadata, placements, and pad geometry") {
    const auto fixture = make_resistor_circuit();
    const auto text = volt::io::write_pcb_board(make_viewer_ready_board(fixture),
                                                volt::builtin_footprint_library());

    const auto restored = volt::io::read_pcb_board_text(fixture.circuit, text);
    auto input = std::istringstream{text};
    const auto stream_restored = volt::io::read_pcb_board(fixture.circuit, input);
    const auto rewritten = volt::io::write_pcb_board(restored, volt::builtin_footprint_library());

    CHECK(rewritten == text);
    CHECK(volt::io::write_pcb_board(stream_restored, volt::builtin_footprint_library()) == text);
    CHECK(restored.name() == volt::BoardName{"Control"});
    REQUIRE(restored.layer_stack().has_value());
    CHECK(restored.layer_stack()->board_thickness_mm() == 1.6);
    REQUIRE(restored.outline().has_value());
    CHECK(restored.outline()->vertices()[2] == volt::BoardPoint{50.0, 30.0});
    CHECK(restored.feature(volt::BoardFeatureId{0}).hole().drill_diameter_mm() == 3.2);
    CHECK(restored.footprint_definition(volt::FootprintDefId{0})
              .pad(volt::FootprintPadId{0})
              .size() == volt::FootprintSize{0.80, 0.95});
    CHECK(restored.placement(volt::ComponentPlacementId{0}).component() == fixture.component);
}

TEST_CASE("PCB projection writer and reader round-trip copper tracks and vias") {
    const auto fixture = make_resistor_circuit();
    auto board = make_viewer_ready_board(fixture);

    [[maybe_unused]] const auto track = board.add_track(volt::BoardTrack{
        fixture.first_net,
        volt::BoardLayerId{0},
        std::vector{
            volt::BoardPoint{5.0, 5.0},
            volt::BoardPoint{12.0, 5.0},
            volt::BoardPoint{12.0, 8.0},
        },
        0.25,
    });
    [[maybe_unused]] const auto via =
        board.add_via(volt::BoardVia{fixture.first_net, volt::BoardPoint{12.0, 8.0},
                                     volt::BoardLayerId{0}, volt::BoardLayerId{1}, 0.30, 0.70});

    const auto text = volt::io::write_pcb_board(board, volt::builtin_footprint_library());
    const auto document = nlohmann::json::parse(text);

    REQUIRE(document["board"]["tracks"].size() == 1);
    CHECK(document["board"]["tracks"][0]["id"] == "board_track:0");
    CHECK(document["board"]["tracks"][0]["net"] == "net:0");
    CHECK(document["board"]["tracks"][0]["layer"] == "board_layer:0");
    CHECK(document["board"]["tracks"][0]["points"] ==
          nlohmann::json::array({nlohmann::json::array({5.0, 5.0}),
                                 nlohmann::json::array({12.0, 5.0}),
                                 nlohmann::json::array({12.0, 8.0})}));
    CHECK(document["board"]["tracks"][0]["width_mm"] == 0.25);

    REQUIRE(document["board"]["vias"].size() == 1);
    CHECK(document["board"]["vias"][0]["id"] == "board_via:0");
    CHECK(document["board"]["vias"][0]["net"] == "net:0");
    CHECK(document["board"]["vias"][0]["position"] == nlohmann::json::array({12.0, 8.0}));
    CHECK(document["board"]["vias"][0]["start_layer"] == "board_layer:0");
    CHECK(document["board"]["vias"][0]["end_layer"] == "board_layer:1");
    CHECK(document["board"]["vias"][0]["drill_diameter_mm"] == 0.30);
    CHECK(document["board"]["vias"][0]["annular_diameter_mm"] == 0.70);

    const auto restored = volt::io::read_pcb_board_text(fixture.circuit, text);
    CHECK(volt::io::write_pcb_board(restored, volt::builtin_footprint_library()) == text);
    CHECK(restored.track(volt::BoardTrackId{0}).points()[2] == volt::BoardPoint{12.0, 8.0});
    CHECK(restored.via(volt::BoardViaId{0}).end_layer() == volt::BoardLayerId{1});
}

TEST_CASE("PCB projection writer and reader round-trip zones, keepouts, rooms, and board text") {
    const auto fixture = make_resistor_circuit();
    auto board = make_viewer_ready_board(fixture);

    [[maybe_unused]] const auto zone = board.add_zone(volt::BoardZone{
        std::vector{
            volt::BoardPoint{2.0, 2.0},
            volt::BoardPoint{12.0, 2.0},
            volt::BoardPoint{12.0, 8.0},
            volt::BoardPoint{2.0, 8.0},
        },
        std::vector{volt::BoardLayerId{0}},
        fixture.first_net,
        volt::BoardZoneFill::Solid,
        5,
    });
    [[maybe_unused]] const auto keepout = board.add_keepout(volt::BoardKeepout{
        std::vector{
            volt::BoardPoint{15.0, 2.0},
            volt::BoardPoint{18.0, 2.0},
            volt::BoardPoint{18.0, 6.0},
            volt::BoardPoint{15.0, 6.0},
        },
        std::vector{volt::BoardLayerId{0}, volt::BoardLayerId{1}},
        std::vector{volt::BoardKeepoutRestriction::Copper,
                    volt::BoardKeepoutRestriction::Placement},
    });
    auto escape_room = volt::BoardRoom{
        "BGA escape",
        volt::BoardOutline::rectangle(volt::BoardPoint{3.0, 10.0}, volt::BoardSize{8.0, 5.0}),
        std::vector{volt::BoardLayerId{0}},
        4,
    };
    escape_room.set_copper_clearance_mm(0.075);
    escape_room.set_track_width_mm(0.10);
    [[maybe_unused]] const auto room = board.add_room(std::move(escape_room));
    [[maybe_unused]] const auto room_without_overrides = board.add_room(volt::BoardRoom{
        "Mechanical moat",
        volt::BoardOutline::rectangle(volt::BoardPoint{20.0, 10.0}, volt::BoardSize{4.0, 4.0}),
        std::vector{volt::BoardLayerId{0}, volt::BoardLayerId{1}},
    });
    [[maybe_unused]] const auto text = board.add_text(
        volt::BoardText{"REV A", volt::BoardPoint{5.0, 24.0}, volt::BoardRotation::degrees(90.0),
                        volt::BoardLayerId{0}, 1.2, true});

    const auto text_json = volt::io::write_pcb_board(board, volt::builtin_footprint_library());
    const auto document = nlohmann::json::parse(text_json);

    REQUIRE(document["board"]["zones"].size() == 1);
    CHECK(document["board"]["zones"][0]["id"] == "board_zone:0");
    CHECK(document["board"]["zones"][0]["layers"] == nlohmann::json::array({"board_layer:0"}));
    CHECK(document["board"]["zones"][0]["net"] == "net:0");
    CHECK(document["board"]["zones"][0]["fill"] == "solid");
    CHECK(document["board"]["zones"][0]["priority"] == 5);
    CHECK(document["board"]["zones"][0]["outline"] ==
          nlohmann::json::array(
              {nlohmann::json::array({2.0, 2.0}), nlohmann::json::array({12.0, 2.0}),
               nlohmann::json::array({12.0, 8.0}), nlohmann::json::array({2.0, 8.0})}));

    REQUIRE(document["board"]["features"].size() == 1);
    REQUIRE(document["board"]["keepouts"].size() == 1);
    CHECK(document["board"]["keepouts"][0]["id"] == "board_keepout:0");
    CHECK(document["board"]["keepouts"][0]["layers"] ==
          nlohmann::json::array({"board_layer:0", "board_layer:1"}));
    CHECK(document["board"]["keepouts"][0]["restrictions"] ==
          nlohmann::json::array({"copper", "placement"}));

    REQUIRE(document["board"]["rooms"].size() == 2);
    CHECK(document["board"]["rooms"][0]["id"] == "board_room:0");
    CHECK(document["board"]["rooms"][0]["name"] == "BGA escape");
    CHECK(document["board"]["rooms"][0]["outline"] ==
          nlohmann::json::array(
              {nlohmann::json::array({3.0, 10.0}), nlohmann::json::array({11.0, 10.0}),
               nlohmann::json::array({11.0, 15.0}), nlohmann::json::array({3.0, 15.0})}));
    CHECK(document["board"]["rooms"][0]["layers"] == nlohmann::json::array({"board_layer:0"}));
    CHECK(document["board"]["rooms"][0]["priority"] == 4);
    CHECK(document["board"]["rooms"][0]["copper_clearance_mm"] == 0.075);
    CHECK(document["board"]["rooms"][0]["track_width_mm"] == 0.10);
    CHECK(document["board"]["rooms"][1]["id"] == "board_room:1");
    CHECK(document["board"]["rooms"][1]["name"] == "Mechanical moat");
    CHECK(document["board"]["rooms"][1].contains("priority") == false);
    CHECK(document["board"]["rooms"][1].contains("copper_clearance_mm") == false);
    CHECK(document["board"]["rooms"][1].contains("track_width_mm") == false);

    REQUIRE(document["board"]["texts"].size() == 1);
    CHECK(document["board"]["texts"][0]["id"] == "board_text:0");
    CHECK(document["board"]["texts"][0]["text"] == "REV A");
    CHECK(document["board"]["texts"][0]["position"] == nlohmann::json::array({5.0, 24.0}));
    CHECK(document["board"]["texts"][0]["rotation_deg"] == 90.0);
    CHECK(document["board"]["texts"][0]["layer"] == "board_layer:0");
    CHECK(document["board"]["texts"][0]["size_mm"] == 1.2);
    CHECK(document["board"]["texts"][0]["locked"] == true);

    const auto restored = volt::io::read_pcb_board_text(fixture.circuit, text_json);
    CHECK(volt::io::write_pcb_board(restored, volt::builtin_footprint_library()) == text_json);
    CHECK(restored.zone(volt::BoardZoneId{0}).priority() == 5);
    CHECK(restored.keepout(volt::BoardKeepoutId{0}).restrictions() ==
          std::vector{volt::BoardKeepoutRestriction::Copper,
                      volt::BoardKeepoutRestriction::Placement});
    CHECK(restored.room(volt::BoardRoomId{0}).name() == "BGA escape");
    REQUIRE(restored.room(volt::BoardRoomId{0}).copper_clearance_mm().has_value());
    REQUIRE(restored.room(volt::BoardRoomId{0}).track_width_mm().has_value());
    CHECK(restored.room(volt::BoardRoomId{0}).copper_clearance_mm().value() == 0.075);
    CHECK(restored.room(volt::BoardRoomId{0}).track_width_mm().value() == 0.10);
    CHECK(restored.room(volt::BoardRoomId{1}).priority() == 0);
    CHECK_FALSE(restored.room(volt::BoardRoomId{1}).track_width_mm().has_value());
    CHECK(restored.text(volt::BoardTextId{0}).text() == "REV A");
}

TEST_CASE("PCB projection writer and reader round-trip board design rules") {
    const auto fixture = make_resistor_circuit();
    auto board = make_viewer_ready_board(fixture);
    board.set_design_rules(volt::BoardDesignRules{0.20, 0.25, 0.30, 0.70, 0.10, 0.35});
    [[maybe_unused]] const auto track = board.add_track(volt::BoardTrack{
        fixture.first_net,
        volt::BoardLayerId{0},
        std::vector{volt::BoardPoint{5.0, 5.0}, volt::BoardPoint{12.0, 5.0}},
        0.10,
    });

    const auto text = volt::io::write_pcb_board(board, volt::builtin_footprint_library());
    const auto document = nlohmann::json::parse(text);

    CHECK(document["board"]["rules"]["copper_clearance_mm"] == 0.20);
    CHECK(document["board"]["rules"]["minimum_track_width_mm"] == 0.25);
    CHECK(document["board"]["rules"]["minimum_via_drill_diameter_mm"] == 0.30);
    CHECK(document["board"]["rules"]["minimum_via_annular_diameter_mm"] == 0.70);
    CHECK(document["board"]["rules"]["board_outline_clearance_mm"] == 0.10);
    CHECK(document["board"]["rules"]["package_assembly_clearance_mm"] == 0.35);
    REQUIRE_FALSE(document["viewer"]["diagnostics"].empty());
    const auto &track_width = document["viewer"]["diagnostics"][0];
    CHECK(track_width["code"] == "PCB_TRACK_WIDTH_BELOW_MINIMUM");
    CHECK(track_width["entities"] ==
          nlohmann::json::array({"board_track:0", "net:0", "board_layer:0"}));
    REQUIRE(track_width["overlays"].size() == 1);
    CHECK(track_width["overlays"][0]["kind"] == "segment");
    CHECK(track_width["overlays"][0]["points"] ==
          nlohmann::json::array(
              {nlohmann::json::array({5.0, 5.0}), nlohmann::json::array({12.0, 5.0})}));
    CHECK(track_width["overlays"][0]["entities"] == nlohmann::json::array({"board_track:0"}));
    CHECK(track_width["overlays"][0]["layers"] == nlohmann::json::array({"board_layer:0"}));
    CHECK(track_width["measurement"] ==
          nlohmann::json::object({{"actual_mm", 0.10}, {"required_mm", 0.25}}));

    const auto restored = volt::io::read_pcb_board_text(fixture.circuit, text);
    CHECK(restored.design_rules().copper_clearance_mm() == 0.20);
    CHECK(restored.design_rules().minimum_track_width_mm() == 0.25);
    CHECK(volt::io::write_pcb_board(restored, volt::builtin_footprint_library()) == text);
}

TEST_CASE("PCB projection writer and reader round-trip embedded capability profiles") {
    const auto fixture = make_resistor_circuit();
    auto board = make_viewer_ready_board(fixture);
    board.set_capability_profile(volt::BoardCapabilityProfile::conservative_default());

    const auto text = volt::io::write_pcb_board(board, volt::builtin_footprint_library());
    const auto document = nlohmann::json::parse(text);

    REQUIRE(document["board"].contains("capability_profile"));
    const auto &profile = document["board"]["capability_profile"];
    CHECK(profile["name"] == "volt.conservative");
    CHECK(profile["provenance"]["source"] == "Volt built-in conservative fallback");
    CHECK(profile["provenance"]["as_of"] == "2026-06-11");
    CHECK(profile["minimum_track_width_mm"] == 0.20);
    CHECK(profile["minimum_via_drill_mm"] == 0.30);
    CHECK(profile["minimum_via_annular_mm"] == 0.60);
    CHECK(profile["minimum_clearances"] ==
          nlohmann::json::array(
              {{{"first", "track"}, {"second", "track"}, {"clearance_mm", 0.2}},
               {{"first", "track"}, {"second", "pad"}, {"clearance_mm", 0.2}},
               {{"first", "track"}, {"second", "via"}, {"clearance_mm", 0.2}},
               {{"first", "track"}, {"second", "zone"}, {"clearance_mm", 0.2}},
               {{"first", "track"}, {"second", "board_edge"}, {"clearance_mm", 0.3}},
               {{"first", "pad"}, {"second", "pad"}, {"clearance_mm", 0.2}},
               {{"first", "pad"}, {"second", "via"}, {"clearance_mm", 0.2}},
               {{"first", "pad"}, {"second", "zone"}, {"clearance_mm", 0.2}},
               {{"first", "pad"}, {"second", "board_edge"}, {"clearance_mm", 0.3}},
               {{"first", "via"}, {"second", "via"}, {"clearance_mm", 0.2}},
               {{"first", "via"}, {"second", "zone"}, {"clearance_mm", 0.2}},
               {{"first", "via"}, {"second", "board_edge"}, {"clearance_mm", 0.3}},
               {{"first", "zone"}, {"second", "zone"}, {"clearance_mm", 0.2}},
               {{"first", "zone"}, {"second", "board_edge"}, {"clearance_mm", 0.3}}}));

    const auto restored = volt::io::read_pcb_board_text(fixture.circuit, text);
    REQUIRE(restored.capability_profile().has_value());
    CHECK(restored.capability_profile()->name() == "volt.conservative");
    CHECK(volt::io::write_pcb_board(restored, volt::builtin_footprint_library()) == text);
}

TEST_CASE("PCB projection reader defaults missing legacy board design rules") {
    const auto fixture = make_resistor_circuit();
    auto document = make_board_json(fixture);
    document["board"].erase("rules");

    const auto restored = volt::io::read_pcb_board_text(fixture.circuit, document.dump());

    CHECK(restored.design_rules().copper_clearance_mm() == 0.15);
    CHECK(restored.design_rules().minimum_track_width_mm() == 0.15);
    CHECK(restored.design_rules().minimum_via_drill_diameter_mm() == 0.20);
    CHECK(restored.design_rules().minimum_via_annular_diameter_mm() == 0.45);
    CHECK(restored.design_rules().board_outline_clearance_mm() == 0.0);
    CHECK(restored.design_rules().package_assembly_clearance_mm() == 0.25);
}

TEST_CASE("PCB projection reader rejects malformed embedded capability profiles") {
    const auto fixture = make_resistor_circuit();
    auto board = make_viewer_ready_board(fixture);
    board.set_capability_profile(volt::BoardCapabilityProfile::conservative_default());
    const auto text = volt::io::write_pcb_board(board, volt::builtin_footprint_library());

    auto missing_provenance = nlohmann::json::parse(text);
    missing_provenance["board"]["capability_profile"].erase("provenance");
    CHECK_THROWS_AS(volt::io::read_pcb_board_text(fixture.circuit, missing_provenance.dump()),
                    std::logic_error);

    auto negative_value = nlohmann::json::parse(text);
    negative_value["board"]["capability_profile"]["minimum_via_drill_mm"] = -0.1;
    CHECK_THROWS_AS(volt::io::read_pcb_board_text(fixture.circuit, negative_value.dump()),
                    std::invalid_argument);
}

TEST_CASE("PCB projection writer includes highlightable diagnostic references") {
    const auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Control"}};
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{5.0, 5.0}));
    [[maybe_unused]] const auto footprint =
        board.cache_footprint_definition(volt::passive_0603_footprint());
    [[maybe_unused]] const auto placement = board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});

    const auto document =
        nlohmann::json::parse(volt::io::write_pcb_board(board, volt::builtin_footprint_library()));

    REQUIRE_FALSE(document["viewer"]["diagnostics"].empty());
    CHECK(document["viewer"]["diagnostics"][0]["severity"] == "error");
    CHECK(document["viewer"]["diagnostics"][0]["code"] == "PCB_PLACEMENT_OUTSIDE_OUTLINE");
    CHECK(document["viewer"]["diagnostics"][0]["message"] ==
          "Placement pad '1' is outside the board outline");
    CHECK(document["viewer"]["diagnostics"][0]["entities"] ==
          nlohmann::json::array({"component:0", "component_placement:0"}));
}

TEST_CASE("PCB projection writer serializes overlay-ready diagnostic geometry") {
    const auto diagnostic = volt::Diagnostic{
        volt::Severity::Warning,
        volt::DiagnosticCode{"PCB_VISUAL_REFERENCE_DESIGNATOR_UNREADABLE"},
        volt::DiagnosticCategory{"pcb.visual"},
        "Reference designator is difficult to read",
        std::vector{volt::EntityRef::board(), volt::EntityRef::board_room(volt::BoardRoomId{1}),
                    volt::EntityRef::board_text(volt::BoardTextId{0})},
        std::vector{
            volt::DiagnosticOverlay::bounding_box(
                volt::DiagnosticPoint{2.0, 3.0}, volt::DiagnosticPoint{6.0, 4.5},
                std::vector{volt::EntityRef::board_text(volt::BoardTextId{0})},
                std::vector{volt::BoardLayerId{0}}),
            volt::DiagnosticOverlay::point(volt::DiagnosticPoint{4.0, 3.75}, {},
                                           std::vector{volt::BoardLayerId{0}}),
            volt::DiagnosticOverlay::polygon(std::vector{volt::DiagnosticPoint{1.0, 1.0},
                                                         volt::DiagnosticPoint{2.0, 1.0},
                                                         volt::DiagnosticPoint{2.0, 2.0}},
                                             {}, std::vector{volt::BoardLayerId{0}}),
        },
        std::nullopt,
        "reference-designator-obstruction",
    };

    auto out = std::ostringstream{};
    volt::io::detail::write_diagnostic(out, diagnostic);
    const auto payload = nlohmann::json::parse(out.str());

    CHECK(payload["category"] == "pcb.visual");
    CHECK(payload["entities"] ==
          nlohmann::json::array({"board:0", "board_room:1", "board_text:0"}));
    REQUIRE(payload["overlays"].size() == 3);
    CHECK(payload["overlays"][0]["kind"] == "bounding_box");
    CHECK(payload["overlays"][0]["points"] ==
          nlohmann::json::array(
              {nlohmann::json::array({2.0, 3.0}), nlohmann::json::array({6.0, 4.5})}));
    CHECK(payload["overlays"][0]["entities"] == nlohmann::json::array({"board_text:0"}));
    CHECK(payload["overlays"][0]["layers"] == nlohmann::json::array({"board_layer:0"}));
    CHECK(payload["overlays"][1]["kind"] == "point");
    CHECK(payload["overlays"][2]["kind"] == "polygon");
    CHECK(payload["rule"] == "reference-designator-obstruction");
}

TEST_CASE("PCB projection writer serializes emitted PCB visual placement diagnostics") {
    auto fixture = make_resistor_circuit();
    const auto second_component = fixture.circuit.instantiate_component(
        fixture.component_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R2"}});
    fixture.circuit.update(
        second_component,
        volt::SelectPhysicalPart{volt::PhysicalPart{
            volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"}, volt::PackageRef{"0603"},
            volt::FootprintRef{"passives", "R_0603_1608Metric"},
            std::vector{
                volt::PinPadMapping{fixture.first_pin_definition, "1"},
                volt::PinPadMapping{fixture.second_pin_definition, "2"},
            }}});

    auto board = volt::Board{fixture.circuit, volt::BoardName{"Control"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{50.0, 30.0}));
    [[maybe_unused]] const auto first = board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    [[maybe_unused]] const auto second = board.place_component(volt::ComponentPlacement{
        second_component, volt::BoardPoint{10.5, 10.0}, volt::BoardRotation::degrees(0.0)});

    const auto document =
        nlohmann::json::parse(volt::io::write_pcb_board(board, volt::builtin_footprint_library()));

    const auto diagnostic = std::find_if(
        document["viewer"]["diagnostics"].begin(), document["viewer"]["diagnostics"].end(),
        [](const nlohmann::json &item) { return item["code"] == "PCB_VISUAL_PLACEMENT_OVERLAP"; });
    REQUIRE(diagnostic != document["viewer"]["diagnostics"].end());
    CHECK((*diagnostic)["severity"] == "warning");
    CHECK((*diagnostic)["category"] == "pcb.visual");
    CHECK((*diagnostic)["code"] == "PCB_VISUAL_PLACEMENT_OVERLAP");
    CHECK((*diagnostic)["message"] == "Placed footprint extents for R1 and R2 overlap");
    CHECK((*diagnostic)["entities"] ==
          nlohmann::json::array(
              {"component:0", "component_placement:0", "component:1", "component_placement:1"}));
    REQUIRE((*diagnostic)["overlays"].size() == 2);
    CHECK((*diagnostic)["overlays"][0]["kind"] == "bounding_box");
    CHECK((*diagnostic)["overlays"][0]["points"] ==
          nlohmann::json::array(
              {nlohmann::json::array({8.85, 9.525}), nlohmann::json::array({11.15, 10.475})}));
    CHECK((*diagnostic)["overlays"][0]["entities"] ==
          nlohmann::json::array({"component:0", "component_placement:0"}));
    CHECK((*diagnostic)["overlays"][0]["layers"] == nlohmann::json::array({"board_layer:0"}));
    CHECK((*diagnostic)["overlays"][1]["kind"] == "bounding_box");
    CHECK((*diagnostic)["overlays"][1]["points"] ==
          nlohmann::json::array(
              {nlohmann::json::array({9.35, 9.525}), nlohmann::json::array({11.65, 10.475})}));
    CHECK((*diagnostic)["overlays"][1]["entities"] ==
          nlohmann::json::array({"component:1", "component_placement:1"}));
    CHECK((*diagnostic)["overlays"][1]["layers"] == nlohmann::json::array({"board_layer:0"}));
}

TEST_CASE("PCB projection reader and writer preserve emitted PCB visual text diagnostics") {
    const auto fixture = make_resistor_circuit();
    auto board = make_viewer_ready_board(fixture);
    static_cast<void>(board.add_text(volt::BoardText{"REV A", volt::BoardPoint{-1.0, 5.0},
                                                     volt::BoardRotation::degrees(0.0),
                                                     volt::BoardLayerId{0}, 1.0}));

    const auto text = volt::io::write_pcb_board(board, volt::builtin_footprint_library());
    const auto document = nlohmann::json::parse(text);
    const auto restored = volt::io::read_pcb_board_text(fixture.circuit, text);
    const auto rewritten = nlohmann::json::parse(
        volt::io::write_pcb_board(restored, volt::builtin_footprint_library()));

    const auto diagnostic =
        std::find_if(document["viewer"]["diagnostics"].begin(),
                     document["viewer"]["diagnostics"].end(), [](const nlohmann::json &item) {
                         return item["code"] == "PCB_VISUAL_LABEL_OUTSIDE_BOARD";
                     });
    const auto rewritten_diagnostic =
        std::find_if(rewritten["viewer"]["diagnostics"].begin(),
                     rewritten["viewer"]["diagnostics"].end(), [](const nlohmann::json &item) {
                         return item["code"] == "PCB_VISUAL_LABEL_OUTSIDE_BOARD";
                     });
    REQUIRE(diagnostic != document["viewer"]["diagnostics"].end());
    REQUIRE(rewritten_diagnostic != rewritten["viewer"]["diagnostics"].end());
    CHECK(*rewritten_diagnostic == *diagnostic);
    CHECK((*diagnostic)["severity"] == "warning");
    CHECK((*diagnostic)["category"] == "pcb.visual");
    CHECK((*diagnostic)["message"] == "Board text 'REV A' is outside the board outline");
    CHECK((*diagnostic)["entities"] == nlohmann::json::array({"board_text:0"}));
    REQUIRE((*diagnostic)["overlays"].size() == 1);
    CHECK((*diagnostic)["overlays"][0]["kind"] == "bounding_box");
    CHECK((*diagnostic)["overlays"][0]["points"] ==
          nlohmann::json::array(
              {nlohmann::json::array({-1.0, 4.0}), nlohmann::json::array({2.0, 5.0})}));
    CHECK((*diagnostic)["overlays"][0]["entities"] == nlohmann::json::array({"board_text:0"}));
    CHECK((*diagnostic)["overlays"][0]["layers"] == nlohmann::json::array({"board_layer:0"}));
}

TEST_CASE("PCB projection JSON orders emitted PCB visual diagnostics deterministically") {
    auto fixture = make_resistor_circuit();
    const auto second_component = fixture.circuit.instantiate_component(
        fixture.component_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R2"}});
    fixture.circuit.update(
        second_component,
        volt::SelectPhysicalPart{volt::PhysicalPart{
            volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"}, volt::PackageRef{"0603"},
            volt::FootprintRef{"passives", "R_0603_1608Metric"},
            std::vector{
                volt::PinPadMapping{fixture.first_pin_definition, "1"},
                volt::PinPadMapping{fixture.second_pin_definition, "2"},
            }}});

    auto board = volt::Board{fixture.circuit, volt::BoardName{"Control"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{50.0, 30.0}));
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        fixture.component, volt::BoardPoint{10.0, 10.0}, volt::BoardRotation::degrees(0.0)}));
    static_cast<void>(board.place_component(volt::ComponentPlacement{
        second_component, volt::BoardPoint{10.5, 10.0}, volt::BoardRotation::degrees(0.0)}));
    static_cast<void>(board.add_text(volt::BoardText{
        "REV A", volt::BoardPoint{20.0, 20.0}, volt::BoardRotation::degrees(0.0), front, 1.0}));
    static_cast<void>(board.add_text(volt::BoardText{
        "DATE", volt::BoardPoint{20.5, 20.0}, volt::BoardRotation::degrees(0.0), front, 1.0}));
    static_cast<void>(board.add_text(volt::BoardText{
        "OFF", volt::BoardPoint{-2.0, 5.0}, volt::BoardRotation::degrees(0.0), front, 1.0}));

    const auto first =
        nlohmann::json::parse(volt::io::write_pcb_board(board, volt::builtin_footprint_library()));
    const auto second =
        nlohmann::json::parse(volt::io::write_pcb_board(board, volt::builtin_footprint_library()));
    auto visual_codes = std::vector<std::string>{};
    for (const auto &diagnostic : first["viewer"]["diagnostics"]) {
        if (diagnostic["category"] == "pcb.visual") {
            visual_codes.push_back(diagnostic["code"].get<std::string>());
        }
    }

    CHECK(first["viewer"]["diagnostics"] == second["viewer"]["diagnostics"]);
    CHECK(visual_codes == std::vector<std::string>{"PCB_VISUAL_PLACEMENT_OVERLAP",
                                                   "PCB_VISUAL_LABEL_OUTSIDE_BOARD",
                                                   "PCB_VISUAL_LABEL_OVERLAP"});
}

TEST_CASE("PCB projection reader rejects dangling references") {
    const auto fixture = make_resistor_circuit();

    SECTION("component references") {
        auto document = make_board_json(fixture);
        document["board"]["placements"][0]["component"] = "component:99";

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::logic_error,
            Catch::Matchers::Message("PCB placement references missing component"));
    }

    SECTION("layer stack references") {
        auto document = make_board_json(fixture);
        document["board"]["layer_stack"]["layers"][1] = "board_layer:99";

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::logic_error,
            Catch::Matchers::Message("PCB layer stack references missing board layer"));
    }

    SECTION("footprint references") {
        auto document = make_board_json(fixture);
        document["board"]["placements"][0]["footprint"] = "footprint_def:99";

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::logic_error,
            Catch::Matchers::Message("PCB placement references missing footprint definition"));
    }

    SECTION("pad references") {
        auto document = make_board_json(fixture);
        document["viewer"]["pad_resolutions"][0]["pad"] = "footprint_pad:99";

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::logic_error,
            Catch::Matchers::Message("PCB viewer pad resolution references missing footprint pad"));
    }

    SECTION("net references") {
        auto document = make_board_json(fixture);
        document["viewer"]["pad_resolutions"][0]["net"] = "net:99";

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::logic_error,
            Catch::Matchers::Message("PCB viewer pad resolution references missing net"));
    }

    SECTION("track net references") {
        auto document = make_board_json(fixture);
        document["board"]["tracks"] = nlohmann::json::array(
            {{{"id", "board_track:0"},
              {"net", "net:99"},
              {"layer", "board_layer:0"},
              {"points", nlohmann::json::array({nlohmann::json::array({1.0, 1.0}),
                                                nlohmann::json::array({2.0, 1.0})})},
              {"width_mm", 0.25}}});

        CHECK_THROWS_MATCHES(volt::io::read_pcb_board_text(fixture.circuit, document.dump()),
                             std::logic_error,
                             Catch::Matchers::Message("PCB track references missing net"));
        try {
            static_cast<void>(volt::io::read_pcb_board_text(fixture.circuit, document.dump()));
            FAIL("Expected typed kernel error");
        } catch (const volt::KernelError &error) {
            CHECK(error.code() == volt::ErrorCode::UnknownEntity);
            CHECK(std::string{error.what()} == "PCB track references missing net");
        }
    }

    SECTION("via layer references") {
        auto document = make_board_json(fixture);
        document["board"]["vias"] =
            nlohmann::json::array({{{"id", "board_via:0"},
                                    {"net", "net:0"},
                                    {"position", nlohmann::json::array({2.0, 1.0})},
                                    {"start_layer", "board_layer:0"},
                                    {"end_layer", "board_layer:99"},
                                    {"drill_diameter_mm", 0.30},
                                    {"annular_diameter_mm", 0.70}}});

        CHECK_THROWS_MATCHES(volt::io::read_pcb_board_text(fixture.circuit, document.dump()),
                             std::logic_error,
                             Catch::Matchers::Message("PCB via references missing board layer"));
    }

    SECTION("zone net references") {
        auto document = make_board_json(fixture);
        document["board"]["zones"] = nlohmann::json::array(
            {{{"id", "board_zone:0"},
              {"outline",
               nlohmann::json::array(
                   {nlohmann::json::array({1.0, 1.0}), nlohmann::json::array({3.0, 1.0}),
                    nlohmann::json::array({3.0, 3.0}), nlohmann::json::array({1.0, 3.0})})},
              {"layers", nlohmann::json::array({"board_layer:0"})},
              {"net", "net:99"},
              {"fill", "solid"},
              {"priority", 0}}});

        CHECK_THROWS_MATCHES(volt::io::read_pcb_board_text(fixture.circuit, document.dump()),
                             std::logic_error,
                             Catch::Matchers::Message("PCB zone references missing net"));
    }

    SECTION("keepout layer references") {
        auto document = make_board_json(fixture);
        document["board"]["keepouts"] = nlohmann::json::array(
            {{{"id", "board_keepout:0"},
              {"outline",
               nlohmann::json::array(
                   {nlohmann::json::array({1.0, 1.0}), nlohmann::json::array({3.0, 1.0}),
                    nlohmann::json::array({3.0, 3.0}), nlohmann::json::array({1.0, 3.0})})},
              {"layers", nlohmann::json::array({"board_layer:99"})},
              {"restrictions", nlohmann::json::array({"copper"})}}});

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::logic_error,
            Catch::Matchers::Message("PCB keepout references missing board layer"));
    }

    SECTION("room layer references") {
        auto document = make_board_json(fixture);
        document["board"]["rooms"] = nlohmann::json::array(
            {{{"id", "board_room:0"},
              {"name", "BGA escape"},
              {"outline",
               nlohmann::json::array(
                   {nlohmann::json::array({1.0, 1.0}), nlohmann::json::array({3.0, 1.0}),
                    nlohmann::json::array({3.0, 3.0}), nlohmann::json::array({1.0, 3.0})})},
              {"layers", nlohmann::json::array({"board_layer:99"})}}});

        CHECK_THROWS_MATCHES(volt::io::read_pcb_board_text(fixture.circuit, document.dump()),
                             std::logic_error,
                             Catch::Matchers::Message("PCB room references missing board layer"));
    }

    SECTION("text layer references") {
        auto document = make_board_json(fixture);
        document["board"]["texts"] =
            nlohmann::json::array({{{"id", "board_text:0"},
                                    {"text", "REV A"},
                                    {"position", nlohmann::json::array({1.0, 1.0})},
                                    {"rotation_deg", 0.0},
                                    {"layer", "board_layer:99"},
                                    {"size_mm", 1.0},
                                    {"locked", false}}});

        CHECK_THROWS_MATCHES(volt::io::read_pcb_board_text(fixture.circuit, document.dump()),
                             std::logic_error,
                             Catch::Matchers::Message("PCB text references missing board layer"));
    }

    SECTION("invalid design rules") {
        auto document = make_board_json(fixture);
        document["board"]["rules"]["copper_clearance_mm"] = -0.1;

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::invalid_argument,
            Catch::Matchers::Message("Board design rule clearances must not be negative"));
    }

    SECTION("room outline geometry") {
        auto document = make_board_json(fixture);
        document["board"]["rooms"] = nlohmann::json::array(
            {{{"id", "board_room:0"},
              {"name", "BGA escape"},
              {"outline", nlohmann::json::array({nlohmann::json::array({1.0, 1.0}),
                                                 nlohmann::json::array({3.0, 1.0})})},
              {"layers", nlohmann::json::array({"board_layer:0"})}}});

        CHECK_THROWS_MATCHES(volt::io::read_pcb_board_text(fixture.circuit, document.dump()),
                             std::invalid_argument,
                             Catch::Matchers::Message("Board outline must contain at least three "
                                                      "vertices"));
    }

    SECTION("room override types") {
        auto document = make_board_json(fixture);
        document["board"]["rooms"] = nlohmann::json::array(
            {{{"id", "board_room:0"},
              {"name", "BGA escape"},
              {"outline",
               nlohmann::json::array(
                   {nlohmann::json::array({1.0, 1.0}), nlohmann::json::array({3.0, 1.0}),
                    nlohmann::json::array({3.0, 3.0}), nlohmann::json::array({1.0, 3.0})})},
              {"layers", nlohmann::json::array({"board_layer:0"})},
              {"copper_clearance_mm", "tight"}}});

        CHECK_THROWS_MATCHES(volt::io::read_pcb_board_text(fixture.circuit, document.dump()),
                             std::logic_error,
                             Catch::Matchers::Message("Expected number field: "
                                                      "copper_clearance_mm"));
    }

    SECTION("duplicate room names") {
        auto document = make_board_json(fixture);
        document["board"]["rooms"] = nlohmann::json::array(
            {{{"id", "board_room:0"},
              {"name", "BGA escape"},
              {"outline",
               nlohmann::json::array(
                   {nlohmann::json::array({1.0, 1.0}), nlohmann::json::array({3.0, 1.0}),
                    nlohmann::json::array({3.0, 3.0}), nlohmann::json::array({1.0, 3.0})})},
              {"layers", nlohmann::json::array({"board_layer:0"})}},
             {{"id", "board_room:1"},
              {"name", "BGA escape"},
              {"outline",
               nlohmann::json::array(
                   {nlohmann::json::array({4.0, 1.0}), nlohmann::json::array({6.0, 1.0}),
                    nlohmann::json::array({6.0, 3.0}), nlohmann::json::array({4.0, 3.0})})},
              {"layers", nlohmann::json::array({"board_layer:0"})}}});

        CHECK_THROWS_MATCHES(volt::io::read_pcb_board_text(fixture.circuit, document.dump()),
                             std::logic_error,
                             Catch::Matchers::Message("Board room name already exists"));
    }
}

TEST_CASE("PCB projection reader rejects invalid footprint library data") {
    const auto fixture = make_resistor_circuit();

    SECTION("duplicate footprint references") {
        auto document = make_board_json(fixture);
        auto duplicate = document["board"]["footprint_definitions"][0];
        duplicate["id"] = "footprint_def:1";
        duplicate["pads"][0]["size"] = nlohmann::json::array({0.90, 0.95});
        document["board"]["footprint_definitions"].push_back(duplicate);

        CHECK_THROWS_MATCHES(volt::io::read_pcb_board_text(fixture.circuit, document.dump()),
                             std::logic_error,
                             Catch::Matchers::Message(
                                 "Board footprint definition conflicts with existing definition"));
    }

    SECTION("duplicate footprint pad labels") {
        auto document = make_board_json(fixture);
        document["board"]["footprint_definitions"][0]["pads"][1]["label"] = "1";

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::invalid_argument,
            Catch::Matchers::Message("Footprint definition contains duplicate pad labels"));
    }

    SECTION("non-positive footprint pad geometry") {
        auto document = make_board_json(fixture);
        document["board"]["footprint_definitions"][0]["pads"][0]["size"] =
            nlohmann::json::array({0.0, 0.95});

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::invalid_argument,
            Catch::Matchers::Message("Footprint size dimensions must be positive"));
    }

    SECTION("empty declared footprint courtyard") {
        auto document = make_board_json(fixture);
        document["board"]["footprint_definitions"][0]["courtyard"] = nlohmann::json::array();

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::invalid_argument,
            Catch::Matchers::Message("Footprint polygon must contain at least three vertices"));
    }
}

TEST_CASE("PCB projection reader rejects stale viewer pad caches") {
    const auto fixture = make_resistor_circuit();

    SECTION("missing resolved pads") {
        auto document = make_board_json(fixture);
        document["viewer"]["pad_resolutions"].erase(1);

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::logic_error,
            Catch::Matchers::Message("PCB viewer pad resolutions must match resolved pads"));
        try {
            static_cast<void>(volt::io::read_pcb_board_text(fixture.circuit, document.dump()));
            FAIL("Expected typed kernel error");
        } catch (const volt::KernelError &error) {
            CHECK(error.code() == volt::ErrorCode::InvalidState);
            CHECK(std::string{error.what()} ==
                  "PCB viewer pad resolutions must match resolved pads");
        }
    }

    SECTION("duplicate resolved pads") {
        auto document = make_board_json(fixture);
        document["viewer"]["pad_resolutions"][1] = document["viewer"]["pad_resolutions"][0];

        CHECK_THROWS_MATCHES(volt::io::read_pcb_board_text(fixture.circuit, document.dump()),
                             std::logic_error,
                             Catch::Matchers::Message(
                                 "PCB viewer pad resolution order does not match resolved pads"));
        try {
            static_cast<void>(volt::io::read_pcb_board_text(fixture.circuit, document.dump()));
            FAIL("Expected typed kernel error");
        } catch (const volt::KernelError &error) {
            CHECK(error.code() == volt::ErrorCode::InvalidState);
            CHECK(std::string{error.what()} ==
                  "PCB viewer pad resolution order does not match resolved pads");
        }
    }

    SECTION("stale pad labels") {
        auto document = make_board_json(fixture);
        document["viewer"]["pad_resolutions"][0]["label"] = "stale";

        CHECK_THROWS_MATCHES(volt::io::read_pcb_board_text(fixture.circuit, document.dump()),
                             std::logic_error,
                             Catch::Matchers::Message(
                                 "PCB viewer pad resolution label does not match footprint pad"));
        try {
            static_cast<void>(volt::io::read_pcb_board_text(fixture.circuit, document.dump()));
            FAIL("Expected typed kernel error");
        } catch (const volt::KernelError &error) {
            CHECK(error.code() == volt::ErrorCode::InvalidState);
            CHECK(std::string{error.what()} ==
                  "PCB viewer pad resolution label does not match footprint pad");
        }
    }

    SECTION("stale pin mapping") {
        auto document = make_board_json(fixture);
        document["viewer"]["pad_resolutions"][0]["pin"] =
            document["viewer"]["pad_resolutions"][1]["pin"];

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::logic_error,
            Catch::Matchers::Message(
                "PCB viewer pad resolution pin does not match selected-part data"));
        try {
            static_cast<void>(volt::io::read_pcb_board_text(fixture.circuit, document.dump()));
            FAIL("Expected typed kernel error");
        } catch (const volt::KernelError &error) {
            CHECK(error.code() == volt::ErrorCode::CrossReferenceViolation);
            CHECK(std::string{error.what()} ==
                  "PCB viewer pad resolution pin does not match selected-part data");
        }
    }

    SECTION("stale pad geometry") {
        auto document = make_board_json(fixture);
        document["viewer"]["pad_resolutions"][0]["geometry"]["size"] =
            nlohmann::json::array({99.0, 99.0});

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::logic_error,
            Catch::Matchers::Message(
                "PCB viewer pad resolution geometry does not match footprint pad"));
        try {
            static_cast<void>(volt::io::read_pcb_board_text(fixture.circuit, document.dump()));
            FAIL("Expected typed kernel error");
        } catch (const volt::KernelError &error) {
            CHECK(error.code() == volt::ErrorCode::InvalidState);
            CHECK(std::string{error.what()} ==
                  "PCB viewer pad resolution geometry does not match footprint pad");
        }
    }
}

TEST_CASE("PCB projection reader rejects malformed viewer diagnostics") {
    const auto fixture = make_resistor_circuit();

    SECTION("dangling diagnostic refs") {
        auto document = make_board_json(fixture);
        document["viewer"]["diagnostics"] =
            nlohmann::json::array({{{"severity", "error"},
                                    {"code", "PCB_FIXTURE"},
                                    {"message", "fixture"},
                                    {"entities", nlohmann::json::array({"component:99"})}}});

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::logic_error,
            Catch::Matchers::Message("PCB viewer diagnostic references missing component"));
    }

    SECTION("footprint pad diagnostic refs are checked against paired footprint definitions") {
        auto document = make_board_json(fixture);
        auto extra_definition = document["board"]["footprint_definitions"][0];
        extra_definition["id"] = "footprint_def:1";
        extra_definition["ref"]["name"] = "R_0603_1608Metric_Derived";
        auto extra_pad = extra_definition["pads"][1];
        extra_pad["id"] = "footprint_pad:2";
        extra_pad["label"] = "3";
        extra_pad["position"] = nlohmann::json::array({1.5, 0.0});
        extra_definition["pads"].push_back(extra_pad);
        document["board"]["footprint_definitions"].push_back(extra_definition);
        document["viewer"]["diagnostics"] = nlohmann::json::array(
            {{{"severity", "error"},
              {"code", "PCB_FIXTURE"},
              {"message", "fixture"},
              {"entities", nlohmann::json::array({"footprint_def:0", "footprint_pad:2"})}}});

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::logic_error,
            Catch::Matchers::Message("PCB viewer diagnostic references missing footprint pad"));
    }

    SECTION("malformed diagnostic refs") {
        auto document = make_board_json(fixture);
        document["viewer"]["diagnostics"] =
            nlohmann::json::array({{{"severity", "error"},
                                    {"code", "PCB_FIXTURE"},
                                    {"message", "fixture"},
                                    {"entities", nlohmann::json::array({"not-an-entity"})}}});

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::logic_error,
            Catch::Matchers::Message("PCB viewer diagnostic has unsupported entity reference"));
    }

    SECTION("empty diagnostic code") {
        auto document = make_board_json(fixture);
        document["viewer"]["diagnostics"] =
            nlohmann::json::array({{{"severity", "error"},
                                    {"category", "drc"},
                                    {"code", ""},
                                    {"message", "fixture"},
                                    {"entities", nlohmann::json::array({"board:0"})}}});

        CHECK_THROWS_MATCHES(volt::io::read_pcb_board_text(fixture.circuit, document.dump()),
                             std::invalid_argument,
                             Catch::Matchers::Message("Diagnostic code must not be empty"));
        try {
            static_cast<void>(volt::io::read_pcb_board_text(fixture.circuit, document.dump()));
            FAIL("Expected typed kernel error");
        } catch (const volt::KernelError &error) {
            CHECK(error.code() == volt::ErrorCode::InvalidArgument);
            CHECK(std::string{error.what()} == "Diagnostic code must not be empty");
        }
    }

    SECTION("empty diagnostic category") {
        auto document = make_board_json(fixture);
        document["viewer"]["diagnostics"] =
            nlohmann::json::array({{{"severity", "error"},
                                    {"category", ""},
                                    {"code", "PCB_FIXTURE"},
                                    {"message", "fixture"},
                                    {"entities", nlohmann::json::array({"board:0"})}}});

        CHECK_THROWS_MATCHES(volt::io::read_pcb_board_text(fixture.circuit, document.dump()),
                             std::invalid_argument,
                             Catch::Matchers::Message("Diagnostic category must not be empty"));
        try {
            static_cast<void>(volt::io::read_pcb_board_text(fixture.circuit, document.dump()));
            FAIL("Expected typed kernel error");
        } catch (const volt::KernelError &error) {
            CHECK(error.code() == volt::ErrorCode::InvalidArgument);
            CHECK(std::string{error.what()} == "Diagnostic category must not be empty");
        }
    }

    SECTION("dangling diagnostic overlay layer refs") {
        auto document = make_board_json(fixture);
        document["viewer"]["diagnostics"] = nlohmann::json::array(
            {{{"severity", "warning"},
              {"category", "pcb.visual"},
              {"code", "PCB_VISUAL_REFERENCE_DESIGNATOR_UNREADABLE"},
              {"message", "fixture"},
              {"entities", nlohmann::json::array({"board:0"})},
              {"overlays",
               nlohmann::json::array(
                   {{{"kind", "bounding_box"},
                     {"points", nlohmann::json::array({nlohmann::json::array({0.0, 0.0}),
                                                       nlohmann::json::array({1.0, 1.0})})},
                     {"entities", nlohmann::json::array()},
                     {"layers", nlohmann::json::array({"board_layer:99"})}}})}}});

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::logic_error,
            Catch::Matchers::Message("PCB viewer diagnostic references missing board layer"));
    }

    SECTION("diagnostic overlay layer refs must be board layers") {
        auto document = make_board_json(fixture);
        document["viewer"]["diagnostics"] = nlohmann::json::array(
            {{{"severity", "warning"},
              {"category", "pcb.visual"},
              {"code", "PCB_VISUAL_REFERENCE_DESIGNATOR_UNREADABLE"},
              {"message", "fixture"},
              {"entities", nlohmann::json::array({"board:0"})},
              {"overlays",
               nlohmann::json::array(
                   {{{"kind", "point"},
                     {"points", nlohmann::json::array({nlohmann::json::array({0.0, 0.0})})},
                     {"entities", nlohmann::json::array()},
                     {"layers", nlohmann::json::array({"component:0"})}}})}}});

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::logic_error,
            Catch::Matchers::Message("PCB viewer diagnostic overlay layer must be a board layer"));
    }

    SECTION("non-object diagnostic measurement") {
        auto document = make_board_json(fixture);
        document["viewer"]["diagnostics"] =
            nlohmann::json::array({{{"severity", "error"},
                                    {"category", "drc"},
                                    {"code", "PCB_FIXTURE"},
                                    {"message", "fixture"},
                                    {"entities", nlohmann::json::array({"board:0"})},
                                    {"measurement", 1.5}}});

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::logic_error,
            Catch::Matchers::Message("PCB viewer diagnostic measurement must be an object"));
    }

    SECTION("diagnostic measurement missing required fields") {
        auto document = make_board_json(fixture);
        document["viewer"]["diagnostics"] =
            nlohmann::json::array({{{"severity", "error"},
                                    {"category", "drc"},
                                    {"code", "PCB_FIXTURE"},
                                    {"message", "fixture"},
                                    {"entities", nlohmann::json::array({"board:0"})},
                                    {"measurement", {{"actual_mm", 0.1}}}}});

        CHECK_THROWS_AS(volt::io::read_pcb_board_text(fixture.circuit, document.dump()),
                        std::logic_error);
    }
}

TEST_CASE("PCB projection round-trips the clearance matrix") {
    const auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Matrix"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    static_cast<void>(front);
    auto rules = volt::BoardDesignRules{0.15, 0.15, 0.20, 0.45, 0.10};
    rules.set_clearance_mm(volt::BoardClearanceKind::Track, volt::BoardClearanceKind::Pad, 0.25);
    rules.set_clearance_mm(volt::BoardClearanceKind::Zone, volt::BoardClearanceKind::BoardEdge,
                           0.50);
    board.set_design_rules(rules);

    const auto text = volt::io::write_pcb_board(board, volt::builtin_footprint_library());
    const auto document = nlohmann::json::parse(text);

    CHECK(document["board"]["rules"]["clearance_matrix"] ==
          nlohmann::json::array(
              {{{"first", "track"}, {"second", "pad"}, {"clearance_mm", 0.25}},
               {{"first", "zone"}, {"second", "board_edge"}, {"clearance_mm", 0.5}}}));

    const auto loaded = volt::io::read_pcb_board_text(fixture.circuit, text);
    CHECK(loaded.design_rules().clearance_mm(volt::BoardClearanceKind::Pad,
                                             volt::BoardClearanceKind::Track) == 0.25);
    CHECK(loaded.design_rules().clearance_mm(volt::BoardClearanceKind::Zone,
                                             volt::BoardClearanceKind::BoardEdge) == 0.5);
    CHECK(volt::io::write_pcb_board(loaded, volt::builtin_footprint_library()) == text);
}

TEST_CASE("PCB projection reader rejects malformed clearance matrices") {
    const auto fixture = make_resistor_circuit();
    const auto board = make_viewer_ready_board(fixture);
    const auto text = volt::io::write_pcb_board(board, volt::builtin_footprint_library());

    auto bad_kind = nlohmann::json::parse(text);
    bad_kind["board"]["rules"]["clearance_matrix"] =
        nlohmann::json::array({{{"first", "sideways"}, {"second", "pad"}, {"clearance_mm", 0.25}}});
    CHECK_THROWS_AS(volt::io::read_pcb_board_text(fixture.circuit, bad_kind.dump()),
                    std::logic_error);

    auto bad_value = nlohmann::json::parse(text);
    bad_value["board"]["rules"]["clearance_matrix"] =
        nlohmann::json::array({{{"first", "track"}, {"second", "pad"}, {"clearance_mm", -1.0}}});
    CHECK_THROWS_AS(volt::io::read_pcb_board_text(fixture.circuit, bad_value.dump()),
                    std::logic_error);

    auto duplicate_pair = nlohmann::json::parse(text);
    duplicate_pair["board"]["rules"]["clearance_matrix"] =
        nlohmann::json::array({{{"first", "track"}, {"second", "pad"}, {"clearance_mm", 0.25}},
                               {{"first", "track"}, {"second", "pad"}, {"clearance_mm", 0.30}}});
    CHECK_THROWS_AS(volt::io::read_pcb_board_text(fixture.circuit, duplicate_pair.dump()),
                    std::logic_error);
    try {
        static_cast<void>(volt::io::read_pcb_board_text(fixture.circuit, duplicate_pair.dump()));
        FAIL("Expected typed kernel error");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::DuplicateName);
        CHECK(std::string{error.what()} == "Duplicate PCB clearance matrix pair");
    }

    auto reversed_pair = nlohmann::json::parse(text);
    reversed_pair["board"]["rules"]["clearance_matrix"] =
        nlohmann::json::array({{{"first", "track"}, {"second", "pad"}, {"clearance_mm", 0.25}},
                               {{"first", "pad"}, {"second", "track"}, {"clearance_mm", 0.30}}});
    CHECK_THROWS_AS(volt::io::read_pcb_board_text(fixture.circuit, reversed_pair.dump()),
                    std::logic_error);
    try {
        static_cast<void>(volt::io::read_pcb_board_text(fixture.circuit, reversed_pair.dump()));
        FAIL("Expected typed kernel error");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::DuplicateName);
        CHECK(std::string{error.what()} == "Duplicate PCB clearance matrix pair");
    }
}

TEST_CASE("PCB projection round-trips stackup copper weight and dielectrics") {
    const auto fixture = make_resistor_circuit();
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Stackup"}};
    auto front_layer =
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top, 0.035};
    front_layer.set_copper_weight_oz(1.0);
    const auto front = board.add_layer(std::move(front_layer));
    auto back_layer =
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom, 0.035};
    back_layer.set_copper_weight_oz(2.0);
    const auto back = board.add_layer(std::move(back_layer));
    board.set_layer_stack(
        volt::LayerStack{{front, back}, 1.6, std::vector{volt::BoardDielectric{1.51, 4.6}}});

    const auto text = volt::io::write_pcb_board(board, volt::builtin_footprint_library());
    const auto document = nlohmann::json::parse(text);

    CHECK(document["board"]["layers"][0]["copper_weight_oz"] == 1.0);
    CHECK(document["board"]["layers"][1]["copper_weight_oz"] == 2.0);
    CHECK(document["board"]["layer_stack"]["dielectrics"] ==
          nlohmann::json::array({{{"thickness_mm", 1.51}, {"relative_permittivity", 4.6}}}));

    const auto loaded = volt::io::read_pcb_board_text(fixture.circuit, text);
    CHECK(loaded.layer(front).copper_weight_oz() == 1.0);
    CHECK(loaded.layer(back).copper_weight_oz() == 2.0);
    REQUIRE(loaded.layer_stack().has_value());
    REQUIRE(loaded.layer_stack()->dielectrics().size() == 1);
    CHECK(loaded.layer_stack()->dielectrics().front().thickness_mm() == 1.51);
    CHECK(loaded.layer_stack()->dielectrics().front().relative_permittivity() == 4.6);
    CHECK(volt::io::write_pcb_board(loaded, volt::builtin_footprint_library()) == text);
}

TEST_CASE("PCB projection reader rejects malformed stackup data") {
    const auto fixture = make_resistor_circuit();
    const auto board = make_viewer_ready_board(fixture);
    const auto text = volt::io::write_pcb_board(board, volt::builtin_footprint_library());

    auto bad_weight = nlohmann::json::parse(text);
    bad_weight["board"]["layers"][0]["copper_weight_oz"] = "heavy";
    CHECK_THROWS_AS(volt::io::read_pcb_board_text(fixture.circuit, bad_weight.dump()),
                    std::logic_error);

    auto bad_dielectric = nlohmann::json::parse(text);
    bad_dielectric["board"]["layer_stack"]["dielectrics"] =
        nlohmann::json::array({{{"thickness_mm", 0.7}, {"relative_permittivity", 0.5}}});
    CHECK_THROWS_AS(volt::io::read_pcb_board_text(fixture.circuit, bad_dielectric.dump()),
                    std::logic_error);
}
