#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>

namespace {

struct TestEntity {
    std::string name;
    int value = 0;
};

} // namespace

TEST_CASE("EntityTable assigns typed IDs in insertion order") {
    volt::EntityTable<TestEntity, volt::ComponentId> table;

    const auto first = table.insert(TestEntity{.name = "R1", .value = 10});
    const auto second = table.insert(TestEntity{.name = "C1", .value = 20});

    CHECK(first == volt::ComponentId{0});
    CHECK(second == volt::ComponentId{1});
    CHECK(table.size() == 2);
    CHECK_FALSE(table.empty());
}

TEST_CASE("EntityTable retrieves const and mutable values by typed ID") {
    volt::EntityTable<TestEntity, volt::ComponentId> table;
    const auto id = table.insert(TestEntity{.name = "R1", .value = 10});

    table.get(id).name = "R_LED";
    table.get(id).value = 330;

    const auto &const_table = table;
    CHECK(const_table.get(id).name == "R_LED");
    CHECK(const_table.get(id).value == 330);
}

TEST_CASE("EntityTable reports containment without accepting other ID types") {
    volt::EntityTable<TestEntity, volt::ComponentId> table;
    const auto id = table.insert(TestEntity{.name = "U1", .value = 1});

    CHECK(table.contains(id));
    CHECK_FALSE(table.contains(volt::ComponentId{1}));

    static_assert(
        !std::is_invocable_v<decltype(&volt::EntityTable<TestEntity, volt::ComponentId>::contains),
                             volt::EntityTable<TestEntity, volt::ComponentId>, volt::NetId>);
}

TEST_CASE("EntityTable rejects out-of-range IDs") {
    volt::EntityTable<TestEntity, volt::ComponentId> table;
    [[maybe_unused]] const auto id = table.insert(TestEntity{.name = "D1", .value = 1});

    CHECK_THROWS_AS(table.get(volt::ComponentId{1}), std::out_of_range);
}
