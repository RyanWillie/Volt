#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/errors.hpp>

#include <support/circuit_test_helpers.hpp>

TEST_CASE("Circuit stores typed electrical attributes by owner kind") {
    volt::Circuit circuit;
    const auto component_resistance = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"resistance"},
        volt::ElectricalAttributeOwner::ComponentInstance,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Resistance,
    };
    const auto pin_voltage_range = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_range"},
        volt::ElectricalAttributeOwner::PinSpec,
        volt::ElectricalAttributeKind::Constraint,
        volt::UnitDimension::Voltage,
    };
    const auto net_voltage = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage"},
        volt::ElectricalAttributeOwner::Net,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };

    auto first_pin_spec = volt::PinSpec{
        .name = "1",
        .number = "1",
        .terminal_kind = volt::ElectricalTerminalKind::Passive,
        .direction = volt::ElectricalDirection::Passive,
        .drive_kind = volt::ElectricalDriveKind::Passive,
    };
    first_pin_spec.electrical_attributes.push_back(volt::ElectricalAttributeAssignment{
        pin_voltage_range,
        volt::ElectricalAttributeValue{
            volt::QuantityRange::bounded(volt::Quantity{volt::UnitDimension::Voltage, 1.8},
                                         volt::Quantity{volt::UnitDimension::Voltage, 3.6})},
    });
    const auto component_definition = circuit.define_component(volt::ComponentSpec{
        .name = "Resistor",
        .pins =
            {
                std::move(first_pin_spec),
                volt::PinSpec{.name = "2",
                              .number = "2",
                              .terminal_kind = volt::ElectricalTerminalKind::Passive,
                              .direction = volt::ElectricalDirection::Passive,
                              .drive_kind = volt::ElectricalDriveKind::Passive},
            },
    });
    const auto first_pin = circuit.get(component_definition).pins()[0];
    const auto component = circuit.instantiate_component(
        component_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    const auto net =
        circuit.add_net(volt::NetSpec{.name = volt::NetName{"VCC"}, .kind = volt::NetKind::Power});
    circuit.update(component,
                   volt::SetComponentElectricalAttribute{
                       component_resistance, volt::ElectricalAttributeValue{volt::Quantity{
                                                 volt::UnitDimension::Resistance, 330.0}}});
    circuit.update(net, volt::SetNetElectricalAttribute{
                            net_voltage, volt::ElectricalAttributeValue{
                                             volt::Quantity{volt::UnitDimension::Voltage, 3.3}}});

    CHECK(volt::queries::component_electrical_attributes(circuit, component)
              .get(volt::ElectricalAttributeName{"resistance"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Resistance, 330.0});
    CHECK(volt::queries::pin_definition_electrical_attributes(circuit, first_pin)
              .get(volt::ElectricalAttributeName{"voltage_range"})
              .as_range()
              .dimension() == volt::UnitDimension::Voltage);
    CHECK(volt::queries::net_electrical_attributes(circuit, net)
              .get(volt::ElectricalAttributeName{"voltage"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 3.3});
}

TEST_CASE("Circuit rejects electrical attributes for the wrong owner kind") {
    volt::Circuit circuit;
    const auto component_definition =
        volt::test::define_component(circuit, "Resistor", {volt::test::passive_pin("1", "1")});
    const auto component = circuit.instantiate_component(
        component_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    const auto net_voltage = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage"},
        volt::ElectricalAttributeOwner::Net,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };

    CHECK_THROWS_AS(circuit.update(component,
                                   volt::SetComponentElectricalAttribute{
                                       net_voltage, volt::ElectricalAttributeValue{volt::Quantity{
                                                        volt::UnitDimension::Voltage, 3.3}}}),
                    std::logic_error);

    try {
        circuit.update(component, volt::SetComponentElectricalAttribute{
                                      net_voltage, volt::ElectricalAttributeValue{volt::Quantity{
                                                       volt::UnitDimension::Voltage, 3.3}}});
        FAIL("Wrong electrical attribute owner must throw");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::InvalidArgument);
        CHECK(std::string{error.what()} == "Electrical attribute spec owner is not valid here");
    }
}
