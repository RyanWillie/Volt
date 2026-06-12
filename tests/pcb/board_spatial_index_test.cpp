#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/board_spatial_index.hpp>
#include <volt/pcb/footprints.hpp>

namespace {

struct BoardFixture {
    volt::Circuit circuit;
    volt::NetId first_net;
    volt::NetId second_net;
    volt::NetId third_net;
};

struct ClearanceDiagnosticSignature {
    std::string message;
    std::vector<volt::EntityRef> entities;

    [[nodiscard]] friend bool operator==(const ClearanceDiagnosticSignature &lhs,
                                         const ClearanceDiagnosticSignature &rhs) = default;
};

[[nodiscard]] BoardFixture make_board_fixture() {
    auto circuit = volt::Circuit{};
    const auto first_net = circuit.add_net(volt::Net{volt::NetName{"A"}, volt::NetKind::Signal});
    const auto second_net = circuit.add_net(volt::Net{volt::NetName{"B"}, volt::NetKind::Signal});
    const auto third_net = circuit.add_net(volt::Net{volt::NetName{"C"}, volt::NetKind::Signal});
    return BoardFixture{std::move(circuit), first_net, second_net, third_net};
}

[[nodiscard]] volt::Board make_two_layer_board(const BoardFixture &fixture) {
    auto board = volt::Board{fixture.circuit};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{40.0, 30.0}));
    return board;
}

[[nodiscard]] volt::BoardSpatialQueryShape
track_candidate(volt::NetId net, volt::BoardLayerId layer, double y_mm, double radius_mm = 0.05) {
    return volt::BoardSpatialQueryShape{
        volt::BoardSpatialQueryShapeKind::Segment,
        net,
        std::vector{layer},
        std::vector{volt::BoardPoint{1.0, y_mm}, volt::BoardPoint{8.0, y_mm}},
        radius_mm,
        volt::BoardClearanceKind::Track,
        volt::BoardKeepoutRestriction::Copper,
    };
}

[[nodiscard]] volt::BoardSpatialQueryShape via_candidate(volt::NetId net,
                                                         std::vector<volt::BoardLayerId> layers,
                                                         volt::BoardPoint position,
                                                         double radius_mm = 0.20) {
    return volt::BoardSpatialQueryShape{
        volt::BoardSpatialQueryShapeKind::Disc,
        net,
        std::move(layers),
        std::vector{position},
        radius_mm,
        volt::BoardClearanceKind::Via,
        volt::BoardKeepoutRestriction::Via,
    };
}

[[nodiscard]] std::vector<volt::detail::BoardCopperShape>
collect_board_shapes(const volt::Board &board) {
    return volt::detail::collect_copper_shapes(board, volt::builtin_footprint_library(), {});
}

[[nodiscard]] ClearanceDiagnosticSignature
clearance_signature(const volt::detail::BoardCopperShape &lhs,
                    const volt::detail::BoardCopperShape &rhs,
                    const volt::detail::BoardCopperClearanceCheck &check) {
    auto entities = lhs.primary_entities;
    entities.insert(entities.end(), rhs.primary_entities.begin(), rhs.primary_entities.end());
    entities.push_back(volt::EntityRef::net(lhs.net));
    entities.push_back(volt::EntityRef::net(rhs.net));
    entities.push_back(volt::EntityRef::board_layer(check.layer.value()));
    if (check.room.has_value()) {
        entities.push_back(volt::EntityRef::board_room(check.room.value()));
    }

    return ClearanceDiagnosticSignature{
        volt::detail::clearance_pair_message(volt::detail::shape_clearance_kind(lhs),
                                             volt::detail::shape_clearance_kind(rhs)),
        std::move(entities),
    };
}

