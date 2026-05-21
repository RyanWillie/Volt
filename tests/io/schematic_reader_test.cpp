#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>

#include <nlohmann/json.hpp>

#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
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

TEST_CASE("Schematic reader loads optional net label display text") {
    volt::Circuit circuit;
    [[maybe_unused]] const auto component = add_resistor(circuit);
    [[maybe_unused]] const auto net =
        circuit.add_net(volt::Net{volt::NetName{"SUPPORT/SWDIO"}, volt::NetKind::Signal});

    auto fixture = schematic_json();
    fixture["net_labels"][0]["label"] = "SWDIO";
    fixture["net_labels"][0]["text_position"] = {{"x", 18.0}, {"y", 14.0}};

    const auto schematic = volt::io::read_schematic(fixture, circuit);

    REQUIRE(schematic.net_label_count() == 1);
    CHECK(schematic.net_label(volt::NetLabelId{0}).label() == std::optional<std::string>{"SWDIO"});
    CHECK(schematic.net_label(volt::NetLabelId{0}).text_position() == volt::Point{18.0, 14.0});
}

TEST_CASE("Schematic reader loads explicit text presentation metadata") {
    volt::Circuit circuit;
    [[maybe_unused]] const auto component = add_resistor(circuit);
    [[maybe_unused]] const auto net =
        circuit.add_net(volt::Net{volt::NetName{"SUPPORT/SWDIO"}, volt::NetKind::Signal});

    auto fixture = schematic_json();
    fixture["symbol_definitions"][0]["primitives"][4]["horizontal_alignment"] = "Start";
    fixture["symbol_definitions"][0]["primitives"][4]["vertical_alignment"] = "Top";
    fixture["symbol_definitions"][0]["primitives"][4]["font_size"] = 3.25;
    fixture["net_labels"][0]["horizontal_alignment"] = "End";
    fixture["net_labels"][0]["vertical_alignment"] = "Bottom";
    fixture["net_labels"][0]["font_size"] = 4.0;
    fixture["sheets"][0]["symbol_fields"] = nlohmann::json::array({"symbol_field:0"});
    fixture["symbol_fields"] = nlohmann::json::array({{{"id", "symbol_field:0"},
                                                       {"sheet", "sheet:0"},
                                                       {"symbol_instance", "symbol_instance:0"},
                                                       {"name", "value"},
                                                       {"value", "10k"},
                                                       {"position", {{"x", 40.0}, {"y", 32.0}}},
                                                       {"orientation", "Right"},
                                                       {"horizontal_alignment", "Start"},
                                                       {"vertical_alignment", "Top"},
                                                       {"font_size", 3.5}}});

    const auto schematic = volt::io::read_schematic(fixture, circuit);

    const auto &primitive = std::get<volt::SymbolText>(
        schematic.symbol_definition(volt::SymbolDefId{0}).primitives()[4]);
    CHECK(primitive.style().horizontal_alignment() == volt::TextHorizontalAlignment::Start);
    CHECK(primitive.style().vertical_alignment() == volt::TextVerticalAlignment::Top);
    REQUIRE(primitive.style().font_size().has_value());
    CHECK(primitive.style().font_size().value() == 3.25);

    const auto &label = schematic.net_label(volt::NetLabelId{0});
    CHECK(label.style().horizontal_alignment() == volt::TextHorizontalAlignment::End);
    CHECK(label.style().vertical_alignment() == volt::TextVerticalAlignment::Bottom);
    REQUIRE(label.style().font_size().has_value());
    CHECK(label.style().font_size().value() == 4.0);

    const auto &field = schematic.symbol_field(volt::SymbolFieldId{0});
    CHECK(field.style().horizontal_alignment() == volt::TextHorizontalAlignment::Start);
    CHECK(field.style().vertical_alignment() == volt::TextVerticalAlignment::Top);
    REQUIRE(field.style().font_size().has_value());
    CHECK(field.style().font_size().value() == 3.5);
}

