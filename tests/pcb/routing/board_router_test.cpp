#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/net_classes.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/board_router.hpp>
#include <volt/pcb/board_spatial_index.hpp>
#include <volt/pcb/footprints.hpp>

namespace {

struct RouterFixture {
    volt::Circuit circuit;
    volt::NetId signal_net;
    volt::NetId other_net;
};

[[nodiscard]] RouterFixture make_router_fixture() {
    auto circuit = volt::Circuit{};
    const auto signal_net = circuit.add_net(volt::Net{volt::NetName{"SIG"}, volt::NetKind::Signal});
    const auto other_net =
        circuit.add_net(volt::Net{volt::NetName{"OBSTACLE"}, volt::NetKind::Signal});
    return RouterFixture{std::move(circuit), signal_net, other_net};
}

struct TwoLayerBoard {
    volt::Board board;
    volt::BoardLayerId front;
    volt::BoardLayerId back;
};

struct FourLayerBoard {
    volt::Board board;
    volt::BoardLayerId front;
    volt::BoardLayerId inner1;
    volt::BoardLayerId inner2;
    volt::BoardLayerId back;
};

[[nodiscard]] TwoLayerBoard make_two_layer_board(const volt::Circuit &circuit) {
    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{60.0, 40.0}));
    return TwoLayerBoard{std::move(board), front, back};
}

[[nodiscard]] FourLayerBoard make_four_layer_board(const volt::Circuit &circuit) {
    auto board = volt::Board{circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto inner1 = board.add_layer(
        volt::BoardLayer{"In1.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Inner});
    const auto inner2 = board.add_layer(
        volt::BoardLayer{"In2.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Inner});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, inner1, inner2, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{60.0, 40.0}));
    return FourLayerBoard{std::move(board), front, inner1, inner2, back};
}

[[nodiscard]] std::vector<std::string> copper_drc_codes(const volt::DiagnosticReport &report) {
    auto codes = std::vector<std::string>{};
    for (const auto &diagnostic : report.diagnostics()) {
        const auto code = diagnostic.code().value();
        if (code.rfind("PCB_", 0) == 0 && code != "PCB_NET_UNROUTED") {
            codes.push_back(code);
        }
    }
    return codes;
}

[[nodiscard]] std::vector<std::tuple<double, double, double, double>>
track_geometry(const volt::Board &board) {
    auto geometry = std::vector<std::tuple<double, double, double, double>>{};
    for (std::size_t index = 0; index < board.track_count(); ++index) {
        const auto &track = board.track(volt::BoardTrackId{index});
        const auto &points = track.points();
        geometry.emplace_back(points.front().x_mm(), points.front().y_mm(), points.back().x_mm(),
                              points.back().y_mm());
    }
    return geometry;
}

} // namespace

TEST_CASE("Router connects a clear straight path with one track", "[pcb][router]") {
    auto fixture = make_router_fixture();
    auto layout = make_two_layer_board(fixture.circuit);
    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};

    const auto result = router.connect(
        volt::BoardRouteRequest{fixture.signal_net, volt::BoardPoint{10.0, 20.0},
                                volt::BoardPoint{40.0, 20.0}, layout.front, layout.front});

    REQUIRE(result.routed);
    REQUIRE(result.tracks.size() == 1U);
    REQUIRE(result.vias.empty());
    REQUIRE(layout.board.track_count() == 1U);

    const auto &track = layout.board.track(result.tracks.front());
    CHECK(track.net() == fixture.signal_net);
    CHECK(track.layer() == layout.front);
    CHECK(track.points().front() == volt::BoardPoint{10.0, 20.0});
    CHECK(track.points().back() == volt::BoardPoint{40.0, 20.0});
}

TEST_CASE("Routed copper passes full DRC clean by construction", "[pcb][router]") {
    auto fixture = make_router_fixture();
    auto layout = make_two_layer_board(fixture.circuit);

    // A pre-existing obstacle track on a different net forces a non-straight route.
    static_cast<void>(layout.board.add_track(volt::BoardTrack{
        fixture.other_net, layout.front,
        std::vector{volt::BoardPoint{25.0, 5.0}, volt::BoardPoint{25.0, 35.0}}, 0.25}));

    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};
    const auto result = router.connect(
        volt::BoardRouteRequest{fixture.signal_net, volt::BoardPoint{10.0, 20.0},
                                volt::BoardPoint{40.0, 20.0}, layout.front, layout.front});

    REQUIRE(result.routed);

    const auto report = volt::validate_board(layout.board, volt::builtin_footprint_library());
    const auto codes = copper_drc_codes(report);
    INFO("unexpected copper DRC codes present");
    CHECK(codes.empty());
}

TEST_CASE("Router routes across layers with a via", "[pcb][router]") {
    auto fixture = make_router_fixture();
    auto layout = make_two_layer_board(fixture.circuit);
    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};

    const auto result = router.connect(
        volt::BoardRouteRequest{fixture.signal_net, volt::BoardPoint{10.0, 20.0},
                                volt::BoardPoint{40.0, 20.0}, layout.front, layout.back});

    REQUIRE(result.routed);
    REQUIRE(result.vias.size() == 1U);

    const auto &via = layout.board.via(result.vias.front());
    CHECK(via.net() == fixture.signal_net);
    CHECK(((via.start_layer() == layout.front && via.end_layer() == layout.back) ||
           (via.start_layer() == layout.back && via.end_layer() == layout.front)));

    const auto report = volt::validate_board(layout.board, volt::builtin_footprint_library());
    CHECK(copper_drc_codes(report).empty());
}

TEST_CASE("Router rejects a via blocked on an intermediate stack layer", "[pcb][router]") {
    auto fixture = make_router_fixture();
    auto layout = make_four_layer_board(fixture.circuit);
    static_cast<void>(layout.board.add_keepout(volt::BoardKeepout{
        std::vector{volt::BoardPoint{0.0, 0.0}, volt::BoardPoint{60.0, 0.0},
                    volt::BoardPoint{60.0, 40.0}, volt::BoardPoint{0.0, 40.0}},
        std::vector{layout.inner1}, std::vector{volt::BoardKeepoutRestriction::Via}}));

    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};
    const auto result = router.connect(
        volt::BoardRouteRequest{fixture.signal_net, volt::BoardPoint{10.0, 20.0},
                                volt::BoardPoint{40.0, 20.0}, layout.front, layout.back});

    CHECK_FALSE(result.routed);
    CHECK(result.tracks.empty());
    CHECK(result.vias.empty());
    REQUIRE_FALSE(result.blockers.empty());
    CHECK(result.blockers.front().kind == volt::BoardSpatialBlockerKind::Keepout);
    CHECK(result.blockers.front().layer == layout.inner1);
    CHECK(layout.board.track_count() == 0U);
    CHECK(layout.board.via_count() == 0U);
}

