#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/circuit/instances.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/circuit/validation.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/ids.hpp>

TEST_CASE("Circuit validation diagnostic code catalog remains stable") {
    const auto codes = std::set<std::string>{
        "EMPTY_NET",
        "MULTIPLE_OUTPUTS_ON_NET",
        "PIN_GROUND_ON_NON_GROUND_NET",
        "PIN_MUST_NOT_CONNECT",
        "PIN_POWER_ON_GROUND_NET",
        "PIN_VOLTAGE_RANGE_VIOLATION",
        "POWER_INPUT_WITHOUT_SOURCE",
        "SELECTED_PART_VOLTAGE_RATING_EXCEEDED",
        "SINGLE_PIN_NET",
        "UNCONNECTED_REQUIRED_PIN",
    };

    CHECK(codes.size() == 10);
}

TEST_CASE("Circuit validation reports required pins that are not connected") {
    volt::Circuit circuit;
    const auto pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "VDD", "1", volt::PinRole::PowerInput, volt::ConnectionRequirement::Required});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Regulator", std::vector{pin_def}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"U1"});
    const auto pin = circuit.pin_by_name(component, "VDD").value();

    const auto report = volt::validate_circuit(circuit);

    REQUIRE(report.count() == 1);
    const auto &diagnostic = report.diagnostics().front();
    CHECK(diagnostic.severity() == volt::Severity::Error);
    CHECK(diagnostic.code() == volt::DiagnosticCode{"UNCONNECTED_REQUIRED_PIN"});
    REQUIRE(diagnostic.entities().size() == 3);
    CHECK(diagnostic.entities()[0] == volt::EntityRef::pin(pin));
    CHECK(diagnostic.entities()[1] == volt::EntityRef::component(component));
    CHECK(diagnostic.entities()[2] == volt::EntityRef::pin_def(pin_def));
}

TEST_CASE("Circuit validation does not report optional unconnected pins") {
    volt::Circuit circuit;
    const auto pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "GPIO", "1", volt::PinRole::Bidirectional, volt::ConnectionRequirement::Optional});
    const auto component_def =
        circuit.add_component_definition(volt::ComponentDefinition{"Header", std::vector{pin_def}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"J1"});
    CHECK(component == volt::ComponentId{0});

    const auto report = volt::validate_circuit(circuit);

    CHECK(report.empty());
}

TEST_CASE("Circuit validation reports must-not-connect pins that are connected") {
    volt::Circuit circuit;
    const auto pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "NC", "1", volt::PinRole::NoConnect, volt::ConnectionRequirement::MustNotConnect});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Package", std::vector{pin_def}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"U1"});
    const auto pin = circuit.pin_by_name(component, "NC").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"ACCIDENTAL"}, volt::NetKind::Signal});

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
    const auto pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::PinRole::Passive, volt::ConnectionRequirement::Optional});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"TestPoint", std::vector{pin_def}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"TP1"});
    const auto pin = circuit.pin_by_number(component, "1").value();
    const auto empty_net =
        circuit.add_net(volt::Net{volt::NetName{"EMPTY"}, volt::NetKind::Signal});
    const auto single_pin_net =
        circuit.add_net(volt::Net{volt::NetName{"PROBE"}, volt::NetKind::Signal});

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

TEST_CASE("Circuit connectivity validation excludes electrical rule diagnostics") {
    volt::Circuit circuit;
    const auto power_input = circuit.add_pin_definition(volt::PinDefinition{
        "VCC", "1", volt::PinRole::PowerInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto unconnected_pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "EN", "2", volt::PinRole::DigitalInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Signal, volt::ElectricalDirection::Input});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Load", std::vector{power_input, unconnected_pin_def}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"U1"});
    const auto power_pin = circuit.pin_by_name(component, "VCC").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});

    circuit.set_pin_definition_electrical_attribute(
        power_input,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage_range"},
            volt::ElectricalAttributeOwner::PinSpec,
            volt::ElectricalAttributeKind::Constraint,
            volt::UnitDimension::Voltage,
        },
        volt::ElectricalAttributeValue{
            volt::QuantityRange::bounded(volt::Quantity{volt::UnitDimension::Voltage, 1.8},
                                         volt::Quantity{volt::UnitDimension::Voltage, 3.6})});
    circuit.connect(net, power_pin);
    circuit.set_net_electrical_attribute(
        net,
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
    const auto power_input = circuit.add_pin_definition(volt::PinDefinition{
        "VCC", "1", volt::PinRole::PowerInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto unconnected_pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "EN", "2", volt::PinRole::DigitalInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Signal, volt::ElectricalDirection::Input});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Load", std::vector{power_input, unconnected_pin_def}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"U1"});
    const auto power_pin = circuit.pin_by_name(component, "VCC").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});

    circuit.set_pin_definition_electrical_attribute(
        power_input,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage_range"},
            volt::ElectricalAttributeOwner::PinSpec,
            volt::ElectricalAttributeKind::Constraint,
            volt::UnitDimension::Voltage,
        },
        volt::ElectricalAttributeValue{
            volt::QuantityRange::bounded(volt::Quantity{volt::UnitDimension::Voltage, 1.8},
                                         volt::Quantity{volt::UnitDimension::Voltage, 3.6})});
    circuit.connect(net, power_pin);
    circuit.set_net_electrical_attribute(
        net,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage"}, volt::ElectricalAttributeOwner::Net,
            volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 5.0}});

    const auto report = volt::validate_electrical_rules(circuit);

    REQUIRE(report.count() == 2);
    CHECK(report.diagnostics()[0].code() == volt::DiagnosticCode{"POWER_INPUT_WITHOUT_SOURCE"});
    CHECK(report.diagnostics()[1].code() == volt::DiagnosticCode{"PIN_VOLTAGE_RANGE_VIOLATION"});
}

