#include <catch2/catch_test_macros.hpp>

#include "support/circuit_test_helpers.hpp"
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/circuit/connectivity/instances.hpp>
#include <volt/circuit/connectivity/nets.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/validation/validation.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/ids.hpp>

namespace {

void set_pin_voltage_range(volt::PinSpec &pin, double minimum, double maximum) {
    pin.electrical_attributes.push_back(volt::ElectricalAttributeAssignment{
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage_range"},
            volt::ElectricalAttributeOwner::PinSpec,
            volt::ElectricalAttributeKind::Constraint,
            volt::UnitDimension::Voltage,
        },
        volt::ElectricalAttributeValue{
            volt::QuantityRange::bounded(volt::Quantity{volt::UnitDimension::Voltage, minimum},
                                         volt::Quantity{volt::UnitDimension::Voltage, maximum})}});
}

void set_net_electrical_attribute(volt::Circuit &circuit, volt::NetId net,
                                  volt::ElectricalAttributeSpec spec,
                                  volt::ElectricalAttributeValue value) {
    circuit.update(net, volt::SetNetElectricalAttribute{std::move(spec), std::move(value)});
}

void select_physical_part(volt::Circuit &circuit, volt::ComponentId component,
                          volt::PhysicalPart part) {
    circuit.update(component, volt::SelectPhysicalPart{std::move(part)});
}

void set_selected_part_electrical_attribute(volt::Circuit &circuit, volt::ComponentId component,
                                            volt::ElectricalAttributeSpec spec,
                                            volt::ElectricalAttributeValue value) {
    circuit.update(component,
                   volt::SetSelectedPartElectricalAttribute{std::move(spec), std::move(value)});
}

} // namespace

TEST_CASE("Circuit validation diagnostic code catalog remains stable") {
    const auto codes = std::set<std::string>{
        "EMPTY_NET",
        "MULTIPLE_OUTPUTS_ON_NET",
        "NET_CLASS_VOLTAGE_EXCEEDED",
        "PHYSICAL_PART_REQUIRED",
        "PIN_GROUND_ON_NON_GROUND_NET",
        "PIN_MUST_NOT_CONNECT",
        "PIN_POWER_ON_GROUND_NET",
        "PIN_VOLTAGE_RANGE_VIOLATION",
        "POWER_INPUT_WITHOUT_SOURCE",
        "SELECTED_PART_VOLTAGE_RATING_EXCEEDED",
        "SINGLE_PIN_NET",
        "INPUT_SIGNAL_DOMAIN_MISMATCH",
        "PIN_INTENTIONAL_NO_CONNECT_IS_CONNECTED",
        "UNBOUND_REQUIRED_PORT",
        "UNCONNECTED_REQUIRED_PIN",
    };

    CHECK(codes.size() == 15);
}

TEST_CASE("Circuit validation reports required pins that are not connected") {
    volt::Circuit circuit;
    const auto pin_def_spec =
        volt::PinSpec{"VDD", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input};
    const auto component_def =
        volt::test::define_component(circuit, "Regulator", std::vector{pin_def_spec});
    const auto &component_def_pins = circuit.get(component_def).pins();
    const auto pin_def = component_def_pins[0];
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto pin = volt::queries::pin_by_name(circuit, component, "VDD").value();

    const auto report = volt::validate_circuit(circuit);

    REQUIRE(report.count() == 1);
    const auto &diagnostic = report.diagnostics().front();
    CHECK(diagnostic.severity() == volt::Severity::Error);
    CHECK(diagnostic.code() == volt::DiagnosticCode{"UNCONNECTED_REQUIRED_PIN"});
    CHECK(diagnostic.category() == volt::DiagnosticCategory{volt::diagnostic_categories::Erc});
    REQUIRE(diagnostic.entities().size() == 3);
    CHECK(diagnostic.entities()[0] == volt::EntityRef::pin(pin));
    CHECK(diagnostic.entities()[1] == volt::EntityRef::component(component));
    CHECK(diagnostic.entities()[2] == volt::EntityRef::pin_def(pin_def));
}

TEST_CASE("Circuit validation does not report optional unconnected pins") {
    volt::Circuit circuit;
    const auto pin_def_spec = volt::PinSpec{"GPIO", "1", volt::ConnectionRequirement::Optional,
                                            volt::ElectricalTerminalKind::Signal,
                                            volt::ElectricalDirection::Bidirectional};
    const auto component_def =
        volt::test::define_component(circuit, "Header", std::vector{pin_def_spec});
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"J1"}});
    CHECK(component == volt::ComponentId{0});

    const auto report = volt::validate_circuit(circuit);

    CHECK(report.empty());
}

