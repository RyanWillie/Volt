#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <string>

#include <volt/circuit/queries.hpp>
#include <volt/circuit/validation.hpp>
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

    const auto circuit = volt::io::read_logical_circuit_text(fixture.dump());
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

    const auto circuit = volt::io::read_logical_circuit_text(fixture.dump());
    const auto &selected_part = circuit.selected_physical_part(volt::ComponentId{1}).value();

    CHECK(circuit.component_electrical_attributes(volt::ComponentId{1})
              .get(volt::ElectricalAttributeName{"resistance"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Resistance, 330.0});
    CHECK(circuit.component_electrical_attributes(volt::ComponentId{1})
              .get(volt::ElectricalAttributeName{"tolerance"})
              .as_tolerance()
              .plus() == volt::Quantity{volt::UnitDimension::Ratio, 0.01});
    CHECK(selected_part.electrical_attributes()
              .get(volt::ElectricalAttributeName{"voltage_rating"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 75.0});
}

TEST_CASE("Logical circuit reader preserves selected-part 3D model metadata") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["components"][1]["selected_physical_part"]["model_3d"] = {
        {"kind", "asset"},
        {"format", "glb"},
        {"file_name", "resistor-body.glb"},
        {"translation_mm", nlohmann::json::array({0.5, -0.25, 0.8})},
        {"rotation_deg", 15.0},
    };

    const auto circuit = volt::io::read_logical_circuit_text(fixture.dump());
    const auto &selected_part = circuit.selected_physical_part(volt::ComponentId{1}).value();

    REQUIRE(selected_part.model_3d().has_value());
    CHECK(selected_part.model_3d()->format() == "glb");
    CHECK(selected_part.model_3d()->file_name() == "resistor-body.glb");
    CHECK(selected_part.model_3d()->translation_mm() == std::array<double, 3>{0.5, -0.25, 0.8});
    CHECK(selected_part.model_3d()->rotation_deg() == 15.0);
    CHECK(nlohmann::json::parse(volt::io::write_logical_circuit(circuit))["components"][1]
              ["selected_physical_part"]["model_3d"]
          == fixture["components"][1]["selected_physical_part"]["model_3d"]);
}

TEST_CASE("Logical circuit reader preserves net typed electrical attributes") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["nets"][0]["electrical_attributes"] = {
        {"voltage", {{"type", "quantity"}, {"dimension", "voltage"}, {"value", 3.3}}},
    };

    const auto circuit = volt::io::read_logical_circuit_text(fixture.dump());

    CHECK(circuit.net_electrical_attributes(volt::NetId{0})
              .get(volt::ElectricalAttributeName{"voltage"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 3.3});
}

TEST_CASE("Logical circuit reader preserves design intent") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["design_intent"] = {
        {"stub_nets", nlohmann::json::array({"net:0"})},
        {"no_connect_pins", nlohmann::json::array({"pin:5"})},
    };

    const auto circuit = volt::io::read_logical_circuit_text(fixture.dump());

    CHECK(circuit.is_intentional_stub_net(volt::NetId{0}));
    CHECK(circuit.is_intentional_no_connect_pin(volt::PinId{5}));
    const auto output = nlohmann::json::parse(volt::io::write_logical_circuit(circuit));
    CHECK(output["design_intent"] == fixture["design_intent"]);
}

