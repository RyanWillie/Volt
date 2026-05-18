#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/io/schematic_writer.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/symbols.hpp>

namespace {

volt::ComponentId add_resistor(volt::Circuit &circuit) {
    const auto first_pin =
        circuit.add_pin_definition(volt::PinDefinition{"1", "1", volt::PinRole::Passive});
    const auto second_pin =
        circuit.add_pin_definition(volt::PinDefinition{"2", "2", volt::PinRole::Passive});
    const auto definition = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    return circuit.instantiate_component(definition, volt::ReferenceDesignator{"R1"});
}

volt::NetId add_net(volt::Circuit &circuit) {
    return circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});
}

volt::SymbolDefinition make_symbol() {
    auto symbol = volt::SymbolDefinition{"Resistor"};
    symbol.add_pin(
        volt::SymbolPin{"1", "1", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Left});
    symbol.add_pin(
        volt::SymbolPin{"2", "2", volt::Point{20.0, 0.0}, volt::SchematicOrientation::Right});
    symbol.add_primitive(volt::SymbolLine{volt::Point{0.0, 0.0}, volt::Point{20.0, 0.0}});
    symbol.add_primitive(volt::SymbolRectangle{volt::Point{4.0, -3.0}, volt::Point{16.0, 3.0}});
    symbol.add_primitive(volt::SymbolCircle{volt::Point{10.0, 0.0}, 2.0});
    symbol.add_primitive(volt::SymbolArc{volt::Point{10.0, 0.0}, 5.0, 0.0, 180.0});
    symbol.add_primitive(volt::SymbolText{"R", volt::Point{10.0, -8.0}});
    return symbol;
}

volt::Schematic make_schematic(const volt::Circuit &circuit, volt::ComponentId component) {
    auto schematic = volt::Schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_symbol());
    [[maybe_unused]] const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 20.0},
                                    volt::SchematicOrientation::Right});
    return schematic;
}

volt::Schematic make_schematic_with_net_projection(const volt::Circuit &circuit,
                                                   volt::ComponentId component, volt::NetId net) {
    auto schematic = make_schematic(circuit, component);
    const auto sheet = volt::SheetId{0};
    [[maybe_unused]] const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{10.0, 20.0}, volt::Point{30.0, 20.0},
                                              volt::Point{30.0, 50.0}}});
    [[maybe_unused]] const auto label =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{12.0, 16.0}});
    return schematic;
}

} // namespace

TEST_CASE("Schematic writer emits structured projection JSON") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto schematic = make_schematic(circuit, component);

    const auto output = nlohmann::json::parse(volt::io::write_schematic(schematic));

    CHECK(output["format"] == "volt.schematic");
    CHECK(output["version"] == 1);
    CHECK(output["symbol_definitions"][0]["id"] == "symbol_def:0");
    CHECK(output["symbol_definitions"][0]["name"] == "Resistor");
    CHECK(output["symbol_definitions"][0]["pins"][0]["anchor"]["x"] == 0.0);
    CHECK(output["symbol_definitions"][0]["pins"][0]["orientation"] == "Left");
    CHECK(output["symbol_definitions"][0]["primitives"][0]["type"] == "line");
    CHECK(output["symbol_definitions"][0]["primitives"][1]["type"] == "rectangle");
    CHECK(output["symbol_definitions"][0]["primitives"][2]["type"] == "circle");
    CHECK(output["symbol_definitions"][0]["primitives"][3]["type"] == "arc");
    CHECK(output["symbol_definitions"][0]["primitives"][4]["type"] == "text");
    CHECK(output["symbol_definitions"][0]["primitives"][4]["orientation"] == "Right");
    CHECK(output["sheets"][0]["id"] == "sheet:0");
    CHECK(output["sheets"][0]["name"] == "Main");
    CHECK(output["sheets"][0]["symbol_instances"] == nlohmann::json::array({"symbol_instance:0"}));
    CHECK(output["sheets"][0]["wire_runs"] == nlohmann::json::array());
    CHECK(output["sheets"][0]["net_labels"] == nlohmann::json::array());
    CHECK(output["symbol_instances"][0]["id"] == "symbol_instance:0");
    CHECK(output["symbol_instances"][0]["sheet"] == "sheet:0");
    CHECK(output["symbol_instances"][0]["symbol_definition"] == "symbol_def:0");
    CHECK(output["symbol_instances"][0]["component"] == "component:0");
    CHECK(output["symbol_instances"][0]["position"]["x"] == 40.0);
    CHECK(output["symbol_instances"][0]["position"]["y"] == 20.0);
    CHECK(output["symbol_instances"][0]["orientation"] == "Right");
    CHECK(output["wire_runs"] == nlohmann::json::array());
    CHECK(output["net_labels"] == nlohmann::json::array());
}

