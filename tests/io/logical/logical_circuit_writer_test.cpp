#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/errors.hpp>
#include <volt/io/logical/logical_circuit_reader.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>

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
    const auto component_def = circuit.define_component(volt::ComponentSpec{
        .name = "Escaped",
        .pins = {volt::PinSpec{
            "CTRL\x01\x1f", "1", volt::ConnectionRequirement::Required,
            volt::ElectricalTerminalKind::Passive, volt::ElectricalDirection::Passive,
            volt::ElectricalSignalDomain::Unspecified, volt::ElectricalDriveKind::Passive}},
    });
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    circuit.update(component, volt::SetComponentProperty{volt::PropertyKey{"note"},
                                                         volt::PropertyValue{"line\nbreak\x01"}});

    const auto output = volt::io::write_logical_circuit(circuit);

    CHECK(output.find("CTRL\\u0001\\u001f") != std::string::npos);
    CHECK(output.find("line\\nbreak\\u0001") != std::string::npos);
}

TEST_CASE("Logical circuit writer preserves double precision and rejects non-finite numbers") {
    volt::Circuit circuit;
    const auto component_def = circuit.define_component(volt::ComponentSpec{
        .name = "Precise",
        .pins = {volt::PinSpec{
            "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
            volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
            volt::ElectricalDriveKind::Passive}},
    });
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    circuit.update(component, volt::SetComponentProperty{volt::PropertyKey{"ratio"},
                                                         volt::PropertyValue{0.12345678901234567}});
    circuit.update(component, volt::SetComponentProperty{
                                  volt::PropertyKey{"invalid"},
                                  volt::PropertyValue{std::numeric_limits<double>::infinity()}});

    CHECK_THROWS_AS(volt::io::write_logical_circuit(circuit), std::logic_error);
    try {
        static_cast<void>(volt::io::write_logical_circuit(circuit));
        FAIL("Expected typed kernel error");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::InvalidArgument);
        CHECK(std::string{error.what()} == "Cannot write non-finite JSON number");
    }
    circuit.update(component, volt::SetComponentProperty{volt::PropertyKey{"invalid"},
                                                         volt::PropertyValue{1.0}});

    CHECK(volt::io::write_logical_circuit(circuit).find("0.12345678901234566") !=
          std::string::npos);
}

TEST_CASE("Logical circuit writer emits typed electrical attributes") {
    volt::Circuit circuit;
    const auto component_def = circuit.define_component(volt::ComponentSpec{
        .name = "Resistor",
        .pins = {volt::PinSpec{
                     "1", "1", volt::ConnectionRequirement::Required,
                     volt::ElectricalTerminalKind::Passive, volt::ElectricalDirection::Passive,
                     volt::ElectricalSignalDomain::Unspecified, volt::ElectricalDriveKind::Passive},
                 volt::PinSpec{"2", "2", volt::ConnectionRequirement::Required,
                               volt::ElectricalTerminalKind::Passive,
                               volt::ElectricalDirection::Passive,
                               volt::ElectricalSignalDomain::Unspecified,
                               volt::ElectricalDriveKind::Passive}},
    });
    const auto &pins = circuit.get(component_def).pins();
    const auto first_pin = pins[0];
    const auto second_pin = pins[1];
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    circuit.update(component, volt::SelectPhysicalPart{volt::PhysicalPart{
                                  volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
                                  volt::PackageRef{"0603"},
                                  volt::FootprintRef{"passives", "R_0603_1608Metric"},
                                  std::vector{
                                      volt::PinPadMapping{first_pin, "1"},
                                      volt::PinPadMapping{second_pin, "2"},
                                  },
                              }});

    circuit.update(component, volt::SetComponentElectricalAttribute{
                                  volt::ElectricalAttributeSpec{
                                      volt::ElectricalAttributeName{"resistance"},
                                      volt::ElectricalAttributeOwner::ComponentInstance,
                                      volt::ElectricalAttributeKind::DesignInput,
                                      volt::UnitDimension::Resistance},
                                  volt::ElectricalAttributeValue{
                                      volt::Quantity{volt::UnitDimension::Resistance, 330.0}}});
    circuit.update(component,
                   volt::SetComponentElectricalAttribute{
                       volt::ElectricalAttributeSpec{
                           volt::ElectricalAttributeName{"tolerance"},
                           volt::ElectricalAttributeOwner::ComponentInstance,
                           volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Ratio},
                       volt::ElectricalAttributeValue{volt::Tolerance::percent(0.01)}});
    circuit.update(
        component,
        volt::SetSelectedPartElectricalAttribute{
            volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{"voltage_rating"},
                                          volt::ElectricalAttributeOwner::SelectedPart,
                                          volt::ElectricalAttributeKind::DesignInput,
                                          volt::UnitDimension::Voltage},
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 75.0}}});

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