TEST_CASE("Logical circuit reader preserves rule classes and net assignments") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["nets"][0]["electrical_attributes"] = {
        {"voltage", {{"type", "quantity"}, {"dimension", "voltage"}, {"value", 5.0}}},
    };
    fixture["rule_classes"] = {
        {"classes", nlohmann::json::array(
                        {{{"id", "rule_class:0"},
                          {"name", "Logic"},
                          {"maximum_net_voltage", {{"dimension", "voltage"}, {"value", 3.6}}},
                          {"copper_clearance_mm", 0.25}}})},
        {"net_assignments",
         nlohmann::json::array({{{"net", "net:0"}, {"rule_class", "rule_class:0"}}})},
    };

    const auto circuit = volt::io::read_logical_circuit_text(fixture.dump());

    CHECK(circuit.rule_class_count() == 1);
    REQUIRE(circuit.rule_class_for_net(volt::NetId{0}).has_value());
    CHECK(circuit.rule_class_for_net(volt::NetId{0}).value() == volt::RuleClassId{0});
    CHECK(circuit.rule_class(volt::RuleClassId{0}).name() == volt::RuleClassName{"Logic"});
    REQUIRE(circuit.rule_class(volt::RuleClassId{0}).maximum_net_voltage().has_value());
    CHECK(circuit.rule_class(volt::RuleClassId{0}).maximum_net_voltage()->value() == 3.6);
    REQUIRE(circuit.rule_class(volt::RuleClassId{0}).copper_clearance_mm().has_value());
    CHECK(circuit.rule_class(volt::RuleClassId{0}).copper_clearance_mm().value() == 0.25);

    const auto report = volt::validate_electrical_rules(circuit);
    REQUIRE(report.count() == 1);
    CHECK(report.diagnostics().front().code() ==
          volt::DiagnosticCode{"NET_RULE_CLASS_VOLTAGE_EXCEEDED"});

    const auto output = nlohmann::json::parse(volt::io::write_logical_circuit(circuit));
    CHECK(output["rule_classes"] == fixture["rule_classes"]);
}

TEST_CASE("Logical circuit reader rejects malformed rule-class references") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["rule_classes"] = {
        {"classes", nlohmann::json::array({{{"id", "rule_class:0"}, {"name", "Logic"}}})},
        {"net_assignments",
         nlohmann::json::array({{{"net", "net:99"}, {"rule_class", "rule_class:0"}}})},
    };

    CHECK_THROWS_AS(volt::io::read_logical_circuit_text(fixture.dump()), std::logic_error);
}

TEST_CASE("Logical circuit reader rejects malformed design intent references") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["design_intent"] = {
        {"stub_nets", nlohmann::json::array({"net:99"})},
        {"no_connect_pins", nlohmann::json::array()},
    };

    CHECK_THROWS_AS(volt::io::read_logical_circuit_text(fixture.dump()), std::logic_error);
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

    const auto circuit = volt::io::read_logical_circuit_text(fixture.dump());
    CHECK(circuit.pin_definition(volt::PinDefId{0}).terminal_kind() ==
          volt::ElectricalTerminalKind::Signal);
    CHECK(circuit.pin_definition(volt::PinDefId{0}).direction() ==
          volt::ElectricalDirection::Output);
    CHECK(circuit.pin_definition(volt::PinDefId{0}).signal_domain() ==
          volt::ElectricalSignalDomain::Digital);
    CHECK(circuit.pin_definition(volt::PinDefId{0}).drive_kind() ==
          volt::ElectricalDriveKind::PushPull);
    CHECK(circuit.pin_definition(volt::PinDefId{0}).polarity() ==
          volt::ElectricalPolarity::ActiveHigh);

    const auto &range = circuit.pin_definition_electrical_attributes(volt::PinDefId{0})
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

    const auto circuit = volt::io::read_logical_circuit_text(fixture.dump());

    CHECK(circuit.module_definition_count() == 1);
    CHECK(circuit.template_net_definition_count() == 2);
    CHECK(circuit.port_definition_count() == 1);
    CHECK(circuit.module_component_count() == 1);
    CHECK(circuit.module_pin_connection_count() == 2);
    CHECK(circuit.module_instance_count() == 1);
    CHECK(circuit.port_binding_count() == 1);
    CHECK(circuit.module_definition(volt::ModuleDefId{0}).name() ==
          volt::ModuleName{"BuckConverter"});
    CHECK(volt::queries::concrete_net_for(circuit, volt::ModuleInstanceId{0},
                                          volt::TemplateNetDefId{0}) == volt::NetId{3});
    CHECK(volt::queries::concrete_net_for(circuit, volt::ModuleInstanceId{0},
                                          volt::TemplateNetDefId{1}) == volt::NetId{4});
    CHECK(volt::queries::concrete_component_for(circuit, volt::ModuleInstanceId{0},
                                                volt::ModuleComponentId{0}) ==
          volt::ComponentId{3});
    CHECK(circuit.port_binding(volt::PortBindingId{0}).parent_net() == volt::NetId{0});
}

TEST_CASE("Logical circuit reader infers missing module component origins for v1 fixtures") {
    auto fixture = nlohmann::json::parse(read_fixture("hierarchy_module.volt.json"));
    fixture["module_instances"][0].erase("component_origins");

    const auto circuit = volt::io::read_logical_circuit_text(fixture.dump());

    CHECK(circuit.module_instance_count() == 1);
    CHECK(circuit.module_component_count() == 1);
    CHECK(volt::queries::concrete_component_for(circuit, volt::ModuleInstanceId{0},
                                                volt::ModuleComponentId{0}) ==
          volt::ComponentId{0});
}

