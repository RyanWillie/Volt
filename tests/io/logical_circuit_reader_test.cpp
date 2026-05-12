#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>

#include <fstream>
#include <iterator>
#include <string>

#include <volt/io/logical_circuit_reader.hpp>
#include <volt/io/logical_circuit_writer.hpp>

namespace {

std::string read_fixture(const std::string &name) {
    auto input = std::ifstream{std::string{VOLT_TEST_FIXTURE_DIR} + "/" + name};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

} // namespace

TEST_CASE("Logical circuit reader round-trips the LED fixture") {
    const auto fixture = read_fixture("led_circuit.volt.json");

    const auto circuit = volt::io::read_logical_circuit_text(fixture);

    CHECK(volt::io::write_logical_circuit(circuit) == fixture);
}

TEST_CASE("Logical circuit reader preserves component definition source metadata") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["component_definitions"][1]["source"] = {
        {"namespace", "volt.passives"}, {"name", "resistor_2pin"}, {"version", "1.0.0"}};

    const auto circuit = volt::io::read_logical_circuit(fixture);
    const auto output = nlohmann::json::parse(volt::io::write_logical_circuit(circuit));

    CHECK(output["component_definitions"][1]["source"] ==
          fixture["component_definitions"][1]["source"]);
}

TEST_CASE("Logical circuit reader preserves typed electrical attributes") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["components"][1]["electrical_attributes"] = {
        {"resistance", {{"type", "quantity"}, {"dimension", "resistance"}, {"value", 330.0}}},
        {"tolerance",
         {{"type", "tolerance"},
          {"mode", "percent"},
          {"dimension", "ratio"},
          {"minus", 0.01},
          {"plus", 0.01}}},
    };
    fixture["components"][1]["selected_physical_part"]["electrical_attributes"] = {
        {"voltage_rating", {{"type", "quantity"}, {"dimension", "voltage"}, {"value", 75.0}}},
    };

    const auto circuit = volt::io::read_logical_circuit(fixture);
    const auto &component = circuit.component(volt::ComponentId{1});
    const auto &selected_part = circuit.selected_physical_part(volt::ComponentId{1}).value();

    CHECK(component.electrical_attributes()
              .get(volt::ElectricalAttributeName{"resistance"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Resistance, 330.0});
    CHECK(component.electrical_attributes()
              .get(volt::ElectricalAttributeName{"tolerance"})
              .as_tolerance()
              .plus() == volt::Quantity{volt::UnitDimension::Ratio, 0.01});
    CHECK(selected_part.electrical_attributes()
              .get(volt::ElectricalAttributeName{"voltage_rating"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 75.0});
}

TEST_CASE("Logical circuit reader preserves net typed electrical attributes") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["nets"][0]["electrical_attributes"] = {
        {"voltage", {{"type", "quantity"}, {"dimension", "voltage"}, {"value", 3.3}}},
    };

    const auto circuit = volt::io::read_logical_circuit(fixture);

    CHECK(circuit.net(volt::NetId{0})
              .electrical_attributes()
              .get(volt::ElectricalAttributeName{"voltage"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 3.3});
}

TEST_CASE("Logical circuit reader preserves design intent") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["design_intent"] = {
        {"stub_nets", nlohmann::json::array({"net:0"})},
        {"no_connect_pins", nlohmann::json::array({"pin:5"})},
    };

    const auto circuit = volt::io::read_logical_circuit(fixture);

    CHECK(circuit.is_intentional_stub_net(volt::NetId{0}));
    CHECK(circuit.is_intentional_no_connect_pin(volt::PinId{5}));
    const auto output = nlohmann::json::parse(volt::io::write_logical_circuit(circuit));
    CHECK(output["design_intent"] == fixture["design_intent"]);
}

TEST_CASE("Logical circuit reader rejects malformed design intent references") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["design_intent"] = {
        {"stub_nets", nlohmann::json::array({"net:99"})},
        {"no_connect_pins", nlohmann::json::array()},
    };

    CHECK_THROWS_AS(volt::io::read_logical_circuit(fixture), std::logic_error);
}

