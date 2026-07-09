#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/electrical/electrical_model.hpp>
#include <volt/core/errors.hpp>

namespace {

template <typename Model>
concept CanSetComponentAttribute =
    requires(Model model, volt::ComponentId component, const volt::ElectricalAttributeSpec &spec,
             volt::ElectricalAttributeValue value) {
        model.set_component_attribute(component, spec, value);
    };

template <typename Model>
concept CanSetPinDefinitionAttribute =
    requires(Model model, volt::PinDefId pin_definition, const volt::ElectricalAttributeSpec &spec,
             volt::ElectricalAttributeValue value) {
        model.set_pin_definition_attribute(pin_definition, spec, value);
    };

template <typename Model>
concept CanSetNetAttribute =
    requires(Model model, volt::NetId net, const volt::ElectricalAttributeSpec &spec,
             volt::ElectricalAttributeValue value) { model.set_net_attribute(net, spec, value); };

template <typename Model>
concept CanSelectPhysicalPart =
    requires(Model model, volt::ComponentId component, volt::PhysicalPart part,
             std::vector<volt::PinDefId> pins) {
        model.select_physical_part(component, std::move(part), pins);
    };

template <typename Model>
concept CanSetSelectedPartAttribute =
    requires(Model model, volt::ComponentId component, const volt::ElectricalAttributeSpec &spec,
             volt::ElectricalAttributeValue value) {
        model.set_selected_part_attribute(component, spec, value);
    };

static_assert(!CanSetComponentAttribute<volt::ElectricalModel>);
static_assert(!CanSetPinDefinitionAttribute<volt::ElectricalModel>);
static_assert(!CanSetNetAttribute<volt::ElectricalModel>);
static_assert(!CanSelectPhysicalPart<volt::ElectricalModel>);
static_assert(!CanSetSelectedPartAttribute<volt::ElectricalModel>);

volt::PhysicalPart make_resistor_physical_part(volt::PinDefId first_pin,
                                               volt::PinDefId second_pin) {
    return volt::PhysicalPart{
        volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
        volt::PackageRef{"0603"},
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{
            volt::PinPadMapping{first_pin, "1"},
            volt::PinPadMapping{second_pin, "2"},
        },
    };
}

} // namespace

TEST_CASE("Circuit stores typed electrical attributes by owner kind") {
    volt::Circuit circuit;
    const auto first_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_definition = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"R1"});
    const auto net =
        circuit.connectivity().add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});
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

    circuit.electrical().set_component_electrical_attribute(
        component, component_resistance,
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Resistance, 330.0}});
    circuit.electrical().set_pin_definition_electrical_attribute(
        first_pin, pin_voltage_range,
        volt::ElectricalAttributeValue{
            volt::QuantityRange::bounded(volt::Quantity{volt::UnitDimension::Voltage, 1.8},
                                         volt::Quantity{volt::UnitDimension::Voltage, 3.6})});
    circuit.electrical().set_net_electrical_attribute(
        net, net_voltage,
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}});

    CHECK(circuit.component_electrical_attributes(component)
              .get(volt::ElectricalAttributeName{"resistance"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Resistance, 330.0});
    CHECK(circuit.pin_definition_electrical_attributes(first_pin)
              .get(volt::ElectricalAttributeName{"voltage_range"})
              .as_range()
              .dimension() == volt::UnitDimension::Voltage);
    CHECK(circuit.net_electrical_attributes(net)
              .get(volt::ElectricalAttributeName{"voltage"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 3.3});
}

TEST_CASE("Circuit rejects electrical attributes for the wrong owner kind") {
    volt::Circuit circuit;
    const auto pin_definition = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_definition = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{pin_definition}});
    const auto component =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"R1"});
    const auto net_voltage = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage"},
        volt::ElectricalAttributeOwner::Net,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };

    CHECK_THROWS_AS(
        circuit.electrical().set_component_electrical_attribute(
            component, net_voltage,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}}),
        std::logic_error);

    try {
        circuit.electrical().set_component_electrical_attribute(
            component, net_voltage,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}});
        FAIL("Wrong electrical attribute owner must throw");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::InvalidArgument);
        CHECK(std::string{error.what()} == "Electrical attribute spec owner is not valid here");
    }
}

TEST_CASE("Circuit owns selected physical parts and selected-part attributes") {
    volt::Circuit circuit;
    const auto first_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_definition = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"R1"});
    const auto voltage_rating = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_rating"},
        volt::ElectricalAttributeOwner::SelectedPart,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };

    circuit.electrical().select_physical_part(component,
                                              make_resistor_physical_part(first_pin, second_pin));
    circuit.electrical().set_selected_part_electrical_attribute(
        component, voltage_rating,
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 75.0}});

    REQUIRE(circuit.selected_physical_part(component).has_value());
    CHECK(circuit.selected_physical_part(component)
              ->electrical_attributes()
              .get(volt::ElectricalAttributeName{"voltage_rating"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 75.0});
}

TEST_CASE("Circuit rejects selected parts that do not match component pins") {
    volt::Circuit circuit;
    const auto first_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "2", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto extra_pin = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "3", "3", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_definition = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"R1"});

    CHECK_THROWS_AS(circuit.electrical().select_physical_part(
                        component, make_resistor_physical_part(first_pin, extra_pin)),
                    std::logic_error);

    try {
        circuit.electrical().select_physical_part(
            component, make_resistor_physical_part(first_pin, extra_pin));
        FAIL("Physical part pin mappings outside the component definition must throw");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::CrossReferenceViolation);
        CHECK(std::string{error.what()} ==
              "Physical part maps a pin outside the component definition");
    }

    const auto incomplete_part = volt::PhysicalPart{
        volt::ManufacturerPart{"Yageo", "RC0603FR-07330RL"},
        volt::PackageRef{"0603"},
        volt::FootprintRef{"passives", "R_0603_1608Metric"},
        std::vector{
            volt::PinPadMapping{first_pin, "1"},
        },
    };
    try {
        circuit.electrical().select_physical_part(component, incomplete_part);
        FAIL("Physical part pin mappings must cover every component pin definition");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::InvalidArgument);
        CHECK(std::string{error.what()} ==
              "Physical part must map every pin in the component definition");
    }
}

TEST_CASE("Circuit rejects selected-part attributes without selected-part metadata") {
    volt::Circuit circuit;
    const auto pin_definition = circuit.connectivity().add_pin_definition(volt::PinDefinition{
        "1", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto component_definition = circuit.connectivity().add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{pin_definition}});
    const auto component =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"R1"});
    const auto voltage_rating = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_rating"},
        volt::ElectricalAttributeOwner::SelectedPart,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };

    CHECK_THROWS_AS(
        circuit.electrical().set_selected_part_electrical_attribute(
            component, voltage_rating,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 75.0}}),
        std::logic_error);

    try {
        circuit.electrical().set_selected_part_electrical_attribute(
            component, voltage_rating,
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 75.0}});
        FAIL("Selected-part attributes require selected-part metadata first");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::InvalidState);
        CHECK(std::string{error.what()} == "Component has no selected physical part");
    }
}