TEST_CASE("Logical circuit writer emits selected-part 3D model metadata") {
    volt::Circuit circuit;
    const auto component_def = circuit.define_component(volt::ComponentSpec{
        .name = "Resistor",
        .pins = {volt::PinSpec{
                     "1", "1", volt::ConnectionRequirement::Required,
                     volt::ElectricalTerminalKind::Passive, volt::ElectricalDirection::Passive,
                     volt::ElectricalSignalDomain::Unspecified, volt::ElectricalDriveKind::Passive},
                 volt::PinSpec{"2", "2", volt::ConnectionRequirement::Required,
                               volt::ElectricalTerminalKind::Passive,
                               volt::ElectricalDirection::Passive,
                               volt::ElectricalSignalDomain::Unspecified,
                               volt::ElectricalDriveKind::Passive}},
    });
    const auto &pins = circuit.get(component_def).pins();
    const auto first_pin = pins[0];
    const auto second_pin = pins[1];
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    circuit.update(
        component,
        volt::SelectPhysicalPart{volt::PhysicalPart{
            volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
            volt::PackageRef{"0603"},
            volt::FootprintRef{"passives", "R_0603_1608Metric"},
            std::vector{volt::PinPadMapping{first_pin, "1"}, volt::PinPadMapping{second_pin, "2"}},
            {},
            volt::PartModel3D{"glb", "resistor-body.glb", {0.5, -0.25, 0.8}, 15.0},
        }});

    const auto output = nlohmann::json::parse(volt::io::write_logical_circuit(circuit));
    const auto &model = output["components"][0]["selected_physical_part"]["model_3d"];

    CHECK(model["format"] == "glb");
    CHECK(model["file_name"] == "resistor-body.glb");
    CHECK(model["translation_mm"] == nlohmann::json::array({0.5, -0.25, 0.8}));
    CHECK(model["rotation_deg"] == 15.0);
}

TEST_CASE("Logical circuit writer emits net typed electrical attributes") {
    volt::Circuit circuit;
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"3V3"}, volt::NetKind::Power});

    circuit.update(
        net,
        volt::SetNetElectricalAttribute{
            volt::ElectricalAttributeSpec{
                volt::ElectricalAttributeName{"voltage"}, volt::ElectricalAttributeOwner::Net,
                volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Voltage},
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}}});

    const auto output = nlohmann::json::parse(volt::io::write_logical_circuit(circuit));
    const auto &attributes = output["nets"][0]["electrical_attributes"];

    CHECK(attributes["voltage"]["type"] == "quantity");
    CHECK(attributes["voltage"]["dimension"] == "voltage");
    CHECK(attributes["voltage"]["value"] == 3.3);
}

TEST_CASE("Logical circuit writer emits design intent") {
    volt::Circuit circuit;
    const auto component_def = circuit.define_component(volt::ComponentSpec{
        .name = "MCU",
        .pins = {volt::PinSpec{"BOOT0", "1", volt::ConnectionRequirement::Required,
                               volt::ElectricalTerminalKind::Signal,
                               volt::ElectricalDirection::Input,
                               volt::ElectricalSignalDomain::Digital}},
    });
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto pin = volt::queries::pin_by_name(circuit, component, "BOOT0").value();
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"BOOT0"}, volt::NetKind::Signal});

    circuit.update(net, volt::MarkIntentionalStub{});
    circuit.mark_no_connect(pin);
    circuit.update(component, volt::SetAssemblyIntent{.dnp = true, .selection_override = true});

    const auto output = nlohmann::json::parse(volt::io::write_logical_circuit(circuit));

    REQUIRE(output.contains("design_intent"));
    CHECK(output["design_intent"]["stub_nets"] == nlohmann::json::array({"net:0"}));
    CHECK(output["design_intent"]["no_connect_pins"] == nlohmann::json::array({"pin:0"}));
    CHECK(output["design_intent"]["component_assembly"] ==
          nlohmann::json::array(
              {{{"component", "component:0"}, {"dnp", true}, {"selection_override", true}}}));
}

