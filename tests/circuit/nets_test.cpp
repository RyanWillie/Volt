#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

#include <volt/circuit/nets.hpp>
#include <volt/core/ids.hpp>

TEST_CASE("NetName stores a non-empty domain name") {
    const auto name = volt::NetName{"GND"};

    CHECK(name.value() == "GND");
    CHECK(name == volt::NetName{"GND"});
}

TEST_CASE("NetName rejects empty values") {
    CHECK_THROWS_AS(volt::NetName{""}, std::invalid_argument);
}

TEST_CASE("Net stores name kind and starts with no pins") {
    const auto net = volt::Net{volt::NetName{"3V3"}, volt::NetKind::Power};

    CHECK(net.name() == volt::NetName{"3V3"});
    CHECK(net.kind() == volt::NetKind::Power);
    CHECK(net.pins().empty());
}

TEST_CASE("Net connects pins in insertion order") {
    auto net = volt::Net{volt::NetName{"LED_A"}, volt::NetKind::Signal};

    CHECK(net.connect(volt::PinId{2}));
    CHECK(net.connect(volt::PinId{5}));

    REQUIRE(net.pins().size() == 2);
    CHECK(net.pins()[0] == volt::PinId{2});
    CHECK(net.pins()[1] == volt::PinId{5});
    CHECK(net.contains(volt::PinId{2}));
    CHECK(net.contains(volt::PinId{5}));
}

TEST_CASE("Net does not duplicate an already connected pin") {
    auto net = volt::Net{volt::NetName{"RESET"}, volt::NetKind::Signal};

    CHECK(net.connect(volt::PinId{4}));
    CHECK_FALSE(net.connect(volt::PinId{4}));

    REQUIRE(net.pins().size() == 1);
    CHECK(net.pins().front() == volt::PinId{4});
}

TEST_CASE("Net disconnects pins explicitly") {
    auto net = volt::Net{volt::NetName{"SDA"}, volt::NetKind::Signal};
    net.connect(volt::PinId{1});
    net.connect(volt::PinId{2});

    CHECK(net.disconnect(volt::PinId{1}));
    CHECK_FALSE(net.disconnect(volt::PinId{9}));

    REQUIRE(net.pins().size() == 1);
    CHECK(net.pins().front() == volt::PinId{2});
    CHECK_FALSE(net.contains(volt::PinId{1}));
}