TEST_CASE("Logical circuit reader rejects mismatched module component origin connectivity") {
    auto fixture = nlohmann::json::parse(read_fixture("hierarchy_module.volt.json"));
    fixture["nets"][0]["pins"] = nlohmann::json::array();
    fixture["nets"][1]["pins"] = nlohmann::json::array({"pin:0", "pin:1"});

    CHECK_THROWS_AS(volt::io::read_logical_circuit_text(fixture.dump()), std::logic_error);
}

TEST_CASE("Logical circuit reader defaults missing typed electrical attributes to empty maps") {
    const auto circuit = volt::io::read_logical_circuit_text(read_fixture("led_circuit.volt.json"));

    CHECK(circuit.pin_definition(volt::PinDefId{0}).terminal_kind() ==
          volt::ElectricalTerminalKind::Unspecified);
    CHECK(circuit.pin_definition_electrical_attributes(volt::PinDefId{0}).empty());
    CHECK(circuit.component_electrical_attributes(volt::ComponentId{1}).empty());
    CHECK(circuit.net_electrical_attributes(volt::NetId{0}).empty());
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

    CHECK_THROWS_AS(volt::io::read_logical_circuit_text(fixture.dump()), std::logic_error);
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

    CHECK_THROWS_AS(volt::io::read_logical_circuit_text(fixture.dump()), std::logic_error);
}

TEST_CASE("Logical circuit reader rejects duplicate net pin references") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["nets"][0]["pins"].push_back("pin:0");

    CHECK_THROWS_AS(volt::io::read_logical_circuit_text(fixture.dump()), std::logic_error);
}

TEST_CASE("Logical circuit reader rejects invalid pin electrical enum values") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["pin_definitions"][0]["terminal_kind"] = "ThresholdInput";

    CHECK_THROWS_AS(volt::io::read_logical_circuit_text(fixture.dump()), std::logic_error);
}

TEST_CASE("Logical circuit reader rejects dangling references") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["component_definitions"][0]["pins"][0] = "pin_def:999";

    CHECK_THROWS_AS(volt::io::read_logical_circuit_text(fixture.dump()), std::logic_error);
}

TEST_CASE("Logical circuit reader rejects wrong typed local IDs") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["pins"][0]["id"] = "component:99";

    CHECK_THROWS_AS(volt::io::read_logical_circuit_text(fixture.dump()), std::logic_error);

    fixture["pins"][0]["id"] = "pin:not-a-number";
    CHECK_THROWS_MATCHES(volt::io::read_logical_circuit_text(fixture.dump()), std::logic_error,
                         Catch::Matchers::Message("Local ID index must be numeric"));
}

TEST_CASE("Logical circuit reader reports unsupported versions deterministically") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["version"] = 2;

    CHECK_THROWS_MATCHES(volt::io::read_logical_circuit_text(fixture.dump()), std::logic_error,
                         Catch::Matchers::Message("Unsupported logical circuit format version: 2"));
}

TEST_CASE("Logical circuit reader reports large unsupported versions deterministically") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["version"] = 2147483648LL;

    CHECK_THROWS_MATCHES(
        volt::io::read_logical_circuit_text(fixture.dump()), std::logic_error,
        Catch::Matchers::Message("Unsupported logical circuit format version: 2147483648"));
}

TEST_CASE("Logical circuit reader reports unsupported formats deterministically") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["format"] = "volt.other";

    CHECK_THROWS_MATCHES(
        volt::io::read_logical_circuit_text(fixture.dump()), std::logic_error,
        Catch::Matchers::Message("Unsupported logical circuit format: volt.other"));
}

TEST_CASE(
    "Logical circuit reader rejects selected part mappings outside the component definition") {
    auto fixture = nlohmann::json::parse(read_fixture("led_circuit.volt.json"));
    fixture["components"][0]["selected_physical_part"]["pin_pad_mappings"][0]["pin"] = "pin_def:2";

    CHECK_THROWS_AS(volt::io::read_logical_circuit_text(fixture.dump()), std::logic_error);
}