TEST_CASE("Logical circuit writer emits override-only component assembly intent") {
    volt::Circuit circuit;
    const auto component_def = circuit.define_component(volt::ComponentSpec{
        .name = "Resistor",
        .pins = {volt::PinSpec{
            "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
            volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
            volt::ElectricalDriveKind::Passive}},
    });
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    circuit.update(component, volt::SetAssemblyIntent{.selection_override = true});

    const auto output = nlohmann::json::parse(volt::io::write_logical_circuit(circuit));
    const auto &assembly = output["design_intent"]["component_assembly"][0];

    CHECK(assembly["component"] == "component:0");
    CHECK_FALSE(assembly.contains("dnp"));
    CHECK(assembly["selection_override"] == true);
}

TEST_CASE("Logical circuit writer emits selected-part alternates") {
    volt::Circuit circuit;
    const auto component_def = circuit.define_component(volt::ComponentSpec{
        .name = "Resistor",
        .pins = {volt::PinSpec{
                     "1", "1", volt::ConnectionRequirement::Required,
                     volt::ElectricalTerminalKind::Passive, volt::ElectricalDirection::Passive,
                     volt::ElectricalSignalDomain::Unspecified, volt::ElectricalDriveKind::Passive},
                 volt::PinSpec{"2", "2", volt::ConnectionRequirement::Required,
                               volt::ElectricalTerminalKind::Passive,
                               volt::ElectricalDirection::Passive,
                               volt::ElectricalSignalDomain::Unspecified,
                               volt::ElectricalDriveKind::Passive}},
    });
    const auto &pins = circuit.get(component_def).pins();
    const auto first_pin = pins[0];
    const auto second_pin = pins[1];
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    circuit.update(
        component,
        volt::SelectPhysicalPart{volt::PhysicalPart{
            volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
            volt::PackageRef{"0603"},
            volt::FootprintRef{"passives", "R_0603_1608Metric"},
            std::vector{volt::PinPadMapping{first_pin, "1"}, volt::PinPadMapping{second_pin, "2"}},
            {},
            std::nullopt,
            std::vector<std::string>{"RC0603FR-07330RLA", "RC0603FR-07330RLB"},
        }});

    const auto output = nlohmann::json::parse(volt::io::write_logical_circuit(circuit));

    CHECK(output["components"][0]["selected_physical_part"]["approved_alternate_mpns"] ==
          nlohmann::json::array({"RC0603FR-07330RLA", "RC0603FR-07330RLB"}));
}

TEST_CASE("Logical circuit writer emits net classes and net assignments") {
    volt::Circuit circuit;
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"HV"}, volt::NetKind::Power});
    auto net_class = volt::NetClass{volt::NetClassName{"HighVoltage"}};
    net_class.set_maximum_net_voltage(volt::Quantity{volt::UnitDimension::Voltage, 60.0});
    net_class.set_copper_clearance_mm(0.5);
    net_class.derive_track_width(volt::ipc2221_trace_width_from_current_mm(
        1.0, 10.0, 1.0, volt::NetClassTraceEnvironment::External));
    const auto net_class_id =
        circuit.define_net_class(volt::NetClassSpec{.net_class = std::move(net_class)});
    circuit.update(net, volt::AssignNetClass{net_class_id});

    const auto output = nlohmann::json::parse(volt::io::write_logical_circuit(circuit));

    REQUIRE(output.contains("net_classes"));
    const auto &classes = output["net_classes"]["classes"];
    const auto &assignments = output["net_classes"]["net_assignments"];
    REQUIRE(classes.size() == 1);
    CHECK(classes[0]["id"] == "net_class:0");
    CHECK(classes[0]["name"] == "HighVoltage");
    CHECK(classes[0]["maximum_net_voltage"]["dimension"] == "voltage");
    CHECK(classes[0]["maximum_net_voltage"]["value"] == 60.0);
    CHECK(classes[0]["copper_clearance_mm"] == 0.5);
    CHECK_FALSE(classes[0].contains("track_width_mm"));
    CHECK(classes[0]["derived_track_width"]["value_mm"] == 0.3003762222199717);
    CHECK(classes[0]["derived_track_width"]["calculator"]["id"] == "ipc-2221.trace-width.current");
    CHECK(classes[0]["derived_track_width"]["calculator"]["standard"] == "IPC-2221");
    CHECK(classes[0]["derived_track_width"]["inputs"][0]["name"] == "current");
    CHECK(classes[0]["derived_track_width"]["inputs"][0]["value"] == 1.0);
    CHECK(classes[0]["derived_track_width"]["inputs"][0]["unit"] == "A");
    CHECK(assignments == nlohmann::json::array({{{"net", "net:0"}, {"net_class", "net_class:0"}}}));

    const auto reloaded = volt::io::read_logical_circuit_text(output.dump());
    const auto &reloaded_class = reloaded.net_class(volt::NetClassId{0});
    CHECK_FALSE(reloaded_class.has_explicit_track_width_mm());
    CHECK(reloaded_class.track_width_mm() == 0.3003762222199717);
    REQUIRE(reloaded_class.derived_track_width().has_value());
    CHECK(reloaded_class.derived_track_width()->derivation.calculator_id ==
          "ipc-2221.trace-width.current");
}

