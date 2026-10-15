#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/resolved_board_test_parts.hpp"
#include "support/route_report_snapshot.hpp"
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/updates.hpp>
#include <volt/core/errors.hpp>
#include <volt/io/pcb/pcb_writer.hpp>
#include <volt/pcb/queries/board_queries.hpp>
#include <volt/pcb/routing/board_router.hpp>

#include <limits>

namespace {
using namespace volt;

struct Fixture {
    Circuit circuit;
    NetId net = circuit.add_net(NetSpec{NetName{"SIG"}, NetKind::Signal});
    NetId other = circuit.add_net(NetSpec{NetName{"OTHER"}, NetKind::Signal});
    Board board{circuit};
    BoardLayerId front =
        board.add_layer(BoardLayer{"F.Cu", BoardLayerRole::Copper, BoardLayerSide::Top});
    BoardLayerId inner =
        board.add_layer(BoardLayer{"In.Cu", BoardLayerRole::Copper, BoardLayerSide::Inner});
    BoardLayerId back =
        board.add_layer(BoardLayer{"B.Cu", BoardLayerRole::Copper, BoardLayerSide::Bottom});
    FootprintLibrary footprints = builtin_footprint_library();
    test::ResolvedBoardTestParts parts;

    Fixture() {
        board.set_layer_stack(LayerStack{{front, inner, back}, 1.6});
        board.set_design_rules(BoardDesignRules{0.2, 0.2, 0.3, 0.7, 0.25});
    }

    ResolvedBoardView view() const { return parts.view(board, footprints); }

    RouteSpec spec(BoardPoint a = {10, 10}, BoardPoint b = {20, 10},
                   CornerStyle style = CornerStyle::Chamfer45) const {
        return {&board,
                RouteNetRef{&circuit, net},
                RoutePoint{a},
                {&board, front},
                {RouteTo{RoutePoint{b}, style}}};
    }

    std::string bytes() const { return io::write_pcb_board(view()); }

    RouteResult report(const RouteSpec &s) {
        return BoardRouter{board, view()}.route(s, RoutePolicy::Report);
    }

    ComponentPlacementId pad_component(bool connected = true,
                                       FootprintPadShape shape = FootprintPadShape::Rectangle,
                                       double angle = 90, BoardPoint position = {10, 10},
                                       BoardSide side = BoardSide::Top) {
        const auto def = circuit.define_component(ComponentSpec{
            .name = "Resistor",
            .pins = {PinSpec{.name = "A", .number = "1"}, PinSpec{.name = "B", .number = "2"}}});
        const auto c = circuit.instantiate_component(
            def, ComponentInstanceSpec{.reference = ReferenceDesignator{"R1"}});
        const auto &pins = circuit.get(def).pins();
        const auto original = passive_0603_footprint();
        auto pads = std::vector<FootprintPad>{};
        for (const auto &pad : original.pads())
            pads.push_back(FootprintPad::surface_mount(
                pad.label(), shape, pad.position(),
                shape == FootprintPadShape::Circle ? FootprintSize{0.8, 0.8} : pad.size(),
                pad.layers()));
        const auto exact =
            FootprintDefinition{FootprintRef{"test", "target"}, pads, original.package_geometry()};
        footprints.add(exact);
        parts.set(circuit, c,
                  PhysicalPart{ManufacturerPart{"Test", "R"},
                               PackageRef{"0603"},
                               exact.ref(),
                               {{pins[0], "1"}, {pins[1], "2"}}});
        if (connected)
            circuit.connect(net, queries::pin_by_definition(circuit, c, pins[0]).value());
        circuit.connect(net, queries::pin_by_definition(circuit, c, pins[1]).value());
        return board.place_component(
            ComponentPlacement{c, position, BoardRotation::degrees(angle), side});
    }

    RoutePad pad(ComponentPlacementId placement, std::size_t index = 0) const {
        const auto closure = view();
        const auto resolution = resolve_footprint(
            closure.part(board.get(placement).component())->physical_part(), closure.footprints());
        return {&board, placement, *resolution.definition(), FootprintPadId{index}};
    }