TEST_CASE("Logical circuit reader preserves pin electrical semantics") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    auto &pin = fixture["pin_definitions"][0];
    pin["terminal_kind"] = "Signal";
    pin["direction"] = "Output";
    pin["signal_domain"] = "Digital";
    pin["drive_kind"] = "PushPull";
    pin["polarity"] = "ActiveHigh";
    pin["electrical_attributes"] = {
        {"voltage_range",
         {{"type", "range"}, {"dimension", "voltage"}, {"minimum", 0.0}, {"maximum", 5.5}}},
    };

    const auto circuit = volt::io::read_logical_circuit(fixture);
    const auto &definition = circuit.pin_definition(volt::PinDefId{0});

    CHECK(definition.terminal_kind() == volt::ElectricalTerminalKind::Signal);
    CHECK(definition.direction() == volt::ElectricalDirection::Output);
    CHECK(definition.signal_domain() == volt::ElectricalSignalDomain::Digital);
    CHECK(definition.drive_kind() == volt::ElectricalDriveKind::PushPull);
    CHECK(definition.polarity() == volt::ElectricalPolarity::ActiveHigh);

    const auto &range = definition.electrical_attributes()
                            .get(volt::ElectricalAttributeName{"voltage_range"})
                            .as_range();
    REQUIRE(range.minimum().has_value());
    REQUIRE(range.maximum().has_value());
    CHECK(range.minimum().value() == volt::Quantity{volt::UnitDimension::Voltage, 0.0});
    CHECK(range.maximum().value() == volt::Quantity{volt::UnitDimension::Voltage, 5.5});
}

TEST_CASE("Logical circuit reader preserves hierarchy module scaffold") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["pin_definitions"].push_back({{"id", "pin_def:6"},
                                          {"name", "1"},
                                          {"number", "1"},
                                          {"role", "Passive"},
                                          {"connection_requirement", "Required"}});
    fixture["pin_definitions"].push_back({{"id", "pin_def:7"},
                                          {"name", "2"},
                                          {"number", "2"},
                                          {"role", "Passive"},
                                          {"connection_requirement", "Required"}});
    fixture["component_definitions"].push_back(
        {{"id", "component_def:3"},
         {"name", "Resistor"},
         {"pins", nlohmann::json::array({"pin_def:6", "pin_def:7"})},
         {"properties", nlohmann::json::object()}});
    fixture["components"].push_back({{"id", "component:3"},
                                     {"definition", "component_def:3"},
                                     {"reference", "BUCK_A/R1"},
                                     {"properties", nlohmann::json::object()}});
    fixture["pins"].push_back(
        {{"id", "pin:6"}, {"component", "component:3"}, {"definition", "pin_def:6"}});
    fixture["pins"].push_back(
        {{"id", "pin:7"}, {"component", "component:3"}, {"definition", "pin_def:7"}});
    fixture["nets"].push_back({{"id", "net:3"},
                               {"name", "BUCK_A/VIN"},
                               {"kind", "Power"},
                               {"pins", nlohmann::json::array({"pin:6"})}});
    fixture["nets"].push_back({{"id", "net:4"},
                               {"name", "BUCK_A/FB"},
                               {"kind", "Signal"},
                               {"pins", nlohmann::json::array({"pin:7"})}});
    fixture["module_definitions"] = nlohmann::json::array(
        {{{"id", "module_def:0"},
          {"name", "BuckConverter"},
          {"local_nets",
           nlohmann::json::array({{{"id", "template_net:0"}, {"name", "VIN"}, {"kind", "Power"}},
                                  {{"id", "template_net:1"}, {"name", "FB"}, {"kind", "Signal"}}})},
          {"components", nlohmann::json::array({{{"id", "module_component:0"},
                                                 {"definition", "component_def:3"},
                                                 {"reference", "R1"},
                                                 {"properties", nlohmann::json::object()}}})},
          {"connections", nlohmann::json::array({{{"net", "template_net:0"},
                                                  {"component", "module_component:0"},
                                                  {"pin", "pin_def:6"}},
                                                 {{"net", "template_net:1"},
                                                  {"component", "module_component:0"},
                                                  {"pin", "pin_def:7"}}})},
          {"ports", nlohmann::json::array({{{"id", "port:0"},
                                            {"name", "VIN"},
                                            {"internal_net", "template_net:0"},
                                            {"role", "PowerInput"},
                                            {"required", true}}})}}});
    fixture["module_instances"] = nlohmann::json::array(
        {{{"id", "module:0"},
          {"definition", "module_def:0"},
          {"name", "BUCK_A"},
          {"net_origins",
           nlohmann::json::array({{{"template_net", "template_net:0"}, {"net", "net:3"}},
                                  {{"template_net", "template_net:1"}, {"net", "net:4"}}})},
          {"component_origins",
           nlohmann::json::array(
               {{{"template_component", "module_component:0"}, {"component", "component:3"}}})},
          {"port_bindings",
           nlohmann::json::array({{{"port", "port:0"}, {"parent_net", "net:0"}}})}}});

    const auto circuit = volt::io::read_logical_circuit(fixture);

    CHECK(circuit.module_definition_count() == 1);
    CHECK(circuit.template_net_definition_count() == 2);
    CHECK(circuit.port_definition_count() == 1);
    CHECK(circuit.module_component_count() == 1);
    CHECK(circuit.module_pin_connection_count() == 2);
    CHECK(circuit.module_instance_count() == 1);
    CHECK(circuit.port_binding_count() == 1);
    CHECK(circuit.module_definition(volt::ModuleDefId{0}).name() ==
          volt::ModuleName{"BuckConverter"});
    CHECK(circuit.concrete_net_for(volt::ModuleInstanceId{0}, volt::TemplateNetDefId{0}) ==
          volt::NetId{3});
    CHECK(circuit.concrete_net_for(volt::ModuleInstanceId{0}, volt::TemplateNetDefId{1}) ==
          volt::NetId{4});
    CHECK(circuit.concrete_component_for(volt::ModuleInstanceId{0}, volt::ModuleComponentId{0}) ==
          volt::ComponentId{3});
    CHECK(circuit.port_binding(volt::PortBindingId{0}).parent_net() == volt::NetId{0});
}