TEST_CASE("Full circuit validation preserves connectivity before electrical rules") {
    volt::Circuit circuit;
    const auto power_input = circuit.add_pin_definition(volt::PinDefinition{
        "VCC", "1", volt::PinRole::PowerInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto unconnected_pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "EN", "2", volt::PinRole::DigitalInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Signal, volt::ElectricalDirection::Input});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Load", std::vector{power_input, unconnected_pin_def}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"U1"});
    const auto power_pin = circuit.pin_by_name(component, "VCC").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});

    circuit.set_pin_definition_electrical_attribute(
        power_input,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage_range"},
            volt::ElectricalAttributeOwner::PinSpec,
            volt::ElectricalAttributeKind::Constraint,
            volt::UnitDimension::Voltage,
        },
        volt::ElectricalAttributeValue{
            volt::QuantityRange::bounded(volt::Quantity{volt::UnitDimension::Voltage, 1.8},
                                         volt::Quantity{volt::UnitDimension::Voltage, 3.6})});
    circuit.connect(net, power_pin);
    circuit.set_net_electrical_attribute(
        net,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage"}, volt::ElectricalAttributeOwner::Net,
            volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 5.0}});

    const auto report = volt::validate_circuit(circuit);

    REQUIRE(report.count() == 4);
    CHECK(report.diagnostics()[0].code() == volt::DiagnosticCode{"UNCONNECTED_REQUIRED_PIN"});
    CHECK(report.diagnostics()[1].code() == volt::DiagnosticCode{"SINGLE_PIN_NET"});
    CHECK(report.diagnostics()[2].code() == volt::DiagnosticCode{"POWER_INPUT_WITHOUT_SOURCE"});
    CHECK(report.diagnostics()[3].code() == volt::DiagnosticCode{"PIN_VOLTAGE_RANGE_VIOLATION"});
}