    NetClassId klass(NetClass c) {
        const auto id = circuit.define_net_class(NetClassSpec{.net_class = std::move(c)});
        circuit.update(net, AssignNetClass{id});
        return id;
    }
};

RouteTrackCopper obstacle(Fixture &f, double y, NetId net, double width = 0.2) {
    return {&f.board, f.board.add_track(BoardTrack{net, f.front, {{5, y}, {25, y}}, width})};
}

RouteSpec gap_spec(Fixture &f, const RouteOperand &lower, const RouteOperand &upper,
                   BoardPoint start = {15, 14}, RouteAxis axis = RouteAxis::Y) {
    auto spec = f.spec(start);
    spec.steps = {RouteGap{lower, upper, axis}};
    return spec;
}

RouteResult query(Fixture &f, const RouteSpec &spec) {
    const auto bytes = f.bytes();
    const auto tracks = f.board.all<BoardTrackId>();
    const auto vias = f.board.all<BoardViaId>();
    const auto *first = tracks.size() == 0 ? nullptr : &f.board.get(BoardTrackId{0});
    const auto result = BoardRouter{f.board, f.view()}.query_gap(spec);
    CHECK(f.bytes() == bytes);
    CHECK(tracks.is_current());
    CHECK(vias.is_current());
    if (first)
        CHECK(&f.board.get(BoardTrackId{0}) == first);
    CHECK_FALSE(result.committed);
    CHECK(result.tracks.size() == 0);
    CHECK(result.vias.empty());
    CHECK(result.committed_order.empty());
    return result;
}
} // namespace

TEST_CASE("native symbolic gap query and route share final coordinates and provenance",
          "[pcb][route-target]") {
    Fixture f;
    const auto lower = obstacle(f, 10, f.other);
    const auto upper = obstacle(f, 20, f.other);
    auto spec = gap_spec(f, lower, upper);
    const auto q = query(f, spec);
    REQUIRE_FALSE(q.failure);
    REQUIRE(q.target_resolutions.size() == 1);
    const auto &t = q.target_resolutions.front();
    CHECK(t.position == BoardPoint{15, 15});
    CHECK(t.raw_lower_mm == Catch::Approx(10.1));
    CHECK(t.raw_upper_mm == Catch::Approx(19.9));
    CHECK(t.lower_mm == Catch::Approx(10.4));
    CHECK(t.upper_mm == Catch::Approx(19.6));
    REQUIRE(t.constraints.size() == 2);
    CHECK_FALSE(t.constraints[0].upper_side);
    CHECK(t.constraints[1].upper_side);
    CHECK(t.constraints[0].clearance.value_mm == 0.2);
    CHECK_FALSE(t.constraints[0].exempt);
    CHECK(q.message == "route SIG gap prefix feasible; board unchanged");
    spec.steps.push_back(RouteTo{RoutePoint{t.position}});
    const auto routed = f.report(spec);
    REQUIRE(routed.committed);
    CHECK(test::route_snapshot(routed)["target_resolutions"] ==
          test::route_snapshot(q)["target_resolutions"]);
    CHECK(test::route_snapshot(routed)["candidates"] == test::route_snapshot(q)["candidates"]);
    CHECK(test::route_snapshot(routed)["clearance_checks"] ==
          test::route_snapshot(q)["clearance_checks"]);
}

TEST_CASE("gap exact fit and one micron short keep both side rules", "[pcb][route-target]") {
    for (const auto shortfall : {0.0, 0.001}) {
        Fixture f;
        const auto a = obstacle(f, 10, f.net);
        const auto b = obstacle(f, 10.6 - shortfall, f.other);
        auto spec = gap_spec(f, a, b, {15, 10});
        const auto result = query(f, spec);
        if (shortfall == 0) {
            REQUIRE_FALSE(result.failure);
            REQUIRE(result.target_resolutions.size() == 1);
            const auto &t = result.target_resolutions.front();
            CHECK(t.position.y_mm() == Catch::Approx(10.2));
            CHECK(t.lower_mm == Catch::Approx(t.upper_mm));
            CHECK(t.constraints[0].exempt);
            CHECK(t.constraints[0].clearance.sources.empty());
            CHECK(t.constraints[1].clearance.value_mm == 0.2);
        } else {
            REQUIRE(result.failure);
            CHECK(result.failure->kind == RouteFailureKind::RuleUnresolvable);
            CHECK_FALSE(result.candidate_complete);
            CHECK(result.target_resolutions.empty());
        }
    }
    Fixture f;
    const auto a = obstacle(f, 10, f.other);
    const auto b = obstacle(f, 10.8, f.other);
    const auto result = query(f, gap_spec(f, a, b, {15, 10.35}));
    REQUIRE(result.target_resolutions.size() == 1);
    CHECK(result.target_resolutions[0].position.y_mm() == Catch::Approx(10.4));
    // The gap fits, but the connecting axis leg starts outside its only legal centre.
    REQUIRE(result.failure);
    CHECK(result.failure->kind == RouteFailureKind::Blocked);
    CHECK(result.candidate_complete);
}

