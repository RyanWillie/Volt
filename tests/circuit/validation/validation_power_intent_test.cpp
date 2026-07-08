#include <catch2/catch_test_macros.hpp>

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
    circuit.electrical().set_net_electrical_attribute(
        net,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage"}, volt::ElectricalAttributeOwner::Net,
            volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, voltage}});
}

struct SwitchedSupplyFixture {
    volt::Circuit circuit;
    volt::NetId switched_rail;
    volt::PinId load_power_pin;
};

SwitchedSupplyFixture make_switched_supply_fixture() {
    volt::Circuit circuit;
    const auto battery_positive = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "+", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Power,
        volt::ElectricalDirection::Output});
    const auto switch_a = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto switch_c = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "C", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto vcc = circuit.connectivity().add_pin_definition(
        volt::PinDefinition{"VCC", "1", volt::ConnectionRequirement::Required,
                            volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto battery_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"CR2032", std::vector{battery_positive}});
    const auto switch_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"SlideSwitch", std::vector{switch_a, switch_c}});
    const auto load_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Load", std::vector{vcc}});
    const auto battery =
        circuit.instantiate_component(battery_def, volt::ReferenceDesignator{"BT1"});
    const auto slide_switch =
        circuit.instantiate_component(switch_def, volt::ReferenceDesignator{"SW1"});
    const auto load = circuit.instantiate_component(load_def, volt::ReferenceDesignator{"U1"});
    const auto vbat =
        circuit.connectivity().add_net(volt::Net{volt::NetName{"VBAT"}, volt::NetKind::Power});
    const auto three_volt =
        circuit.connectivity().add_net(volt::Net{volt::NetName{"+3V"}, volt::NetKind::Power});

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
    const auto power_input = circuit.connectivity().add_pin_definition(
        volt::PinDefinition{"VCC", "1", volt::ConnectionRequirement::Required,
                            volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto passive_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto load_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Load", std::vector{power_input}});
    const auto tap_def = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"TestPoint", std::vector{passive_pin}});
    const auto module = circuit.hierarchy().add_module_definition(volt::ModuleDefinition{
        volt::ModuleName{"LoadBlock"},
    });
    const auto template_net = circuit.hierarchy().add_template_net(
        module, volt::TemplateNetDefinition{volt::NetName{"VCC"}, volt::NetKind::Power});
    const auto port = circuit.hierarchy().add_port_definition(
        module,
        volt::PortDefinition{volt::PortName{"VCC"}, template_net, volt::PortRole::PowerInput});
    const auto module_component = circuit.hierarchy().add_module_component(
        module, volt::ModuleComponentTemplate{load_def, volt::ReferenceDesignator{"U1"}});
    CHECK(circuit.hierarchy().connect_module_pin(module, template_net, module_component,
                                                 power_input));
    const auto instance =
        circuit.instantiate_root_module(module, volt::ModuleInstanceName{"LOAD_A"});
    const auto tap = circuit.instantiate_component(tap_def, volt::ReferenceDesignator{"TP1"});
    const auto parent_net =
        circuit.connectivity().add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});

    circuit.connect(parent_net, volt::queries::pin_by_name(circuit, tap, "1").value());
    [[maybe_unused]] const auto binding = circuit.bind_port(instance, port, parent_net);
    set_net_voltage(circuit, parent_net, 3.3);

    const auto report = volt::validate_circuit(circuit);

    CHECK(report.empty());
}
