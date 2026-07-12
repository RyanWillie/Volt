#include <catch2/catch_test_macros.hpp>

#include "support/circuit_test_helpers.hpp"
#include <algorithm>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/validation/validation.hpp>
#include <volt/core/diagnostics.hpp>

TEST_CASE("Circuit validation reports mixed input signal domains without a driver") {
    volt::Circuit circuit;
    const auto input_a = volt::PinSpec{"IN_A",
                                       "1",
                                       volt::ConnectionRequirement::Required,
                                       volt::ElectricalTerminalKind::Signal,
                                       volt::ElectricalDirection::Input,
                                       volt::ElectricalSignalDomain::Digital};
    const auto input_b = volt::PinSpec{"IN_B",
                                       "1",
                                       volt::ConnectionRequirement::Required,
                                       volt::ElectricalTerminalKind::Signal,
                                       volt::ElectricalDirection::Input,
                                       volt::ElectricalSignalDomain::Analog};
    const auto receiver_a = volt::test::define_component(circuit, "ReceiverA", {input_a});
    const auto receiver_b = volt::test::define_component(circuit, "ReceiverB", {input_b});
    const auto component_a = circuit.instantiate_component(
        receiver_a, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto component_b = circuit.instantiate_component(
        receiver_b, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U2"}});
    const auto pin_a = volt::queries::pin_by_name(circuit, component_a, "IN_A").value();
    const auto pin_b = volt::queries::pin_by_name(circuit, component_b, "IN_B").value();
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"SENSE"}, volt::NetKind::Signal});

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
    const auto input_a = volt::PinSpec{"IN_A",
                                       "1",
                                       volt::ConnectionRequirement::Required,
                                       volt::ElectricalTerminalKind::Signal,
                                       volt::ElectricalDirection::Input,
                                       volt::ElectricalSignalDomain::Digital};
    const auto input_b = volt::PinSpec{"IN_B",
                                       "1",
                                       volt::ConnectionRequirement::Required,
                                       volt::ElectricalTerminalKind::Signal,
                                       volt::ElectricalDirection::Input,
                                       volt::ElectricalSignalDomain::Analog};
    const auto power_output =
        volt::PinSpec{"PWR_OUT", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Output};
    const auto untyped_bidirectional = volt::PinSpec{
        "IO", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Signal,
        volt::ElectricalDirection::Bidirectional};
    const auto receiver_a = volt::test::define_component(circuit, "ReceiverA", {input_a});
    const auto receiver_b = volt::test::define_component(circuit, "ReceiverB", {input_b});
    const auto supply = volt::test::define_component(circuit, "Supply", {power_output});
    const auto header = volt::test::define_component(circuit, "Header", {untyped_bidirectional});
    const auto component_a = circuit.instantiate_component(
        receiver_a, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto component_b = circuit.instantiate_component(
        receiver_b, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U2"}});
    const auto supply_component = circuit.instantiate_component(
        supply, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U3"}});
    const auto header_component = circuit.instantiate_component(
        header, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"J1"}});
    const auto pin_a = volt::queries::pin_by_name(circuit, component_a, "IN_A").value();
    const auto pin_b = volt::queries::pin_by_name(circuit, component_b, "IN_B").value();
    const auto pwr_pin = volt::queries::pin_by_name(circuit, supply_component, "PWR_OUT").value();
    const auto io_pin = volt::queries::pin_by_name(circuit, header_component, "IO").value();
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"SENSE"}, volt::NetKind::Signal});

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

TEST_CASE("Circuit validation accepts signal inputs driven by outputs") {
    volt::Circuit circuit;
    const auto input = volt::PinSpec{"IN",
                                     "1",
                                     volt::ConnectionRequirement::Required,
                                     volt::ElectricalTerminalKind::Signal,
                                     volt::ElectricalDirection::Input,
                                     volt::ElectricalSignalDomain::Digital};
    const auto output = volt::PinSpec{"OUT",
                                      "1",
                                      volt::ConnectionRequirement::Required,
                                      volt::ElectricalTerminalKind::Signal,
                                      volt::ElectricalDirection::Output,
                                      volt::ElectricalSignalDomain::Digital};
    const auto receiver = volt::test::define_component(circuit, "Receiver", {input});
    const auto driver = volt::test::define_component(circuit, "Driver", {output});
    const auto receiver_component = circuit.instantiate_component(
        receiver, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto driver_component = circuit.instantiate_component(
        driver, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U2"}});
    const auto input_pin = volt::queries::pin_by_name(circuit, receiver_component, "IN").value();
    const auto output_pin = volt::queries::pin_by_name(circuit, driver_component, "OUT").value();
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"GPIO"}, volt::NetKind::Signal});

    circuit.connect(net, input_pin);
    circuit.connect(net, output_pin);

    CHECK(volt::validate_circuit(circuit).empty());
}

TEST_CASE("Circuit validation accepts same-domain input-only signal nets") {
    volt::Circuit circuit;
    const auto input_a = volt::PinSpec{"IN_A",
                                       "1",
                                       volt::ConnectionRequirement::Required,
                                       volt::ElectricalTerminalKind::Signal,
                                       volt::ElectricalDirection::Input,
                                       volt::ElectricalSignalDomain::Digital};
    const auto input_b = volt::PinSpec{"IN_B",
                                       "1",
                                       volt::ConnectionRequirement::Required,
                                       volt::ElectricalTerminalKind::Signal,
                                       volt::ElectricalDirection::Input,
                                       volt::ElectricalSignalDomain::Digital};
    const auto receiver_a = volt::test::define_component(circuit, "ReceiverA", {input_a});
    const auto receiver_b = volt::test::define_component(circuit, "ReceiverB", {input_b});
    const auto component_a = circuit.instantiate_component(
        receiver_a, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto component_b = circuit.instantiate_component(
        receiver_b, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U2"}});
    const auto pin_a = volt::queries::pin_by_name(circuit, component_a, "IN_A").value();
    const auto pin_b = volt::queries::pin_by_name(circuit, component_b, "IN_B").value();
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"RESET"}, volt::NetKind::Signal});

    circuit.connect(net, pin_a);
    circuit.connect(net, pin_b);

    CHECK(volt::validate_circuit(circuit).empty());
}
