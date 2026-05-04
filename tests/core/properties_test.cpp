#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>

#include <volt/core/properties.hpp>

TEST_CASE("PropertyKey stores comparable non-empty keys") {
    const auto key = volt::PropertyKey{"value"};

    CHECK(key.value() == "value");
    CHECK(key == volt::PropertyKey{"value"});
    CHECK(key < volt::PropertyKey{"voltage_rating"});
    CHECK_THROWS_AS(volt::PropertyKey{""}, std::invalid_argument);
}

TEST_CASE("PropertyValue stores supported scalar values") {
    const auto string_value = volt::PropertyValue{"330 ohm"};
    const auto bool_value = volt::PropertyValue{true};
    const auto integer_value = volt::PropertyValue{std::int64_t{42}};
    const auto number_value = volt::PropertyValue{0.125};

    CHECK(string_value.kind() == volt::PropertyValueKind::String);
    CHECK(string_value.as_string() == "330 ohm");
    CHECK(bool_value.kind() == volt::PropertyValueKind::Boolean);
    CHECK(bool_value.as_bool());
    CHECK(integer_value.kind() == volt::PropertyValueKind::Integer);
    CHECK(integer_value.as_integer() == 42);
    CHECK(number_value.kind() == volt::PropertyValueKind::Number);
    CHECK(number_value.as_number() == 0.125);
}

TEST_CASE("PropertyValue compares values by kind and payload") {
    CHECK(volt::PropertyValue{"1"} == volt::PropertyValue{"1"});
    CHECK_FALSE(volt::PropertyValue{"1"} == volt::PropertyValue{std::int64_t{1}});
    CHECK(volt::PropertyValue{false} == volt::PropertyValue{false});
    CHECK(volt::PropertyValue{2.5} == volt::PropertyValue{2.5});
}

TEST_CASE("PropertyMap starts empty") {
    const volt::PropertyMap properties;

    CHECK(properties.empty());
    CHECK(properties.size() == 0);
    CHECK_FALSE(properties.contains(volt::PropertyKey{"value"}));
}

TEST_CASE("PropertyMap sets replaces and retrieves properties") {
    volt::PropertyMap properties;
    const auto value_key = volt::PropertyKey{"value"};
    const auto tolerance_key = volt::PropertyKey{"tolerance"};

    properties.set(value_key, volt::PropertyValue{"330 ohm"});
    properties.set(tolerance_key, volt::PropertyValue{"1%"});
    properties.set(value_key, volt::PropertyValue{"470 ohm"});

    CHECK(properties.size() == 2);
    CHECK(properties.contains(value_key));
    CHECK(properties.get(value_key) == volt::PropertyValue{"470 ohm"});
    CHECK(properties.get(tolerance_key) == volt::PropertyValue{"1%"});
}

TEST_CASE("PropertyMap reports missing properties") {
    const volt::PropertyMap properties;

    CHECK_THROWS_AS(properties.get(volt::PropertyKey{"missing"}), std::out_of_range);
}

TEST_CASE("PropertyMap can be constructed from key value pairs") {
    const auto properties = volt::PropertyMap{
        {volt::PropertyKey{"value"}, volt::PropertyValue{"330 ohm"}},
        {volt::PropertyKey{"fitted"}, volt::PropertyValue{true}},
    };

    CHECK(properties.size() == 2);
    CHECK(properties.get(volt::PropertyKey{"value"}) == volt::PropertyValue{"330 ohm"});
    CHECK(properties.get(volt::PropertyKey{"fitted"}) == volt::PropertyValue{true});
}