TEST_CASE("Router output is deterministic for a given board state", "[pcb][router]") {
    auto fixture = make_router_fixture();

    const auto route_once = [&fixture]() {
        auto layout = make_two_layer_board(fixture.circuit);
        static_cast<void>(layout.board.add_track(volt::BoardTrack{
            fixture.other_net, layout.front,
            std::vector{volt::BoardPoint{25.0, 5.0}, volt::BoardPoint{25.0, 35.0}}, 0.25}));
        auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};
        const auto result = router.connect(
            volt::BoardRouteRequest{fixture.signal_net, volt::BoardPoint{10.0, 20.0},
                                    volt::BoardPoint{40.0, 20.0}, layout.front, layout.front});
        REQUIRE(result.routed);
        return track_geometry(layout.board);
    };

    CHECK(route_once() == route_once());
}

TEST_CASE("Router reports blockers and leaves the board unchanged on failure", "[pcb][router]") {
    auto fixture = make_router_fixture();
    auto layout = make_two_layer_board(fixture.circuit);

    // A keepout covering both endpoints blocks every candidate corridor on the front layer.
    static_cast<void>(layout.board.add_keepout(volt::BoardKeepout{
        std::vector{volt::BoardPoint{2.0, 2.0}, volt::BoardPoint{58.0, 2.0},
                    volt::BoardPoint{58.0, 38.0}, volt::BoardPoint{2.0, 38.0}},
        std::vector{layout.front}, std::vector{volt::BoardKeepoutRestriction::Copper}}));

    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};
    const auto track_count_before = layout.board.track_count();
    const auto via_count_before = layout.board.via_count();

    const auto result = router.connect(
        volt::BoardRouteRequest{fixture.signal_net, volt::BoardPoint{10.0, 20.0},
                                volt::BoardPoint{40.0, 20.0}, layout.front, layout.front});

    CHECK_FALSE(result.routed);
    CHECK(result.tracks.empty());
    CHECK(result.vias.empty());
    REQUIRE_FALSE(result.blockers.empty());
    CHECK(result.blockers.front().kind == volt::BoardSpatialBlockerKind::Keepout);
    CHECK(result.blockers.front().keepout.has_value());
    CHECK(result.blockers.front().layer == layout.front);

    CHECK(layout.board.track_count() == track_count_before);
    CHECK(layout.board.via_count() == via_count_before);
}

