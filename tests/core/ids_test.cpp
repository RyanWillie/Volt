#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include <volt/core/ids.hpp>

TEST_CASE("logical entity IDs are distinct typed storage indexes") {
    const auto component_def = volt::ComponentDefId{0};
    const auto component = volt::ComponentId{1};
    const auto pin_def = volt::PinDefId{2};
    const auto pin = volt::PinId{3};
    const auto net = volt::NetId{4};

    CHECK(component_def.index() == 0);
    CHECK(component.index() == 1);
    CHECK(pin_def.index() == 2);
    CHECK(pin.index() == 3);
    CHECK(net.index() == 4);

    static_assert(!std::is_same_v<volt::ComponentId, volt::NetId>);
    static_assert(!std::is_same_v<volt::PinDefId, volt::PinId>);
    static_assert(!std::is_constructible_v<volt::NetId, volt::ComponentId>);
    static_assert(!std::is_convertible_v<volt::ComponentId, volt::NetId>);
}
