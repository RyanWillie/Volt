#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include <volt/core/electrical_attributes.hpp>
#include <volt/io/logical_circuit_writer.hpp>

#include "led_circuit.hpp"

namespace {

std::string read_fixture(const std::string &name) {
    auto input = std::ifstream{std::string{VOLT_TEST_FIXTURE_DIR} + "/" + name};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

} // namespace

TEST_CASE("Logical circuit writer emits deterministic output") {
    const auto circuit = volt::examples::build_led_circuit();

    CHECK(volt::io::write_logical_circuit(circuit) == volt::io::write_logical_circuit(circuit));
}

TEST_CASE("Logical circuit writer escapes JSON control characters") {
    volt::Circuit circuit;
    const auto pin = circuit.add_pin_definition(
        volt::PinDefinition{"CTRL\x01\x1f", "1", volt::PinRole::Passive});
    const auto component_def =
        circuit.add_component_definition(volt::ComponentDefinition{"Escaped", std::vector{pin}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"U1"});
    circuit.set_component_property(component, volt::PropertyKey{"note"},
                                   volt::PropertyValue{"line\nbreak\x01"});

    const auto output = volt::io::write_logical_circuit(circuit);

    CHECK(output.find("CTRL\\u0001\\u001f") != std::string::npos);
    CHECK(output.find("line\\nbreak\\u0001") != std::string::npos);
}

TEST_CASE("Logical circuit writer preserves double precision and rejects non-finite numbers") {
    volt::Circuit circuit;
    const auto pin =
        circuit.add_pin_definition(volt::PinDefinition{"1", "1", volt::PinRole::Passive});
    const auto component_def =
        circuit.add_component_definition(volt::ComponentDefinition{"Precise", std::vector{pin}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"U1"});
    circuit.set_component_property(component, volt::PropertyKey{"ratio"},
                                   volt::PropertyValue{0.12345678901234567});
    circuit.set_component_property(component, volt::PropertyKey{"invalid"},
                                   volt::PropertyValue{std::numeric_limits<double>::infinity()});

    CHECK_THROWS_AS(volt::io::write_logical_circuit(circuit), std::logic_error);
    circuit.set_component_property(component, volt::PropertyKey{"invalid"},
                                   volt::PropertyValue{1.0});

    CHECK(volt::io::write_logical_circuit(circuit).find("0.12345678901234566") !=
          std::string::npos);
}

TEST_CASE("Logical circuit writer emits typed electrical attributes") {
    volt::Circuit circuit;
    const auto first_pin =
        circuit.add_pin_definition(volt::PinDefinition{"1", "1", volt::PinRole::Passive});
    const auto second_pin =
        circuit.add_pin_definition(volt::PinDefinition{"2", "2", volt::PinRole::Passive});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"R1"});
    circuit.select_physical_part(component, volt::PhysicalPart{
                                                volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                                                volt::PackageRef{"0603"},
                                                volt::FootprintRef{"passives", "R_0603_1608Metric"},
                                                std::vector{
                                                    volt::PinPadMapping{first_pin, "1"},
                                                    volt::PinPadMapping{second_pin, "2"},
                                                },
                                            });

    circuit.set_component_electrical_attribute(
        component,
        volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{"resistance"},
                                      volt::ElectricalAttributeOwner::ComponentInstance,
                                      volt::ElectricalAttributeKind::DesignInput,
                                      volt::UnitDimension::Resistance},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Resistance, 330.0}});
    circuit.set_component_electrical_attribute(
        component,
        volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{"tolerance"},
                                      volt::ElectricalAttributeOwner::ComponentInstance,
                                      volt::ElectricalAttributeKind::DesignInput,
                                      volt::UnitDimension::Ratio},
        volt::ElectricalAttributeValue{volt::Tolerance::percent(0.01)});
    circuit.set_selected_part_electrical_attribute(
        component,
        volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{"voltage_rating"},
                                      volt::ElectricalAttributeOwner::SelectedPart,
                                      volt::ElectricalAttributeKind::DesignInput,
                                      volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 75.0}});

    const auto output = nlohmann::json::parse(volt::io::write_logical_circuit(circuit));
    const auto &attributes = output["components"][0]["electrical_attributes"];
    const auto &part_attributes =
        output["components"][0]["selected_physical_part"]["electrical_attributes"];

    CHECK(attributes["resistance"]["type"] == "quantity");
    CHECK(attributes["resistance"]["dimension"] == "resistance");
    CHECK(attributes["resistance"]["value"] == 330.0);
    CHECK(attributes["tolerance"]["type"] == "tolerance");
    CHECK(attributes["tolerance"]["mode"] == "percent");
    CHECK(attributes["tolerance"]["dimension"] == "ratio");
    CHECK(attributes["tolerance"]["minus"] == 0.01);
    CHECK(attributes["tolerance"]["plus"] == 0.01);
    CHECK(part_attributes["voltage_rating"]["type"] == "quantity");
    CHECK(part_attributes["voltage_rating"]["dimension"] == "voltage");
    CHECK(part_attributes["voltage_rating"]["value"] == 75.0);
}