TEST_CASE("target widths classes and final geometry room rules recalculate",
          "[pcb][route-target]") {
    Fixture f;
    const auto a = obstacle(f, 10, f.other);
    const auto b = obstacle(f, 20, f.net);
    auto spec = gap_spec(f, a, b);
    const auto first = query(f, spec);
    CHECK(first.target_resolutions[0].position.y_mm() == Catch::Approx(15.1));
    auto klass = NetClass{NetClassName{"wide"}};
    klass.set_track_width_mm(0.6);
    klass.set_copper_clearance_mm(0.5);
    const auto cls = f.klass(std::move(klass));
    const auto wide = query(f, spec);
    REQUIRE_FALSE(wide.failure);
    CHECK(wide.target_resolutions[0].position.y_mm() == Catch::Approx(15.25));
    CHECK(wide.target_resolutions[0].width.value_mm == 0.6);
    CHECK(wide.target_resolutions[0].constraints[0].clearance.sources[0].net_class == cls);
    auto room = BoardRoom{"local", BoardOutline::rectangle({0, 0}, {30, 30}), {f.front}, 2};
    room.set_track_width_mm(0.8);
    room.set_copper_clearance_mm(0.05);
    const auto room_id = f.board.add_room(room);
    const auto local = query(f, spec);
    REQUIRE_FALSE(local.failure);
    CHECK(local.target_resolutions[0].position.y_mm() == Catch::Approx(15.025));
    CHECK(local.target_resolutions[0].width.value_mm == 0.8);
    CHECK(local.target_resolutions[0].constraints[0].clearance.sources[0].room == room_id);
    spec.width_mm = 0.1;
    const auto under = query(f, spec);
    REQUIRE(under.failure);
    CHECK(under.failure->kind == RouteFailureKind::RuleViolation);
    CHECK(under.candidate_complete);
    CHECK(under.target_resolutions[0].width.requested_mm == 0.1);
}

TEST_CASE("finite target rule resolution rejects inconsistent final containment",
          "[pcb][route-target]") {
    Fixture f;
    const auto a = obstacle(f, 10, f.net);
    auto room = BoardRoom{"width", BoardOutline::rectangle({0, 9}, {30, 1.35}), {f.front}};
    room.set_track_width_mm(0.4);
    static_cast<void>(f.board.add_room(room));
    auto spec = f.spec({15, 10});
    spec.steps.insert(spec.steps.begin(), RouteBeside{a, RouteDirection::PositiveY});
    const auto result = f.report(spec);
    REQUIRE(result.failure);
    CHECK(result.failure->kind == RouteFailureKind::RuleUnresolvable);
    CHECK(result.target_resolutions.empty());
    CHECK_FALSE(result.candidate_complete);
}

TEST_CASE("symbolic sides exit containment and negative board axes are explicit",
          "[pcb][route-target]") {
    Fixture f;
    const auto a = obstacle(f, -10, f.net);
    auto spec = f.spec({15, -10}, {15, -15});
    spec.steps.insert(spec.steps.begin(), RouteExit{a, RouteDirection::NegativeY, 0.25});
    const auto result = f.report(spec);
    REQUIRE(result.committed);
    CHECK(result.target_resolutions[0].position == BoardPoint{15, -10.45});
    CHECK(result.target_resolutions[0].constraints[0].upper_side);
    spec.start = RoutePoint{{15, -9}};
    const auto failure = f.report(spec);
    REQUIRE(failure.failure);
    CHECK(failure.failure->kind == RouteFailureKind::GeometryUnresolvable);
    spec.steps[0] = RouteBeside{a, RouteDirection::NegativeY, 0.25};
    CHECK(f.report(spec).committed);
}