TEST_CASE("Circuit validation reports must-not-connect pins that are connected") {
    volt::Circuit circuit;
    const auto pin_def_spec = volt::PinSpec{"NC", "1", volt::ConnectionRequirement::MustNotConnect,
                                            volt::ElectricalTerminalKind::NoConnect,
                                            volt::ElectricalDirection::Unspecified};
    const auto component_def =
        volt::test::define_component(circuit, "Package", std::vector{pin_def_spec});
    const auto &component_def_pins = circuit.get(component_def).pins();
    const auto pin_def = component_def_pins[0];
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto pin = volt::queries::pin_by_name(circuit, component, "NC").value();
    const auto net =
        circuit.add_net(volt::NetSpec{volt::NetName{"ACCIDENTAL"}, volt::NetKind::Signal});

    circuit.connect(net, pin);

    const auto report = volt::validate_circuit(circuit);

    REQUIRE(report.count() == 2);
    CHECK(report.diagnostics()[0].code() == volt::DiagnosticCode{"PIN_MUST_NOT_CONNECT"});
    CHECK(report.diagnostics()[0].severity() == volt::Severity::Error);
    REQUIRE(report.diagnostics()[0].entities().size() == 4);
    CHECK(report.diagnostics()[0].entities()[0] == volt::EntityRef::pin(pin));
    CHECK(report.diagnostics()[0].entities()[1] == volt::EntityRef::component(component));
    CHECK(report.diagnostics()[0].entities()[2] == volt::EntityRef::pin_def(pin_def));
    CHECK(report.diagnostics()[0].entities()[3] == volt::EntityRef::net(net));
    CHECK(report.diagnostics()[1].code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
}

TEST_CASE("Circuit validation reports empty and single-pin nets") {
    volt::Circuit circuit;
    const auto pin_def_spec = volt::PinSpec{"1",
                                            "1",
                                            volt::ConnectionRequirement::Optional,
                                            volt::ElectricalTerminalKind::Passive,
                                            volt::ElectricalDirection::Passive,
                                            volt::ElectricalSignalDomain::Unspecified,
                                            volt::ElectricalDriveKind::Passive};
    const auto component_def =
        volt::test::define_component(circuit, "TestPoint", std::vector{pin_def_spec});
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"TP1"}});
    const auto pin = volt::queries::pin_by_number(circuit, component, "1").value();
    const auto empty_net =
        circuit.add_net(volt::NetSpec{volt::NetName{"EMPTY"}, volt::NetKind::Signal});
    const auto single_pin_net =
        circuit.add_net(volt::NetSpec{volt::NetName{"PROBE"}, volt::NetKind::Signal});

    circuit.connect(single_pin_net, pin);

    const auto report = volt::validate_circuit(circuit);

    REQUIRE(report.count() == 2);
    CHECK(report.diagnostics()[0].severity() == volt::Severity::Warning);
    CHECK(report.diagnostics()[0].code() == volt::DiagnosticCode{"EMPTY_NET"});
    CHECK(report.diagnostics()[0].entities().front() == volt::EntityRef::net(empty_net));
    CHECK(report.diagnostics()[1].severity() == volt::Severity::Warning);
    CHECK(report.diagnostics()[1].code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
    CHECK(report.diagnostics()[1].entities().front() == volt::EntityRef::net(single_pin_net));
}

TEST_CASE("Circuit validation accepts intentional stub nets") {
    volt::Circuit circuit;
    const auto pin_def_spec = volt::PinSpec{"SWDIO", "1", volt::ConnectionRequirement::Optional,
                                            volt::ElectricalTerminalKind::Signal,
                                            volt::ElectricalDirection::Bidirectional};
    const auto component_def =
        volt::test::define_component(circuit, "MCU", std::vector{pin_def_spec});
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto pin = volt::queries::pin_by_name(circuit, component, "SWDIO").value();
    const auto empty_stub =
        circuit.add_net(volt::NetSpec{volt::NetName{"BOOT_TRACE"}, volt::NetKind::Signal});
    const auto single_pin_stub =
        circuit.add_net(volt::NetSpec{volt::NetName{"SWDIO"}, volt::NetKind::Signal});

    circuit.connect(single_pin_stub, pin);
    circuit.update(empty_stub, volt::MarkIntentionalStub{});
    circuit.update(single_pin_stub, volt::MarkIntentionalStub{});

    CHECK(circuit.is_intentional_stub_net(empty_stub));
    CHECK(circuit.is_intentional_stub_net(single_pin_stub));
    CHECK(volt::validate_connectivity(circuit).empty());
}

TEST_CASE("Circuit validation accepts intentional no-connect pins") {
    volt::Circuit circuit;
    const auto pin_def_spec = volt::PinSpec{"PB2",
                                            "1",
                                            volt::ConnectionRequirement::Required,
                                            volt::ElectricalTerminalKind::Signal,
                                            volt::ElectricalDirection::Input,
                                            volt::ElectricalSignalDomain::Digital};
    const auto component_def =
        volt::test::define_component(circuit, "MCU", std::vector{pin_def_spec});
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto pin = volt::queries::pin_by_name(circuit, component, "PB2").value();

    circuit.mark_no_connect(pin);

    CHECK(circuit.is_intentional_no_connect_pin(pin));
    CHECK(volt::validate_connectivity(circuit).empty());
}

