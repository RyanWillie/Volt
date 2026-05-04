#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/circuit/instances.hpp>
#include <volt/circuit/nets.hpp>
#include <volt/circuit/validation.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/ids.hpp>

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