TEST_CASE("keepout and edge operands retain mechanical constraints and interior",
          "[pcb][route-target]") {
    Fixture f;
    f.board.set_outline(BoardOutline::rectangle({0, 0}, {30, 30}));
    const auto k = f.board.add_keepout(BoardKeepout{
        {{0, 10}, {30, 10}, {30, 11}, {0, 11}}, {f.front}, {BoardKeepoutRestriction::Copper}});
    auto spec = gap_spec(f, RouteKeepout{&f.board, k}, RouteBoardEdge{&f.board, 2}, {15, 14});
    const auto result = query(f, spec);
    REQUIRE_FALSE(result.failure);
    const auto &t = result.target_resolutions[0];
    CHECK(t.lower_mm == Catch::Approx(11.1 + 2 * detail::board_drc_epsilon));
    CHECK(t.upper_mm == Catch::Approx(29.65));
    CHECK(t.constraints[0].clearance.sources[0].kind == RouteRuleSourceKind::Keepout);
    CHECK_FALSE(t.constraints[0].exempt);
    CHECK(t.constraints[1].clearance.sources[0].second_kind == BoardClearanceKind::BoardEdge);
    auto edge_spec = f.spec({15, 25}, {15, 28});
    edge_spec.steps.insert(edge_spec.steps.begin(),
                           RouteBeside{RouteBoardEdge{&f.board, 2}, RouteDirection::NegativeY});
    CHECK(f.report(edge_spec).committed);
    edge_spec.steps[0] = RouteBeside{RouteBoardEdge{&f.board, 2}, RouteDirection::PositiveY};
    CHECK(f.report(edge_spec).failure->kind == RouteFailureKind::GeometryUnresolvable);
    static_cast<void>(f.board.add_feature(BoardFeature::hole("MH", {15, 20}, 3)));
    const auto blocked = query(f, spec);
    REQUIRE(blocked.failure);
    CHECK(blocked.failure->kind == RouteFailureKind::Blocked);
    CHECK(std::any_of(blocked.blockers.begin(), blocked.blockers.end(), [](const auto &v) {
        return v.kind == BoardSpatialBlockerKind::MechanicalOpening;
    }));
}

TEST_CASE("angled track projection and via copper span are native", "[pcb][route-target]") {
    Fixture f;
    const auto track =
        f.board.add_track(BoardTrack{f.net, f.front, {{10, 10}, {12, 13}, {14, 11}}, 0.4});
    const auto via = f.board.add_via(BoardVia{f.net, {20, 10}, f.front, f.back, 0.3, 0.8});
    auto spec = f.spec({14, 11}, {18, 11});
    spec.steps.insert(spec.steps.begin(),
                      RouteBeside{RouteTrackCopper{&f.board, track}, RouteDirection::PositiveX});
    const auto result = f.report(spec);
    REQUIRE(result.committed);
    CHECK(result.target_resolutions[0].position.x_mm() == Catch::Approx(14.3));
    spec.steps[0] = RouteBeside{RouteViaRef{&f.board, via}, RouteDirection::NegativeX};
    const auto vr = f.report(spec);
    REQUIRE(vr.committed);
    CHECK(vr.target_resolutions[0].position.x_mm() == Catch::Approx(19.5));
    spec.layer = {&f.board, f.inner};
    CHECK(f.report(spec).target_resolutions.size() == 1);
}

TEST_CASE("rotated placed copper uses exact active pads and mixed nets", "[pcb][route-target]") {
    Fixture f;
    const auto placement = f.pad_component();
    const auto identity = f.pad(placement);
    const auto placed = RoutePlacedCopper{&f.board, placement, identity.footprint};
    auto spec = f.spec({10, 10}, {15, 10});
    spec.steps.insert(spec.steps.begin(), RouteBeside{placed, RouteDirection::PositiveX});
    const auto result = f.report(spec);
    REQUIRE(result.committed);
    CHECK(result.target_resolutions[0].position.x_mm() == Catch::Approx(10.575));
    REQUIRE(result.target_resolutions[0].constraints.size() == 2);
    const auto component = f.board.get(placement).component();
    const auto pin = queries::pin_by_name(f.circuit, component, "B").value();
    f.circuit.disconnect(pin);
    f.circuit.connect(f.other, pin);
    const auto mixed = f.report(spec);
    REQUIRE(mixed.target_resolutions.size() == 1);
    CHECK(mixed.target_resolutions[0].position.x_mm() == Catch::Approx(10.775));
    CHECK(mixed.target_resolutions[0].constraints[0].exempt);
    CHECK_FALSE(mixed.target_resolutions[0].constraints[1].exempt);
    spec.layer = {&f.board, f.back};
    const auto inactive = f.report(spec);
    REQUIRE(inactive.failure);
    CHECK(inactive.failure->kind == RouteFailureKind::GeometryUnresolvable);
}

