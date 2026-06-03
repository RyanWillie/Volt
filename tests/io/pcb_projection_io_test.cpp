#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>

#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/queries.hpp>
#include <volt/io/pcb_reader.hpp>
#include <volt/io/pcb_writer.hpp>
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

[[nodiscard]] ResistorCircuit make_resistor_circuit() {
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
    circuit.select_physical_part(component,
                                 volt::PhysicalPart{
                                     volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                                     volt::PackageRef{"0603"},
                                     volt::FootprintRef{"passives", "R_0603_1608Metric"},
                                     std::vector{volt::PinPadMapping{first_pin_definition, "1"},
                                                 volt::PinPadMapping{second_pin_definition, "2"}},
                                 });

    return ResistorCircuit{std::move(circuit),
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
        volt::BoardFeature::mounting_hole("MH1", volt::BoardPoint{3.0, 3.0}, 3.2));
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

TEST_CASE("PCB projection writer emits deterministic product-viewer-ready JSON") {
    const auto fixture = make_resistor_circuit();
    const auto board = make_viewer_ready_board(fixture);

    const auto first = volt::io::write_pcb_board(board, volt::builtin_footprint_library());
    const auto second = volt::io::write_pcb_board(board, volt::builtin_footprint_library());
    const auto document = nlohmann::json::parse(first);

    CHECK(first == second);
    CHECK(document["format"] == "volt.pcb");
    CHECK(document["version"] == 1);
    CHECK(document["board"]["id"] == "board:0");
    CHECK(document["board"]["name"] == "Control");
    CHECK(document["board"]["units"] == "mm");
    CHECK(document["board"]["rules"]["copper_clearance_mm"] == 0.15);
    CHECK(document["board"]["rules"]["minimum_track_width_mm"] == 0.15);
    CHECK(document["board"]["rules"]["minimum_via_drill_diameter_mm"] == 0.20);
    CHECK(document["board"]["rules"]["minimum_via_annular_diameter_mm"] == 0.45);
    CHECK(document["board"]["rules"]["board_outline_clearance_mm"] == 0.0);

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
    CHECK(document["board"]["features"][0]["kind"] == "mounting_hole");
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
    CHECK(document["viewer"]["diagnostics"] == nlohmann::json::array());
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
    static_cast<void>(
        board.add_feature(volt::BoardFeature::fiducial("FID", volt::BoardPoint{34.0, 4.0}, 1.0)));
    static_cast<void>(board.add_feature(
        volt::BoardFeature::tooling_hole("TH", volt::BoardPoint{4.0, 24.0}, 2.0)));

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
    CHECK(document["board"]["features"][4]["kind"] == "fiducial");
    CHECK(document["board"]["features"][4]["side"] == "top");
    CHECK(document["board"]["features"][5]["kind"] == "tooling_hole");
    CHECK(document["board"]["features"][5]["role"] == "tooling");

    const auto restored = volt::io::read_pcb_board_text(fixture.circuit, text);
    CHECK(volt::io::write_pcb_board(restored, volt::builtin_footprint_library()) == text);
    CHECK(restored.feature(volt::BoardFeatureId{1}).hole().drill_diameter_mm() == 1.0);
    CHECK(restored.feature(volt::BoardFeatureId{2}).slot().width_mm() == 1.5);
    CHECK(restored.feature(volt::BoardFeatureId{3}).cutout().outline().size() == 4);
    CHECK(restored.feature(volt::BoardFeatureId{4}).fiducial().diameter_mm() == 1.0);
    CHECK(restored.feature(volt::BoardFeatureId{5}).hole().drill_diameter_mm() == 2.0);
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

TEST_CASE("PCB projection writer and reader round-trip zones, keepouts, and board text") {
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

    REQUIRE(document["board"]["features"].size() == 3);
    CHECK(document["board"]["features"][1]["id"] == "board_feature:1");
    CHECK(document["board"]["features"][1]["kind"] == "mechanical_keepout");
    CHECK(document["board"]["features"][1]["layers"] ==
          nlohmann::json::array({"board_layer:0", "board_layer:1"}));
    CHECK(document["board"]["features"][1]["restrictions"] ==
          nlohmann::json::array({"copper", "placement"}));

    CHECK(document["board"]["features"][2]["id"] == "board_feature:2");
    CHECK(document["board"]["features"][2]["kind"] == "text");
    CHECK(document["board"]["features"][2]["text"] == "REV A");
    CHECK(document["board"]["features"][2]["position"] == nlohmann::json::array({5.0, 24.0}));
    CHECK(document["board"]["features"][2]["rotation_deg"] == 90.0);
    CHECK(document["board"]["features"][2]["layer"] == "board_layer:0");
    CHECK(document["board"]["features"][2]["size_mm"] == 1.2);
    CHECK(document["board"]["features"][2]["locked"] == true);
    CHECK(!document["board"].contains("keepouts"));
    CHECK(!document["board"].contains("texts"));

    const auto restored = volt::io::read_pcb_board_text(fixture.circuit, text_json);
    CHECK(volt::io::write_pcb_board(restored, volt::builtin_footprint_library()) == text_json);
    CHECK(restored.zone(volt::BoardZoneId{0}).priority() == 5);
    CHECK(restored.keepout(volt::BoardKeepoutId{0}).restrictions() ==
          std::vector{volt::BoardKeepoutRestriction::Copper,
                      volt::BoardKeepoutRestriction::Placement});
    CHECK(restored.text(volt::BoardTextId{0}).text() == "REV A");
}

TEST_CASE("PCB projection writer and reader round-trip board design rules") {
    const auto fixture = make_resistor_circuit();
    auto board = make_viewer_ready_board(fixture);
    board.set_design_rules(volt::BoardDesignRules{0.20, 0.25, 0.30, 0.70, 0.10});
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
    REQUIRE_FALSE(document["viewer"]["diagnostics"].empty());
    CHECK(document["viewer"]["diagnostics"][0]["code"] == "PCB_TRACK_WIDTH_BELOW_MINIMUM");
    CHECK(document["viewer"]["diagnostics"][0]["entities"] ==
          nlohmann::json::array({"board_track:0", "net:0", "board_layer:0"}));

    const auto restored = volt::io::read_pcb_board_text(fixture.circuit, text);
    CHECK(restored.design_rules().copper_clearance_mm() == 0.20);
    CHECK(restored.design_rules().minimum_track_width_mm() == 0.25);
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
}

TEST_CASE("PCB projection reader rejects stale viewer pad caches") {
    const auto fixture = make_resistor_circuit();

    SECTION("missing resolved pads") {
        auto document = make_board_json(fixture);
        document["viewer"]["pad_resolutions"].erase(1);

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::logic_error,
            Catch::Matchers::Message("PCB viewer pad resolutions must match resolved pads"));
    }

    SECTION("duplicate resolved pads") {
        auto document = make_board_json(fixture);
        document["viewer"]["pad_resolutions"][1] = document["viewer"]["pad_resolutions"][0];

        CHECK_THROWS_MATCHES(volt::io::read_pcb_board_text(fixture.circuit, document.dump()),
                             std::logic_error,
                             Catch::Matchers::Message(
                                 "PCB viewer pad resolution order does not match resolved pads"));
    }

    SECTION("stale pad labels") {
        auto document = make_board_json(fixture);
        document["viewer"]["pad_resolutions"][0]["label"] = "stale";

        CHECK_THROWS_MATCHES(volt::io::read_pcb_board_text(fixture.circuit, document.dump()),
                             std::logic_error,
                             Catch::Matchers::Message(
                                 "PCB viewer pad resolution label does not match footprint pad"));
    }

    SECTION("stale pad geometry") {
        auto document = make_board_json(fixture);
        document["viewer"]["pad_resolutions"][0]["geometry"]["size"] =
            nlohmann::json::array({99.0, 99.0});

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board_text(fixture.circuit, document.dump()), std::logic_error,
            Catch::Matchers::Message(
                "PCB viewer pad resolution geometry does not match footprint pad"));
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
}
