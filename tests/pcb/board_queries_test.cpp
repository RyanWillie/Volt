#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints/footprints.hpp>
#include <volt/pcb/queries/board_queries.hpp>
#include <volt/pcb/routing/board_router.hpp>

namespace {

template <typename Owner>
concept HasPadResolutionRoot =
    requires(const Owner &owner, const volt::FootprintLibrary &footprints) {
        owner.resolve_pads(footprints);
    };

template <typename Owner>
concept HasPlacementLookupRoot = requires(const Owner &owner, volt::ComponentId component) {
    owner.placement_for_component(component);
};

template <typename Owner>
concept HasRouteRequestRoot = requires(Owner &owner, volt::BoardTrackRouteRequest request,
                                       const volt::FootprintLibrary &footprints) {
    owner.add_track(std::move(request), footprints);
};

static_assert(!HasPadResolutionRoot<volt::Board>);
static_assert(!HasPlacementLookupRoot<volt::Board>);
static_assert(!HasRouteRequestRoot<volt::Board>);

struct QueryFixture {
    volt::Circuit circuit;
    volt::ComponentId first_component;
    volt::ComponentId second_component;
    volt::NetId shared_net;
};

[[nodiscard]] QueryFixture make_query_fixture() {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(volt::ComponentSpec{
        .name = "OnePad",
        .pins = {volt::PinSpec{.name = "IO", .number = "1"}},
    });
    const auto pin_definition = circuit.get(definition).pins().front();
    const auto first_component = circuit.instantiate_component(
        definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto second_component = circuit.instantiate_component(
        definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U2"}});
    const auto shared_net =
        circuit.add_net(volt::NetSpec{volt::NetName{"SHARED"}, volt::NetKind::Signal});
    circuit.connect(
        shared_net,
        volt::queries::pin_by_definition(circuit, first_component, pin_definition).value());
    circuit.connect(
        shared_net,
        volt::queries::pin_by_definition(circuit, second_component, pin_definition).value());

    const auto selected_part = volt::PhysicalPart{
        volt::ManufacturerPart{"Volt", "ONE-PAD"},
        volt::PackageRef{"OnePad"},
        volt::FootprintRef{"tests", "OnePad"},
        std::vector{volt::PinPadMapping{pin_definition, "1"}},
    };
    circuit.update(first_component, volt::SelectPhysicalPart{selected_part});
    circuit.update(second_component, volt::SelectPhysicalPart{selected_part});
    return QueryFixture{std::move(circuit), first_component, second_component, shared_net};
}

[[nodiscard]] volt::FootprintDefinition query_footprint() {
    const auto body = volt::FootprintPolygon{std::vector{
        volt::FootprintPoint{-0.5, -0.5},
        volt::FootprintPoint{0.5, -0.5},
        volt::FootprintPoint{0.5, 0.5},
        volt::FootprintPoint{-0.5, 0.5},
    }};
    return volt::FootprintDefinition{
        volt::FootprintRef{"tests", "OnePad"},
        std::vector{volt::FootprintPad::surface_mount(
            "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, 0.0},
            volt::FootprintSize{0.6, 0.6}, volt::FootprintLayerSet::front_smd())},
        std::nullopt,
        body,
    };
}

struct QueryBoard {
    volt::Board board;
    volt::BoardLayerId front;
    volt::ComponentPlacementId first_placement;
    volt::ComponentPlacementId second_placement;
    volt::FootprintDefId footprint;
};

[[nodiscard]] QueryBoard make_query_board(const QueryFixture &fixture) {
    auto board = volt::Board{fixture.circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{20.0, 10.0}));
    const auto footprint = board.cache_footprint_definition(query_footprint());
    const auto first_placement = board.place_component(volt::ComponentPlacement{
        fixture.first_component, volt::BoardPoint{4.0, 5.0}, volt::BoardRotation::degrees(0.0)});
    const auto second_placement = board.place_component(volt::ComponentPlacement{
        fixture.second_component, volt::BoardPoint{16.0, 5.0}, volt::BoardRotation::degrees(0.0)});
    return QueryBoard{std::move(board), front, first_placement, second_placement, footprint};
}

} // namespace

TEST_CASE("Board queries expose deterministic lookup, resolution, geometry, and ratsnest reads",
          "[pcb][queries]") {
    auto fixture = make_query_fixture();
    auto layout = make_query_board(fixture);
    const auto empty_library = volt::FootprintLibrary{};
    const auto geometry_mutations = layout.board.geometry_mutation_count();

    CHECK(volt::queries::footprint_definition_id(
              layout.board, volt::FootprintRef{"tests", "OnePad"}) == layout.footprint);
    CHECK(volt::queries::placement_for_component(layout.board, fixture.first_component) ==
          layout.first_placement);
    CHECK(volt::queries::placement_for_component(layout.board, fixture.second_component) ==
          layout.second_placement);

    const auto resolution_footprints =
        volt::queries::board_resolution_footprints(layout.board, empty_library);
    REQUIRE(resolution_footprints.find(volt::FootprintRef{"tests", "OnePad"}) != nullptr);

    const auto first_resolutions = volt::queries::resolve_pads(layout.board, empty_library);
    const auto second_resolutions = volt::queries::resolve_pads(layout.board, empty_library);
    REQUIRE(first_resolutions.size() == 2U);
    REQUIRE(second_resolutions.size() == first_resolutions.size());
    for (std::size_t index = 0; index < first_resolutions.size(); ++index) {
        CHECK(first_resolutions[index].placement() == second_resolutions[index].placement());
        CHECK(first_resolutions[index].component() == second_resolutions[index].component());
        CHECK(first_resolutions[index].pad() == second_resolutions[index].pad());
        CHECK(first_resolutions[index].pin() == second_resolutions[index].pin());
        CHECK(first_resolutions[index].net() == second_resolutions[index].net());
        CHECK(first_resolutions[index].status() == second_resolutions[index].status());
    }
    CHECK(first_resolutions[0].placement() == layout.first_placement);
    CHECK(first_resolutions[0].net() == fixture.shared_net);
    CHECK(first_resolutions[1].placement() == layout.second_placement);
    CHECK(first_resolutions[1].net() == fixture.shared_net);

    const auto geometries =
        volt::queries::project_footprint_geometries(layout.board, empty_library);
    REQUIRE(geometries.size() == 2U);
    CHECK(geometries[0].placement() == layout.first_placement);
    REQUIRE(geometries[0].body().has_value());
    CHECK(geometries[0].body().value() ==
          std::vector{volt::BoardPoint{3.5, 4.5}, volt::BoardPoint{4.5, 4.5},
                      volt::BoardPoint{4.5, 5.5}, volt::BoardPoint{3.5, 5.5}});

    const auto first_ratsnest = volt::queries::ratsnest_edges(layout.board, empty_library);
    const auto second_ratsnest = volt::queries::ratsnest_edges(layout.board, empty_library);
    REQUIRE(first_ratsnest.size() == 1U);
    REQUIRE(second_ratsnest.size() == 1U);
    CHECK(first_ratsnest[0].net() == fixture.shared_net);
    CHECK(first_ratsnest[0].from().placement() == layout.first_placement);
    CHECK(first_ratsnest[0].to().placement() == layout.second_placement);
    CHECK(second_ratsnest[0].from().placement() == first_ratsnest[0].from().placement());
    CHECK(second_ratsnest[0].to().placement() == first_ratsnest[0].to().placement());
    CHECK(layout.board.geometry_mutation_count() == geometry_mutations);
}

TEST_CASE("BoardRouter owns endpoint route sugar and rejects before mutation", "[pcb][router]") {
    auto fixture = make_query_fixture();
    auto layout = make_query_board(fixture);
    const auto other_net =
        fixture.circuit.add_net(volt::NetSpec{volt::NetName{"OTHER"}, volt::NetKind::Signal});
    auto router = volt::BoardRouter{layout.board, volt::FootprintLibrary{}};
    const auto endpoints = std::vector{
        volt::BoardRouteEndpoint::footprint_pad(volt::BoardPoint{4.0, 5.0}, layout.first_placement,
                                                volt::FootprintPadId{0}),
        volt::BoardRouteEndpoint::footprint_pad(volt::BoardPoint{16.0, 5.0},
                                                layout.second_placement, volt::FootprintPadId{0}),
    };

    CHECK_THROWS(
        router.add_track(volt::BoardTrackRouteRequest{other_net, layout.front, endpoints, 0.25}));
    CHECK(layout.board.track_count() == 0U);

    const auto result =
        router.add_track(volt::BoardTrackRouteRequest{std::nullopt, layout.front, endpoints, 0.25});
    CHECK(result.net == fixture.shared_net);
    CHECK(result.track == volt::BoardTrackId{0});
    CHECK(layout.board.track_count() == 1U);
    CHECK(layout.board.track(result.track).net() == fixture.shared_net);
}