TEST_CASE("symbolic grammar owner references and unsupported degeneracy reject atomically",
          "[pcb][route-target]") {
    Fixture f;
    Fixture other;
    const auto a = obstacle(f, 10, f.other);
    const auto b = obstacle(f, 20, f.other);
    auto spec = gap_spec(f, a, b);
    const auto before = f.bytes();
    spec.steps[0] = RouteGap{a, RouteTrackCopper{&other.board, BoardTrackId{0}}, RouteAxis::Y};
    CHECK_THROWS_AS(query(f, spec), KernelArgumentError);
    spec.steps[0] = RouteGap{a, RouteTrackCopper{&f.board, BoardTrackId{100}}, RouteAxis::Y};
    CHECK_THROWS_AS(query(f, spec), KernelRangeError);
    spec.steps[0] = RouteGap{b, a, RouteAxis::Y};
    CHECK(query(f, spec).failure->kind == RouteFailureKind::GeometryUnresolvable);
    spec.steps[0] = RouteGap{a, b, RouteAxis::Y};
    spec.start = RoutePoint{{15, 15}};
    CHECK(query(f, spec).failure->kind == RouteFailureKind::GeometryUnresolvable);
    spec.net.reset();
    CHECK_THROWS_AS(query(f, spec), KernelArgumentError);
    CHECK(f.bytes() == before);
    const auto placement = f.pad_component(false);
    auto route = f.spec({10, 10});
    route.steps.insert(route.steps.begin(),
                       RouteBeside{f.pad(placement), RouteDirection::PositiveX});
    CHECK(f.report(route).failure->kind == RouteFailureKind::GeometryUnresolvable);
}

TEST_CASE("target failure after earlier candidate copper retains chronology and no publication",
          "[pcb][route-target]") {
    Fixture f;
    const auto a = obstacle(f, 10, f.net);
    const auto b = obstacle(f, 10.3, f.other);
    auto spec = f.spec({15, 5});
    spec.steps = {RouteThrough{RoutePoint{{15, 8}}}, RouteVia{{&f.board, f.back}},
                  RouteVia{{&f.board, f.front}}, RouteGap{a, b, RouteAxis::Y},
                  RouteTo{RoutePoint{{20, 15}}}};
    const auto before = f.bytes();
    const auto report = f.report(spec);
    REQUIRE(report.failure);
    CHECK(report.failure->step_index == 3);
    CHECK(report.candidates.size() == 3);
    CHECK_FALSE(report.candidate_complete);
    CHECK(f.bytes() == before);
    try {
        static_cast<void>(BoardRouter{f.board, f.view()}.route(spec));
        FAIL("Strict must throw");
    } catch (const RouteRejected &e) {
        CHECK(test::route_snapshot(e.result()) == test::route_snapshot(report));
    }
    auto klass = NetClass{NetClassName{"back-only"}};
    klass.set_allowed_layer_names({"B.Cu"});
    static_cast<void>(f.klass(std::move(klass)));
    const auto start = f.report(spec);
    REQUIRE(start.failure);
    CHECK_FALSE(start.failure->step_index);
    CHECK(start.failure->kind == RouteFailureKind::LayerNotAllowed);
}

TEST_CASE("symbolic pad projection follows placement rotation and board side",
          "[pcb][route-target]") {
    for (const auto shape : {FootprintPadShape::Rectangle, FootprintPadShape::RoundedRectangle}) {
        for (const auto angle : {0.0, 45.0, 90.0, 180.0}) {
            for (const auto side : {BoardSide::Top, BoardSide::Bottom}) {
                Fixture f;
                const auto placement = f.pad_component(true, shape, angle, {12, 13}, side);
                const auto p = f.pad(placement, 1);
                auto spec = f.spec({12, 13}, {18, 13});
                spec.layer = {&f.board, side == BoardSide::Top ? f.front : f.back};
                spec.steps.insert(spec.steps.begin(),
                                  RouteBeside{p, RouteDirection::PositiveX, 0.3});
                const auto result = f.report(spec);
                REQUIRE(result.target_resolutions.size() == 1);
                const auto corners = detail::transformed_pad_body_corners(f.board.get(placement),
                                                                          p.footprint.pad(p.pad));
                const auto high =
                    std::max_element(corners.begin(), corners.end(),
                                     [](const auto a, const auto b) { return a.x_mm() < b.x_mm(); })
                        ->x_mm();
                CHECK(result.target_resolutions[0].position.x_mm() == Catch::Approx(high + 0.4));
                CHECK(result.target_resolutions[0].position.y_mm() == 13);
            }
        }
    }
    Fixture f;
    const auto placement = f.pad_component(true, FootprintPadShape::Circle, 45);
    auto spec = f.spec({10, 10});
    spec.steps.insert(spec.steps.begin(), RouteBeside{f.pad(placement), RouteDirection::PositiveX});
    const auto result = f.report(spec);
    REQUIRE(result.target_resolutions.size() == 1);
    const auto centre = detail::transform_footprint_point(
        f.board.get(placement), f.pad(placement).footprint.pad(FootprintPadId{0}).position());
    CHECK(result.target_resolutions[0].position.x_mm() == Catch::Approx(centre.x_mm() + 0.5));
}

