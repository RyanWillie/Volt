#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <utility>

#include <volt/circuit/circuit.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/placement/board_placement_updates.hpp>

namespace {

template <typename T>
concept HasLayerGetter = requires(const T &value) { value.layer(volt::BoardLayerId{0}); };

template <typename T>
concept HasLayerCount = requires(const T &value) { value.layer_count(); };

template <typename T>
concept HasGeometryMutationCount = requires(const T &value) { value.geometry_mutation_count(); };

template <typename T>
concept CanBorrowAllFromTemporary =
    requires(T value) { std::move(value).template all<volt::BoardLayerId>(); };

static_assert(!HasLayerGetter<volt::Board>);
static_assert(!HasLayerCount<volt::Board>);
static_assert(!HasGeometryMutationCount<volt::Board>);
static_assert(!CanBorrowAllFromTemporary<volt::Board>);

static_assert(volt::BoardEntityId<volt::BoardLayerId>);
static_assert(volt::BoardEntityId<volt::BoardFeatureId>);
static_assert(volt::BoardEntityId<volt::FootprintDefId>);
static_assert(volt::BoardEntityId<volt::ComponentPlacementId>);
static_assert(volt::BoardEntityId<volt::BoardTrackId>);
static_assert(volt::BoardEntityId<volt::BoardViaId>);
static_assert(volt::BoardEntityId<volt::BoardZoneId>);
static_assert(volt::BoardEntityId<volt::BoardKeepoutId>);
static_assert(volt::BoardEntityId<volt::BoardRoomId>);
static_assert(volt::BoardEntityId<volt::BoardTextId>);

} // namespace

TEST_CASE("Board exposes typed generic reads over canonical physical entities") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit, volt::BoardName{"Production"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});

    REQUIRE(board.all<volt::BoardLayerId>().size() == 1U);
    CHECK(board.get(front).name() == "F.Cu");
    CHECK(&*board.all<volt::BoardLayerId>().begin() == &board.get(front));
}

TEST_CASE("Board retains typed physical configuration mutations") {
    auto circuit = volt::Circuit{};
    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});

    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{30.0, 20.0}));

    REQUIRE(board.layer_stack().has_value());
    CHECK(board.layer_stack()->layers() == std::vector{front, back});
    REQUIRE(board.outline().has_value());
    CHECK(board.outline()->vertices().size() == 4U);
}

TEST_CASE("Board placement moves preserve logical identity and lock state") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(volt::ComponentSpec{
        .name = "R",
        .pins = {volt::PinSpec{.name = "A",
                               .number = "1",
                               .terminal_kind = volt::ElectricalTerminalKind::Passive,
                               .direction = volt::ElectricalDirection::Passive,
                               .drive_kind = volt::ElectricalDriveKind::Passive}},
    });
    const auto component = circuit.instantiate_component(
        definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    auto board = volt::Board{circuit};
    const auto placement = board.place_component(
        volt::ComponentPlacement{component, volt::BoardPoint{1.0, 2.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Top, true});

    board.move(volt::BoardPlacementMove{placement, volt::BoardPoint{4.0, 5.0},
                                        volt::BoardRotation::degrees(90.0),
                                        volt::BoardSide::Bottom});

    const auto &moved = board.get(placement);
    CHECK(moved.component() == component);
    CHECK(moved.position() == volt::BoardPoint{4.0, 5.0});
    CHECK(moved.rotation() == volt::BoardRotation::degrees(90.0));
    CHECK(moved.side() == volt::BoardSide::Bottom);
    CHECK(moved.locked());
}

TEST_CASE("Board placement moves reject foreign IDs before mutation") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(volt::ComponentSpec{
        .name = "R",
        .pins = {volt::PinSpec{.name = "A",
                               .number = "1",
                               .terminal_kind = volt::ElectricalTerminalKind::Passive,
                               .direction = volt::ElectricalDirection::Passive,
                               .drive_kind = volt::ElectricalDriveKind::Passive}},
    });
    const auto component = circuit.instantiate_component(
        definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    auto board = volt::Board{circuit};
    const auto placement = board.place_component(volt::ComponentPlacement{
        component, volt::BoardPoint{1.0, 2.0}, volt::BoardRotation::degrees(0.0)});

    CHECK_THROWS_AS(board.move(volt::BoardPlacementMove{
                        volt::ComponentPlacementId{99}, volt::BoardPoint{4.0, 5.0},
                        volt::BoardRotation::degrees(90.0), volt::BoardSide::Bottom}),
                    std::out_of_range);
    CHECK(board.get(placement).position() == volt::BoardPoint{1.0, 2.0});
}