TEST_CASE("Circuit validation reports selected part voltage rating violations") {
    volt::Circuit circuit;
    const auto pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::PinRole::Passive, volt::ConnectionRequirement::Required});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Capacitor", std::vector{pin_def}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"C1"});
    const auto pin = circuit.pin_by_number(component, "1").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VDD"}, volt::NetKind::Power});

    circuit.connect(net, pin);
    circuit.set_net_electrical_attribute(
        net,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage"}, volt::ElectricalAttributeOwner::Net,
            volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 5.0}});
    circuit.select_physical_part(
        component,
        volt::PhysicalPart{volt::ManufacturerPart{"Test", "C-3V3"}, volt::PackageRef{"0603"},
                           volt::FootprintRef{"Capacitor_SMD", "C_0603"},
                           std::vector{volt::PinPadMapping{pin_def, "1"}}});
    circuit.set_selected_part_electrical_attribute(
        component,
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
    const auto pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::PinRole::Passive, volt::ConnectionRequirement::Required});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Capacitor", std::vector{pin_def}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"C1"});
    const auto pin = circuit.pin_by_number(component, "1").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VDD"}, volt::NetKind::Power});

    circuit.connect(net, pin);
    circuit.set_net_electrical_attribute(
        net,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage"}, volt::ElectricalAttributeOwner::Net,
            volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Ratio},
        volt::ElectricalAttributeValue{volt::Tolerance::percent(0.1)});
    circuit.select_physical_part(
        component,
        volt::PhysicalPart{volt::ManufacturerPart{"Test", "C-16V"}, volt::PackageRef{"0603"},
                           volt::FootprintRef{"Capacitor_SMD", "C_0603"},
                           std::vector{volt::PinPadMapping{pin_def, "1"}}});
    circuit.set_selected_part_electrical_attribute(
        component,
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
    const auto pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::PinRole::Passive, volt::ConnectionRequirement::Required});
    const auto component_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Capacitor", std::vector{pin_def}});
    const auto component =
        circuit.instantiate_component(component_def, volt::ReferenceDesignator{"C1"});
    const auto pin = circuit.pin_by_number(component, "1").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VDD"}, volt::NetKind::Power});

    circuit.connect(net, pin);
    circuit.set_net_electrical_attribute(
        net,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage"}, volt::ElectricalAttributeOwner::Net,
            volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}});
    circuit.select_physical_part(
        component,
        volt::PhysicalPart{volt::ManufacturerPart{"Test", "C-16V"}, volt::PackageRef{"0603"},
                           volt::FootprintRef{"Capacitor_SMD", "C_0603"},
                           std::vector{volt::PinPadMapping{pin_def, "1"}}});
    circuit.set_selected_part_electrical_attribute(
        component,
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
    const auto power_input = circuit.add_pin_definition(volt::PinDefinition{
        "VCC", "1", volt::PinRole::PowerInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto power_source = circuit.add_pin_definition(volt::PinDefinition{
        "OUT", "1", volt::PinRole::PowerOutput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Output});
    const auto load_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Load", std::vector{power_input}});
    const auto regulator_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Regulator", std::vector{power_source}});
    const auto load = circuit.instantiate_component(load_def, volt::ReferenceDesignator{"U1"});
    const auto regulator =
        circuit.instantiate_component(regulator_def, volt::ReferenceDesignator{"U2"});
    const auto load_pin = circuit.pin_by_name(load, "VCC").value();
    const auto source_pin = circuit.pin_by_name(regulator, "OUT").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});

    circuit.set_pin_definition_electrical_attribute(
        power_input,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage_range"},
            volt::ElectricalAttributeOwner::PinSpec,
            volt::ElectricalAttributeKind::Constraint,
            volt::UnitDimension::Voltage,
        },
        volt::ElectricalAttributeValue{
            volt::QuantityRange::bounded(volt::Quantity{volt::UnitDimension::Voltage, 1.8},
                                         volt::Quantity{volt::UnitDimension::Voltage, 3.6})});
    circuit.connect(net, load_pin);
    circuit.connect(net, source_pin);
    circuit.set_net_electrical_attribute(
        net,
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
    const auto power_input = circuit.add_pin_definition(volt::PinDefinition{
        "VCC", "1", volt::PinRole::PowerInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto power_source = circuit.add_pin_definition(volt::PinDefinition{
        "OUT", "1", volt::PinRole::PowerOutput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Output});
    const auto load_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Load", std::vector{power_input}});
    const auto regulator_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Regulator", std::vector{power_source}});
    const auto load = circuit.instantiate_component(load_def, volt::ReferenceDesignator{"U1"});
    const auto regulator =
        circuit.instantiate_component(regulator_def, volt::ReferenceDesignator{"U2"});
    const auto load_pin = circuit.pin_by_name(load, "VCC").value();
    const auto source_pin = circuit.pin_by_name(regulator, "OUT").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});

    circuit.set_pin_definition_electrical_attribute(
        power_input,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage_range"},
            volt::ElectricalAttributeOwner::PinSpec,
            volt::ElectricalAttributeKind::Constraint,
            volt::UnitDimension::Voltage,
        },
        volt::ElectricalAttributeValue{
            volt::QuantityRange::bounded(volt::Quantity{volt::UnitDimension::Voltage, 1.8},
                                         volt::Quantity{volt::UnitDimension::Voltage, 3.6})});
    circuit.connect(net, load_pin);
    circuit.connect(net, source_pin);
    circuit.set_net_electrical_attribute(
        net,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage"}, volt::ElectricalAttributeOwner::Net,
            volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}});

    const auto report = volt::validate_circuit(circuit);

    CHECK(report.empty());
}

