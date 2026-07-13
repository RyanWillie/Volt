#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/validation/validation.hpp>
#include <volt/io/logical/logical_circuit_reader.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>

namespace {

std::string read_fixture(const std::string &name) {
    auto input = std::ifstream{std::string{VOLT_TEST_FIXTURE_DIR} + "/" + name};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void check_fixture_round_trips(const std::string &name) {
    const auto fixture = read_fixture(name);
    const auto first_read = volt::io::read_logical_circuit_text(fixture);
    const auto first_write = volt::io::write_logical_circuit(first_read);
    const auto second_read = volt::io::read_logical_circuit_text(first_write);

    CHECK(first_write == fixture);
    CHECK(volt::io::write_logical_circuit(second_read) == fixture);
}

} // namespace

TEST_CASE("Golden LED fixture round-trips without logical diagnostics") {
    const auto fixture = read_fixture("led_circuit.volt.json");
    const auto circuit = volt::io::read_logical_circuit_text(fixture);

    CHECK(volt::validate_circuit(circuit).empty());
    check_fixture_round_trips("led_circuit.volt.json");
}

TEST_CASE("Golden diagnostic fixture round-trips and preserves connectivity") {
    const auto fixture = read_fixture("single_pin_net.volt.json");
    const auto circuit = volt::io::read_logical_circuit_text(fixture);
    const auto report = volt::validate_circuit(circuit);

    REQUIRE(circuit.all<volt::NetId>().size() == 1);
    CHECK(circuit.get(volt::NetId{0}).pins().size() == 1);
    CHECK_FALSE(report.empty());
    check_fixture_round_trips("single_pin_net.volt.json");
}

TEST_CASE("Golden typed electrical attribute fixture round-trips") {
    check_fixture_round_trips("typed_electrical_attributes.volt.json");
}

TEST_CASE("Golden hierarchy module fixture round-trips") {
    check_fixture_round_trips("hierarchy_module.volt.json");
}

TEST_CASE("Logical reader preserves independent connectivity table identity") {
    const auto serialized = R"json({
  "format": "volt.logical_circuit",
  "version": 1,
  "pin_definitions": [
    { "id": "pin_def:0", "name": "A1", "number": "1", "connection_requirement": "Required" },
    { "id": "pin_def:1", "name": "B1", "number": "1", "connection_requirement": "Required" },
    { "id": "pin_def:2", "name": "A2", "number": "2", "connection_requirement": "Required" },
    { "id": "pin_def:3", "name": "B2", "number": "2", "connection_requirement": "Required" },
    { "id": "pin_def:4", "name": "Unowned", "number": "3", "connection_requirement": "Required", "electrical_attributes": { "voltage_range": { "type": "quantity", "dimension": "voltage", "value": 5 } } }
  ],
  "component_definitions": [
    { "id": "component_def:0", "name": "First", "pins": ["pin_def:0", "pin_def:2"], "properties": {} },
    { "id": "component_def:1", "name": "Second", "pins": ["pin_def:1", "pin_def:3"], "properties": {} }
  ],
  "components": [
    { "id": "component:0", "definition": "component_def:1", "reference": "U1", "properties": {} },
    { "id": "component:1", "definition": "component_def:0", "reference": "U2", "properties": {} }
  ],
  "pins": [
    { "id": "pin:0", "component": "component:0", "definition": "pin_def:1" },
    { "id": "pin:1", "component": "component:0", "definition": "pin_def:3" },
    { "id": "pin:2", "component": "component:1", "definition": "pin_def:0" },
    { "id": "pin:3", "component": "component:1", "definition": "pin_def:2" }
  ],
  "nets": []
})json";
    const auto restored = volt::io::read_logical_circuit_text(serialized);

    CHECK(restored.get(volt::ComponentDefId{0}).pins() ==
          std::vector{volt::PinDefId{0}, volt::PinDefId{2}});
    CHECK(restored.get(volt::ComponentDefId{1}).pins() ==
          std::vector{volt::PinDefId{1}, volt::PinDefId{3}});
    CHECK(restored.get(volt::PinDefId{4}).name() == "Unowned");
    CHECK(restored.pin_definition_electrical_attributes(volt::PinDefId{4})
              .get(volt::ElectricalAttributeName{"voltage_range"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 5.0});
    const auto rewritten = volt::io::write_logical_circuit(restored);
    CHECK(volt::io::write_logical_circuit(volt::io::read_logical_circuit_text(rewritten)) ==
          rewritten);
}

