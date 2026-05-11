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

TEST_CASE("Schematic writer is deterministic") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto schematic = make_schematic(circuit, component);

    CHECK(volt::io::write_schematic(schematic) == volt::io::write_schematic(schematic));
}