[[nodiscard]] std::vector<ClearanceDiagnosticSignature>
brute_force_clearance_signatures(const volt::Board &board) {
    const auto shapes = collect_board_shapes(board);
    auto signatures = std::vector<ClearanceDiagnosticSignature>{};
    for (std::size_t lhs_index = 0; lhs_index < shapes.size(); ++lhs_index) {
        for (std::size_t rhs_index = lhs_index + 1U; rhs_index < shapes.size(); ++rhs_index) {
            const auto check =
                volt::detail::check_copper_clearance(board, shapes[lhs_index], shapes[rhs_index]);
            if (!check.violates) {
                continue;
            }
            signatures.push_back(clearance_signature(shapes[lhs_index], shapes[rhs_index], check));
        }
    }
    return signatures;
}

[[nodiscard]] std::vector<ClearanceDiagnosticSignature>
validated_clearance_signatures(const volt::Board &board) {
    const auto report = volt::validate_board(board, volt::builtin_footprint_library());
    auto signatures = std::vector<ClearanceDiagnosticSignature>{};
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code() != volt::DiagnosticCode{"PCB_COPPER_CLEARANCE_VIOLATION"}) {
            continue;
        }
        signatures.push_back(
            ClearanceDiagnosticSignature{diagnostic.message(), diagnostic.entities()});
    }
    return signatures;
}

} // namespace

TEST_CASE("BoardSpatialIndex reports net-class pair clearance using the larger class value") {
    auto fixture = make_board_fixture();
    auto first_class = volt::NetClass{volt::NetClassName{"A rules"}};
    first_class.set_copper_clearance_mm(0.30);
    fixture.circuit.assign_net_class(fixture.first_net,
                                     fixture.circuit.add_net_class(std::move(first_class)));
    auto second_class = volt::NetClass{volt::NetClassName{"B rules"}};
    second_class.set_copper_clearance_mm(0.50);
    fixture.circuit.assign_net_class(fixture.second_net,
                                     fixture.circuit.add_net_class(std::move(second_class)));

    auto board = make_two_layer_board(fixture);
    const auto front = volt::BoardLayerId{0};
    board.set_design_rules(volt::BoardDesignRules{0.10, 0.05, 0.10, 0.20, 0.0});
    [[maybe_unused]] const auto obstacle = board.add_track(volt::BoardTrack{
        fixture.first_net, front,
        std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0}}, 0.10});

    const auto index = volt::BoardSpatialIndex{board, volt::builtin_footprint_library()};
    const auto result = index.query_legality(track_candidate(fixture.second_net, front, 1.50));

    REQUIRE_FALSE(result.legal);
    REQUIRE(result.blockers.size() == 1);
    CHECK(result.blockers[0].kind == volt::BoardSpatialBlockerKind::CopperClearance);
    CHECK(result.blockers[0].shape_index == 0U);
    CHECK(result.blockers[0].required_clearance_mm == 0.50);
    CHECK(result.blockers[0].actual_clearance_mm == 0.40);
}

TEST_CASE("BoardSpatialIndex lets a room override replace larger class and matrix clearances") {
    auto fixture = make_board_fixture();
    auto net_class = volt::NetClass{volt::NetClassName{"Wide"}};
    net_class.set_copper_clearance_mm(0.50);
    fixture.circuit.assign_net_class(fixture.first_net,
                                     fixture.circuit.add_net_class(std::move(net_class)));

    auto board = make_two_layer_board(fixture);
    const auto front = volt::BoardLayerId{0};
    auto rules = volt::BoardDesignRules{0.10, 0.05, 0.10, 0.20, 0.0};
    rules.set_clearance_mm(volt::BoardClearanceKind::Track, volt::BoardClearanceKind::Track, 0.50);
    board.set_design_rules(rules);
    auto room = volt::BoardRoom{
        "Fine pitch",
        volt::BoardOutline::rectangle(volt::BoardPoint{0.5, 0.5}, volt::BoardSize{9.0, 2.0}),
        std::vector{front},
    };
    room.set_copper_clearance_mm(0.10);
    [[maybe_unused]] const auto room_id = board.add_room(std::move(room));
    [[maybe_unused]] const auto obstacle = board.add_track(volt::BoardTrack{
        fixture.first_net, front,
        std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0}}, 0.10});

    const auto index = volt::BoardSpatialIndex{board, volt::builtin_footprint_library()};
    const auto result = index.query_legality(track_candidate(fixture.second_net, front, 1.30));

    CHECK(result.legal);
    CHECK(result.blockers.empty());
}