TEST_CASE("Circuit validation reports connected intentional no-connect pins") {
    volt::Circuit circuit;
    const auto pin_def_spec = volt::PinSpec{"PB2",
                                            "1",
                                            volt::ConnectionRequirement::Required,
                                            volt::ElectricalTerminalKind::Signal,
                                            volt::ElectricalDirection::Input,
                                            volt::ElectricalSignalDomain::Digital};
    const auto component_def =
        volt::test::define_component(circuit, "MCU", std::vector{pin_def_spec});
    const auto &component_def_pins = circuit.get(component_def).pins();
    const auto pin_def = component_def_pins[0];
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto pin = volt::queries::pin_by_name(circuit, component, "PB2").value();
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"PB2"}, volt::NetKind::Signal});

    circuit.connect(net, pin);
    circuit.mark_no_connect(pin);

    const auto report = volt::validate_connectivity(circuit);

    REQUIRE(report.count() == 2);
    CHECK(report.diagnostics()[0].severity() == volt::Severity::Error);
    CHECK(report.diagnostics()[0].code() ==
          volt::DiagnosticCode{"PIN_INTENTIONAL_NO_CONNECT_IS_CONNECTED"});
    REQUIRE(report.diagnostics()[0].entities().size() == 4);
    CHECK(report.diagnostics()[0].entities()[0] == volt::EntityRef::pin(pin));
    CHECK(report.diagnostics()[0].entities()[1] == volt::EntityRef::component(component));
    CHECK(report.diagnostics()[0].entities()[2] == volt::EntityRef::pin_def(pin_def));
    CHECK(report.diagnostics()[0].entities()[3] == volt::EntityRef::net(net));
    CHECK(report.diagnostics()[1].code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
}

TEST_CASE("Circuit validation treats bound module port nets as connected for net shape") {
    volt::Circuit circuit;
    const auto input_pin_spec = volt::PinSpec{"IN",
                                              "1",
                                              volt::ConnectionRequirement::Required,
                                              volt::ElectricalTerminalKind::Passive,
                                              volt::ElectricalDirection::Passive,
                                              volt::ElectricalSignalDomain::Unspecified,
                                              volt::ElectricalDriveKind::Passive};
    const auto output_pin_spec = volt::PinSpec{"OUT",
                                               "1",
                                               volt::ConnectionRequirement::Required,
                                               volt::ElectricalTerminalKind::Passive,
                                               volt::ElectricalDirection::Passive,
                                               volt::ElectricalSignalDomain::Unspecified,
                                               volt::ElectricalDriveKind::Passive};
    const auto module_component_def =
        volt::test::define_component(circuit, "Load", std::vector{input_pin_spec});
    const auto &module_component_def_pins = circuit.get(module_component_def).pins();
    const auto input_pin = module_component_def_pins[0];
    const auto parent_component_def =
        volt::test::define_component(circuit, "Source", std::vector{output_pin_spec});

    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"LoadBlock"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"VIN"}, volt::NetKind::Power}},
        .components = {volt::ModuleComponentTemplate{module_component_def,
                                                     volt::ReferenceDesignator{"U1"}}},
        .connections = {volt::ModulePinConnectionSpec{volt::NetName{"VIN"},
                                                      volt::ReferenceDesignator{"U1"}, input_pin}},
        .ports = {volt::ModulePortSpec{volt::PortName{"VIN"}, volt::NetName{"VIN"},
                                       volt::PortRole::PowerInput}},
    });
    const auto port = circuit.get(module).ports().front();
    const auto instance =
        circuit.instantiate_root_module(module, volt::ModuleInstanceName{"LOAD_A"});
    const auto parent_component = circuit.instantiate_component(
        parent_component_def,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U2"}});
    const auto parent_net =
        circuit.add_net(volt::NetSpec{volt::NetName{"VIN"}, volt::NetKind::Power});

    circuit.connect(parent_net,
                    volt::queries::pin_by_name(circuit, parent_component, "OUT").value());
    [[maybe_unused]] const auto binding = circuit.bind_port(instance, port, parent_net);

    const auto report = volt::validate_connectivity(circuit);

    CHECK(report.empty());
}

TEST_CASE("Circuit validation treats bound module port nets as connected for power sources") {
    volt::Circuit circuit;
    auto power_input_spec =
        volt::PinSpec{"VCC", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input};
    const auto power_source_spec =
        volt::PinSpec{"OUT", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Output};
    const auto load_def =
        volt::test::define_component(circuit, "Load", std::vector{power_input_spec});
    const auto &load_def_pins = circuit.get(load_def).pins();
    const auto power_input = load_def_pins[0];
    const auto regulator_def =
        volt::test::define_component(circuit, "Regulator", std::vector{power_source_spec});

    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"LoadBlock"},
        .template_nets = {volt::TemplateNetDefinition{volt::NetName{"VCC"}, volt::NetKind::Power}},
        .components = {volt::ModuleComponentTemplate{load_def, volt::ReferenceDesignator{"U1"}}},
        .connections = {volt::ModulePinConnectionSpec{
            volt::NetName{"VCC"}, volt::ReferenceDesignator{"U1"}, power_input}},
        .ports = {volt::ModulePortSpec{volt::PortName{"VCC"}, volt::NetName{"VCC"},
                                       volt::PortRole::PowerInput}},
    });
    const auto port = circuit.get(module).ports().front();
    const auto instance =
        circuit.instantiate_root_module(module, volt::ModuleInstanceName{"LOAD_A"});
    const auto regulator = circuit.instantiate_component(
        regulator_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U2"}});
    const auto parent_net =
        circuit.add_net(volt::NetSpec{volt::NetName{"VCC"}, volt::NetKind::Power});

    circuit.connect(parent_net, volt::queries::pin_by_name(circuit, regulator, "OUT").value());
    [[maybe_unused]] const auto binding = circuit.bind_port(instance, port, parent_net);

    const auto report = volt::validate_circuit(circuit);

    CHECK(report.empty());
}