TEST_CASE("Logical circuit reader infers missing module component origins for v1 fixtures") {
    auto fixture = nlohmann::json::parse(read_fixture("hierarchy_module.volt.json"));
    fixture["module_instances"][0].erase("component_origins");

    const auto circuit = volt::io::read_logical_circuit(fixture);

    CHECK(circuit.module_instance_count() == 1);
    CHECK(circuit.module_component_count() == 1);
    CHECK(circuit.concrete_component_for(volt::ModuleInstanceId{0}, volt::ModuleComponentId{0}) ==
          volt::ComponentId{0});
}

TEST_CASE("Logical circuit reader rejects mismatched module component origin connectivity") {
    auto fixture = nlohmann::json::parse(read_fixture("hierarchy_module.volt.json"));
    fixture["nets"][0]["pins"] = nlohmann::json::array();
    fixture["nets"][1]["pins"] = nlohmann::json::array({"pin:0", "pin:1"});

    CHECK_THROWS_AS(volt::io::read_logical_circuit(fixture), std::logic_error);
}

TEST_CASE("Logical circuit reader defaults missing typed electrical attributes to empty maps") {
    const auto circuit = volt::io::read_logical_circuit_text(read_fixture("led_circuit.volt.json"));

    CHECK(circuit.pin_definition(volt::PinDefId{0}).terminal_kind() ==
          volt::ElectricalTerminalKind::Unspecified);
    CHECK(circuit.pin_definition(volt::PinDefId{0}).electrical_attributes().empty());
    CHECK(circuit.component(volt::ComponentId{1}).electrical_attributes().empty());
    CHECK(circuit.net(volt::NetId{0}).electrical_attributes().empty());
    REQUIRE(circuit.selected_physical_part(volt::ComponentId{1}).has_value());
    CHECK(circuit.selected_physical_part(volt::ComponentId{1})
              .value()
              .electrical_attributes()
              .empty());
}