TEST_CASE("Logical circuit writer emits net typed electrical attributes") {
    volt::Circuit circuit;
    const auto net = circuit.add_net(volt::Net{volt::NetName{"3V3"}, volt::NetKind::Power});

    circuit.set_net_electrical_attribute(
        net,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage"}, volt::ElectricalAttributeOwner::Net,
            volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}});

    const auto output = nlohmann::json::parse(volt::io::write_logical_circuit(circuit));
    const auto &attributes = output["nets"][0]["electrical_attributes"];

    CHECK(attributes["voltage"]["type"] == "quantity");
    CHECK(attributes["voltage"]["dimension"] == "voltage");
    CHECK(attributes["voltage"]["value"] == 3.3);
}

TEST_CASE("Logical circuit writer emits design intent") {
    volt::Circuit circuit;
    const auto pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "BOOT0", "1", volt::PinRole::DigitalInput, volt::ConnectionRequirement::Required});
    const auto component_def =
        circuit.add_component_definition(volt::ComponentDefinition{"MCU", std::vector{pin_def}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"U1"});
    const auto pin = circuit.pin_by_name(component, "BOOT0").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"BOOT0"}, volt::NetKind::Signal});

    circuit.mark_intentional_stub_net(net);
    circuit.mark_intentional_no_connect_pin(pin);

    const auto output = nlohmann::json::parse(volt::io::write_logical_circuit(circuit));

    REQUIRE(output.contains("design_intent"));
    CHECK(output["design_intent"]["stub_nets"] == nlohmann::json::array({"net:0"}));
    CHECK(output["design_intent"]["no_connect_pins"] == nlohmann::json::array({"pin:0"}));
}

TEST_CASE("Logical circuit writer emits pin electrical semantics") {
    volt::Circuit circuit;
    const auto pin = circuit.add_pin_definition(volt::PinDefinition{
        "RESET",
        "4",
        volt::PinRole::DigitalInput,
        volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Signal,
        volt::ElectricalDirection::Input,
        volt::ElectricalSignalDomain::Digital,
        volt::ElectricalDriveKind::HighImpedance,
        volt::ElectricalPolarity::ActiveLow,
    });

    circuit.set_pin_definition_electrical_attribute(
        pin,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage_range"}, volt::ElectricalAttributeOwner::PinSpec,
            volt::ElectricalAttributeKind::Constraint, volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{
            volt::QuantityRange::bounded(volt::Quantity{volt::UnitDimension::Voltage, 0.0},
                                         volt::Quantity{volt::UnitDimension::Voltage, 5.5})});

    const auto output = nlohmann::json::parse(volt::io::write_logical_circuit(circuit));
    const auto &pin_json = output["pin_definitions"][0];
    const auto &attributes = pin_json["electrical_attributes"];

    CHECK(pin_json["terminal_kind"] == "Signal");
    CHECK(pin_json["direction"] == "Input");
    CHECK(pin_json["signal_domain"] == "Digital");
    CHECK(pin_json["drive_kind"] == "HighImpedance");
    CHECK(pin_json["polarity"] == "ActiveLow");
    CHECK(attributes["voltage_range"]["type"] == "range");
    CHECK(attributes["voltage_range"]["dimension"] == "voltage");
    CHECK(attributes["voltage_range"]["minimum"] == 0.0);
    CHECK(attributes["voltage_range"]["maximum"] == 5.5);
}