TEST_CASE("Circuit connectivity validation excludes electrical rule diagnostics") {
    volt::Circuit circuit;
    auto power_input_spec =
        volt::PinSpec{"VCC", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input};
    const auto unconnected_pin_def_spec = volt::PinSpec{"EN",
                                                        "2",
                                                        volt::ConnectionRequirement::Required,
                                                        volt::ElectricalTerminalKind::Signal,
                                                        volt::ElectricalDirection::Input,
                                                        volt::ElectricalSignalDomain::Digital};
    set_pin_voltage_range(power_input_spec, 1.8, 3.6);
    const auto component_def = volt::test::define_component(
        circuit, "Load", std::vector{power_input_spec, unconnected_pin_def_spec});
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto power_pin = volt::queries::pin_by_name(circuit, component, "VCC").value();
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"VCC"}, volt::NetKind::Power});

    circuit.connect(net, power_pin);
    set_net_electrical_attribute(
        circuit, net,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage"}, volt::ElectricalAttributeOwner::Net,
            volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 5.0}});

    const auto report = volt::validate_connectivity(circuit);

    REQUIRE(report.count() == 2);
    CHECK(report.diagnostics()[0].code() == volt::DiagnosticCode{"UNCONNECTED_REQUIRED_PIN"});
    CHECK(report.diagnostics()[1].code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
}

TEST_CASE("Circuit electrical-rule validation excludes connectivity diagnostics") {
    volt::Circuit circuit;
    auto power_input_spec =
        volt::PinSpec{"VCC", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input};
    const auto unconnected_pin_def_spec = volt::PinSpec{"EN",
                                                        "2",
                                                        volt::ConnectionRequirement::Required,
                                                        volt::ElectricalTerminalKind::Signal,
                                                        volt::ElectricalDirection::Input,
                                                        volt::ElectricalSignalDomain::Digital};
    set_pin_voltage_range(power_input_spec, 1.8, 3.6);
    const auto component_def = volt::test::define_component(
        circuit, "Load", std::vector{power_input_spec, unconnected_pin_def_spec});
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto power_pin = volt::queries::pin_by_name(circuit, component, "VCC").value();
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"VCC"}, volt::NetKind::Power});

    circuit.connect(net, power_pin);
    set_net_electrical_attribute(
        circuit, net,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage"}, volt::ElectricalAttributeOwner::Net,
            volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 5.0}});

    const auto report = volt::validate_electrical_rules(circuit);

    REQUIRE(report.count() == 1);
    CHECK(report.diagnostics()[0].code() == volt::DiagnosticCode{"PIN_VOLTAGE_RANGE_VIOLATION"});
}

TEST_CASE("Full circuit validation preserves connectivity before electrical rules") {
    volt::Circuit circuit;
    auto power_input_spec =
        volt::PinSpec{"VCC", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input};
    const auto unconnected_pin_def_spec = volt::PinSpec{"EN",
                                                        "2",
                                                        volt::ConnectionRequirement::Required,
                                                        volt::ElectricalTerminalKind::Signal,
                                                        volt::ElectricalDirection::Input,
                                                        volt::ElectricalSignalDomain::Digital};
    set_pin_voltage_range(power_input_spec, 1.8, 3.6);
    const auto component_def = volt::test::define_component(
        circuit, "Load", std::vector{power_input_spec, unconnected_pin_def_spec});
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto power_pin = volt::queries::pin_by_name(circuit, component, "VCC").value();
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"VCC"}, volt::NetKind::Power});

    circuit.connect(net, power_pin);
    set_net_electrical_attribute(
        circuit, net,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage"}, volt::ElectricalAttributeOwner::Net,
            volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 5.0}});

    const auto report = volt::validate_circuit(circuit);

    REQUIRE(report.count() == 3);
    CHECK(report.diagnostics()[0].code() == volt::DiagnosticCode{"UNCONNECTED_REQUIRED_PIN"});
    CHECK(report.diagnostics()[1].code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
    CHECK(report.diagnostics()[2].code() == volt::DiagnosticCode{"PIN_VOLTAGE_RANGE_VIOLATION"});
    for (const auto &diagnostic : report.diagnostics()) {
        CHECK(diagnostic.category() == volt::DiagnosticCategory{volt::diagnostic_categories::Erc});
    }
}