TEST_CASE("unsupported pad models and missing physical closure do not fabricate extents",
          "[pcb][route-target]") {
    for (const auto shape : {FootprintPadShape::Oval}) {
        Fixture f;
        const auto placement = f.pad_component(true, shape);
        auto spec = f.spec();
        spec.steps.insert(spec.steps.begin(),
                          RouteBeside{f.pad(placement), RouteDirection::PositiveX});
        const auto result = f.report(spec);
        REQUIRE(result.failure);
        CHECK(result.failure->kind == RouteFailureKind::GeometryUnresolvable);
        CHECK(result.target_resolutions.empty());
    }
    Fixture f;
    const auto placement = f.pad_component();
    auto spec = f.spec();
    spec.steps.insert(spec.steps.begin(), RouteBeside{f.pad(placement), RouteDirection::PositiveX});
    const auto missing = ResolvedBoardView::test_only(f.board, f.footprints, {});
    const auto before = f.bytes();
    const auto result = BoardRouter{f.board, missing}.route(spec, RoutePolicy::Report);
    REQUIRE(result.failure);
    CHECK(result.failure->kind == RouteFailureKind::GeometryUnresolvable);
    CHECK(f.bytes() == before);
    auto foreign = f.pad(placement);
    foreign.footprint = passive_0603_footprint();
    spec.steps[0] = RouteBeside{foreign, RouteDirection::PositiveX};
    CHECK_THROWS_AS(f.report(spec), KernelArgumentError);
}

TEST_CASE("ambiguous room clearance regimes and overflow reject without guessed coordinates",
          "[pcb][route-target]") {
    Fixture f;
    const auto a = obstacle(f, 10, f.other);
    auto room = BoardRoom{"local", BoardOutline::rectangle({0, 9}, {30, 1.4}), {f.front}};
    room.set_copper_clearance_mm(0.05);
    static_cast<void>(f.board.add_room(room));
    auto spec = f.spec({15, 10.15});
    spec.steps.insert(spec.steps.begin(), RouteBeside{a, RouteDirection::PositiveY});
    const auto result = f.report(spec);
    REQUIRE(result.failure);
    CHECK(result.failure->kind == RouteFailureKind::GeometryUnresolvable);
    CHECK(result.failure->message == "target has multiple self-consistent native rule solutions");
    CHECK(result.target_resolutions.empty());
    const auto max = std::numeric_limits<double>::max();
    const auto far = f.board.add_via(BoardVia{f.net, {max, 10}, f.front, f.back, 0.3, max});
    spec.steps[0] = RouteBeside{RouteViaRef{&f.board, far}, RouteDirection::PositiveX};
    CHECK(f.report(spec).failure->kind == RouteFailureKind::GeometryUnresolvable);
}

