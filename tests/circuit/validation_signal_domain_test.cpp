#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/circuit/queries.hpp>
#include <volt/circuit/validation.hpp>
#include <volt/core/diagnostics.hpp>

TEST_CASE("Circuit validation reports mixed input signal domains without a driver") {
    volt::Circuit circuit;
    const auto input_a = circuit.add_pin_definition(volt::PinDefinition{
        "IN_A", "1", volt::PinRole::DigitalInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Signal, volt::ElectricalDirection::Input,
        volt::ElectricalSignalDomain::Digital});
    const auto input_b = circuit.add_pin_definition(volt::PinDefinition{
        "IN_B", "1", volt::PinRole::AnalogInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Signal, volt::ElectricalDirection::Input,
        volt::ElectricalSignalDomain::Analog});
    const auto receiver_a =
        circuit.add_component_definition(volt::ComponentDefinition{"ReceiverA", {input_a}});
    const auto receiver_b =
        circuit.add_component_definition(volt::ComponentDefinition{"ReceiverB", {input_b}});
    const auto component_a =
        circuit.instantiate_component(receiver_a, volt::ReferenceDesignator{"U1"});
    const auto component_b =
        circuit.instantiate_component(receiver_b, volt::ReferenceDesignator{"U2"});
    const auto pin_a = volt::queries::pin_by_name(circuit, component_a, "IN_A").value();
    const auto pin_b = volt::queries::pin_by_name(circuit, component_b, "IN_B").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"SENSE"}, volt::NetKind::Signal});

    circuit.connect(net, pin_a);
    circuit.connect(net, pin_b);

    const auto report = volt::validate_circuit(circuit);

    REQUIRE(report.count() == 1);
    const auto &diagnostic = report.diagnostics().front();
    CHECK(diagnostic.severity() == volt::Severity::Error);
    CHECK(diagnostic.code() == volt::DiagnosticCode{"INPUT_SIGNAL_DOMAIN_MISMATCH"});
    CHECK(diagnostic.category() == volt::DiagnosticCategory{volt::diagnostic_categories::Erc});
    REQUIRE(diagnostic.entities().size() == 3);
    CHECK(diagnostic.entities()[0] == volt::EntityRef::net(net));
    CHECK(diagnostic.entities()[1] == volt::EntityRef::pin(pin_a));
    CHECK(diagnostic.entities()[2] == volt::EntityRef::pin(pin_b));
}

TEST_CASE("Circuit validation ignores non-signal and untyped pins as signal-domain drivers") {
    volt::Circuit circuit;
    const auto input_a = circuit.add_pin_definition(volt::PinDefinition{
        "IN_A", "1", volt::PinRole::DigitalInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Signal, volt::ElectricalDirection::Input,
        volt::ElectricalSignalDomain::Digital});
    const auto input_b = circuit.add_pin_definition(volt::PinDefinition{
        "IN_B", "1", volt::PinRole::AnalogInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Signal, volt::ElectricalDirection::Input,
        volt::ElectricalSignalDomain::Analog});
    const auto power_output = circuit.add_pin_definition(volt::PinDefinition{
        "PWR_OUT", "1", volt::PinRole::PowerOutput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Output});
    const auto untyped_bidirectional = circuit.add_pin_definition(volt::PinDefinition{
        "IO", "1", volt::PinRole::Bidirectional, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Unspecified, volt::ElectricalDirection::Bidirectional});
    const auto receiver_a =
        circuit.add_component_definition(volt::ComponentDefinition{"ReceiverA", {input_a}});
    const auto receiver_b =
        circuit.add_component_definition(volt::ComponentDefinition{"ReceiverB", {input_b}});
    const auto supply =
        circuit.add_component_definition(volt::ComponentDefinition{"Supply", {power_output}});
    const auto header = circuit.add_component_definition(
        volt::ComponentDefinition{"Header", {untyped_bidirectional}});
    const auto component_a =
        circuit.instantiate_component(receiver_a, volt::ReferenceDesignator{"U1"});
    const auto component_b =
        circuit.instantiate_component(receiver_b, volt::ReferenceDesignator{"U2"});
    const auto supply_component =
        circuit.instantiate_component(supply, volt::ReferenceDesignator{"U3"});
    const auto header_component =
        circuit.instantiate_component(header, volt::ReferenceDesignator{"J1"});
    const auto pin_a = volt::queries::pin_by_name(circuit, component_a, "IN_A").value();
    const auto pin_b = volt::queries::pin_by_name(circuit, component_b, "IN_B").value();
    const auto pwr_pin = volt::queries::pin_by_name(circuit, supply_component, "PWR_OUT").value();
    const auto io_pin = volt::queries::pin_by_name(circuit, header_component, "IO").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"SENSE"}, volt::NetKind::Signal});

    circuit.connect(net, pin_a);
    circuit.connect(net, pin_b);
    circuit.connect(net, pwr_pin);
    circuit.connect(net, io_pin);

    const auto report = volt::validate_circuit(circuit);

    REQUIRE(report.count() == 1);
    const auto &diagnostic = report.diagnostics().front();
    CHECK(diagnostic.code() == volt::DiagnosticCode{"INPUT_SIGNAL_DOMAIN_MISMATCH"});
    CHECK(diagnostic.entities() == std::vector{volt::EntityRef::net(net),
                                               volt::EntityRef::pin(pin_a),
                                               volt::EntityRef::pin(pin_b)});
}