TEST_CASE("PCB readiness validation reports components without selected physical parts") {
    volt::Circuit circuit;
    const auto first_pin_def_spec = volt::PinSpec{"1",
                                                  "1",
                                                  volt::ConnectionRequirement::Required,
                                                  volt::ElectricalTerminalKind::Passive,
                                                  volt::ElectricalDirection::Passive,
                                                  volt::ElectricalSignalDomain::Unspecified,
                                                  volt::ElectricalDriveKind::Passive};
    const auto second_pin_def_spec = volt::PinSpec{"2",
                                                   "2",
                                                   volt::ConnectionRequirement::Required,
                                                   volt::ElectricalTerminalKind::Passive,
                                                   volt::ElectricalDirection::Passive,
                                                   volt::ElectricalSignalDomain::Unspecified,
                                                   volt::ElectricalDriveKind::Passive};
    const auto resistor_def = volt::test::define_component(
        circuit, "Resistor", std::vector{first_pin_def_spec, second_pin_def_spec});
    const auto resistor = circuit.instantiate_component(
        resistor_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    const auto first_pin = volt::queries::pin_by_number(circuit, resistor, "1").value();
    const auto second_pin = volt::queries::pin_by_number(circuit, resistor, "2").value();
    const auto input = circuit.add_net(volt::NetSpec{volt::NetName{"IN"}, volt::NetKind::Signal});
    const auto output = circuit.add_net(volt::NetSpec{volt::NetName{"OUT"}, volt::NetKind::Signal});

    circuit.connect(input, first_pin);
    circuit.connect(output, second_pin);

    const auto logical_report = volt::validate_circuit(circuit);
    const auto pcb_report = volt::validate_for_pcb(circuit);

    REQUIRE(logical_report.count() == 2);
    CHECK(logical_report.diagnostics()[0].code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
    CHECK(logical_report.diagnostics()[1].code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
    REQUIRE(pcb_report.count() == 3);
    CHECK(pcb_report.diagnostics()[0].code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
    CHECK(pcb_report.diagnostics()[1].code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
    const auto &diagnostic = pcb_report.diagnostics()[2];
    CHECK(diagnostic.severity() == volt::Severity::Error);
    CHECK(diagnostic.code() == volt::DiagnosticCode{"PHYSICAL_PART_REQUIRED"});
    REQUIRE(diagnostic.entities().size() == 2);
    CHECK(diagnostic.entities()[0] == volt::EntityRef::component(resistor));
    CHECK(diagnostic.entities()[1] == volt::EntityRef::component_def(resistor_def));
}

TEST_CASE("PCB readiness validation accepts components with selected physical parts") {
    volt::Circuit circuit;
    const auto first_pin_def_spec = volt::PinSpec{"1",
                                                  "1",
                                                  volt::ConnectionRequirement::Required,
                                                  volt::ElectricalTerminalKind::Passive,
                                                  volt::ElectricalDirection::Passive,
                                                  volt::ElectricalSignalDomain::Unspecified,
                                                  volt::ElectricalDriveKind::Passive};
    const auto second_pin_def_spec = volt::PinSpec{"2",
                                                   "2",
                                                   volt::ConnectionRequirement::Required,
                                                   volt::ElectricalTerminalKind::Passive,
                                                   volt::ElectricalDirection::Passive,
                                                   volt::ElectricalSignalDomain::Unspecified,
                                                   volt::ElectricalDriveKind::Passive};
    const auto resistor_def = volt::test::define_component(
        circuit, "Resistor", std::vector{first_pin_def_spec, second_pin_def_spec});
    const auto &resistor_def_pins = circuit.get(resistor_def).pins();
    const auto first_pin_def = resistor_def_pins[0];
    const auto second_pin_def = resistor_def_pins[1];
    const auto resistor = circuit.instantiate_component(
        resistor_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    const auto first_pin = volt::queries::pin_by_number(circuit, resistor, "1").value();
    const auto second_pin = volt::queries::pin_by_number(circuit, resistor, "2").value();
    const auto input = circuit.add_net(volt::NetSpec{volt::NetName{"IN"}, volt::NetKind::Signal});
    const auto output = circuit.add_net(volt::NetSpec{volt::NetName{"OUT"}, volt::NetKind::Signal});

    circuit.connect(input, first_pin);
    circuit.connect(output, second_pin);
    select_physical_part(circuit, resistor,
                         volt::PhysicalPart{volt::ManufacturerPart{"Yageo", "RC0603FR-0710KL"},
                                            volt::PackageRef{"0603"},
                                            volt::FootprintRef{"Resistor_SMD", "R_0603_1608Metric"},
                                            std::vector{volt::PinPadMapping{first_pin_def, "1"},
                                                        volt::PinPadMapping{second_pin_def, "2"}}});

    const auto report = volt::validate_for_pcb(circuit);

    REQUIRE(report.count() == 2);
    CHECK(report.diagnostics()[0].code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
    CHECK(report.diagnostics()[1].code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
}

TEST_CASE("Circuit validation reports selected part voltage rating violations") {
    volt::Circuit circuit;
    const auto pin_def_spec = volt::PinSpec{"1",
                                            "1",
                                            volt::ConnectionRequirement::Required,
                                            volt::ElectricalTerminalKind::Passive,
                                            volt::ElectricalDirection::Passive,
                                            volt::ElectricalSignalDomain::Unspecified,
                                            volt::ElectricalDriveKind::Passive};
    const auto component_def =
        volt::test::define_component(circuit, "Capacitor", std::vector{pin_def_spec});
    const auto &component_def_pins = circuit.get(component_def).pins();
    const auto pin_def = component_def_pins[0];
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"C1"}});
    const auto pin = volt::queries::pin_by_number(circuit, component, "1").value();
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"VDD"}, volt::NetKind::Power});

    circuit.connect(net, pin);
    set_net_electrical_attribute(
        circuit, net,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage"}, volt::ElectricalAttributeOwner::Net,
            volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 5.0}});
    select_physical_part(circuit, component,
                         volt::PhysicalPart{volt::ManufacturerPart{"Test", "C-3V3"},
                                            volt::PackageRef{"0603"},
                                            volt::FootprintRef{"Capacitor_SMD", "C_0603"},
                                            std::vector{volt::PinPadMapping{pin_def, "1"}}});
    set_selected_part_electrical_attribute(
        circuit, component,
        volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{"voltage_rating"},
                                      volt::ElectricalAttributeOwner::SelectedPart,
                                      volt::ElectricalAttributeKind::DesignInput,
                                      volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}});

    const auto report = volt::validate_circuit(circuit);

    REQUIRE(report.count() == 2);
    CHECK(report.diagnostics()[0].code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
    const auto &diagnostic = report.diagnostics()[1];
    CHECK(diagnostic.severity() == volt::Severity::Error);
    CHECK(diagnostic.code() == volt::DiagnosticCode{"SELECTED_PART_VOLTAGE_RATING_EXCEEDED"});
    REQUIRE(diagnostic.entities().size() == 3);
    CHECK(diagnostic.entities()[0] == volt::EntityRef::net(net));
    CHECK(diagnostic.entities()[1] == volt::EntityRef::pin(pin));
    CHECK(diagnostic.entities()[2] == volt::EntityRef::component(component));
}