TEST_CASE("mechanical operand applicability and unsupported outline outcomes are closed",
          "[pcb][route-target]") {
    Fixture f;
    auto spec = f.spec();
    spec.steps.insert(spec.steps.begin(),
                      RouteBeside{RouteBoardEdge{&f.board, 0}, RouteDirection::PositiveY});
    CHECK(f.report(spec).failure->kind == RouteFailureKind::GeometryUnresolvable);
    f.board.set_outline(BoardOutline::rectangle({0, 0}, {30, 30}));
    spec.steps[0] = RouteBeside{RouteBoardEdge{&f.board, 100}, RouteDirection::PositiveY};
    CHECK_THROWS_AS(f.report(spec), KernelRangeError);
    spec.steps[0] = RouteBeside{RouteBoardEdge{&f.board, 0}, RouteDirection::PositiveX};
    CHECK(f.report(spec).failure->kind == RouteFailureKind::GeometryUnresolvable);
    spec.steps[0] = RouteExit{RouteBoardEdge{&f.board, 0}, RouteDirection::PositiveY};
    CHECK(f.report(spec).failure->kind == RouteFailureKind::GeometryUnresolvable);
    const auto k = f.board.add_keepout(BoardKeepout{
        {{5, 5}, {8, 5}, {8, 8}, {5, 8}}, {f.back}, {BoardKeepoutRestriction::Placement}});
    spec.steps[0] = RouteBeside{RouteKeepout{&f.board, k}, RouteDirection::PositiveX};
    CHECK(f.report(spec).failure->kind == RouteFailureKind::GeometryUnresolvable);
    f.board.set_outline(BoardOutline{{{0, 0}, {30, 0}, {15, 30}}});
    spec.steps[0] = RouteBeside{RouteBoardEdge{&f.board, 0}, RouteDirection::PositiveY};
    CHECK(f.report(spec).failure->kind == RouteFailureKind::GeometryUnresolvable);
}

TEST_CASE("gap prefix parity includes earlier candidates layers and mechanical blockers",
          "[pcb][route-target]") {
    Fixture f;
    const auto a = f.board.add_track(BoardTrack{f.other, f.back, {{5, 10}, {25, 10}}, 0.2});
    const auto b = f.board.add_track(BoardTrack{f.other, f.back, {{5, 20}, {25, 20}}, 0.2});
    auto prefix = f.spec({15, 12});
    prefix.steps = {
        RouteThrough{RoutePoint{{15, 13}}}, RouteVia{{&f.board, f.back}},
        RouteGap{RouteTrackCopper{&f.board, a}, RouteTrackCopper{&f.board, b}, RouteAxis::Y}};
    const auto q = query(f, prefix);
    REQUIRE_FALSE(q.failure);
    REQUIRE(q.candidates.size() == 3);
    CHECK(q.target_resolutions[0].step_index == 2);
    CHECK(q.resolved_steps[2].incoming_layer == f.back);
    auto route = prefix;
    route.steps.push_back(RouteTo{RoutePoint{q.target_resolutions[0].position}});
    const auto committed = f.report(route);
    REQUIRE(committed.committed);
    CHECK(test::route_snapshot(committed)["target_resolutions"] ==
          test::route_snapshot(q)["target_resolutions"]);
    CHECK(test::route_snapshot(committed)["candidates"] == test::route_snapshot(q)["candidates"]);
    static_cast<void>(f.board.add_feature(BoardFeature::hole("MH", {15, 12.5}, 0.5)));
    const auto rejected_q = query(f, prefix);
    REQUIRE(rejected_q.failure);
    CHECK(rejected_q.failure->step_index == 0);
    const auto rejected_route = f.report(route);
    CHECK(test::route_snapshot(rejected_route)["blockers"] ==
          test::route_snapshot(rejected_q)["blockers"]);
    CHECK(test::route_snapshot(rejected_route)["failure"] ==
          test::route_snapshot(rejected_q)["failure"]);
}

TEST_CASE("gap room priority ID ties and explicit class replacement use native semantics",
          "[pcb][route-target]") {
    Fixture f;
    const auto a = obstacle(f, 10, f.other);
    const auto b = obstacle(f, 20, f.net);
    auto klass = NetClass{NetClassName{"wide"}};
    klass.set_track_width_mm(0.6);
    klass.set_copper_clearance_mm(0.5);
    static_cast<void>(f.klass(std::move(klass)));
    std::optional<BoardRoomId> controlling;
    for (const auto clearance : {0.08, 0.04, 0.02}) {
        auto room = BoardRoom{"room-" + std::to_string(clearance),
                              BoardOutline::rectangle({0, 0}, {30, 30}),
                              {f.front},
                              clearance == 0.08 ? 2 : 4};
        room.set_track_width_mm(0.2);
        room.set_copper_clearance_mm(clearance);
        const auto id = f.board.add_room(room);
        if (clearance == 0.04)
            controlling = id;
    }
    auto spec = gap_spec(f, a, b);
    spec.width_mm = 0.2;
    const auto result = query(f, spec);
    REQUIRE_FALSE(result.failure);
    const auto &t = result.target_resolutions[0];
    CHECK(t.position.y_mm() == Catch::Approx(15.02));
    CHECK(t.width.default_mm == 0.6);
    CHECK(t.width.value_mm == 0.2);
    CHECK(t.constraints[0].clearance.value_mm == 0.04);
    CHECK(t.constraints[0].clearance.sources[0].room == controlling);
    CHECK(std::any_of(t.width.enforced_sources.begin(), t.width.enforced_sources.end(),
                      [&](const auto &source) { return source.room == controlling; }));
}

