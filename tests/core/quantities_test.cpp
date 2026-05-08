#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <stdexcept>

#include <volt/core/quantities.hpp>

TEST_CASE("Quantity stores a finite value with a unit dimension") {
    const auto resistance = volt::Quantity{volt::UnitDimension::Resistance, 330.0};

    CHECK(resistance.dimension() == volt::UnitDimension::Resistance);
    CHECK(resistance.value() == 330.0);
    CHECK(resistance == volt::Quantity{volt::UnitDimension::Resistance, 330.0});
    CHECK_FALSE(resistance == volt::Quantity{volt::UnitDimension::Voltage, 330.0});

    CHECK_THROWS_AS(
        volt::Quantity(volt::UnitDimension::Voltage, std::numeric_limits<double>::infinity()),
        std::invalid_argument);
    CHECK_THROWS_AS(
        volt::Quantity(volt::UnitDimension::Voltage, std::numeric_limits<double>::quiet_NaN()),
        std::invalid_argument);
}

TEST_CASE("Tolerance stores absolute or percent plus minus bounds") {
    const auto absolute =
        volt::Tolerance::absolute(volt::Quantity{volt::UnitDimension::Resistance, 3.3},
                                  volt::Quantity{volt::UnitDimension::Resistance, 3.3});

    CHECK(absolute.mode() == volt::ToleranceMode::Absolute);
    CHECK(absolute.minus() == volt::Quantity{volt::UnitDimension::Resistance, 3.3});
    CHECK(absolute.plus() == volt::Quantity{volt::UnitDimension::Resistance, 3.3});

    const auto percent = volt::Tolerance::percent(0.01);

    CHECK(percent.mode() == volt::ToleranceMode::Percent);
    CHECK(percent.minus() == volt::Quantity{volt::UnitDimension::Ratio, 0.01});
    CHECK(percent.plus() == volt::Quantity{volt::UnitDimension::Ratio, 0.01});

    CHECK_THROWS_AS(volt::Tolerance::absolute(volt::Quantity{volt::UnitDimension::Resistance, 3.3},
                                              volt::Quantity{volt::UnitDimension::Voltage, 3.3}),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::Tolerance::percent(-0.01), std::invalid_argument);
}

TEST_CASE("QuantityRange validates compatible ordered bounds") {
    const auto range =
        volt::QuantityRange::bounded(volt::Quantity{volt::UnitDimension::Voltage, 3.0},
                                     volt::Quantity{volt::UnitDimension::Voltage, 3.6});

    REQUIRE(range.minimum().has_value());
    REQUIRE(range.maximum().has_value());
    CHECK(range.dimension() == volt::UnitDimension::Voltage);
    CHECK(range.minimum().value() == volt::Quantity{volt::UnitDimension::Voltage, 3.0});
    CHECK(range.maximum().value() == volt::Quantity{volt::UnitDimension::Voltage, 3.6});

    const auto lower_bounded =
        volt::QuantityRange::minimum(volt::Quantity{volt::UnitDimension::Current, 0.0});
    CHECK(lower_bounded.dimension() == volt::UnitDimension::Current);
    CHECK(lower_bounded.minimum().has_value());
    CHECK_FALSE(lower_bounded.maximum().has_value());

    CHECK_THROWS_AS(volt::QuantityRange::bounded(volt::Quantity{volt::UnitDimension::Voltage, 3.0},
                                                 volt::Quantity{volt::UnitDimension::Current, 3.6}),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::QuantityRange::bounded(volt::Quantity{volt::UnitDimension::Voltage, 3.6},
                                                 volt::Quantity{volt::UnitDimension::Voltage, 3.0}),
                    std::invalid_argument);
}
