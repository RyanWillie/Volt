#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>

#include <nlohmann/json.hpp>

#include <limits>
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

volt::NetId add_net(volt::Circuit &circuit) {
    return circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});
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
        {"sheets",
         nlohmann::json::array({{{"id", "sheet:0"},
                                 {"name", "Main"},
                                 {"symbol_instances", nlohmann::json::array({"symbol_instance:0"})},
                                 {"wire_runs", nlohmann::json::array({"wire_run:0"})},
                                 {"net_labels", nlohmann::json::array({"net_label:0"})}}})},
        {"symbol_instances", nlohmann::json::array({{{"id", "symbol_instance:0"},
                                                     {"sheet", "sheet:0"},
                                                     {"symbol_definition", "symbol_def:0"},
                                                     {"component", "component:0"},
                                                     {"position", {{"x", 40.0}, {"y", 20.0}}},
                                                     {"orientation", "Right"}}})},
        {"wire_runs", nlohmann::json::array(
                          {{{"id", "wire_run:0"},
                            {"sheet", "sheet:0"},
                            {"net", "net:0"},
                            {"points", nlohmann::json::array({{{"x", 10.0}, {"y", 20.0}},
                                                              {{"x", 40.0}, {"y", 20.0}}})}}})},
        {"net_labels", nlohmann::json::array({{{"id", "net_label:0"},
                                               {"sheet", "sheet:0"},
                                               {"net", "net:0"},
                                               {"position", {{"x", 12.0}, {"y", 16.0}}},
                                               {"orientation", "Right"}}})},
    };
}

} // namespace

TEST_CASE("Schematic reader loads projection JSON over a logical circuit") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = add_net(circuit);

    const auto schematic = volt::io::read_schematic(schematic_json(), circuit);

    REQUIRE(schematic.symbol_definition_count() == 1);
    REQUIRE(schematic.sheet_count() == 1);
    REQUIRE(schematic.symbol_instance_count() == 1);
    REQUIRE(schematic.wire_run_count() == 1);
    REQUIRE(schematic.net_label_count() == 1);
    CHECK(schematic.symbol_definition(volt::SymbolDefId{0}).name() == "Resistor");
    CHECK(schematic.symbol_definition(volt::SymbolDefId{0}).pins()[1].anchor() ==
          volt::Point{20.0, 0.0});
    CHECK(schematic.sheet(volt::SheetId{0}).symbol_instances() ==
          std::vector{volt::SymbolInstanceId{0}});
    CHECK(schematic.symbol_instance(volt::SymbolInstanceId{0}).component() == component);
    CHECK(schematic.symbol_instance(volt::SymbolInstanceId{0}).position() ==
          volt::Point{40.0, 20.0});
    CHECK(schematic.sheet(volt::SheetId{0}).wire_runs() == std::vector{volt::WireRunId{0}});
    CHECK(schematic.sheet(volt::SheetId{0}).net_labels() == std::vector{volt::NetLabelId{0}});
    CHECK(schematic.wire_run(volt::WireRunId{0}).net() == net);
    CHECK(schematic.net_label(volt::NetLabelId{0}).net() == net);
}

TEST_CASE("Schematic reader rejects dangling projection references") {
    volt::Circuit circuit;
    add_resistor(circuit);
    add_net(circuit);

    auto missing_sheet = schematic_json();
    missing_sheet["symbol_instances"][0]["sheet"] = "sheet:99";
    CHECK_THROWS_AS(volt::io::read_schematic(missing_sheet, circuit), std::logic_error);

    auto missing_symbol = schematic_json();
    missing_symbol["symbol_instances"][0]["symbol_definition"] = "symbol_def:99";
    CHECK_THROWS_AS(volt::io::read_schematic(missing_symbol, circuit), std::logic_error);

    auto missing_component = schematic_json();
    missing_component["symbol_instances"][0]["component"] = "component:99";
    CHECK_THROWS_MATCHES(
        volt::io::read_schematic(missing_component, circuit), std::logic_error,
        Catch::Matchers::Message(
            "Component reference points to a missing logical component: component:99"));

    auto missing_net = schematic_json();
    missing_net["wire_runs"][0]["net"] = "net:99";
    CHECK_THROWS_MATCHES(
        volt::io::read_schematic(missing_net, circuit), std::logic_error,
        Catch::Matchers::Message("Net reference points to a missing logical net: net:99"));
}

TEST_CASE("Schematic reader rejects overflowing local ID indices") {
    volt::Circuit circuit;
    add_resistor(circuit);
    add_net(circuit);

    auto overflowing_component = schematic_json();
    overflowing_component["symbol_instances"][0]["component"] =
        "component:" + std::to_string(std::numeric_limits<std::size_t>::max()) + "0";

    CHECK_THROWS_MATCHES(volt::io::read_schematic(overflowing_component, circuit), std::logic_error,
                         Catch::Matchers::Message("Local ID index is too large"));
}

TEST_CASE("Schematic reader rejects non-finite numeric fields at parse time") {
    volt::Circuit circuit;
    add_resistor(circuit);
    add_net(circuit);

    auto fixture = schematic_json();
    fixture["symbol_instances"][0]["position"]["x"] = std::numeric_limits<double>::infinity();

    CHECK_THROWS_MATCHES(volt::io::read_schematic(fixture, circuit), std::logic_error,
                         Catch::Matchers::Message("Schematic numeric field must be finite: x"));
}

TEST_CASE("Schematic reader rejects sheet instance list mismatches") {
    volt::Circuit circuit;
    add_resistor(circuit);
    add_net(circuit);

    auto fixture = schematic_json();
    fixture["sheets"][0]["symbol_instances"] = nlohmann::json::array();

    CHECK_THROWS_AS(volt::io::read_schematic(fixture, circuit), std::logic_error);

    auto wire_fixture = schematic_json();
    wire_fixture["sheets"][0]["wire_runs"] = nlohmann::json::array();

    CHECK_THROWS_AS(volt::io::read_schematic(wire_fixture, circuit), std::logic_error);

    auto label_fixture = schematic_json();
    label_fixture["sheets"][0]["net_labels"] = nlohmann::json::array();

    CHECK_THROWS_AS(volt::io::read_schematic(label_fixture, circuit), std::logic_error);
}

TEST_CASE("Schematic reader rejects wire runs that collide with different logical nets") {
    volt::Circuit circuit;
    add_resistor(circuit);
    add_net(circuit);
    [[maybe_unused]] const auto ground =
        circuit.add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});

    auto fixture = schematic_json();
    fixture["sheets"][0]["wire_runs"].push_back("wire_run:1");
    fixture["wire_runs"].push_back(
        {{"id", "wire_run:1"},
         {"sheet", "sheet:0"},
         {"net", "net:1"},
         {"points",
          nlohmann::json::array({{{"x", 25.0}, {"y", 10.0}}, {{"x", 25.0}, {"y", 30.0}}})}});

    CHECK_NOTHROW(volt::io::read_schematic(fixture, circuit));

    fixture["wire_runs"][1]["points"] =
        nlohmann::json::array({{{"x", 10.0}, {"y", 20.0}}, {{"x", 25.0}, {"y", 20.0}}});
    CHECK_THROWS_MATCHES(
        volt::io::read_schematic(fixture, circuit), std::logic_error,
        Catch::Matchers::Message("Schematic wire run collides with a different logical net"));
}