TEST_CASE("Circuit validation ignores non-quantity voltage attributes for voltage rating checks") {
    volt::Circuit circuit;
    const auto pin_def_spec = volt::PinSpec{"1",
                                            "1",
                                            volt::ConnectionRequirement::Required,
                                            volt::ElectricalTerminalKind::Passive,
                                            volt::ElectricalDirection::Passive,
                                            volt::ElectricalSignalDomain::Unspecified,
                                            volt::ElectricalDriveKind::Passive};
    const auto component_def =
        volt::test::define_component(circuit, "Capacitor", std::vector{pin_def_spec});
    const auto &component_def_pins = circuit.get(component_def).pins();
    const auto pin_def = component_def_pins[0];
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"C1"}});
    const auto pin = volt::queries::pin_by_number(circuit, component, "1").value();
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"VDD"}, volt::NetKind::Power});

    circuit.connect(net, pin);
    set_net_electrical_attribute(
        circuit, net,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage"}, volt::ElectricalAttributeOwner::Net,
            volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Ratio},
        volt::ElectricalAttributeValue{volt::Tolerance::percent(0.1)});
    select_physical_part(circuit, component,
                         volt::PhysicalPart{volt::ManufacturerPart{"Test", "C-16V"},
                                            volt::PackageRef{"0603"},
                                            volt::FootprintRef{"Capacitor_SMD", "C_0603"},
                                            std::vector{volt::PinPadMapping{pin_def, "1"}}});
    set_selected_part_electrical_attribute(
        circuit, component,
        volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{"voltage_rating"},
                                      volt::ElectricalAttributeOwner::SelectedPart,
                                      volt::ElectricalAttributeKind::DesignInput,
                                      volt::UnitDimension::Ratio},
        volt::ElectricalAttributeValue{volt::Tolerance::percent(0.1)});

    const auto report = volt::validate_circuit(circuit);

    REQUIRE(report.count() == 1);
    CHECK(report.diagnostics()[0].code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
}

TEST_CASE("Circuit validation accepts nets within selected part voltage ratings") {
    volt::Circuit circuit;
    const auto pin_def_spec = volt::PinSpec{"1",
                                            "1",
                                            volt::ConnectionRequirement::Required,
                                            volt::ElectricalTerminalKind::Passive,
                                            volt::ElectricalDirection::Passive,
                                            volt::ElectricalSignalDomain::Unspecified,
                                            volt::ElectricalDriveKind::Passive};
    const auto component_def =
        volt::test::define_component(circuit, "Capacitor", std::vector{pin_def_spec});
    const auto &component_def_pins = circuit.get(component_def).pins();
    const auto pin_def = component_def_pins[0];
    const auto component = circuit.instantiate_component(
        component_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"C1"}});
    const auto pin = volt::queries::pin_by_number(circuit, component, "1").value();
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"VDD"}, volt::NetKind::Power});

    circuit.connect(net, pin);
    set_net_electrical_attribute(
        circuit, net,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage"}, volt::ElectricalAttributeOwner::Net,
            volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}});
    select_physical_part(circuit, component,
                         volt::PhysicalPart{volt::ManufacturerPart{"Test", "C-16V"},
                                            volt::PackageRef{"0603"},
                                            volt::FootprintRef{"Capacitor_SMD", "C_0603"},
                                            std::vector{volt::PinPadMapping{pin_def, "1"}}});
    set_selected_part_electrical_attribute(
        circuit, component,
        volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{"voltage_rating"},
                                      volt::ElectricalAttributeOwner::SelectedPart,
                                      volt::ElectricalAttributeKind::DesignInput,
                                      volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 16.0}});

    const auto report = volt::validate_circuit(circuit);

    REQUIRE(report.count() == 1);
    CHECK(report.diagnostics()[0].code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
}