TEST_CASE("BoardSpatialIndex applies clearance-matrix kind pairs to transient candidates") {
    auto fixture = make_board_fixture();
    auto board = make_two_layer_board(fixture);
    const auto front = volt::BoardLayerId{0};
    const auto back = volt::BoardLayerId{1};
    auto rules = volt::BoardDesignRules{0.10, 0.05, 0.10, 0.20, 0.0};
    rules.set_clearance_mm(volt::BoardClearanceKind::Track, volt::BoardClearanceKind::Via, 0.40);
    board.set_design_rules(rules);
    [[maybe_unused]] const auto via = board.add_via(
        volt::BoardVia{fixture.first_net, volt::BoardPoint{4.0, 1.0}, front, back, 0.10, 0.40});

    const auto index = volt::BoardSpatialIndex{board, volt::builtin_footprint_library()};
    const auto result = index.query_legality(track_candidate(fixture.second_net, front, 1.50));

    REQUIRE_FALSE(result.legal);
    REQUIRE(result.blockers.size() == 1);
    CHECK(result.blockers[0].required_clearance_mm == 0.40);
    CHECK(result.blockers[0].actual_clearance_mm == 0.25);
}

TEST_CASE("BoardSpatialIndex ignores shapes on disjoint layers") {
    auto fixture = make_board_fixture();
    auto board = make_two_layer_board(fixture);
    const auto front = volt::BoardLayerId{0};
    const auto back = volt::BoardLayerId{1};
    board.set_design_rules(volt::BoardDesignRules{0.50, 0.05, 0.10, 0.20, 0.0});
    [[maybe_unused]] const auto obstacle = board.add_track(volt::BoardTrack{
        fixture.first_net, front,
        std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0}}, 0.10});

    const auto index = volt::BoardSpatialIndex{board, volt::builtin_footprint_library()};
    const auto result = index.query_legality(track_candidate(fixture.second_net, back, 1.0));

    CHECK(result.legal);
}

TEST_CASE("BoardSpatialIndex detects multi-layer via collisions on any shared layer") {
    auto fixture = make_board_fixture();
    auto board = make_two_layer_board(fixture);
    const auto front = volt::BoardLayerId{0};
    const auto back = volt::BoardLayerId{1};
    board.set_design_rules(volt::BoardDesignRules{0.30, 0.05, 0.10, 0.20, 0.0});
    [[maybe_unused]] const auto via = board.add_via(
        volt::BoardVia{fixture.first_net, volt::BoardPoint{4.0, 1.0}, front, back, 0.10, 0.40});

    const auto index = volt::BoardSpatialIndex{board, volt::builtin_footprint_library()};
    const auto result = index.query_legality(track_candidate(fixture.second_net, back, 1.20));

    REQUIRE_FALSE(result.legal);
    REQUIRE(result.blockers.size() == 1);
    CHECK(result.blockers[0].layer == back);
}

TEST_CASE("BoardSpatialIndex uses the conservative bound when configured clearance is largest") {
    auto fixture = make_board_fixture();
    auto board = make_two_layer_board(fixture);
    const auto front = volt::BoardLayerId{0};
    board.set_design_rules(volt::BoardDesignRules{2.00, 0.05, 0.10, 0.20, 0.0});
    [[maybe_unused]] const auto obstacle = board.add_track(volt::BoardTrack{
        fixture.first_net, front,
        std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0}}, 0.10});

    const auto index = volt::BoardSpatialIndex{board, volt::builtin_footprint_library()};
    const auto result = index.query_legality(track_candidate(fixture.second_net, front, 2.95));

    REQUIRE_FALSE(result.legal);
    REQUIRE(result.blockers.size() == 1);
    CHECK(result.blockers[0].required_clearance_mm == 2.00);
    CHECK(result.blockers[0].actual_clearance_mm == 1.85);
}