TEST_CASE("Schematic reader rejects invalid text presentation metadata") {
    volt::Circuit circuit;
    [[maybe_unused]] const auto component = add_resistor(circuit);
    [[maybe_unused]] const auto net = add_net(circuit);

    auto invalid_alignment = schematic_json();
    invalid_alignment["net_labels"][0]["horizontal_alignment"] = "Centerish";
    CHECK_THROWS_MATCHES(volt::io::read_schematic(invalid_alignment, circuit), std::logic_error,
                         Catch::Matchers::Message("Invalid text horizontal alignment"));

    auto invalid_font_size = schematic_json();
    invalid_font_size["symbol_definitions"][0]["primitives"][4]["font_size"] = 0.0;
    CHECK_THROWS_MATCHES(
        volt::io::read_schematic(invalid_font_size, circuit), std::logic_error,
        Catch::Matchers::Message("Expected positive finite number field: font_size"));
}

TEST_CASE("Schematic reader rejects instance-owned reference labels") {
    volt::Circuit circuit;
    [[maybe_unused]] const auto component = add_resistor(circuit);
    [[maybe_unused]] const auto net = add_net(circuit);
    auto fixture = schematic_json();
    fixture["symbol_instances"][0]["reference_label"] = "R1";

    CHECK_THROWS_MATCHES(
        volt::io::read_schematic(fixture, circuit), std::logic_error,
        Catch::Matchers::Message("Schematic symbol instance reference_label is no longer "
                                 "supported; use a symbol_fields entry named reference"));
}

TEST_CASE("Schematic reader loads professional primitives over logical IDs") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto vcc = add_net(circuit);
    const auto gnd = circuit.add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});
    const auto no_connect_pin = circuit.pin_by_number(component, "2").value();
    circuit.mark_intentional_no_connect_pin(no_connect_pin);

    auto fixture = schematic_json();
    fixture["sheets"][0]["metadata"] = {
        {"title", "Power sheet"},
        {"size", {{"width", 420.0}, {"height", 297.0}}},
        {"title_block", nlohmann::json::array({{{"key", "Revision"}, {"value", "A"}}})}};
    fixture["sheets"][0]["junctions"] = nlohmann::json::array({"junction:0"});
    fixture["sheets"][0]["power_ports"] = nlohmann::json::array({"power_port:0", "power_port:1"});
    fixture["sheets"][0]["no_connect_markers"] = nlohmann::json::array({"no_connect_marker:0"});
    fixture["sheets"][0]["sheet_ports"] = nlohmann::json::array({"sheet_port:0"});
    fixture["sheets"][0]["symbol_fields"] = nlohmann::json::array({"symbol_field:0"});
    fixture["wire_runs"][0]["route_intent"] = "Orthogonal";
    fixture["junctions"] = nlohmann::json::array({{{"id", "junction:0"},
                                                   {"sheet", "sheet:0"},
                                                   {"net", "net:0"},
                                                   {"position", {{"x", 40.0}, {"y", 20.0}}}}});
    fixture["power_ports"] = nlohmann::json::array({{{"id", "power_port:0"},
                                                     {"sheet", "sheet:0"},
                                                     {"net", "net:0"},
                                                     {"kind", "Power"},
                                                     {"position", {{"x", 12.0}, {"y", 16.0}}},
                                                     {"orientation", "Up"}},
                                                    {{"id", "power_port:1"},
                                                     {"sheet", "sheet:0"},
                                                     {"net", "net:1"},
                                                     {"kind", "Ground"},
                                                     {"position", {{"x", 60.0}, {"y", 24.0}}},
                                                     {"orientation", "Down"}}});
    fixture["no_connect_markers"] =
        nlohmann::json::array({{{"id", "no_connect_marker:0"},
                                {"sheet", "sheet:0"},
                                {"pin", "pin:1"},
                                {"position", {{"x", 65.0}, {"y", 20.0}}},
                                {"orientation", "Right"},
                                {"reason", "not populated"}}});
    fixture["sheet_ports"] = nlohmann::json::array({{{"id", "sheet_port:0"},
                                                     {"sheet", "sheet:0"},
                                                     {"net", "net:0"},
                                                     {"name", "VIN"},
                                                     {"kind", "OffPage"},
                                                     {"position", {{"x", 5.0}, {"y", 20.0}}},
                                                     {"orientation", "Right"}}});
    fixture["symbol_fields"] = nlohmann::json::array({{{"id", "symbol_field:0"},
                                                       {"sheet", "sheet:0"},
                                                       {"symbol_instance", "symbol_instance:0"},
                                                       {"name", "value"},
                                                       {"value", "10k"},
                                                       {"position", {{"x", 40.0}, {"y", 32.0}}},
                                                       {"orientation", "Right"}}});

    const auto schematic = volt::io::read_schematic(fixture, circuit);

    CHECK(schematic.sheet(volt::SheetId{0}).metadata().title() == "Power sheet");
    CHECK(schematic.sheet(volt::SheetId{0}).metadata().size().width() == 420.0);
    CHECK(schematic.wire_run(volt::WireRunId{0}).route_intent() == volt::RouteIntent::Orthogonal);
    CHECK(schematic.junction_count() == 1);
    CHECK(schematic.power_port_count() == 2);
    CHECK(schematic.no_connect_marker_count() == 1);
    CHECK(schematic.sheet_port_count() == 1);
    CHECK(schematic.symbol_field_count() == 1);
    CHECK(schematic.junction(volt::JunctionId{0}).net() == vcc);
    CHECK(schematic.power_port(volt::PowerPortId{1}).net() == gnd);
    CHECK(schematic.no_connect_marker(volt::NoConnectMarkerId{0}).pin() == no_connect_pin);
    CHECK(schematic.no_connect_marker(volt::NoConnectMarkerId{0}).reason() == "not populated");
    CHECK(schematic.sheet_port(volt::SheetPortId{0}).name() == "VIN");
    CHECK(schematic.symbol_field(volt::SymbolFieldId{0}).value() == "10k");
    CHECK(circuit.net(vcc).pins().empty());
    CHECK(circuit.net(gnd).pins().empty());
}