TEST_CASE("Circuit validation reports pin voltage range violations") {
    volt::Circuit circuit;
    auto power_input_spec =
        volt::PinSpec{"VCC", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input};
    const auto power_source_spec =
        volt::PinSpec{"OUT", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Output};
    set_pin_voltage_range(power_input_spec, 1.8, 3.6);
    const auto load_def =
        volt::test::define_component(circuit, "Load", std::vector{power_input_spec});
    const auto &load_def_pins = circuit.get(load_def).pins();
    const auto power_input = load_def_pins[0];
    const auto regulator_def =
        volt::test::define_component(circuit, "Regulator", std::vector{power_source_spec});
    const auto load = circuit.instantiate_component(
        load_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto regulator = circuit.instantiate_component(
        regulator_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U2"}});
    const auto load_pin = volt::queries::pin_by_name(circuit, load, "VCC").value();
    const auto source_pin = volt::queries::pin_by_name(circuit, regulator, "OUT").value();
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"VCC"}, volt::NetKind::Power});

    circuit.connect(net, load_pin);
    circuit.connect(net, source_pin);
    set_net_electrical_attribute(
        circuit, net,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage"}, volt::ElectricalAttributeOwner::Net,
            volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 5.0}});

    const auto report = volt::validate_circuit(circuit);

    REQUIRE(report.count() == 1);
    const auto &diagnostic = report.diagnostics().front();
    CHECK(diagnostic.severity() == volt::Severity::Error);
    CHECK(diagnostic.code() == volt::DiagnosticCode{"PIN_VOLTAGE_RANGE_VIOLATION"});
    REQUIRE(diagnostic.entities().size() == 3);
    CHECK(diagnostic.entities()[0] == volt::EntityRef::net(net));
    CHECK(diagnostic.entities()[1] == volt::EntityRef::pin(load_pin));
    CHECK(diagnostic.entities()[2] == volt::EntityRef::pin_def(power_input));
}

TEST_CASE("Circuit validation accepts net voltages within pin voltage ranges") {
    volt::Circuit circuit;
    auto power_input_spec =
        volt::PinSpec{"VCC", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input};
    const auto power_source_spec =
        volt::PinSpec{"OUT", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Output};
    set_pin_voltage_range(power_input_spec, 1.8, 3.6);
    const auto load_def =
        volt::test::define_component(circuit, "Load", std::vector{power_input_spec});
    const auto regulator_def =
        volt::test::define_component(circuit, "Regulator", std::vector{power_source_spec});
    const auto load = circuit.instantiate_component(
        load_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto regulator = circuit.instantiate_component(
        regulator_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U2"}});
    const auto load_pin = volt::queries::pin_by_name(circuit, load, "VCC").value();
    const auto source_pin = volt::queries::pin_by_name(circuit, regulator, "OUT").value();
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"VCC"}, volt::NetKind::Power});

    circuit.connect(net, load_pin);
    circuit.connect(net, source_pin);
    set_net_electrical_attribute(
        circuit, net,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage"}, volt::ElectricalAttributeOwner::Net,
            volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}});

    const auto report = volt::validate_circuit(circuit);

    CHECK(report.empty());
}

TEST_CASE("Circuit validation ignores pin voltage ranges without net voltage") {
    volt::Circuit circuit;
    auto power_input_spec =
        volt::PinSpec{"VCC", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input};
    const auto power_source_spec =
        volt::PinSpec{"OUT", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Output};
    set_pin_voltage_range(power_input_spec, 1.8, 3.6);
    const auto load_def =
        volt::test::define_component(circuit, "Load", std::vector{power_input_spec});
    const auto regulator_def =
        volt::test::define_component(circuit, "Regulator", std::vector{power_source_spec});
    const auto load = circuit.instantiate_component(
        load_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto regulator = circuit.instantiate_component(
        regulator_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U2"}});
    const auto load_pin = volt::queries::pin_by_name(circuit, load, "VCC").value();
    const auto source_pin = volt::queries::pin_by_name(circuit, regulator, "OUT").value();
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"VCC"}, volt::NetKind::Power});

    circuit.connect(net, load_pin);
    circuit.connect(net, source_pin);

    const auto report = volt::validate_circuit(circuit);

    CHECK(report.empty());
}

TEST_CASE("Circuit validation reports power inputs without typed supply sources") {
    volt::Circuit circuit;
    auto power_input_spec =
        volt::PinSpec{"VCC", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input};
    const auto passive_pin_spec = volt::PinSpec{"1",
                                                "1",
                                                volt::ConnectionRequirement::Required,
                                                volt::ElectricalTerminalKind::Passive,
                                                volt::ElectricalDirection::Passive,
                                                volt::ElectricalSignalDomain::Unspecified,
                                                volt::ElectricalDriveKind::Passive};
    const auto load_def =
        volt::test::define_component(circuit, "Load", std::vector{power_input_spec});
    const auto resistor_def =
        volt::test::define_component(circuit, "Resistor", std::vector{passive_pin_spec});
    const auto load = circuit.instantiate_component(
        load_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto resistor = circuit.instantiate_component(
        resistor_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    const auto load_pin = volt::queries::pin_by_name(circuit, load, "VCC").value();
    const auto resistor_pin = volt::queries::pin_by_number(circuit, resistor, "1").value();
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"VCC"}, volt::NetKind::Power});

    circuit.connect(net, load_pin);
    circuit.connect(net, resistor_pin);

    const auto report = volt::validate_circuit(circuit);

    REQUIRE(report.count() == 1);
    CHECK(report.diagnostics()[0].severity() == volt::Severity::Error);
    CHECK(report.diagnostics()[0].code() == volt::DiagnosticCode{"POWER_INPUT_WITHOUT_SOURCE"});
    REQUIRE(report.diagnostics()[0].entities().size() == 2);
    CHECK(report.diagnostics()[0].entities()[0] == volt::EntityRef::net(net));
    CHECK(report.diagnostics()[0].entities()[1] == volt::EntityRef::pin(load_pin));
}