TEST_CASE("BoardSpatialIndex includes derived net-class clearances in the conservative bound") {
    auto fixture = make_board_fixture();
    auto net_class = volt::NetClass{volt::NetClassName{"HV"}};
    net_class.derive_copper_clearance(volt::ipc2221_external_voltage_clearance_mm(600.0));
    fixture.circuit.assign_net_class(fixture.first_net,
                                     fixture.circuit.add_net_class(std::move(net_class)));

    auto board = make_two_layer_board(fixture);
    const auto front = volt::BoardLayerId{0};
    board.set_design_rules(volt::BoardDesignRules{0.10, 0.05, 0.10, 0.20, 0.0});
    [[maybe_unused]] const auto obstacle = board.add_track(volt::BoardTrack{
        fixture.first_net, front,
        std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0}}, 0.10});

    const auto index = volt::BoardSpatialIndex{board, volt::builtin_footprint_library()};
    const auto result = index.query_legality(track_candidate(fixture.second_net, front, 2.39));

    CHECK(index.conservative_clearance_mm() == Catch::Approx(1.30));
    REQUIRE_FALSE(result.legal);
    REQUIRE(result.blockers.size() == 1);
    CHECK(result.blockers[0].required_clearance_mm == Catch::Approx(1.30));
    CHECK(result.blockers[0].actual_clearance_mm == Catch::Approx(1.29));
}

TEST_CASE("BoardSpatialIndex rejects queries after the board clearance bound grows") {
    auto fixture = make_board_fixture();
    auto board = make_two_layer_board(fixture);
    const auto front = volt::BoardLayerId{0};
    board.set_design_rules(volt::BoardDesignRules{0.10, 0.05, 0.10, 0.20, 0.0});
    [[maybe_unused]] const auto obstacle = board.add_track(volt::BoardTrack{
        fixture.first_net, front,
        std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0}}, 0.10});

    const auto index = volt::BoardSpatialIndex{board, volt::builtin_footprint_library()};
    board.set_design_rules(volt::BoardDesignRules{0.50, 0.05, 0.10, 0.20, 0.0});

    CHECK_THROWS_AS(index.query_legality(track_candidate(fixture.second_net, front, 1.40)),
                    std::logic_error);
}

TEST_CASE("BoardSpatialIndex incremental insert is visible to subsequent queries") {
    auto fixture = make_board_fixture();
    auto board = make_two_layer_board(fixture);
    const auto front = volt::BoardLayerId{0};
    board.set_design_rules(volt::BoardDesignRules{0.30, 0.05, 0.10, 0.20, 0.0});

    auto index = volt::BoardSpatialIndex{board};
    CHECK(index.query_legality(track_candidate(fixture.second_net, front, 1.20)).legal);

    index.insert(track_candidate(fixture.first_net, front, 1.0));
    const auto result = index.query_legality(track_candidate(fixture.second_net, front, 1.20));

    REQUIRE_FALSE(result.legal);
    REQUIRE(result.blockers.size() == 1);
    CHECK(result.blockers[0].shape_index == 0U);
}

TEST_CASE("BoardSpatialIndex query output is deterministic across equivalent builds") {
    auto fixture = make_board_fixture();
    auto board = make_two_layer_board(fixture);
    const auto front = volt::BoardLayerId{0};
    board.set_design_rules(volt::BoardDesignRules{0.30, 0.05, 0.10, 0.20, 0.0});
    [[maybe_unused]] const auto first = board.add_track(volt::BoardTrack{
        fixture.first_net, front,
        std::vector{volt::BoardPoint{1.0, 1.0}, volt::BoardPoint{8.0, 1.0}}, 0.10});
    [[maybe_unused]] const auto second = board.add_track(volt::BoardTrack{
        fixture.third_net, front,
        std::vector{volt::BoardPoint{1.0, 1.4}, volt::BoardPoint{8.0, 1.4}}, 0.10});

    const auto first_index = volt::BoardSpatialIndex{board, volt::builtin_footprint_library()};
    const auto second_index = volt::BoardSpatialIndex{board, volt::builtin_footprint_library()};

    const auto first_result =
        first_index.query_legality(track_candidate(fixture.second_net, front, 1.20));
    const auto second_result =
        second_index.query_legality(track_candidate(fixture.second_net, front, 1.20));

    CHECK(first_result == second_result);
}

