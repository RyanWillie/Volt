#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/io/schematic_reader.hpp>
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

nlohmann::json schematic_json() {
    return {
        {"format", "volt.schematic"},
        {"version", 1},
        {"symbol_definitions",
         nlohmann::json::array(
             {{{"id", "symbol_def:0"},
               {"name", "Resistor"},
               {"pins", nlohmann::json::array({{{"name", "1"},
                                                {"number", "1"},
                                                {"anchor", {{"x", 0.0}, {"y", 0.0}}},
                                                {"orientation", "Left"}},
                                               {{"name", "2"},
                                                {"number", "2"},
                                                {"anchor", {{"x", 20.0}, {"y", 0.0}}},
                                                {"orientation", "Right"}}})},
               {"primitives",
                nlohmann::json::array(
                    {{{"type", "line"},
                      {"start", {{"x", 0.0}, {"y", 0.0}}},
                      {"end", {{"x", 20.0}, {"y", 0.0}}}},
                     {{"type", "rectangle"},
                      {"first_corner", {{"x", 4.0}, {"y", -3.0}}},
                      {"second_corner", {{"x", 16.0}, {"y", 3.0}}}},
                     {{"type", "circle"}, {"center", {{"x", 10.0}, {"y", 0.0}}}, {"radius", 2.0}},
                     {{"type", "arc"},
                      {"center", {{"x", 10.0}, {"y", 0.0}}},
                      {"radius", 5.0},
                      {"start_degrees", 0.0},
                      {"sweep_degrees", 180.0}},
                     {{"type", "text"},
                      {"text", "R"},
                      {"anchor", {{"x", 10.0}, {"y", -8.0}}},
                      {"orientation", "Right"}}})}}})},
        {"sheets", nlohmann::json::array(
                       {{{"id", "sheet:0"},
                         {"name", "Main"},
                         {"symbol_instances", nlohmann::json::array({"symbol_instance:0"})}}})},
        {"symbol_instances", nlohmann::json::array({{{"id", "symbol_instance:0"},
                                                     {"sheet", "sheet:0"},
                                                     {"symbol_definition", "symbol_def:0"},
                                                     {"component", "component:0"},
                                                     {"position", {{"x", 40.0}, {"y", 20.0}}},
                                                     {"orientation", "Right"}}})},
    };
}

} // namespace

TEST_CASE("Schematic reader loads projection JSON over a logical circuit") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);

    const auto schematic = volt::io::read_schematic(schematic_json(), circuit);

    REQUIRE(schematic.symbol_definition_count() == 1);
    REQUIRE(schematic.sheet_count() == 1);
    REQUIRE(schematic.symbol_instance_count() == 1);
    CHECK(schematic.symbol_definition(volt::SymbolDefId{0}).name() == "Resistor");
    CHECK(schematic.symbol_definition(volt::SymbolDefId{0}).pins()[1].anchor() ==
          volt::Point{20.0, 0.0});
    CHECK(schematic.sheet(volt::SheetId{0}).symbol_instances() ==
          std::vector{volt::SymbolInstanceId{0}});
    CHECK(schematic.symbol_instance(volt::SymbolInstanceId{0}).component() == component);
    CHECK(schematic.symbol_instance(volt::SymbolInstanceId{0}).position() ==
          volt::Point{40.0, 20.0});
}

TEST_CASE("Schematic reader rejects dangling projection references") {
    volt::Circuit circuit;
    add_resistor(circuit);

    auto missing_sheet = schematic_json();
    missing_sheet["symbol_instances"][0]["sheet"] = "sheet:99";
    CHECK_THROWS_AS(volt::io::read_schematic(missing_sheet, circuit), std::logic_error);

    auto missing_symbol = schematic_json();
    missing_symbol["symbol_instances"][0]["symbol_definition"] = "symbol_def:99";
    CHECK_THROWS_AS(volt::io::read_schematic(missing_symbol, circuit), std::logic_error);

    auto missing_component = schematic_json();
    missing_component["symbol_instances"][0]["component"] = "component:99";
    CHECK_THROWS_AS(volt::io::read_schematic(missing_component, circuit), std::out_of_range);
}

TEST_CASE("Schematic reader rejects sheet instance list mismatches") {
    volt::Circuit circuit;
    add_resistor(circuit);

    auto fixture = schematic_json();
    fixture["sheets"][0]["symbol_instances"] = nlohmann::json::array();

    CHECK_THROWS_AS(volt::io::read_schematic(fixture, circuit), std::logic_error);
}