TEST_CASE("Circuit validation follows canonical signal metadata when pin roles disagree") {
    {
        volt::Circuit circuit;
        const auto input_a = circuit.add_pin_definition(volt::PinDefinition{
            "IN_A", "1", volt::PinRole::DigitalInput, volt::ConnectionRequirement::Required,
            volt::ElectricalTerminalKind::Signal, volt::ElectricalDirection::Input,
            volt::ElectricalSignalDomain::Digital});
        const auto input_b = circuit.add_pin_definition(volt::PinDefinition{
            "IN_B", "1", volt::PinRole::AnalogInput, volt::ConnectionRequirement::Required,
            volt::ElectricalTerminalKind::Signal, volt::ElectricalDirection::Input,
            volt::ElectricalSignalDomain::Analog});
        const auto mismatched_output = circuit.add_pin_definition(volt::PinDefinition{
            "MISROLE", "1", volt::PinRole::DigitalOutput, volt::ConnectionRequirement::Required,
            volt::ElectricalTerminalKind::Signal, volt::ElectricalDirection::Input,
            volt::ElectricalSignalDomain::Digital});
        const auto receiver_a =
            circuit.add_component_definition(volt::ComponentDefinition{"ReceiverA", {input_a}});
        const auto receiver_b =
            circuit.add_component_definition(volt::ComponentDefinition{"ReceiverB", {input_b}});
        const auto misrole = circuit.add_component_definition(
            volt::ComponentDefinition{"Misrole", {mismatched_output}});
        const auto component_a =
            circuit.instantiate_component(receiver_a, volt::ReferenceDesignator{"U1"});
        const auto component_b =
            circuit.instantiate_component(receiver_b, volt::ReferenceDesignator{"U2"});
        const auto misrole_component =
            circuit.instantiate_component(misrole, volt::ReferenceDesignator{"U3"});
        const auto pin_a = volt::queries::pin_by_name(circuit, component_a, "IN_A").value();
        const auto pin_b = volt::queries::pin_by_name(circuit, component_b, "IN_B").value();
        const auto misrole_pin =
            volt::queries::pin_by_name(circuit, misrole_component, "MISROLE").value();
        const auto net = circuit.add_net(volt::Net{volt::NetName{"SENSE"}, volt::NetKind::Signal});

        circuit.connect(net, pin_a);
        circuit.connect(net, pin_b);
        circuit.connect(net, misrole_pin);

        const auto report = volt::validate_circuit(circuit);

        REQUIRE(report.count() == 1);
        CHECK(report.diagnostics().front().code() ==
              volt::DiagnosticCode{"INPUT_SIGNAL_DOMAIN_MISMATCH"});
    }

    {
        volt::Circuit circuit;
        const auto digital_input = circuit.add_pin_definition(volt::PinDefinition{
            "DIN", "1", volt::PinRole::DigitalInput, volt::ConnectionRequirement::Required,
            volt::ElectricalTerminalKind::Signal, volt::ElectricalDirection::Input,
            volt::ElectricalSignalDomain::Digital});
        const auto mismatched_input = circuit.add_pin_definition(volt::PinDefinition{
            "MISROLE", "1", volt::PinRole::AnalogInput, volt::ConnectionRequirement::Required,
            volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input,
            volt::ElectricalSignalDomain::Analog});
        const auto receiver = circuit.add_component_definition(
            volt::ComponentDefinition{"Receiver", {digital_input}});
        const auto misrole = circuit.add_component_definition(
            volt::ComponentDefinition{"Misrole", {mismatched_input}});
        const auto receiver_component =
            circuit.instantiate_component(receiver, volt::ReferenceDesignator{"U1"});
        const auto misrole_component =
            circuit.instantiate_component(misrole, volt::ReferenceDesignator{"U2"});
        const auto input_pin =
            volt::queries::pin_by_name(circuit, receiver_component, "DIN").value();
        const auto misrole_pin =
            volt::queries::pin_by_name(circuit, misrole_component, "MISROLE").value();
        const auto net = circuit.add_net(volt::Net{volt::NetName{"SENSE"}, volt::NetKind::Signal});

        circuit.connect(net, input_pin);
        circuit.connect(net, misrole_pin);

        const auto report = volt::validate_circuit(circuit);

        CHECK(std::none_of(report.diagnostics().begin(), report.diagnostics().end(),
                           [](const volt::Diagnostic &diagnostic) {
                               return diagnostic.code() ==
                                      volt::DiagnosticCode{"INPUT_SIGNAL_DOMAIN_MISMATCH"};
                           }));
    }
}

