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

    REQUIRE(circuit.net_count() == 1);
    CHECK(circuit.net(volt::NetId{0}).pins().size() == 1);
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
    auto circuit = volt::Circuit{};
    const auto a1 = circuit.connectivity().add_pin_definition(volt::PinDefinition{"A1", "1"});
    const auto b1 = circuit.connectivity().add_pin_definition(volt::PinDefinition{"B1", "1"});
    const auto a2 = circuit.connectivity().add_pin_definition(volt::PinDefinition{"A2", "2"});
    const auto b2 = circuit.connectivity().add_pin_definition(volt::PinDefinition{"B2", "2"});
    const auto first_definition = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"First", std::vector{a1, a2}});
    const auto second_definition = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Second", std::vector{b1, b2}});
    const auto transitional_orphan =
        circuit.connectivity().add_pin_definition(volt::PinDefinition{"Unowned", "3"});
    circuit.electrical().set_pin_definition_electrical_attribute(
        transitional_orphan,
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage_range"}, volt::ElectricalAttributeOwner::PinSpec,
            volt::ElectricalAttributeKind::Constraint, volt::UnitDimension::Voltage},
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 5.0}});
    static_cast<void>(circuit.connectivity().add_component(
        volt::ComponentInstance{second_definition, volt::ReferenceDesignator{"U1"}}));
    static_cast<void>(circuit.connectivity().add_component(
        volt::ComponentInstance{first_definition, volt::ReferenceDesignator{"U2"}}));

    const auto serialized = volt::io::write_logical_circuit(circuit);
    const auto restored = volt::io::read_logical_circuit_text(serialized);

    CHECK(restored.component_definition(volt::ComponentDefId{0}).pins() ==
          std::vector{volt::PinDefId{0}, volt::PinDefId{2}});
    CHECK(restored.component_definition(volt::ComponentDefId{1}).pins() ==
          std::vector{volt::PinDefId{1}, volt::PinDefId{3}});
    CHECK(transitional_orphan == volt::PinDefId{4});
    CHECK(restored.pin_definition(volt::PinDefId{4}).name() == "Unowned");
    CHECK(restored.pin_definition_electrical_attributes(volt::PinDefId{4})
              .get(volt::ElectricalAttributeName{"voltage_range"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 5.0});
    CHECK(volt::io::write_logical_circuit(restored) == serialized);
}

TEST_CASE("Logical reader preserves interleaved global hierarchy table identity") {
    auto circuit = volt::Circuit{};
    const auto pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive});
    const auto component_definition = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"One pin", std::vector{pin}});
    const auto first_module = circuit.hierarchy().add_module_definition(
        volt::ModuleDefinition{volt::ModuleName{"First"}});
    const auto second_module = circuit.hierarchy().add_module_definition(
        volt::ModuleDefinition{volt::ModuleName{"Second"}});

    const auto first_net = circuit.hierarchy().add_template_net(
        first_module, volt::TemplateNetDefinition{volt::NetName{"A"}, volt::NetKind::Signal});
    const auto second_module_net = circuit.hierarchy().add_template_net(
        second_module, volt::TemplateNetDefinition{volt::NetName{"B"}, volt::NetKind::Signal});
    const auto second_net = circuit.hierarchy().add_template_net(
        first_module, volt::TemplateNetDefinition{volt::NetName{"C"}, volt::NetKind::Signal});

    const auto first_component = circuit.hierarchy().add_module_component(
        first_module,
        volt::ModuleComponentTemplate{component_definition, volt::ReferenceDesignator{"R1"}});
    const auto second_module_component = circuit.hierarchy().add_module_component(
        second_module,
        volt::ModuleComponentTemplate{component_definition, volt::ReferenceDesignator{"R1"}});
    const auto second_component = circuit.hierarchy().add_module_component(
        first_module,
        volt::ModuleComponentTemplate{component_definition, volt::ReferenceDesignator{"R2"}});

    REQUIRE(circuit.hierarchy().connect_module_pin(first_module, first_net, first_component, pin));
    REQUIRE(circuit.hierarchy().connect_module_pin(second_module, second_module_net,
                                                   second_module_component, pin));
    REQUIRE(
        circuit.hierarchy().connect_module_pin(first_module, second_net, second_component, pin));

    const auto first_port = circuit.hierarchy().add_port_definition(
        first_module,
        volt::PortDefinition{volt::PortName{"A"}, first_net, volt::PortRole::Input, true});
    const auto second_module_port = circuit.hierarchy().add_port_definition(
        second_module,
        volt::PortDefinition{volt::PortName{"B"}, second_module_net, volt::PortRole::Input, true});
    const auto second_port = circuit.hierarchy().add_port_definition(
        first_module,
        volt::PortDefinition{volt::PortName{"C"}, second_net, volt::PortRole::Output, true});

    CHECK(first_net == volt::TemplateNetDefId{0});
    CHECK(second_module_net == volt::TemplateNetDefId{1});
    CHECK(second_net == volt::TemplateNetDefId{2});
    CHECK(first_component == volt::ModuleComponentId{0});
    CHECK(second_module_component == volt::ModuleComponentId{1});
    CHECK(second_component == volt::ModuleComponentId{2});
    CHECK(first_port == volt::PortDefId{0});
    CHECK(second_module_port == volt::PortDefId{1});
    CHECK(second_port == volt::PortDefId{2});

    const auto serialized = volt::io::write_logical_circuit(circuit);
    const auto restored = volt::io::read_logical_circuit_text(serialized);

    CHECK(volt::io::write_logical_circuit(restored) == serialized);
}
