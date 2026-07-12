#include <catch2/catch_test_macros.hpp>

#include "support/circuit_test_helpers.hpp"
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/circuit/connectivity/nets.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/validation/validation.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/electrical_attributes.hpp>

namespace {

void set_net_voltage(volt::Circuit &circuit, volt::NetId net, double voltage) {
    circuit.update(
        net,
        volt::SetNetElectricalAttribute{
            volt::ElectricalAttributeSpec{
                volt::ElectricalAttributeName{"voltage"}, volt::ElectricalAttributeOwner::Net,
                volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Voltage},
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, voltage}}});
}

struct SwitchedSupplyFixture {
    volt::Circuit circuit;
    volt::NetId switched_rail;
    volt::PinId load_power_pin;
};

SwitchedSupplyFixture make_switched_supply_fixture() {
    volt::Circuit circuit;
    const auto battery_positive_spec =
        volt::PinSpec{"+", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Output};
    const auto switch_a_spec = volt::PinSpec{"A",
                                             "1",
                                             volt::ConnectionRequirement::Required,
                                             volt::ElectricalTerminalKind::Passive,
                                             volt::ElectricalDirection::Passive,
                                             volt::ElectricalSignalDomain::Unspecified,
                                             volt::ElectricalDriveKind::Passive};
    const auto switch_c_spec = volt::PinSpec{"C",
                                             "2",
                                             volt::ConnectionRequirement::Required,
                                             volt::ElectricalTerminalKind::Passive,
                                             volt::ElectricalDirection::Passive,
                                             volt::ElectricalSignalDomain::Unspecified,
                                             volt::ElectricalDriveKind::Passive};
    const auto vcc_spec =
        volt::PinSpec{"VCC", "1", volt::ConnectionRequirement::Required,
                      volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input};
    const auto battery_def =
        volt::test::define_component(circuit, "CR2032", std::vector{battery_positive_spec});
    const auto switch_def = volt::test::define_component(circuit, "SlideSwitch",
                                                         std::vector{switch_a_spec, switch_c_spec});
    const auto load_def = volt::test::define_component(circuit, "Load", std::vector{vcc_spec});
    const auto battery = circuit.instantiate_component(
        battery_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"BT1"}});
    const auto slide_switch = circuit.instantiate_component(
        switch_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"SW1"}});
    const auto load = circuit.instantiate_component(
        load_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto vbat = circuit.add_net(volt::NetSpec{volt::NetName{"VBAT"}, volt::NetKind::Power});
    const auto three_volt =
        circuit.add_net(volt::NetSpec{volt::NetName{"+3V"}, volt::NetKind::Power});

    circuit.connect(vbat, volt::queries::pin_by_name(circuit, battery, "+").value());
    circuit.connect(vbat, volt::queries::pin_by_name(circuit, slide_switch, "A").value());
    circuit.connect(three_volt, volt::queries::pin_by_name(circuit, slide_switch, "C").value());
    const auto load_power_pin = volt::queries::pin_by_name(circuit, load, "VCC").value();
    circuit.connect(three_volt, load_power_pin);

    return SwitchedSupplyFixture{std::move(circuit), three_volt, load_power_pin};
}

} // namespace

TEST_CASE("Circuit validation accepts authored power intent on a switched supply rail") {
    auto fixture = make_switched_supply_fixture();
    set_net_voltage(fixture.circuit, fixture.switched_rail, 3.0);

    const auto report = volt::validate_circuit(fixture.circuit);

    CHECK(report.empty());
}

TEST_CASE("Circuit validation reports passive-gated supply rails without authored power intent") {
    const auto fixture = make_switched_supply_fixture();

    const auto report = volt::validate_circuit(fixture.circuit);

    REQUIRE(report.count() == 1);
    const auto &diagnostic = report.diagnostics().front();
    CHECK(diagnostic.severity() == volt::Severity::Error);
    CHECK(diagnostic.code() == volt::DiagnosticCode{"POWER_INPUT_WITHOUT_SOURCE"});
    REQUIRE(diagnostic.entities().size() == 2);
    CHECK(diagnostic.entities()[0] == volt::EntityRef::net(fixture.switched_rail));
    CHECK(diagnostic.entities()[1] == volt::EntityRef::pin(fixture.load_power_pin));
}

TEST_CASE("Circuit validation applies authored power intent across bound module ports") {
    volt::Circuit circuit;
    const auto power_input_spec =
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
    const auto &load_def_pins = circuit.get(load_def).pins();
    const auto power_input = load_def_pins[0];
    const auto tap_def =
        volt::test::define_component(circuit, "TestPoint", std::vector{passive_pin_spec});
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
    const auto tap = circuit.instantiate_component(
        tap_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"TP1"}});
    const auto parent_net =
        circuit.add_net(volt::NetSpec{volt::NetName{"VCC"}, volt::NetKind::Power});

    circuit.connect(parent_net, volt::queries::pin_by_name(circuit, tap, "1").value());
    [[maybe_unused]] const auto binding = circuit.bind_port(instance, port, parent_net);
    set_net_voltage(circuit, parent_net, 3.3);

    const auto report = volt::validate_circuit(circuit);

    CHECK(report.empty());
}