TEST_CASE("Logical circuit reader rejects malformed hierarchy references") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["module_definitions"] = nlohmann::json::array(
        {{{"id", "module_def:0"},
          {"name", "BuckConverter"},
          {"local_nets",
           nlohmann::json::array({{{"id", "template_net:0"}, {"name", "VIN"}, {"kind", "Power"}}})},
          {"ports", nlohmann::json::array({{{"id", "port:0"},
                                            {"name", "VIN"},
                                            {"internal_net", "template_net:99"},
                                            {"role", "PowerInput"},
                                            {"required", true}}})}}});

    CHECK_THROWS_AS(volt::io::read_logical_circuit(fixture), std::logic_error);
}

TEST_CASE("Logical circuit reader rejects hierarchy self-bindings") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["nets"].push_back({{"id", "net:3"},
                               {"name", "BUCK_A/VIN"},
                               {"kind", "Power"},
                               {"pins", nlohmann::json::array()}});
    fixture["module_definitions"] = nlohmann::json::array(
        {{{"id", "module_def:0"},
          {"name", "BuckConverter"},
          {"local_nets",
           nlohmann::json::array({{{"id", "template_net:0"}, {"name", "VIN"}, {"kind", "Power"}}})},
          {"ports", nlohmann::json::array({{{"id", "port:0"},
                                            {"name", "VIN"},
                                            {"internal_net", "template_net:0"},
                                            {"role", "PowerInput"},
                                            {"required", true}}})}}});
    fixture["module_instances"] = nlohmann::json::array(
        {{{"id", "module:0"},
          {"definition", "module_def:0"},
          {"name", "BUCK_A"},
          {"net_origins",
           nlohmann::json::array({{{"template_net", "template_net:0"}, {"net", "net:3"}}})},
          {"port_bindings",
           nlohmann::json::array({{{"port", "port:0"}, {"parent_net", "net:3"}}})}}});

    CHECK_THROWS_AS(volt::io::read_logical_circuit(fixture), std::logic_error);
}

TEST_CASE("Logical circuit reader rejects duplicate net pin references") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["nets"][0]["pins"].push_back("pin:0");

    CHECK_THROWS_AS(volt::io::read_logical_circuit(fixture), std::logic_error);
}

TEST_CASE("Logical circuit reader rejects invalid pin electrical enum values") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["pin_definitions"][0]["terminal_kind"] = "ThresholdInput";

    CHECK_THROWS_AS(volt::io::read_logical_circuit(fixture), std::logic_error);
}

TEST_CASE("Logical circuit reader rejects dangling references") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["component_definitions"][0]["pins"][0] = "pin_def:999";

    CHECK_THROWS_AS(volt::io::read_logical_circuit(fixture), std::logic_error);
}

TEST_CASE("Logical circuit reader rejects wrong typed local IDs") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["pins"][0]["id"] = "component:99";

    CHECK_THROWS_AS(volt::io::read_logical_circuit(fixture), std::logic_error);
}

TEST_CASE("Logical circuit reader reports unsupported versions deterministically") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["version"] = 2;

    CHECK_THROWS_MATCHES(volt::io::read_logical_circuit(fixture), std::logic_error,
                         Catch::Matchers::Message("Unsupported logical circuit format version: 2"));
}

TEST_CASE("Logical circuit reader reports large unsupported versions deterministically") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["version"] = 2147483648LL;

    CHECK_THROWS_MATCHES(
        volt::io::read_logical_circuit(fixture), std::logic_error,
        Catch::Matchers::Message("Unsupported logical circuit format version: 2147483648"));
}

TEST_CASE("Logical circuit reader reports unsupported formats deterministically") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["format"] = "volt.other";

    CHECK_THROWS_MATCHES(
        volt::io::read_logical_circuit(fixture), std::logic_error,
        Catch::Matchers::Message("Unsupported logical circuit format: volt.other"));
}

TEST_CASE(
    "Logical circuit reader rejects selected part mappings outside the component definition") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["components"][0]["selected_physical_part"]["pin_pad_mappings"][0]["pin"] = "pin_def:2";

    CHECK_THROWS_AS(volt::io::read_logical_circuit(fixture), std::logic_error);
}