TEST_CASE("Schematic writer emits wire runs and net labels over canonical nets") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = add_net(circuit);
    const auto schematic = make_schematic_with_net_projection(circuit, component, net);

    const auto output = nlohmann::json::parse(volt::io::write_schematic(schematic));

    CHECK(output["sheets"][0]["wire_runs"] == nlohmann::json::array({"wire_run:0"}));
    CHECK(output["sheets"][0]["net_labels"] == nlohmann::json::array({"net_label:0"}));
    CHECK(output["wire_runs"][0]["id"] == "wire_run:0");
    CHECK(output["wire_runs"][0]["sheet"] == "sheet:0");
    CHECK(output["wire_runs"][0]["net"] == "net:0");
    CHECK(output["wire_runs"][0]["points"] == nlohmann::json::array({{{"x", 10.0}, {"y", 20.0}},
                                                                     {{"x", 30.0}, {"y", 20.0}},
                                                                     {{"x", 30.0}, {"y", 50.0}}}));
    CHECK(output["net_labels"][0]["id"] == "net_label:0");
    CHECK(output["net_labels"][0]["sheet"] == "sheet:0");
    CHECK(output["net_labels"][0]["net"] == "net:0");
    CHECK(output["net_labels"][0]["position"] == nlohmann::json({{"x", 12.0}, {"y", 16.0}}));
    CHECK(output["net_labels"][0]["orientation"] == "Right");
}

TEST_CASE("Schematic writer emits optional net label display text") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = circuit.add_net(volt::Net{volt::NetName{"SUPPORT/SWDIO"}, volt::NetKind::Signal});
    auto schematic = make_schematic(circuit, component);
    [[maybe_unused]] const auto label = schematic.add_net_label(
        volt::SheetId{0},
        volt::NetLabel{net, volt::Point{12.0, 16.0}, volt::SchematicOrientation::Right,
                       std::nullopt, std::string{"SWDIO"}});

    const auto output = nlohmann::json::parse(volt::io::write_schematic(schematic));

    CHECK(output["net_labels"][0]["net"] == "net:0");
    CHECK(output["net_labels"][0]["label"] == "SWDIO");
}