TEST_CASE("Schematic reader loads drawing page metadata and named regions") {
    volt::Circuit circuit;

    auto fixture = schematic_json();
    fixture["sheets"][0]["symbol_instances"] = nlohmann::json::array();
    fixture["symbol_instances"] = nlohmann::json::array();
    fixture["wire_runs"] = nlohmann::json::array();
    fixture["net_labels"] = nlohmann::json::array();
    fixture["sheets"][0]["wire_runs"] = nlohmann::json::array();
    fixture["sheets"][0]["net_labels"] = nlohmann::json::array();
    fixture["sheets"][0]["metadata"] = {
        {"title", "STM32 USB Buck Board"},
        {"orientation", "Landscape"},
        {"size", {{"width", 297.0}, {"height", 210.0}}},
        {"title_block", nlohmann::json::array({{{"key", "Number"}, {"value", "1/1"}},
                                               {{"key", "Revision"}, {"value", "2.0"}}})},
        {"frame",
         {{"visible", true},
          {"margins", {{"left", 12.0}, {"top", 10.0}, {"right", 12.0}, {"bottom", 10.0}}}}},
        {"coordinate_zones", {{"columns", 10}, {"rows", 6}, {"visible", true}}},
        {"grid", {{"spacing", 2.5}, {"visible", true}}},
    };
    fixture["sheets"][0]["regions"] = nlohmann::json::array(
        {{{"name", "Power Circuitry"},
          {"title", "Power Circuitry"},
          {"bounds", {{"x", 10.0}, {"y", 12.0}, {"width", 260.0}, {"height", 55.0}}},
          {"style", {{"accent", "orange"}}}}});

    const auto schematic = volt::io::read_schematic(fixture, circuit);
    const auto &sheet = schematic.sheet(volt::SheetId{0});

    CHECK(sheet.metadata().orientation() == volt::SheetOrientation::Landscape);
    CHECK(sheet.metadata().frame().margins().left() == 12.0);
    REQUIRE(sheet.metadata().coordinate_zones().has_value());
    CHECK(sheet.metadata().coordinate_zones()->columns() == 10U);
    REQUIRE(sheet.metadata().grid().has_value());
    CHECK(sheet.metadata().grid()->spacing() == 2.5);
    REQUIRE(sheet.regions().size() == 1);
    CHECK(sheet.regions()[0].name() == "Power Circuitry");
    CHECK(sheet.regions()[0].bounds().x() == 10.0);
    CHECK(sheet.regions()[0].style()[0].key() == "accent");
    CHECK(sheet.regions()[0].style()[0].value() == "orange");
}

