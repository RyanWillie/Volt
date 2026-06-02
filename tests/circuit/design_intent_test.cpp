#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <volt/circuit/design_intent.hpp>

TEST_CASE("DesignIntent records stub nets idempotently in deterministic order") {
    volt::DesignIntent intent;

    CHECK(intent.mark_intentional_stub_net(volt::NetId{2}));
    CHECK(intent.mark_intentional_stub_net(volt::NetId{4}));
    CHECK_FALSE(intent.mark_intentional_stub_net(volt::NetId{2}));

    CHECK(intent.is_intentional_stub_net(volt::NetId{2}));
    CHECK_FALSE(intent.is_intentional_stub_net(volt::NetId{1}));
    CHECK(intent.intentional_stub_nets() == std::vector{volt::NetId{2}, volt::NetId{4}});
}

TEST_CASE("DesignIntent records no-connect pins idempotently in deterministic order") {
    volt::DesignIntent intent;

    CHECK(intent.mark_intentional_no_connect_pin(volt::PinId{3}));
    CHECK(intent.mark_intentional_no_connect_pin(volt::PinId{5}));
    CHECK_FALSE(intent.mark_intentional_no_connect_pin(volt::PinId{3}));

    CHECK(intent.is_intentional_no_connect_pin(volt::PinId{3}));
    CHECK_FALSE(intent.is_intentional_no_connect_pin(volt::PinId{1}));
    CHECK(intent.intentional_no_connect_pins() == std::vector{volt::PinId{3}, volt::PinId{5}});
}