TEST_CASE("gap continuity group exemptions keep mechanical constraints independent",
          "[pcb][route-target]") {
    Fixture f;
    const auto module = f.circuit.define_module(
        ModuleSpec{.name = ModuleName{"Child"},
                   .template_nets = {TemplateNetDefinition{NetName{"N"}, NetKind::Signal}},
                   .ports = {ModulePortSpec{PortName{"N"}, NetName{"N"}, PortRole::Passive}}});
    const auto instance =
        f.circuit.instantiate_module(module, ModuleInstanceSpec{.name = ModuleInstanceName{"M"}});
    const auto internal =
        queries::concrete_net_for(f.circuit, instance, f.circuit.get(module).template_nets()[0])
            .value();
    const auto a = obstacle(f, 10, internal);
    const auto b = obstacle(f, 20, f.other);
    const auto spec = gap_spec(f, a, b);
    const auto separate = query(f, spec);
    CHECK_FALSE(separate.target_resolutions[0].constraints[0].exempt);
    static_cast<void>(f.circuit.bind_port(instance, f.circuit.get(module).ports()[0], f.net));
    const auto grouped = query(f, spec);
    REQUIRE_FALSE(grouped.failure);
    CHECK(grouped.target_resolutions[0].constraints[0].exempt);
    CHECK(grouped.target_resolutions[0].position.y_mm() == Catch::Approx(14.9));
    static_cast<void>(f.board.add_feature(BoardFeature::hole("MH", {15, 14.9}, 0.5)));
    const auto mechanical = query(f, spec);
    REQUIRE(mechanical.failure);
    CHECK(mechanical.failure->kind == RouteFailureKind::Blocked);
    CHECK(mechanical.target_resolutions[0].constraints[0].exempt);
    CHECK(std::any_of(mechanical.blockers.begin(), mechanical.blockers.end(),
                      [](const auto &blocker) {
                          return blocker.kind == BoardSpatialBlockerKind::MechanicalOpening;
                      }));
}

TEST_CASE("rounded pad gap preserves complete query route evidence and conservative rejection",
          "[pcb][route-target][session]") {
    for (const auto narrow : {false, true}) {
        Fixture f;
        const auto placement = f.pad_component(true, FootprintPadShape::RoundedRectangle, 0);
        const auto pad = f.pad(placement, 1);
        const auto upper = obstacle(f, narrow ? 10.6 : 20.0, f.other);
        auto spec = gap_spec(f, pad, upper, {14, narrow ? 10.49 : 14.0});
        const auto queried = query(f, spec);
        if (narrow) {
            REQUIRE(queried.failure);
            CHECK(queried.failure->kind == RouteFailureKind::RuleUnresolvable);
            CHECK(!queried.candidate_complete);
            // Rounded corners never create capacity beyond the rectangular DRC envelope.
            spec.steps.push_back(RouteFinish{});
            CHECK(test::route_snapshot(f.report(spec)) == test::route_snapshot(queried));
        } else {
            REQUIRE(!queried.failure);
            REQUIRE(queried.target_resolutions.size() == 1);
            spec.steps.push_back(RouteTo{RoutePoint{queried.target_resolutions[0].position}});
            const auto routed = f.report(spec);
            REQUIRE(routed.committed);
            auto actual = test::route_snapshot(routed);
            auto expected = test::route_snapshot(queried);
            // Only terminal publication/status and its zero-length final step differ.
            actual["committed"] = false;
            actual["message"] = expected["message"];
            actual["tracks"] = expected["tracks"];
            actual["vias"] = expected["vias"];
            actual["committed_order"] = expected["committed_order"];
            actual["resolved_steps"].erase(actual["resolved_steps"].end() - 1);
            CHECK(actual == expected);
        }
    }
}
