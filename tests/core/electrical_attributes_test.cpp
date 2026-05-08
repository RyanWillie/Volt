#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include <volt/core/electrical_attributes.hpp>

TEST_CASE("ElectricalAttributeName stores comparable non-empty names") {
    const auto name = volt::ElectricalAttributeName{"resistance"};

    CHECK(name.value() == "resistance");
    CHECK(name == volt::ElectricalAttributeName{"resistance"});
    CHECK(name < volt::ElectricalAttributeName{"voltage_rating"});
    CHECK_THROWS_AS(volt::ElectricalAttributeName{""}, std::invalid_argument);
}

TEST_CASE("AuthoringUnit records explicit plain-number scaling metadata") {
    const auto ohm = volt::AuthoringUnit{volt::UnitDimension::Resistance, 1.0, "ohm"};

    CHECK(ohm.dimension() == volt::UnitDimension::Resistance);
    CHECK(ohm.scale_to_canonical() == 1.0);
    CHECK(ohm.symbol() == "ohm");
    CHECK(ohm == volt::AuthoringUnit{volt::UnitDimension::Resistance, 1.0, "ohm"});

    CHECK_THROWS_AS(volt::AuthoringUnit(volt::UnitDimension::Voltage, 0.0, "V"),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::AuthoringUnit(volt::UnitDimension::Voltage, -1.0, "V"),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::AuthoringUnit(volt::UnitDimension::Voltage, 1.0, ""),
                    std::invalid_argument);
}

TEST_CASE("ElectricalAttributeSpec records owner kind dimension and authoring default") {
    const auto resistance = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"resistance"},
        volt::ElectricalAttributeOwner::ComponentInstance,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Resistance,
        volt::AuthoringUnit{volt::UnitDimension::Resistance, 1.0, "ohm"},
    };

    REQUIRE(resistance.default_authoring_unit().has_value());
    CHECK(resistance.name() == volt::ElectricalAttributeName{"resistance"});
    CHECK(resistance.owner() == volt::ElectricalAttributeOwner::ComponentInstance);
    CHECK(resistance.kind() == volt::ElectricalAttributeKind::DesignInput);
    CHECK(resistance.dimension() == volt::UnitDimension::Resistance);
    CHECK(resistance.default_authoring_unit().value().symbol() == "ohm");

    const auto constraint = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_limit"},
        volt::ElectricalAttributeOwner::Constraint,
        volt::ElectricalAttributeKind::Constraint,
        volt::UnitDimension::Voltage,
    };
    CHECK_FALSE(constraint.default_authoring_unit().has_value());

    CHECK_THROWS_AS(volt::ElectricalAttributeSpec(
                        volt::ElectricalAttributeName{"bad_default"},
                        volt::ElectricalAttributeOwner::ComponentInstance,
                        volt::ElectricalAttributeKind::DesignInput, volt::UnitDimension::Resistance,
                        volt::AuthoringUnit{volt::UnitDimension::Voltage, 1.0, "V"}),
                    std::invalid_argument);
}

TEST_CASE("ElectricalAttributeValue stores typed quantity tolerance or range payloads") {
    const auto resistance =
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Resistance, 330.0}};

    CHECK(resistance.kind() == volt::ElectricalAttributeValueKind::Quantity);
    CHECK(resistance.dimension() == volt::UnitDimension::Resistance);
    CHECK(resistance.as_quantity() == volt::Quantity{volt::UnitDimension::Resistance, 330.0});

    const auto tolerance = volt::ElectricalAttributeValue{volt::Tolerance::percent(0.01)};

    CHECK(tolerance.kind() == volt::ElectricalAttributeValueKind::Tolerance);
    CHECK(tolerance.dimension() == volt::UnitDimension::Ratio);
    CHECK(tolerance.as_tolerance().plus() == volt::Quantity{volt::UnitDimension::Ratio, 0.01});

    const auto range = volt::ElectricalAttributeValue{
        volt::QuantityRange::bounded(volt::Quantity{volt::UnitDimension::Voltage, 3.0},
                                     volt::Quantity{volt::UnitDimension::Voltage, 3.6})};

    CHECK(range.kind() == volt::ElectricalAttributeValueKind::Range);
    CHECK(range.dimension() == volt::UnitDimension::Voltage);
    CHECK(range.as_range().dimension() == volt::UnitDimension::Voltage);
}

TEST_CASE("ElectricalAttributeSpec rejects values with incompatible dimensions") {
    const auto spec = volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage"},
        volt::ElectricalAttributeOwner::Net,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
        volt::AuthoringUnit{volt::UnitDimension::Voltage, 1.0, "V"},
    };

    CHECK_NOTHROW(spec.require_compatible(
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 3.3}}));
    CHECK_THROWS_AS(spec.require_compatible(volt::ElectricalAttributeValue{
                        volt::Quantity{volt::UnitDimension::Current, 3.3}}),
                    std::invalid_argument);
}