TEST_CASE("Schematic writer emits professional primitives and sheet metadata") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto vcc = add_net(circuit);
    const auto gnd = circuit.add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});
    const auto no_connect_pin = circuit.pin_by_number(component, "2").value();
    circuit.mark_intentional_no_connect_pin(no_connect_pin);

    auto schematic = volt::Schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{
        "Power",
        volt::SheetMetadata{"Power sheet", volt::SheetSize{420.0, 297.0},
                            std::vector{volt::TitleBlockField{"Revision", "A"}}},
    });
    const auto symbol = schematic.add_symbol_definition(make_symbol());
    const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 20.0}});
    [[maybe_unused]] const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{vcc, std::vector{volt::Point{10.0, 20.0}, volt::Point{30.0, 20.0}},
                             volt::RouteIntent::Orthogonal});
    [[maybe_unused]] const auto junction =
        schematic.add_junction(sheet, volt::Junction{vcc, volt::Point{30.0, 20.0}});
    [[maybe_unused]] const auto power = schematic.add_power_port(
        sheet, volt::PowerPort{vcc, volt::PowerPortKind::Power, volt::Point{10.0, 16.0}});
    [[maybe_unused]] const auto ground = schematic.add_power_port(
        sheet, volt::PowerPort{gnd, volt::PowerPortKind::Ground, volt::Point{50.0, 24.0}});
    [[maybe_unused]] const auto marker = schematic.add_no_connect_marker(
        sheet, volt::NoConnectMarker{no_connect_pin, volt::Point{60.0, 20.0},
                                     volt::SchematicOrientation::Right, "not populated"});
    [[maybe_unused]] const auto port = schematic.add_sheet_port(
        sheet, volt::SheetPort{vcc, "VIN", volt::SheetPortKind::OffPage, volt::Point{5.0, 20.0}});
    [[maybe_unused]] const auto field = schematic.add_symbol_field(
        sheet, volt::SymbolField{instance, "value", "10k", volt::Point{40.0, 32.0}});

    const auto output = nlohmann::json::parse(volt::io::write_schematic(schematic));

    CHECK(output["sheets"][0]["metadata"]["title"] == "Power sheet");
    CHECK(output["sheets"][0]["metadata"]["size"] ==
          nlohmann::json({{"width", 420.0}, {"height", 297.0}}));
    CHECK(output["sheets"][0]["metadata"]["title_block"] ==
          nlohmann::json::array({{{"key", "Revision"}, {"value", "A"}}}));
    CHECK(output["sheets"][0]["junctions"] == nlohmann::json::array({"junction:0"}));
    CHECK(output["sheets"][0]["power_ports"] ==
          nlohmann::json::array({"power_port:0", "power_port:1"}));
    CHECK(output["sheets"][0]["no_connect_markers"] ==
          nlohmann::json::array({"no_connect_marker:0"}));
    CHECK(output["sheets"][0]["sheet_ports"] == nlohmann::json::array({"sheet_port:0"}));
    CHECK(output["sheets"][0]["symbol_fields"] == nlohmann::json::array({"symbol_field:0"}));
    CHECK(output["wire_runs"][0]["route_intent"] == "Orthogonal");
    CHECK(output["junctions"][0]["id"] == "junction:0");
    CHECK(output["junctions"][0]["sheet"] == "sheet:0");
    CHECK(output["junctions"][0]["net"] == "net:0");
    CHECK(output["junctions"][0]["position"] == nlohmann::json({{"x", 30.0}, {"y", 20.0}}));
    CHECK(output["power_ports"][0]["kind"] == "Power");
    CHECK(output["power_ports"][1]["kind"] == "Ground");
    CHECK(output["no_connect_markers"][0]["pin"] == "pin:1");
    CHECK(output["no_connect_markers"][0]["reason"] == "not populated");
    CHECK(output["sheet_ports"][0]["name"] == "VIN");
    CHECK(output["sheet_ports"][0]["kind"] == "OffPage");
    CHECK(output["symbol_fields"][0]["symbol_instance"] == "symbol_instance:0");
    CHECK(output["symbol_fields"][0]["name"] == "value");
    CHECK(output["symbol_fields"][0]["value"] == "10k");
}

TEST_CASE("Schematic writer emits drawing page metadata and named regions") {
    volt::Circuit circuit;

    auto schematic = volt::Schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{
        "Main",
        volt::SheetMetadata{
            "STM32 USB Buck Board",
            volt::SheetSize{297.0, 210.0},
            std::vector{volt::TitleBlockField{"Number", "1/1"},
                        volt::TitleBlockField{"Revision", "2.0"}},
            volt::SheetOrientation::Landscape,
            volt::SheetFrame{true, volt::SheetMargins{12.0, 10.0, 12.0, 10.0}},
            volt::SheetCoordinateZones{10U, 6U},
            volt::SheetGrid{2.5, true},
        },
    });
    [[maybe_unused]] const auto region = schematic.add_sheet_region(
        sheet, volt::SheetRegion{
                   "Power Circuitry",
                   "Power Circuitry",
                   volt::SheetRegionBounds{10.0, 12.0, 260.0, 55.0},
                   std::vector{volt::SheetRegionStyleField{"accent", "orange"}},
               });

    const auto output = nlohmann::json::parse(volt::io::write_schematic(schematic));

    const auto &metadata = output["sheets"][0]["metadata"];
    CHECK(metadata["orientation"] == "Landscape");
    CHECK(metadata["frame"]["visible"] == true);
    CHECK(metadata["frame"]["margins"] ==
          nlohmann::json({{"left", 12.0}, {"top", 10.0}, {"right", 12.0}, {"bottom", 10.0}}));
    CHECK(metadata["coordinate_zones"] ==
          nlohmann::json({{"columns", 10}, {"rows", 6}, {"visible", true}}));
    CHECK(metadata["grid"] == nlohmann::json({{"spacing", 2.5}, {"visible", true}}));
    CHECK(output["sheets"][0]["regions"] ==
          nlohmann::json::array(
              {{{"name", "Power Circuitry"},
                {"title", "Power Circuitry"},
                {"bounds", {{"x", 10.0}, {"y", 12.0}, {"width", 260.0}, {"height", 55.0}}},
                {"style", {{"accent", "orange"}}}}}));
}

TEST_CASE("Schematic writer is deterministic") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto schematic = make_schematic(circuit, component);

    CHECK(volt::io::write_schematic(schematic) == volt::io::write_schematic(schematic));
}
