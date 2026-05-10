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
    CHECK(output["sheets"][0]["id"] == "sheet:0");
    CHECK(output["sheets"][0]["name"] == "Main");
    CHECK(output["sheets"][0]["symbol_instances"] == nlohmann::json::array({"symbol_instance:0"}));
    CHECK(output["symbol_instances"][0]["id"] == "symbol_instance:0");
    CHECK(output["symbol_instances"][0]["sheet"] == "sheet:0");
    CHECK(output["symbol_instances"][0]["symbol_definition"] == "symbol_def:0");
    CHECK(output["symbol_instances"][0]["component"] == "component:0");
    CHECK(output["symbol_instances"][0]["position"]["x"] == 40.0);
    CHECK(output["symbol_instances"][0]["position"]["y"] == 20.0);
    CHECK(output["symbol_instances"][0]["orientation"] == "Right");
}

TEST_CASE("Schematic writer is deterministic") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto schematic = make_schematic(circuit, component);

    CHECK(volt::io::write_schematic(schematic) == volt::io::write_schematic(schematic));
}
