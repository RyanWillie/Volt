#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

#include <volt/circuit/electrical_model.hpp>

namespace {

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

TEST_CASE("ElectricalModel stores typed attributes by owner kind") {
    volt::ElectricalModel model;
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

    model.set_component_attribute(
        volt::ComponentId{1}, component_resistance,
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Resistance, 330.0}});
    model.set_pin_definition_attribute(volt::PinDefId{2}, pin_voltage_range,
                                       volt::ElectricalAttributeValue{volt::QuantityRange::bounded(
                                           volt::Quantity{volt::UnitDimension::Voltage, 1.8},
                                           volt::Quantity{volt::UnitDimension::Voltage, 3.6})});
    model.set_net_attribute(
        volt::NetId{3}, net_voltage,
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}});

    CHECK(model.component_attributes(volt::ComponentId{1})
              .get(volt::ElectricalAttributeName{"resistance"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Resistance, 330.0});
    CHECK(model.pin_definition_attributes(volt::PinDefId{2})
              .get(volt::ElectricalAttributeName{"voltage_range"})
              .as_range()
              .dimension() == volt::UnitDimension::Voltage);
    CHECK(model.net_attributes(volt::NetId{3})
              .get(volt::ElectricalAttributeName{"voltage"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 3.3});
}

TEST_CASE("ElectricalModel rejects attributes for the wrong owner kind") {
    volt::ElectricalModel model;
    const auto net_voltage = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage"},
        volt::ElectricalAttributeOwner::Net,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };

    CHECK_THROWS_AS(model.set_component_attribute(volt::ComponentId{1}, net_voltage,
                                                  volt::ElectricalAttributeValue{volt::Quantity{
                                                      volt::UnitDimension::Voltage, 3.3}}),
                    std::logic_error);
}

TEST_CASE("ElectricalModel owns selected physical parts and selected-part attributes") {
    volt::ElectricalModel model;
    const auto first_pin = volt::PinDefId{0};
    const auto second_pin = volt::PinDefId{1};
    const auto component = volt::ComponentId{4};
    const auto voltage_rating = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_rating"},
        volt::ElectricalAttributeOwner::SelectedPart,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };

    model.select_physical_part(component, make_resistor_physical_part(first_pin, second_pin),
                               std::vector{first_pin, second_pin});
    model.set_selected_part_attribute(
        component, voltage_rating,
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 75.0}});

    REQUIRE(model.selected_physical_part(component).has_value());
    CHECK(model.selected_physical_part(component)
              ->electrical_attributes()
              .get(volt::ElectricalAttributeName{"voltage_rating"})
              .as_quantity() == volt::Quantity{volt::UnitDimension::Voltage, 75.0});
}

TEST_CASE("ElectricalModel rejects selected parts that do not match component pins") {
    volt::ElectricalModel model;
    const auto first_pin = volt::PinDefId{0};
    const auto second_pin = volt::PinDefId{1};
    const auto extra_pin = volt::PinDefId{2};

    CHECK_THROWS_AS(model.select_physical_part(volt::ComponentId{4},
                                               make_resistor_physical_part(first_pin, extra_pin),
                                               std::vector{first_pin, second_pin}),
                    std::logic_error);
}
