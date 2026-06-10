#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include <volt/core/ids.hpp>

TEST_CASE("logical entity IDs are distinct typed storage indexes") {
    const auto component_def = volt::ComponentDefId{0};
    const auto component = volt::ComponentId{1};
    const auto pin_def = volt::PinDefId{2};
    const auto pin = volt::PinId{3};
    const auto net = volt::NetId{4};
    const auto symbol_def = volt::SymbolDefId{5};
    const auto sheet = volt::SheetId{6};
    const auto symbol_instance = volt::SymbolInstanceId{7};
    const auto board_track = volt::BoardTrackId{8};
    const auto board_via = volt::BoardViaId{9};
    const auto board_zone = volt::BoardZoneId{10};
    const auto board_keepout = volt::BoardKeepoutId{11};
    const auto board_text = volt::BoardTextId{12};
    const auto net_class = volt::NetClassId{13};

    CHECK(component_def.index() == 0);
    CHECK(component.index() == 1);
    CHECK(pin_def.index() == 2);
    CHECK(pin.index() == 3);
    CHECK(net.index() == 4);
    CHECK(symbol_def.index() == 5);
    CHECK(sheet.index() == 6);
    CHECK(symbol_instance.index() == 7);
    CHECK(board_track.index() == 8);
    CHECK(board_via.index() == 9);
    CHECK(board_zone.index() == 10);
    CHECK(board_keepout.index() == 11);
    CHECK(board_text.index() == 12);
    CHECK(net_class.index() == 13);

    static_assert(!std::is_same_v<volt::ComponentId, volt::NetId>);
    static_assert(!std::is_same_v<volt::PinDefId, volt::PinId>);
    static_assert(!std::is_same_v<volt::SymbolDefId, volt::ComponentDefId>);
    static_assert(!std::is_same_v<volt::SheetId, volt::NetId>);
    static_assert(!std::is_same_v<volt::SymbolInstanceId, volt::ComponentId>);
    static_assert(!std::is_constructible_v<volt::NetId, volt::ComponentId>);
    static_assert(!std::is_convertible_v<volt::ComponentId, volt::NetId>);
    static_assert(!std::is_constructible_v<volt::ComponentId, volt::SymbolInstanceId>);
    static_assert(!std::is_same_v<volt::BoardTrackId, volt::BoardViaId>);
    static_assert(!std::is_constructible_v<volt::BoardTrackId, volt::BoardLayerId>);
    static_assert(!std::is_same_v<volt::BoardZoneId, volt::BoardKeepoutId>);
    static_assert(!std::is_same_v<volt::BoardZoneId, volt::BoardTextId>);
    static_assert(!std::is_constructible_v<volt::BoardZoneId, volt::BoardLayerId>);
    static_assert(!std::is_same_v<volt::NetClassId, volt::NetId>);
    static_assert(!std::is_constructible_v<volt::NetClassId, volt::NetId>);
}