TEST_CASE("Router rejects structurally invalid endpoint layers", "[pcb][router]") {
    auto fixture = make_router_fixture();
    auto layout = make_two_layer_board(fixture.circuit);
    const auto silkscreen = layout.board.add_layer(
        volt::BoardLayer{"F.SilkS", volt::BoardLayerRole::Silkscreen, volt::BoardLayerSide::Top});
    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};

    CHECK_THROWS_AS(router.connect(volt::BoardRouteRequest{
                        fixture.signal_net, volt::BoardPoint{10.0, 20.0},
                        volt::BoardPoint{40.0, 20.0}, volt::BoardLayerId{99}, layout.front}),
                    std::out_of_range);
    CHECK_THROWS_AS(router.connect(volt::BoardRouteRequest{
                        fixture.signal_net, volt::BoardPoint{10.0, 20.0},
                        volt::BoardPoint{40.0, 20.0}, silkscreen, layout.front}),
                    std::invalid_argument);
    CHECK(layout.board.track_count() == 0U);
    CHECK(layout.board.via_count() == 0U);
}

TEST_CASE("Router respects net-class width, via size, and allowed layers", "[pcb][router]") {
    auto fixture = make_router_fixture();

    auto net_class = volt::NetClass{volt::NetClassName{"POWER"}};
    net_class.set_track_width_mm(0.6);
    net_class.set_via_size_mm(0.4, 0.8);
    net_class.set_layer_scope(volt::NetClassLayerScope::TopOnly);
    const auto class_id = fixture.circuit.add_net_class(std::move(net_class));
    REQUIRE(fixture.circuit.assign_net_class(fixture.signal_net, class_id));

    auto layout = make_two_layer_board(fixture.circuit);
    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};

    const auto params = router.resolve_parameters(fixture.signal_net);
    CHECK(params.track_width_mm == Catch::Approx(0.6));
    CHECK(params.via_diameter_mm == Catch::Approx(0.8));
    REQUIRE(params.allowed_layers.size() == 1U);
    CHECK(params.allowed_layers.front() == layout.front);

    SECTION("routes on the allowed layer at the class width") {
        const auto result = router.connect(
            volt::BoardRouteRequest{fixture.signal_net, volt::BoardPoint{10.0, 20.0},
                                    volt::BoardPoint{40.0, 20.0}, layout.front, layout.front});
        REQUIRE(result.routed);
        CHECK(layout.board.track(result.tracks.front()).width_mm() == Catch::Approx(0.6));
        const auto report = volt::validate_board(layout.board, volt::builtin_footprint_library());
        CHECK(copper_drc_codes(report).empty());
    }

    SECTION("refuses to route onto a disallowed layer") {
        const auto result = router.connect(
            volt::BoardRouteRequest{fixture.signal_net, volt::BoardPoint{10.0, 20.0},
                                    volt::BoardPoint{40.0, 20.0}, layout.back, layout.back});
        CHECK_FALSE(result.routed);
        CHECK(layout.board.track_count() == 0U);
    }
}

TEST_CASE("Router refuses vias whose copper span crosses a disallowed layer", "[pcb][router]") {
    auto fixture = make_router_fixture();

    auto net_class = volt::NetClass{volt::NetClassName{"SURFACE_ONLY"}};
    net_class.set_allowed_layer_names({"F.Cu", "B.Cu"});
    const auto class_id = fixture.circuit.add_net_class(std::move(net_class));
    REQUIRE(fixture.circuit.assign_net_class(fixture.signal_net, class_id));

    auto layout = make_four_layer_board(fixture.circuit);
    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};

    const auto params = router.resolve_parameters(fixture.signal_net);
    CHECK(params.allowed_layers == std::vector{layout.front, layout.back});

    const auto result = router.connect(
        volt::BoardRouteRequest{fixture.signal_net, volt::BoardPoint{10.0, 20.0},
                                volt::BoardPoint{40.0, 20.0}, layout.front, layout.back});

    CHECK_FALSE(result.routed);
    CHECK(result.tracks.empty());
    CHECK(result.vias.empty());
    CHECK(layout.board.track_count() == 0U);
    CHECK(layout.board.via_count() == 0U);
}