TEST_CASE("Logical reader preserves interleaved global hierarchy table identity") {
    const auto serialized = R"json({
  "format": "volt.logical_circuit",
  "version": 1,
  "pin_definitions": [
    { "id": "pin_def:0", "name": "1", "number": "1", "connection_requirement": "Required", "terminal_kind": "Passive", "direction": "Passive" }
  ],
  "component_definitions": [
    { "id": "component_def:0", "name": "One pin", "pins": ["pin_def:0"], "properties": {} }
  ],
  "components": [],
  "pins": [],
  "nets": [],
  "module_definitions": [
    { "id": "module_def:0", "name": "First", "local_nets": [{ "id": "template_net:0", "name": "A", "kind": "Signal" }, { "id": "template_net:2", "name": "C", "kind": "Signal" }], "components": [{ "id": "module_component:0", "definition": "component_def:0", "reference": "R1", "properties": {} }, { "id": "module_component:2", "definition": "component_def:0", "reference": "R2", "properties": {} }], "connections": [{ "net": "template_net:0", "component": "module_component:0", "pin": "pin_def:0" }, { "net": "template_net:2", "component": "module_component:2", "pin": "pin_def:0" }], "ports": [{ "id": "port:0", "name": "A", "internal_net": "template_net:0", "role": "Input", "required": true }, { "id": "port:2", "name": "C", "internal_net": "template_net:2", "role": "Output", "required": true }] },
    { "id": "module_def:1", "name": "Second", "local_nets": [{ "id": "template_net:1", "name": "B", "kind": "Signal" }], "components": [{ "id": "module_component:1", "definition": "component_def:0", "reference": "R1", "properties": {} }], "connections": [{ "net": "template_net:1", "component": "module_component:1", "pin": "pin_def:0" }], "ports": [{ "id": "port:1", "name": "B", "internal_net": "template_net:1", "role": "Input", "required": true }] }
  ],
  "module_instances": []
})json";
    const auto circuit = volt::io::read_logical_circuit_text(serialized);
    const auto first_module = volt::ModuleDefId{0};
    const auto second_module = volt::ModuleDefId{1};
    const auto &first_definition = circuit.get(first_module);
    const auto &second_definition = circuit.get(second_module);
    const auto first_net = first_definition.template_nets()[0];
    const auto second_net = first_definition.template_nets()[1];
    const auto second_module_net = second_definition.template_nets()[0];
    const auto first_component = first_definition.components()[0];
    const auto second_component = first_definition.components()[1];
    const auto second_module_component = second_definition.components()[0];
    const auto first_port = first_definition.ports()[0];
    const auto second_port = first_definition.ports()[1];
    const auto second_module_port = second_definition.ports()[0];

    CHECK(first_net == volt::TemplateNetDefId{0});
    CHECK(second_module_net == volt::TemplateNetDefId{1});
    CHECK(second_net == volt::TemplateNetDefId{2});
    CHECK(first_component == volt::ModuleComponentId{0});
    CHECK(second_module_component == volt::ModuleComponentId{1});
    CHECK(second_component == volt::ModuleComponentId{2});
    CHECK(first_port == volt::PortDefId{0});
    CHECK(second_module_port == volt::PortDefId{1});
    CHECK(second_port == volt::PortDefId{2});

    const auto rewritten = volt::io::write_logical_circuit(circuit);
    const auto restored = volt::io::read_logical_circuit_text(rewritten);

    CHECK(volt::io::write_logical_circuit(restored) == rewritten);
}