TEST_CASE("Logical circuit writer emits pin electrical semantics") {
    const auto circuit = volt::io::read_logical_circuit_text(R"json({
  "format": "volt.logical_circuit",
  "version": 1,
  "pin_definitions": [
    { "id": "pin_def:0", "name": "RESET", "number": "4", "connection_requirement": "Required", "terminal_kind": "Signal", "direction": "Input", "signal_domain": "Digital", "drive_kind": "HighImpedance", "polarity": "ActiveLow", "electrical_attributes": { "voltage_range": { "type": "range", "dimension": "voltage", "minimum": 0, "maximum": 5.5 } } }
  ],
  "component_definitions": [],
  "components": [],
  "pins": [],
  "nets": []
})json");

    const auto output = nlohmann::json::parse(volt::io::write_logical_circuit(circuit));
    const auto &pin_json = output["pin_definitions"][0];
    const auto &attributes = pin_json["electrical_attributes"];

    CHECK_FALSE(pin_json.contains("role"));
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
    const auto resistor = circuit.define_component(volt::ComponentSpec{
        .name = "Resistor",
        .pins = {volt::PinSpec{
                     "1", "1", volt::ConnectionRequirement::Required,
                     volt::ElectricalTerminalKind::Passive, volt::ElectricalDirection::Passive,
                     volt::ElectricalSignalDomain::Unspecified, volt::ElectricalDriveKind::Passive},
                 volt::PinSpec{"2", "2", volt::ConnectionRequirement::Required,
                               volt::ElectricalTerminalKind::Passive,
                               volt::ElectricalDirection::Passive,
                               volt::ElectricalSignalDomain::Unspecified,
                               volt::ElectricalDriveKind::Passive}},
    });
    const auto &pins = circuit.get(resistor).pins();
    const auto left = pins[0];
    const auto right = pins[1];
    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"BuckConverter"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power},
                          volt::TemplateNetDefinition{volt::NetName{"FB"}, volt::NetKind::Signal}},
        .components = {volt::ModuleComponentTemplate{resistor, volt::ReferenceDesignator{"R1"}}},
        .connections = {volt::ModulePinConnectionSpec{volt::NetName{"VIN"},
                                                      volt::ReferenceDesignator{"R1"}, left},
                        volt::ModulePinConnectionSpec{volt::NetName{"FB"},
                                                      volt::ReferenceDesignator{"R1"}, right}},
        .ports = {volt::ModulePortSpec{volt::PortName{"VIN"}, volt::NetName{"VIN"},
                                       volt::PortRole::PowerInput}},
    });
    const auto port = circuit.get(module).ports().front();
    const auto instance =
        circuit.instantiate_root_module(module, volt::ModuleInstanceName{"BUCK_A"});
    const auto parent_net =
        circuit.add_net(volt::NetSpec{volt::NetName{"VIN"}, volt::NetKind::Power});
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