TEST_CASE("Circuit validation accepts signal inputs driven by outputs") {
    volt::Circuit circuit;
    const auto input = circuit.add_pin_definition(volt::PinDefinition{
        "IN", "1", volt::PinRole::DigitalInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Signal, volt::ElectricalDirection::Input,
        volt::ElectricalSignalDomain::Digital});
    const auto output = circuit.add_pin_definition(volt::PinDefinition{
        "OUT", "1", volt::PinRole::DigitalOutput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Signal, volt::ElectricalDirection::Output,
        volt::ElectricalSignalDomain::Digital});
    const auto receiver =
        circuit.add_component_definition(volt::ComponentDefinition{"Receiver", {input}});
    const auto driver =
        circuit.add_component_definition(volt::ComponentDefinition{"Driver", {output}});
    const auto receiver_component =
        circuit.instantiate_component(receiver, volt::ReferenceDesignator{"U1"});
    const auto driver_component =
        circuit.instantiate_component(driver, volt::ReferenceDesignator{"U2"});
    const auto input_pin = volt::queries::pin_by_name(circuit, receiver_component, "IN").value();
    const auto output_pin = volt::queries::pin_by_name(circuit, driver_component, "OUT").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"GPIO"}, volt::NetKind::Signal});

    circuit.connect(net, input_pin);
    circuit.connect(net, output_pin);

    CHECK(volt::validate_circuit(circuit).empty());
}

TEST_CASE("Circuit validation accepts same-domain input-only signal nets") {
    volt::Circuit circuit;
    const auto input_a = circuit.add_pin_definition(volt::PinDefinition{
        "IN_A", "1", volt::PinRole::DigitalInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Signal, volt::ElectricalDirection::Input,
        volt::ElectricalSignalDomain::Digital});
    const auto input_b = circuit.add_pin_definition(volt::PinDefinition{
        "IN_B", "1", volt::PinRole::DigitalInput, volt::ConnectionRequirement::Required,
        volt::ElectricalTerminalKind::Signal, volt::ElectricalDirection::Input,
        volt::ElectricalSignalDomain::Digital});
    const auto receiver_a =
        circuit.add_component_definition(volt::ComponentDefinition{"ReceiverA", {input_a}});
    const auto receiver_b =
        circuit.add_component_definition(volt::ComponentDefinition{"ReceiverB", {input_b}});
    const auto component_a =
        circuit.instantiate_component(receiver_a, volt::ReferenceDesignator{"U1"});
    const auto component_b =
        circuit.instantiate_component(receiver_b, volt::ReferenceDesignator{"U2"});
    const auto pin_a = volt::queries::pin_by_name(circuit, component_a, "IN_A").value();
    const auto pin_b = volt::queries::pin_by_name(circuit, component_b, "IN_B").value();
    const auto net = circuit.add_net(volt::Net{volt::NetName{"RESET"}, volt::NetKind::Signal});

    circuit.connect(net, pin_a);
    circuit.connect(net, pin_b);

    CHECK(volt::validate_circuit(circuit).empty());
}