TEST_CASE("BoardSpatialIndex routing query rejects outline and keepout violations") {
    auto fixture = make_board_fixture();
    auto board = make_two_layer_board(fixture);
    const auto front = volt::BoardLayerId{0};
    auto rules = volt::BoardDesignRules{0.10, 0.05, 0.10, 0.20, 0.25};
    rules.set_clearance_mm(volt::BoardClearanceKind::Track, volt::BoardClearanceKind::BoardEdge,
                           0.50);
    board.set_design_rules(rules);
    const auto keepout = board.add_keepout(volt::BoardKeepout{
        std::vector{volt::BoardPoint{2.0, 2.0}, volt::BoardPoint{6.0, 2.0},
                    volt::BoardPoint{6.0, 6.0}, volt::BoardPoint{2.0, 6.0}},
        std::vector{front},
        std::vector{volt::BoardKeepoutRestriction::Copper, volt::BoardKeepoutRestriction::Via},
    });

    const auto index = volt::BoardSpatialIndex{board, volt::builtin_footprint_library()};
    const auto outline_result =
        index.query_legality(track_candidate(fixture.first_net, front, 0.20));
    const auto keepout_result =
        index.query_legality(track_candidate(fixture.first_net, front, 4.0));
    const auto via_keepout_result =
        index.query_legality(via_candidate(fixture.first_net, std::vector{front}, {4.0, 4.0}));

    REQUIRE_FALSE(outline_result.legal);
    REQUIRE(outline_result.blockers.size() == 1);
    CHECK(outline_result.blockers[0].kind == volt::BoardSpatialBlockerKind::BoardOutline);
    CHECK(outline_result.blockers[0].required_clearance_mm == 0.50);

    REQUIRE_FALSE(keepout_result.legal);
    REQUIRE(keepout_result.blockers.size() == 1);
    CHECK(keepout_result.blockers[0].kind == volt::BoardSpatialBlockerKind::Keepout);
    CHECK(keepout_result.blockers[0].keepout == keepout);

    REQUIRE_FALSE(via_keepout_result.legal);
    REQUIRE(via_keepout_result.blockers.size() == 1);
    CHECK(via_keepout_result.blockers[0].kind == volt::BoardSpatialBlockerKind::Keepout);
    CHECK(via_keepout_result.blockers[0].keepout == keepout);
}

TEST_CASE("BoardSpatialIndex DRC integration preserves brute-force clearance diagnostics") {
    auto fixture = make_board_fixture();
    auto board = make_two_layer_board(fixture);
    const auto front = volt::BoardLayerId{0};
    const auto back = volt::BoardLayerId{1};
    board.set_design_rules(volt::BoardDesignRules{0.22, 0.05, 0.10, 0.20, 0.0});

    for (std::size_t index = 0; index < 18U; ++index) {
        const auto y = 1.0 + (static_cast<double>(index) * 0.18);
        const auto net = index % 3U == 0U   ? fixture.first_net
                         : index % 3U == 1U ? fixture.second_net
                                            : fixture.third_net;
        const auto layer = index % 2U == 0U ? front : back;
        [[maybe_unused]] const auto track = board.add_track(volt::BoardTrack{
            net, layer, std::vector{volt::BoardPoint{1.0, y}, volt::BoardPoint{12.0, y}}, 0.10});
    }
    [[maybe_unused]] const auto first_via = board.add_via(
        volt::BoardVia{fixture.first_net, volt::BoardPoint{6.0, 2.4}, front, back, 0.10, 0.40});
    [[maybe_unused]] const auto second_via = board.add_via(
        volt::BoardVia{fixture.second_net, volt::BoardPoint{6.2, 2.6}, front, back, 0.10, 0.40});

    CHECK(validated_clearance_signatures(board) == brute_force_clearance_signatures(board));
}