TEST_CASE("Logical circuit writer emits hierarchy module scaffold") {
    volt::Circuit circuit;
    const auto left =
        circuit.add_pin_definition(volt::PinDefinition{"1", "1", volt::PinRole::Passive});
    const auto right =
        circuit.add_pin_definition(volt::PinDefinition{"2", "2", volt::PinRole::Passive});
    const auto resistor =
        circuit.add_component_definition(volt::ComponentDefinition{"Resistor", {left, right}});
    const auto module =
        circuit.add_module_definition(volt::ModuleDefinition{volt::ModuleName{"BuckConverter"}});
    const auto vin = circuit.add_template_net(
        module, volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power});
    const auto fb = circuit.add_template_net(
        module, volt::TemplateNetDefinition{volt::NetName{"FB"}, volt::NetKind::Signal});
    const auto template_component = circuit.add_module_component(
        module, volt::ModuleComponentTemplate{resistor, volt::ReferenceDesignator{"R1"}});
    CHECK(circuit.connect_module_pin(module, vin, template_component, left));
    CHECK(circuit.connect_module_pin(module, fb, template_component, right));
    const auto port = circuit.add_port_definition(
        module, volt::PortDefinition{volt::PortName{"VIN"}, vin, volt::PortRole::PowerInput});
    const auto instance =
        circuit.instantiate_root_module(module, volt::ModuleInstanceName{"BUCK_A"});
    const auto parent_net = circuit.add_net(volt::Net{volt::NetName{"VIN"}, volt::NetKind::Power});
    [[maybe_unused]] const auto binding = circuit.bind_port(instance, port, parent_net);

    const auto output = nlohmann::json::parse(volt::io::write_logical_circuit(circuit));
    const auto &module_json = output["module_definitions"][0];
    const auto &instance_json = output["module_instances"][0];

    CHECK(module_json["id"] == "module_def:0");
    CHECK(module_json["name"] == "BuckConverter");
    CHECK(module_json["local_nets"][0]["id"] == "template_net:0");
    CHECK(module_json["local_nets"][0]["name"] == "VIN");
    CHECK(module_json["local_nets"][0]["kind"] == "Power");
    CHECK(module_json["local_nets"][1]["id"] == "template_net:1");
    CHECK(module_json["components"][0]["id"] == "module_component:0");
    CHECK(module_json["components"][0]["definition"] == "component_def:0");
    CHECK(module_json["components"][0]["reference"] == "R1");
    CHECK(module_json["connections"][0]["net"] == "template_net:0");
    CHECK(module_json["connections"][0]["component"] == "module_component:0");
    CHECK(module_json["connections"][0]["pin"] == "pin_def:0");
    CHECK(module_json["connections"][1]["net"] == "template_net:1");
    CHECK(module_json["connections"][1]["component"] == "module_component:0");
    CHECK(module_json["connections"][1]["pin"] == "pin_def:1");
    CHECK(module_json["ports"][0]["id"] == "port:0");
    CHECK(module_json["ports"][0]["name"] == "VIN");
    CHECK(module_json["ports"][0]["internal_net"] == "template_net:0");
    CHECK(module_json["ports"][0]["role"] == "PowerInput");
    CHECK(module_json["ports"][0]["required"] == true);
    CHECK(instance_json["id"] == "module:0");
    CHECK(instance_json["definition"] == "module_def:0");
    CHECK(instance_json["name"] == "BUCK_A");
    CHECK(instance_json["net_origins"][0]["template_net"] == "template_net:0");
    CHECK(instance_json["net_origins"][0]["net"] == "net:0");
    CHECK(instance_json["net_origins"][1]["template_net"] == "template_net:1");
    CHECK(instance_json["net_origins"][1]["net"] == "net:1");
    CHECK(instance_json["component_origins"][0]["template_component"] == "module_component:0");
    CHECK(instance_json["component_origins"][0]["component"] == "component:0");
    CHECK(instance_json["port_bindings"][0]["port"] == "port:0");
    CHECK(instance_json["port_bindings"][0]["parent_net"] == "net:2");
}

TEST_CASE("Logical circuit writer matches the LED golden fixture") {
    const auto circuit = volt::examples::build_led_circuit();

    CHECK(volt::io::write_logical_circuit(circuit) == read_fixture("led_circuit.volt.json"));
}
