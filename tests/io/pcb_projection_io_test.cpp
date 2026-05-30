#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>

#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/circuit/circuit.hpp>
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
    const auto first_pin = circuit.pin_by_definition(component, first_pin_definition).value();
    const auto second_pin = circuit.pin_by_definition(component, second_pin_definition).value();
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

    REQUIRE(document["board"]["layers"].size() == 2);
    CHECK(document["board"]["layers"][0]["id"] == "board_layer:0");
    CHECK(document["board"]["layers"][0]["name"] == "F.Cu");
    CHECK(document["board"]["layers"][0]["role"] == "copper");
    CHECK(document["board"]["layers"][0]["side"] == "top");
    CHECK(document["board"]["layer_stack"]["board_thickness_mm"] == 1.6);
    CHECK(document["board"]["layer_stack"]["layers"] ==
          nlohmann::json::array({"board_layer:0", "board_layer:1"}));

    CHECK(document["board"]["outline"]["kind"] == "polygon");
    CHECK(document["board"]["outline"]["vertices"][2] == nlohmann::json::array({50.0, 30.0}));
    CHECK(document["board"]["features"][0]["id"] == "board_feature:0");
    CHECK(document["board"]["features"][0]["kind"] == "mounting_hole");
    CHECK(document["board"]["features"][0]["position"] == nlohmann::json::array({3.0, 3.0}));

    REQUIRE(document["board"]["footprint_definitions"].size() == 1);
    CHECK(document["board"]["footprint_definitions"][0]["id"] == "footprint_def:0");
    CHECK(document["board"]["footprint_definitions"][0]["ref"]["library"] == "passives");
    CHECK(document["board"]["footprint_definitions"][0]["pads"][0]["id"] == "footprint_pad:0");
    CHECK(document["board"]["footprint_definitions"][0]["pads"][0]["shape"] == "rounded_rectangle");

    REQUIRE(document["board"]["placements"].size() == 1);
    CHECK(document["board"]["placements"][0]["id"] == "component_placement:0");
    CHECK(document["board"]["placements"][0]["component"] == "component:0");
    CHECK(document["board"]["placements"][0]["footprint"] == "footprint_def:0");
    CHECK(document["board"]["placements"][0]["position"] == nlohmann::json::array({25.0, 15.0}));
    CHECK(document["board"]["placements"][0]["rotation_deg"] == 90.0);
    CHECK(document["board"]["placements"][0]["locked"] == true);

    REQUIRE(document["viewer"]["pad_resolutions"].size() == 2);
    CHECK(document["viewer"]["pad_resolutions"][0]["id"] == "pcb_pad:0:0");
    CHECK(document["viewer"]["pad_resolutions"][0]["placement"] == "component_placement:0");
    CHECK(document["viewer"]["pad_resolutions"][0]["pad"] == "footprint_pad:0");
    CHECK(document["viewer"]["pad_resolutions"][0]["pin"] == "pin:0");
    CHECK(document["viewer"]["pad_resolutions"][0]["net"] == "net:0");
    CHECK(document["viewer"]["pad_resolutions"][0]["status"] == "connected");
    CHECK(document["viewer"]["diagnostics"] == nlohmann::json::array());
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
    CHECK(restored.feature(volt::BoardFeatureId{0}).diameter_mm() == 3.2);
    CHECK(restored.footprint_definition(volt::FootprintDefId{0})
              .pad(volt::FootprintPadId{0})
              .size() == volt::FootprintSize{0.80, 0.95});
    CHECK(restored.placement(volt::ComponentPlacementId{0}).component() == fixture.component);
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
    CHECK(document["viewer"]["diagnostics"][0]["code"] == "PCB_PLACEMENT_OUTSIDE_OUTLINE");
    CHECK(document["viewer"]["diagnostics"][0]["entities"] ==
          nlohmann::json::array({"component:0", "component_placement:0"}));
}

TEST_CASE("PCB projection reader rejects dangling references") {
    const auto fixture = make_resistor_circuit();

    SECTION("component references") {
        auto document = make_board_json(fixture);
        document["board"]["placements"][0]["component"] = "component:99";

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board(fixture.circuit, document), std::logic_error,
            Catch::Matchers::Message("PCB placement references missing component"));
    }

    SECTION("layer stack references") {
        auto document = make_board_json(fixture);
        document["board"]["layer_stack"]["layers"][1] = "board_layer:99";

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board(fixture.circuit, document), std::logic_error,
            Catch::Matchers::Message("PCB layer stack references missing board layer"));
    }

    SECTION("footprint references") {
        auto document = make_board_json(fixture);
        document["board"]["placements"][0]["footprint"] = "footprint_def:99";

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board(fixture.circuit, document), std::logic_error,
            Catch::Matchers::Message("PCB placement references missing footprint definition"));
    }

    SECTION("pad references") {
        auto document = make_board_json(fixture);
        document["viewer"]["pad_resolutions"][0]["pad"] = "footprint_pad:99";

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board(fixture.circuit, document), std::logic_error,
            Catch::Matchers::Message("PCB viewer pad resolution references missing footprint pad"));
    }

    SECTION("net references") {
        auto document = make_board_json(fixture);
        document["viewer"]["pad_resolutions"][0]["net"] = "net:99";

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board(fixture.circuit, document), std::logic_error,
            Catch::Matchers::Message("PCB viewer pad resolution references missing net"));
    }
}

TEST_CASE("PCB projection reader rejects stale viewer pad caches") {
    const auto fixture = make_resistor_circuit();

    SECTION("missing resolved pads") {
        auto document = make_board_json(fixture);
        document["viewer"]["pad_resolutions"].erase(1);

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board(fixture.circuit, document), std::logic_error,
            Catch::Matchers::Message("PCB viewer pad resolutions must match resolved pads"));
    }

    SECTION("duplicate resolved pads") {
        auto document = make_board_json(fixture);
        document["viewer"]["pad_resolutions"][1] = document["viewer"]["pad_resolutions"][0];

        CHECK_THROWS_MATCHES(volt::io::read_pcb_board(fixture.circuit, document), std::logic_error,
                             Catch::Matchers::Message(
                                 "PCB viewer pad resolution order does not match resolved pads"));
    }

    SECTION("stale pad labels") {
        auto document = make_board_json(fixture);
        document["viewer"]["pad_resolutions"][0]["label"] = "stale";

        CHECK_THROWS_MATCHES(volt::io::read_pcb_board(fixture.circuit, document), std::logic_error,
                             Catch::Matchers::Message(
                                 "PCB viewer pad resolution label does not match footprint pad"));
    }

    SECTION("stale pad geometry") {
        auto document = make_board_json(fixture);
        document["viewer"]["pad_resolutions"][0]["geometry"]["size"] =
            nlohmann::json::array({99.0, 99.0});

        CHECK_THROWS_MATCHES(
            volt::io::read_pcb_board(fixture.circuit, document), std::logic_error,
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
            volt::io::read_pcb_board(fixture.circuit, document), std::logic_error,
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
            volt::io::read_pcb_board(fixture.circuit, document), std::logic_error,
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
            volt::io::read_pcb_board(fixture.circuit, document), std::logic_error,
            Catch::Matchers::Message("PCB viewer diagnostic has unsupported entity reference"));
    }
}