TEST_CASE("Circuit validation accepts typed power inputs with typed supply sources") {
    volt::Circuit circuit;
    auto power_input_spec =
        volt::PinSpec{"VCC", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input};
    const auto power_source_spec =
        volt::PinSpec{"OUT", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Output};
    const auto load_def =
        volt::test::define_component(circuit, "Load", std::vector{power_input_spec});
    const auto regulator_def =
        volt::test::define_component(circuit, "Regulator", std::vector{power_source_spec});
    const auto load = circuit.instantiate_component(
        load_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto regulator = circuit.instantiate_component(
        regulator_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U2"}});
    const auto load_pin = volt::queries::pin_by_name(circuit, load, "VCC").value();
    const auto source_pin = volt::queries::pin_by_name(circuit, regulator, "OUT").value();
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"VCC"}, volt::NetKind::Power});

    circuit.connect(net, load_pin);
    circuit.connect(net, source_pin);

    const auto report = volt::validate_circuit(circuit);

    CHECK(report.empty());
}

TEST_CASE("Circuit validation reports power and ground domain mismatches") {
    volt::Circuit circuit;
    const auto ground_pin_def_spec =
        volt::PinSpec{"GND", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Ground, volt::ElectricalDirection::Passive};
    const auto power_pin_def_spec =
        volt::PinSpec{"VCC", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input};
    const auto ground_def =
        volt::test::define_component(circuit, "Grounded", std::vector{ground_pin_def_spec});
    const auto power_def =
        volt::test::define_component(circuit, "Powered", std::vector{power_pin_def_spec});
    const auto grounded = circuit.instantiate_component(
        ground_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto powered = circuit.instantiate_component(
        power_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U2"}});
    const auto ground_pin = volt::queries::pin_by_name(circuit, grounded, "GND").value();
    const auto power_pin = volt::queries::pin_by_name(circuit, powered, "VCC").value();
    const auto power_net =
        circuit.add_net(volt::NetSpec{volt::NetName{"VCC"}, volt::NetKind::Power});
    const auto ground_net =
        circuit.add_net(volt::NetSpec{volt::NetName{"GND"}, volt::NetKind::Ground});

    circuit.connect(power_net, ground_pin);
    circuit.connect(ground_net, power_pin);

    const auto report = volt::validate_circuit(circuit);

    REQUIRE(report.count() == 4);
    CHECK(report.diagnostics()[0].code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
    CHECK(report.diagnostics()[1].code() == volt::DiagnosticCode{"PIN_GROUND_ON_NON_GROUND_NET"});
    CHECK(report.diagnostics()[2].code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
    CHECK(report.diagnostics()[3].code() == volt::DiagnosticCode{"PIN_POWER_ON_GROUND_NET"});
}

TEST_CASE("Circuit validation reports multiple output drivers on one net") {
    volt::Circuit circuit;
    const auto output_a_spec = volt::PinSpec{"OUT_A",
                                             "1",
                                             volt::ConnectionRequirement::Required,
                                             volt::ElectricalTerminalKind::Signal,
                                             volt::ElectricalDirection::Output,
                                             volt::ElectricalSignalDomain::Digital};
    const auto output_b_spec =
        volt::PinSpec{"OUT_B", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Output};
    const auto driver_a =
        volt::test::define_component(circuit, "DriverA", std::vector{output_a_spec});
    const auto driver_b =
        volt::test::define_component(circuit, "DriverB", std::vector{output_b_spec});
    const auto component_a = circuit.instantiate_component(
        driver_a, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto component_b = circuit.instantiate_component(
        driver_b, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U2"}});
    const auto pin_a = volt::queries::pin_by_name(circuit, component_a, "OUT_A").value();
    const auto pin_b = volt::queries::pin_by_name(circuit, component_b, "OUT_B").value();
    const auto net =
        circuit.add_net(volt::NetSpec{volt::NetName{"CONFLICT"}, volt::NetKind::Signal});

    circuit.connect(net, pin_a);
    circuit.connect(net, pin_b);

    const auto report = volt::validate_circuit(circuit);

    REQUIRE(report.count() == 1);
    const auto &diagnostic = report.diagnostics().front();
    CHECK(diagnostic.severity() == volt::Severity::Error);
    CHECK(diagnostic.code() == volt::DiagnosticCode{"MULTIPLE_OUTPUTS_ON_NET"});
    REQUIRE(diagnostic.entities().size() == 3);
    CHECK(diagnostic.entities()[0] == volt::EntityRef::net(net));
    CHECK(diagnostic.entities()[1] == volt::EntityRef::pin(pin_a));
    CHECK(diagnostic.entities()[2] == volt::EntityRef::pin(pin_b));
}