TEST_CASE("Schematic reader rejects duplicate sheet region names") {
    volt::Circuit circuit;

    auto fixture = schematic_json();
    fixture["sheets"][0]["symbol_instances"] = nlohmann::json::array();
    fixture["symbol_instances"] = nlohmann::json::array();
    fixture["wire_runs"] = nlohmann::json::array();
    fixture["net_labels"] = nlohmann::json::array();
    fixture["sheets"][0]["wire_runs"] = nlohmann::json::array();
    fixture["sheets"][0]["net_labels"] = nlohmann::json::array();
    fixture["sheets"][0]["regions"] = nlohmann::json::array(
        {{{"name", "Power"},
          {"title", "Power"},
          {"bounds", {{"x", 10.0}, {"y", 12.0}, {"width", 260.0}, {"height", 55.0}}},
          {"style", nlohmann::json::object()}},
         {{"name", "Power"},
          {"title", "Power"},
          {"bounds", {{"x", 15.0}, {"y", 12.0}, {"width", 260.0}, {"height", 55.0}}},
          {"style", nlohmann::json::object()}}});

    CHECK_THROWS_MATCHES(volt::io::read_schematic(fixture, circuit), std::logic_error,
                         Catch::Matchers::Message("Sheet region name already exists"));
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

TEST_CASE("Schematic reader rejects dangling professional primitive references") {
    volt::Circuit circuit;
    add_resistor(circuit);
    add_net(circuit);

    auto missing_power_net = schematic_json();
    missing_power_net["sheets"][0]["power_ports"] = nlohmann::json::array({"power_port:0"});
    missing_power_net["power_ports"] =
        nlohmann::json::array({{{"id", "power_port:0"},
                                {"sheet", "sheet:0"},
                                {"net", "net:99"},
                                {"kind", "Power"},
                                {"position", {{"x", 0.0}, {"y", 0.0}}},
                                {"orientation", "Up"}}});
    CHECK_THROWS_MATCHES(
        volt::io::read_schematic(missing_power_net, circuit), std::logic_error,
        Catch::Matchers::Message("Net reference points to a missing logical net: net:99"));

    auto missing_pin = schematic_json();
    missing_pin["sheets"][0]["no_connect_markers"] = nlohmann::json::array({"no_connect_marker:0"});
    missing_pin["no_connect_markers"] =
        nlohmann::json::array({{{"id", "no_connect_marker:0"},
                                {"sheet", "sheet:0"},
                                {"pin", "pin:99"},
                                {"position", {{"x", 0.0}, {"y", 0.0}}},
                                {"orientation", "Right"}}});
    CHECK_THROWS_MATCHES(
        volt::io::read_schematic(missing_pin, circuit), std::logic_error,
        Catch::Matchers::Message("Pin reference points to a missing logical pin: pin:99"));

    auto missing_instance = schematic_json();
    missing_instance["sheets"][0]["symbol_fields"] = nlohmann::json::array({"symbol_field:0"});
    missing_instance["symbol_fields"] =
        nlohmann::json::array({{{"id", "symbol_field:0"},
                                {"sheet", "sheet:0"},
                                {"symbol_instance", "symbol_instance:99"},
                                {"name", "value"},
                                {"value", "10k"},
                                {"position", {{"x", 0.0}, {"y", 0.0}}},
                                {"orientation", "Right"}}});
    CHECK_THROWS_AS(volt::io::read_schematic(missing_instance, circuit), std::logic_error);
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