TEST_CASE("Circuit validation ignores pin voltage ranges without net voltage") {
    volt::Circuit circuit;
    const auto power_input = circuit.add_pin_definition(volt::PinDefinition{
        "VCC", "1", volt::PinRole::PowerInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto power_source = circuit.add_pin_definition(volt::PinDefinition{
        "OUT", "1", volt::PinRole::PowerOutput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Output});
    const auto load_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Load", std::vector{power_input}});
    const auto regulator_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Regulator", std::vector{power_source}});
    const auto load = circuit.instantiate_component(load_def, volt::ReferenceDesignator{"U1"});
    const auto regulator =
        circuit.instantiate_component(regulator_def, volt::ReferenceDesignator{"U2"});
    const auto load_pin = circuit.pin_by_name(load, "VCC").value();
    const auto source_pin = circuit.pin_by_name(regulator, "OUT").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});

    circuit.set_pin_definition_electrical_attribute(
        power_input,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage_range"},
            volt::ElectricalAttributeOwner::PinSpec,
            volt::ElectricalAttributeKind::Constraint,
            volt::UnitDimension::Voltage,
        },
        volt::ElectricalAttributeValue{
            volt::QuantityRange::bounded(volt::Quantity{volt::UnitDimension::Voltage, 1.8},
                                         volt::Quantity{volt::UnitDimension::Voltage, 3.6})});
    circuit.connect(net, load_pin);
    circuit.connect(net, source_pin);

    const auto report = volt::validate_circuit(circuit);

    CHECK(report.empty());
}

TEST_CASE("Circuit validation reports power inputs without typed supply sources") {
    volt::Circuit circuit;
    const auto power_input = circuit.add_pin_definition(volt::PinDefinition{
        "VCC", "1", volt::PinRole::PowerInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto passive_pin = circuit.add_pin_definition(volt::PinDefinition{
        "1", "1", volt::PinRole::Passive, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Passive, volt::ElectricalDirection::Passive});
    const auto load_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Load", std::vector{power_input}});
    const auto resistor_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{passive_pin}});
    const auto load = circuit.instantiate_component(load_def, volt::ReferenceDesignator{"U1"});
    const auto resistor =
        circuit.instantiate_component(resistor_def, volt::ReferenceDesignator{"R1"});
    const auto load_pin = circuit.pin_by_name(load, "VCC").value();
    const auto resistor_pin = circuit.pin_by_number(resistor, "1").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});

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
    const auto power_input = circuit.add_pin_definition(volt::PinDefinition{
        "VCC", "1", volt::PinRole::PowerInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto power_source = circuit.add_pin_definition(volt::PinDefinition{
        "OUT", "1", volt::PinRole::PowerOutput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Output});
    const auto load_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Load", std::vector{power_input}});
    const auto regulator_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Regulator", std::vector{power_source}});
    const auto load = circuit.instantiate_component(load_def, volt::ReferenceDesignator{"U1"});
    const auto regulator =
        circuit.instantiate_component(regulator_def, volt::ReferenceDesignator{"U2"});
    const auto load_pin = circuit.pin_by_name(load, "VCC").value();
    const auto source_pin = circuit.pin_by_name(regulator, "OUT").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});

    circuit.connect(net, load_pin);
    circuit.connect(net, source_pin);

    const auto report = volt::validate_circuit(circuit);

    CHECK(report.empty());
}

TEST_CASE("Circuit validation reports power and ground domain mismatches") {
    volt::Circuit circuit;
    const auto ground_pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "GND", "1", volt::PinRole::Ground, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Ground, volt::ElectricalDirection::Passive});
    const auto power_pin_def = circuit.add_pin_definition(volt::PinDefinition{
        "VCC", "1", volt::PinRole::PowerInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto ground_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Grounded", std::vector{ground_pin_def}});
    const auto power_def = circuit.add_component_definition(
        volt::ComponentDefinition{"Powered", std::vector{power_pin_def}});
    const auto grounded =
        circuit.instantiate_component(ground_def, volt::ReferenceDesignator{"U1"});
    const auto powered = circuit.instantiate_component(power_def, volt::ReferenceDesignator{"U2"});
    const auto ground_pin = circuit.pin_by_name(grounded, "GND").value();
    const auto power_pin = circuit.pin_by_name(powered, "VCC").value();
    const auto power_net = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});
    const auto ground_net = circuit.add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});

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
    const auto output_a = circuit.add_pin_definition(volt::PinDefinition{
        "OUT_A", "1", volt::PinRole::DigitalOutput, volt::ConnectionRequirement::Required});
    const auto output_b = circuit.add_pin_definition(volt::PinDefinition{
        "OUT_B", "1", volt::PinRole::PowerOutput, volt::ConnectionRequirement::Required});
    const auto driver_a = circuit.add_component_definition(
        volt::ComponentDefinition{"DriverA", std::vector{output_a}});
    const auto driver_b = circuit.add_component_definition(
        volt::ComponentDefinition{"DriverB", std::vector{output_b}});
    const auto component_a =
        circuit.instantiate_component(driver_a, volt::ReferenceDesignator{"U1"});
    const auto component_b =
        circuit.instantiate_component(driver_b, volt::ReferenceDesignator{"U2"});
    const auto pin_a = circuit.pin_by_name(component_a, "OUT_A").value();
    const auto pin_b = circuit.pin_by_name(component_b, "OUT_B").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"CONFLICT"}, volt::NetKind::Signal});

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