TEST_CASE("Router floors net-class sizes at board design minima", "[pcb][router]") {
    auto fixture = make_router_fixture();

    auto net_class = volt::NetClass{volt::NetClassName{"TOO_SMALL"}};
    net_class.set_track_width_mm(0.10);
    net_class.set_via_size_mm(0.20, 0.40);
    const auto class_id = fixture.circuit.add_net_class(std::move(net_class));
    REQUIRE(fixture.circuit.assign_net_class(fixture.signal_net, class_id));

    auto layout = make_two_layer_board(fixture.circuit);
    layout.board.set_design_rules(volt::BoardDesignRules{0.10, 0.35, 0.30, 0.70, 0.0});
    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};

    const auto params = router.resolve_parameters(fixture.signal_net);
    CHECK(params.track_width_mm == Catch::Approx(0.35));
    CHECK(params.via_drill_mm == Catch::Approx(0.30));
    CHECK(params.via_diameter_mm == Catch::Approx(0.70));

    const auto result = router.connect(
        volt::BoardRouteRequest{fixture.signal_net, volt::BoardPoint{10.0, 20.0},
                                volt::BoardPoint{40.0, 20.0}, layout.front, layout.back});

    REQUIRE(result.routed);
    REQUIRE(result.tracks.size() == 1U);
    REQUIRE(result.vias.size() == 1U);
    CHECK(layout.board.track(result.tracks.front()).width_mm() == Catch::Approx(0.35));
    CHECK(layout.board.via(result.vias.front()).drill_diameter_mm() == Catch::Approx(0.30));
    CHECK(layout.board.via(result.vias.front()).annular_diameter_mm() == Catch::Approx(0.70));
    const auto report = volt::validate_board(layout.board, volt::builtin_footprint_library());
    CHECK(copper_drc_codes(report).empty());
}

TEST_CASE("Router respects room-local track width overrides", "[pcb][router]") {
    auto fixture = make_router_fixture();
    auto layout = make_two_layer_board(fixture.circuit);
    auto room = volt::BoardRoom{
        "Timing",
        volt::BoardOutline::rectangle(volt::BoardPoint{5.0, 10.0}, volt::BoardSize{45.0, 20.0}),
        std::vector{layout.front}, 0};
    room.set_track_width_mm(0.70);
    static_cast<void>(layout.board.add_room(std::move(room)));

    auto router = volt::BoardRouter{layout.board, volt::builtin_footprint_library()};
    const auto result = router.connect(
        volt::BoardRouteRequest{fixture.signal_net, volt::BoardPoint{10.0, 20.0},
                                volt::BoardPoint{40.0, 20.0}, layout.front, layout.front});

    REQUIRE(result.routed);
    REQUIRE(result.tracks.size() == 1U);
    CHECK(layout.board.track(result.tracks.front()).width_mm() == Catch::Approx(0.70));
    const auto report = volt::validate_board(layout.board, volt::builtin_footprint_library());
    CHECK(copper_drc_codes(report).empty());
}

TEST_CASE("Spatial index detects board geometry mutated outside the index", "[pcb][router]") {
    auto fixture = make_router_fixture();
    auto layout = make_two_layer_board(fixture.circuit);

    auto index = volt::BoardSpatialIndex{layout.board, volt::builtin_footprint_library()};
    const auto candidate = volt::BoardSpatialQueryShape{
        volt::BoardSpatialQueryShapeKind::Segment,
        fixture.signal_net,
        std::vector{layout.front},
        std::vector{volt::BoardPoint{10.0, 20.0}, volt::BoardPoint{20.0, 20.0}},
        0.1,
        volt::BoardClearanceKind::Track,
        volt::BoardKeepoutRestriction::Copper,
    };

    // A direct board mutation that does not flow through the index makes the snapshot stale.
    static_cast<void>(layout.board.add_track(volt::BoardTrack{
        fixture.other_net, layout.front,
        std::vector{volt::BoardPoint{30.0, 5.0}, volt::BoardPoint{30.0, 35.0}}, 0.25}));

    CHECK_THROWS_AS(index.query_legality(candidate), std::logic_error);
}
