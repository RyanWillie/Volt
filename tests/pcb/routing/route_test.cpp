#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/resolved_board_test_parts.hpp"
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

    ComponentPlacementId pad_component(bool connected = true) {
        const auto def = circuit.define_component(ComponentSpec{
            .name = "Resistor",
            .pins = {PinSpec{.name = "A", .number = "1"}, PinSpec{.name = "B", .number = "2"}}});
        const auto c = circuit.instantiate_component(
            def, ComponentInstanceSpec{.reference = ReferenceDesignator{"R1"}});
        const auto &pins = circuit.get(def).pins();
        parts.set(circuit, c,
                  PhysicalPart{ManufacturerPart{"Test", "R"},
                               PackageRef{"0603"},
                               passive_0603_footprint().ref(),
                               {{pins[0], "1"}, {pins[1], "2"}}});
        if (connected)
            circuit.connect(net, queries::pin_by_definition(circuit, c, pins[0]).value());
        circuit.connect(net, queries::pin_by_definition(circuit, c, pins[1]).value());
        return board.place_component(ComponentPlacement{c, {10, 10}, BoardRotation::degrees(90)});
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

void unchanged_failure(Fixture &f, const RouteSpec &spec, RouteFailureKind kind, bool complete) {
    const auto bytes = f.bytes();
    const auto range = f.board.all<BoardTrackId>();
    const auto report = f.report(spec);
    REQUIRE(!report.committed);
    REQUIRE(report.failure.has_value());
    CHECK(report.failure->kind == kind);
    CHECK(report.candidate_complete == complete);
    CHECK(report.tracks.empty());
    CHECK(report.vias.empty());
    CHECK(report.committed_order.empty());
    CHECK(f.bytes() == bytes);
    CHECK(range.is_current());
    try {
        static_cast<void>(BoardRouter{f.board, f.view()}.route(spec));
        FAIL("Strict must throw native RouteRejected");
    } catch (const RouteRejected &error) {
        CHECK(error.what() == report.message);
        CHECK(error.result().message == report.message);
        CHECK(error.result().failure->kind == kind);
        CHECK(error.result().blockers.size() == report.blockers.size());
    }
    CHECK(f.bytes() == bytes);
    CHECK(range.is_current());
}
} // namespace

TEST_CASE("R1a golden corners preserve exact geometry in all quadrants", "[pcb][route]") {
    using namespace volt;
    for (const auto sx : {-1.0, 1.0})
        for (const auto sy : {-1.0, 1.0}) {
            for (const auto dominant_x : {false, true}) {
                Fixture f;
                const auto end =
                    BoardPoint{sx * (dominant_x ? 9.0 : 3.0), sy * (dominant_x ? 3.0 : 9.0)};
                const auto result = f.report(f.spec({0, 0}, end));
                REQUIRE(result.committed);
                REQUIRE(result.candidates.size() == 2);
                const auto bend = BoardPoint{dominant_x ? sx * 6 : 0, dominant_x ? 0 : sy * 6};
                CHECK(std::get<BoardTrack>(result.candidates[0].copper).points() ==
                      std::vector<BoardPoint>{{0, 0}, bend});
                CHECK(std::get<BoardTrack>(result.candidates[1].copper).points() ==
                      std::vector<BoardPoint>{bend, end});
            }
        }
    for (const auto end :
         {BoardPoint{8, 0}, BoardPoint{0, -8}, BoardPoint{-8, 8}, BoardPoint{8, -8}}) {
        Fixture f;
        const auto result = f.report(f.spec({0, 0}, end));
        REQUIRE(result.committed);
        REQUIRE(result.candidates.size() == 1);
        CHECK(std::get<BoardTrack>(result.candidates[0].copper).points() ==
              std::vector<BoardPoint>{{0, 0}, end});
    }
    Fixture direct;
    const auto d = direct.report(direct.spec({-3, -5}, {8, 2}, CornerStyle::Direct));
    REQUIRE(d.candidates.size() == 1);
    CHECK(std::get<BoardTrack>(d.candidates[0].copper).points() ==
          std::vector<BoardPoint>{{-3, -5}, {8, 2}});
    Fixture orthogonal;
    const auto o = orthogonal.report(orthogonal.spec({-3, -5}, {8, 2}, CornerStyle::Orthogonal));
    REQUIRE(o.candidates.size() == 2);
    CHECK(std::get<BoardTrack>(o.candidates[0].copper).points().back() == BoardPoint{8, -5});
}

TEST_CASE("R1a literal axis and collinear steps stay separate and sharp", "[pcb][route]") {
    Fixture f;
    auto spec = f.spec();
    spec.steps = {volt::RouteToX{15}, volt::RouteToY{18},
                  volt::RouteThrough{volt::RoutePoint{{15, 18}}},
                  volt::RouteTo{volt::RoutePoint{{15, 20}}}};
    const auto result = f.report(spec);
    REQUIRE(result.committed);
    REQUIRE(result.candidates.size() == 3);
    CHECK(std::get<volt::BoardTrack>(result.candidates[0].copper).points().back() ==
          volt::BoardPoint{15, 10});
    CHECK(std::get<volt::BoardTrack>(result.candidates[1].copper).points().front() ==
          volt::BoardPoint{15, 10});
    CHECK(result.resolved_steps[2].candidates.empty());
    CHECK(result.candidates[2].step_index == 3);
}

TEST_CASE("R1a zero motion, via only and nonfinite arithmetic have typed outcomes",
          "[pcb][route]") {
    Fixture f;
    unchanged_failure(f, f.spec({10, 10}, {10, 10}), volt::RouteFailureKind::NoCandidate, true);
    const auto max = std::numeric_limits<double>::max();
    unchanged_failure(f, f.spec({-max, 0}, {max, 2}), volt::RouteFailureKind::GeometryUnresolvable,
                      false);
    auto spec = f.spec({10, 10}, {10, 10});
    spec.steps.insert(spec.steps.begin(), volt::RouteVia{{&f.board, f.back}});
    const auto before = f.board.all<volt::BoardTrackId>();
    const auto result = f.report(spec);
    REQUIRE(result.committed);
    CHECK(result.tracks.empty());
    REQUIRE(result.vias.size() == 1);
    CHECK(before.advanced_once());
    CHECK(result.candidate_complete);
}

TEST_CASE("R1a rejects complete grammar and owner corruption before planning", "[pcb][route]") {
    using namespace volt;
    Fixture f, foreign;
    auto spec = f.spec();
    SECTION("empty") { spec.steps.clear(); }
    SECTION("no terminal") { spec.steps = {RouteToX{12}}; }
    SECTION("early terminal") { spec.steps.push_back(RouteTo{RoutePoint{{25, 10}}}); }
    SECTION("wrong board") { spec.board = &foreign.board; }
    SECTION("wrong layer owner colliding ID") { spec.layer.owner = &foreign.board; }
    SECTION("wrong net owner colliding ID") { spec.net->owner = &foreign.circuit; }
    SECTION("unknown layer") { spec.layer.layer = BoardLayerId{999}; }
    SECTION("unknown net") { spec.net->net = NetId{999}; }
    SECTION("unknown track") {
        spec.start = RouteTrackRef{&f.board, BoardTrackId{99}, RouteTrackEnd::First};
    }
    SECTION("unknown via") { spec.start = RouteViaRef{&f.board, BoardViaId{99}}; }
    SECTION("same layer via") {
        spec.steps.insert(spec.steps.begin(), RouteVia{{&f.board, f.front}});
    }
    SECTION("nonfinite axis") {
        spec.steps.insert(spec.steps.begin(), RouteToX{std::numeric_limits<double>::infinity()});
    }
    SECTION("invalid width") { spec.width_mm = 0; }
    SECTION("invalid via") { spec.via_size = RouteViaOverride{0.5, 0.4}; }
    const auto bytes = f.bytes();
    CHECK_THROWS_AS(f.report(spec), KernelError);
    CHECK(f.bytes() == bytes);
}

TEST_CASE("R1a pad identity matches native physical truth and rejects colliding payloads",
          "[pcb][route]") {
    using namespace volt;
    Fixture f;
    const auto placement = f.pad_component();
    auto spec = f.spec();
    spec.net.reset();
    spec.start = f.pad(placement);
    const auto position = queries::resolve_pads(f.view())[0].position();
    SECTION("valid independent resolution copies") {
        const auto result = f.report(spec);
        REQUIRE(result.committed);
        CHECK(result.net == f.net);
        CHECK(std::get<BoardTrack>(result.candidates[0].copper).points().front() == position);
    }
    SECTION("same reference pad zero but different exact definition") {
        auto &pad = std::get<RoutePad>(spec.start);
        const auto original = pad.footprint;
        auto pads = original.pads();
        pads[0] = FootprintPad::surface_mount("1", FootprintPadShape::Rectangle, {3, 4}, {0.8, 0.9},
                                              FootprintLayerSet::front_smd());
        pad.footprint = FootprintDefinition{original.ref(), pads, original.package_geometry()};
        const auto bytes = f.bytes();
        CHECK_THROWS_AS(f.report(spec), KernelError);
        CHECK(f.bytes() == bytes);
    }
    SECTION("unknown pad") {
        std::get<RoutePad>(spec.start).pad = FootprintPadId{100};
        CHECK_THROWS_AS(f.report(spec), KernelRangeError);
    }
    SECTION("wrong owner with colliding placement") {
        Fixture other;
        const auto other_placement = other.pad_component();
        spec.start = other.pad(other_placement);
        CHECK_THROWS_AS(f.report(spec), KernelArgumentError);
    }
    SECTION("wrong active layer") {
        spec.layer.layer = f.back;
        unchanged_failure(f, spec, RouteFailureKind::EndpointUnreachable, true);
    }
}

TEST_CASE("R1a unconnected pad is a design rejection and late owner errors still win",
          "[pcb][route]") {
    Fixture f;
    const auto placement = f.pad_component(false);
    auto spec = f.spec();
    spec.start = f.pad(placement);
    spec.net.reset();
    unchanged_failure(f, spec, volt::RouteFailureKind::EndpointUnreachable, false);
    spec.steps.insert(spec.steps.begin(),
                      volt::RouteThrough{volt::RouteTrackRef{&f.board, volt::BoardTrackId{999},
                                                             volt::RouteTrackEnd::First}});
    CHECK_THROWS_AS(f.report(spec), volt::KernelRangeError);
}

TEST_CASE("R1a infers all endpoint kinds and exact logical nets must agree", "[pcb][route]") {
    using namespace volt;
    Fixture f;
    const auto track = f.board.add_track(BoardTrack{f.net, f.front, {{5, 10}, {10, 10}}, 0.2});
    const auto via = f.board.add_via(BoardVia{f.net, {20, 10}, f.front, f.back, 0.3, 0.7});
    auto spec = f.spec();
    spec.net.reset();
    spec.start = RouteTrackRef{&f.board, track, RouteTrackEnd::Last};
    spec.steps = {RouteThrough{RouteViaRef{&f.board, via}}, RouteTo{RoutePoint{{25, 10}}}};
    SECTION("agreement") {
        const auto result = f.report(spec);
        REQUIRE(result.committed);
        CHECK(result.net == f.net);
    }
    SECTION("later conflicting endpoint") {
        const auto foreign =
            f.board.add_track(BoardTrack{f.other, f.front, {{30, 10}, {35, 10}}, 0.2});
        spec.steps.push_back(RouteTo{RouteTrackRef{&f.board, foreign, RouteTrackEnd::Last}});
        spec.steps[1] = RouteThrough{RoutePoint{{25, 10}}};
        CHECK_THROWS_AS(f.report(spec), KernelArgumentError);
    }
    SECTION("point only needs explicit net") {
        spec.start = RoutePoint{{10, 10}};
        spec.steps = {RouteTo{RoutePoint{{20, 10}}}};
        CHECK_THROWS_AS(f.report(spec), KernelArgumentError);
    }
    SECTION("track wrong layer") {
        spec.layer.layer = f.back;
        unchanged_failure(f, spec, RouteFailureKind::EndpointUnreachable, true);
    }
}

TEST_CASE("R1a explicit sizes reject floors without clamping and preserve independent sources",
          "[pcb][route]") {
    using namespace volt;
    Fixture f;
    auto c = NetClass{NetClassName{"SIGNAL"}};
    c.set_track_width_mm(0.4);
    c.set_via_size_mm(0.25, 0.9);
    const auto klass = f.klass(c);
    auto spec = f.spec();
    spec.steps.insert(spec.steps.begin(), RouteVia{{&f.board, f.back}});
    SECTION("inherited") {
        const auto r = f.report(spec);
        REQUIRE(r.committed);
        const auto &via = r.resolved_rules[0];
        CHECK(via.drill->value_mm == 0.3);
        CHECK(via.diameter->value_mm == 0.9);
        CHECK(via.drill->sources[0].kind == RouteRuleSourceKind::BoardRule);
        CHECK(via.diameter->sources[0].net_class == klass);
        CHECK(r.resolved_rules[1].width->value_mm == 0.4);
    }
    SECTION("under width") {
        spec.width_mm = 0.25;
        unchanged_failure(f, spec, RouteFailureKind::RuleViolation, true);
        const auto r = f.report(spec);
        CHECK(r.resolved_rules[1].width->value_mm == 0.25);
        CHECK(r.resolved_rules[1].width->sources[0].kind == RouteRuleSourceKind::ExplicitOverride);
        CHECK(r.resolved_rules[1].width->enforced_sources[0].net_class == klass);
    }
    SECTION("under drill") {
        spec.via_size = RouteViaOverride{0.2, 0.9};
        unchanged_failure(f, spec, RouteFailureKind::RuleViolation, true);
    }
    SECTION("under diameter") {
        spec.via_size = RouteViaOverride{0.3, 0.8};
        unchanged_failure(f, spec, RouteFailureKind::RuleViolation, true);
    }
    SECTION("epsilon") {
        spec.width_mm = 0.4 - 0.5e-9;
        REQUIRE(f.report(spec).committed);
    }
}

TEST_CASE("R1a rooms preserve native width replacement iteration and priority", "[pcb][route]") {
    using namespace volt;
    Fixture f;
    auto c = NetClass{NetClassName{"WIDE"}};
    c.set_track_width_mm(0.5);
    f.klass(c);
    auto room = BoardRoom{"local", BoardOutline::rectangle({5, 5}, {25, 15}), {f.front}, 5};
    room.set_track_width_mm(0.3);
    const auto room_id = f.board.add_room(room);
    auto spec = f.spec();
    SECTION("explicit room relaxes class not board") {
        spec.width_mm = 0.3;
        const auto r = f.report(spec);
        REQUIRE(r.committed);
        CHECK(r.resolved_rules[0].width->enforced_sources[0].room == room_id);
        auto widths = DiagnosticReport{};
        detail::validate_track_widths(f.board, widths);
        CHECK(!widths.has_errors());
    }
    SECTION("inherited only raises") {
        const auto r = f.report(spec);
        REQUIRE(r.committed);
        CHECK(r.resolved_rules[0].width->value_mm == 0.5);
    }
    SECTION("room minimum enforced") {
        spec.width_mm = 0.25;
        unchanged_failure(f, spec, RouteFailureKind::RuleViolation, true);
    }
    SECTION("whole copper containment not centerline") {
        spec.start = RoutePoint{{10, 5.1}};
        spec.steps = {RouteTo{RoutePoint{{20, 5.1}}}};
        spec.width_mm = 0.3;
        unchanged_failure(f, spec, RouteFailureKind::RuleViolation, true);
    }
    SECTION("higher priority widens with single source") {
        auto higher = BoardRoom{"higher", room.outline(), {f.front}, 6};
        higher.set_track_width_mm(0.8);
        const auto high = f.board.add_room(higher);
        const auto r = f.report(spec);
        REQUIRE(r.committed);
        CHECK(r.resolved_rules[0].width->value_mm == 0.8);
        REQUIRE(r.resolved_rules[0].width->sources.size() == 1);
        CHECK(r.resolved_rules[0].width->sources[0].room == high);
    }
}

TEST_CASE("R1a inline via span includes disallowed intermediate copper", "[pcb][route]") {
    Fixture f;
    auto c = volt::NetClass{volt::NetClassName{"OUTER"}};
    c.set_allowed_layer_names({"F.Cu", "B.Cu"});
    f.klass(c);
    auto spec = f.spec();
    spec.steps.insert(spec.steps.begin(), volt::RouteVia{{&f.board, f.back}});
    unchanged_failure(f, spec, volt::RouteFailureKind::LayerNotAllowed, true);
    const auto r = f.report(spec);
    CHECK(r.layer_rules.excluded == std::vector{f.inner});
}

TEST_CASE("R1a room width controlling ties sort by ID after containment iteration",
          "[pcb][route]") {
    using namespace volt;
    Fixture f;
    auto broad = BoardRoom{"broad", BoardOutline::rectangle({5, 5}, {25, 15}), {f.front}, 1};
    broad.set_track_width_mm(0.8);
    const auto low_id = f.board.add_room(broad);
    auto tight = BoardRoom{"tight", BoardOutline::rectangle({5, 9.8}, {25, 0.4}), {f.front}, 2};
    tight.set_track_width_mm(0.8);
    const auto high_id = f.board.add_room(tight);
    const auto result = f.report(f.spec());
    REQUIRE(result.committed);
    const auto &sources = result.resolved_rules[0].width->sources;
    REQUIRE(sources.size() == 2);
    CHECK(sources[0].room == low_id);
    CHECK(sources[1].room == high_id);
}

TEST_CASE("R1a late blockers keep exact bytes IDs references and generation unchanged",
          "[pcb][route]") {
    using namespace volt;
    Fixture f;
    const auto obstacle = f.board.add_track(BoardTrack{f.other, f.back, {{25, 5}, {25, 15}}, 0.2});
    const auto *reference = &f.board.get(obstacle);
    auto spec = f.spec();
    spec.steps = {RouteThrough{RoutePoint{{15, 10}}}, RouteVia{{&f.board, f.back}},
                  RouteTo{RoutePoint{{30, 10}}}};
    unchanged_failure(f, spec, RouteFailureKind::Blocked, true);
    const auto r = f.report(spec);
    REQUIRE(r.candidates.size() == 3);
    CHECK(&f.board.get(obstacle) == reference);
    REQUIRE(r.blockers.size() == 1);
    CHECK(r.blockers[0].candidate_ordinal == 2);
    CHECK(r.blockers[0].step_index == 2);
    CHECK(r.message == "route SIG step 2 blocked: on B.Cu: needs 0.200 mm to track 0 (OTHER), has "
                       "-0.200 mm; board unchanged");
    REQUIRE(r.clearance_checks.size() == 1);
    CHECK(r.clearance_checks[0].clearance.sources[0].kind == RouteRuleSourceKind::BoardRule);
}

TEST_CASE("R1a committed IDs and geometry follow interleaved candidate order", "[pcb][route]") {
    using namespace volt;
    Fixture f;
    auto spec = f.spec();
    spec.steps = {RouteThrough{RoutePoint{{15, 10}}}, RouteVia{{&f.board, f.back}},
                  RouteThrough{RoutePoint{{15, 20}}}, RouteVia{{&f.board, f.front}},
                  RouteTo{RoutePoint{{20, 20}}}};
    const auto generation = f.board.all<BoardTrackId>();
    const auto r = f.report(spec);
    REQUIRE(r.committed);
    REQUIRE(r.candidates.size() == 5);
    CHECK(generation.advanced_once());
    CHECK(r.tracks == std::vector{BoardTrackId{0}, BoardTrackId{1}, BoardTrackId{2}});
    CHECK(r.vias == std::vector{BoardViaId{0}, BoardViaId{1}});
    CHECK(std::get<BoardViaId>(r.committed_order[1]) == BoardViaId{0});
    CHECK(std::get<BoardTrackId>(r.committed_order[2]) == BoardTrackId{1});
    for (std::size_t i = 0; i < r.candidates.size(); ++i) {
        if (const auto *track = std::get_if<BoardTrack>(&r.candidates[i].copper)) {
            const auto &stored = f.board.get(std::get<BoardTrackId>(r.committed_order[i]));
            CHECK(stored.points() == track->points());
            CHECK(stored.width_mm() == track->width_mm());
        } else {
            const auto &via = std::get<BoardVia>(r.candidates[i].copper);
            CHECK(f.board.get(std::get<BoardViaId>(r.committed_order[i])).position() ==
                  via.position());
        }
    }
}

TEST_CASE("Transient via obstacles retain category and actual logical net", "[pcb][route]") {
    using namespace volt;
    Fixture f;
    auto rules = BoardDesignRules{0.1, 0.2, 0.3, 0.7, 0.25};
    rules.set_clearance_mm(BoardClearanceKind::Track, BoardClearanceKind::Via, 0.8);
    f.board.set_design_rules(rules);
    auto index = BoardSpatialIndex{f.view()};
    index.insert({BoardSpatialQueryShapeKind::Disc,
                  f.other,
                  {f.front},
                  {{10, 10}},
                  0.35,
                  BoardClearanceKind::Via,
                  BoardKeepoutRestriction::Via});
    const auto query = BoardSpatialQueryShape{
        BoardSpatialQueryShapeKind::Segment, f.net, {f.front}, {{10.8, 9}, {10.8, 11}}, 0.1};
    const auto blocked = index.query_legality(query);
    REQUIRE(!blocked.legal);
    REQUIRE(blocked.blockers.size() == 1);
    CHECK(blocked.blockers[0].required_clearance_mm == 0.8);
    CHECK(index.obstacle(0).net == f.other);
    CHECK(index.obstacle(0).clearance_kind == BoardClearanceKind::Via);
    auto same = query;
    same.net = f.other;
    CHECK(index.query_legality(same).legal);
}

TEST_CASE("R1a disabled noncopper and offstack layer references are structural", "[pcb][route]") {
    using namespace volt;
    Fixture f;
    auto layer = f.front;
    SECTION("disabled") {
        layer = f.board.add_layer(
            BoardLayer{"Disabled", BoardLayerRole::Copper, BoardLayerSide::Inner, 0, false});
    }
    SECTION("noncopper") {
        layer =
            f.board.add_layer(BoardLayer{"Silk", BoardLayerRole::Silkscreen, BoardLayerSide::Top});
    }
    SECTION("offstack") {
        layer =
            f.board.add_layer(BoardLayer{"Extra", BoardLayerRole::Copper, BoardLayerSide::Inner});
    }
    auto spec = f.spec();
    spec.layer.layer = layer;
    const auto bytes = f.bytes();
    CHECK_THROWS_AS(f.report(spec), KernelArgumentError);
    CHECK(f.bytes() == bytes);
}

TEST_CASE("R1a inherited via sizing respects native pair integrity without invented margin",
          "[pcb][route]") {
    Fixture f;
    const auto bytes = f.bytes();
    CHECK_THROWS_AS(f.board.set_design_rules(volt::BoardDesignRules{0.2, 0.2, 0.8, 0.7, 0.25}),
                    volt::KernelArgumentError);
    CHECK(f.bytes() == bytes);
    f.board.set_design_rules(volt::BoardDesignRules{0.2, 0.2, 0.7 - 0.5e-9, 0.7, 0.25});
    auto spec = f.spec();
    spec.steps = {volt::RouteThrough{volt::RoutePoint{{15, 10}}},
                  volt::RouteVia{{&f.board, f.back}}, volt::RouteTo{volt::RoutePoint{{20, 10}}}};
    const auto result = f.report(spec);
    REQUIRE(result.committed);
    REQUIRE(result.candidates.size() == 3);
    CHECK(result.resolved_rules[1].drill->value_mm == 0.7 - 0.5e-9);
    CHECK(result.resolved_rules[1].diameter->value_mm == 0.7);
}

TEST_CASE("R1a missing physical closure is diagnosable not foreign identity", "[pcb][route]") {
    Fixture f;
    const auto definition = f.circuit.define_component(volt::ComponentSpec{
        .name = "NoPhysical", .pins = {volt::PinSpec{.name = "A", .number = "1"}}});
    const auto component = f.circuit.instantiate_component(
        definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto placement = f.board.place_component(
        volt::ComponentPlacement{component, {10, 10}, volt::BoardRotation::degrees(0)});
    auto spec = f.spec();
    spec.start = volt::RoutePad{&f.board, placement, volt::passive_0603_footprint(),
                                volt::FootprintPadId{0}};
    unchanged_failure(f, spec, volt::RouteFailureKind::EndpointUnreachable, false);
}

TEST_CASE("R1a default derived class and exact board ties retain provenance", "[pcb][route]") {
    using namespace volt;
    Fixture f;
    auto c = NetClass{NetClassName{"DEFAULT"}};
    c.set_default_for_net_kind(NetKind::Signal);
    SECTION("ties") {
        c.set_track_width_mm(0.2);
        const auto klass = f.circuit.define_net_class(NetClassSpec{.net_class = c});
        const auto r = f.report(f.spec());
        REQUIRE(r.committed);
        REQUIRE(r.resolved_rules[0].width->sources.size() == 2);
        CHECK(r.resolved_rules[0].width->sources[0].kind == RouteRuleSourceKind::BoardRule);
        CHECK(r.resolved_rules[0].width->sources[1].net_class == klass);
    }
    SECTION("derived value") {
        c.derive_track_width(DerivedNetClassRuleValue{
            0.3, NetClassRuleDerivation{"fixture", "Fixture", "test", "native", {}}});
        const auto klass = f.circuit.define_net_class(NetClassSpec{.net_class = c});
        const auto r = f.report(f.spec());
        REQUIRE(r.committed);
        const auto &s = r.resolved_rules[0].width->sources[0];
        CHECK(s.net_class == klass);
        REQUIRE(s.derivation);
        CHECK(s.derivation->calculator_id == "fixture");
    }
    SECTION("assigned beats default") {
        c.set_track_width_mm(0.9);
        static_cast<void>(f.circuit.define_net_class(NetClassSpec{.net_class = c}));
        auto assigned = NetClass{NetClassName{"ASSIGNED"}};
        assigned.set_track_width_mm(0.3);
        f.klass(assigned);
        CHECK(f.report(f.spec()).resolved_rules[0].width->value_mm == 0.3);
    }
}

TEST_CASE("R1a local copper clearance room replaces global base with legal-check provenance",
          "[pcb][route]") {
    using namespace volt;
    Fixture f;
    auto c = NetClass{NetClassName{"WIDE_GAP"}};
    c.set_copper_clearance_mm(0.4);
    f.klass(c);
    static_cast<void>(
        f.board.add_track(BoardTrack{f.other, f.front, {{10, 10.35}, {20, 10.35}}, 0.2}));
    auto room = BoardRoom{"first", BoardOutline::rectangle({5, 5}, {25, 15}), {f.front}, 5};
    room.set_copper_clearance_mm(0.1);
    const auto first = f.board.add_room(room);
    auto later = BoardRoom{"tie", room.outline(), {f.front}, 5};
    later.set_copper_clearance_mm(0.2);
    static_cast<void>(f.board.add_room(later));
    const auto result = f.report(f.spec());
    REQUIRE(result.committed);
    REQUIRE(result.clearance_checks.size() == 1);
    const auto &check = result.clearance_checks[0];
    CHECK(check.clearance.value_mm == 0.1);
    CHECK(check.actual_mm == Catch::Approx(0.15));
    REQUIRE(check.clearance.sources.size() == 1);
    CHECK(check.clearance.sources[0].room == first);
    auto report = DiagnosticReport{};
    const auto shapes = detail::collect_copper_shapes(f.view(), queries::resolve_pads(f.view()));
    detail::validate_copper_clearance(f.board, detail::NetContinuityView{f.circuit}, shapes,
                                      report);
    CHECK(!report.has_errors());
}

TEST_CASE("R1a outline opening and keepout blockers have deterministic native identities",
          "[pcb][route]") {
    using namespace volt;
    Fixture f;
    f.board.set_outline(BoardOutline::rectangle({0, 0}, {20, 20}));
    const auto hole = f.board.add_feature(BoardFeature::hole("MH1", {15, 10}, 2));
    const auto keepout = f.board.add_keepout(BoardKeepout{
        {{12, 9}, {18, 9}, {18, 11}, {12, 11}}, {f.front}, {BoardKeepoutRestriction::Copper}});
    unchanged_failure(f, f.spec({10, 10}, {25, 10}), RouteFailureKind::Blocked, true);
    const auto result = f.report(f.spec({10, 10}, {25, 10}));
    REQUIRE(result.blockers.size() == 3);
    CHECK(result.blockers[0].kind == BoardSpatialBlockerKind::BoardOutline);
    CHECK(result.blockers[1].kind == BoardSpatialBlockerKind::MechanicalOpening);
    CHECK(result.blockers[1].entities[0] == EntityRef::board_feature(hole));
    CHECK(result.blockers[2].kind == BoardSpatialBlockerKind::Keepout);
    CHECK(result.blockers[2].sources[0].keepout == keepout);
    CHECK(!result.blockers[2].required_mm);
    CHECK(!result.blockers[2].actual_mm);
}

TEST_CASE("R1a inline via checks keepouts on intermediate layers and consecutive vias are retained",
          "[pcb][route]") {
    using namespace volt;
    Fixture f;
    auto spec = f.spec({10, 10}, {10, 10});
    spec.steps = {RouteVia{{&f.board, f.back}}, RouteVia{{&f.board, f.front}},
                  RouteTo{RoutePoint{{10, 10}}}};
    SECTION("legal consecutive vias") {
        const auto result = f.report(spec);
        REQUIRE(result.committed);
        CHECK(result.vias.size() == 2);
    }
    SECTION("middle layer keepout") {
        static_cast<void>(f.board.add_keepout(BoardKeepout{
            {{9, 9}, {11, 9}, {11, 11}, {9, 11}}, {f.inner}, {BoardKeepoutRestriction::Via}}));
        unchanged_failure(f, spec, RouteFailureKind::Blocked, true);
        const auto result = f.report(spec);
        REQUIRE(result.blockers.size() == 2);
        CHECK(result.blockers[0].layer == f.inner);
    }
}

TEST_CASE("R1a same continuity group exempts copper but not conflicting endpoint NetIds",
          "[pcb][route]") {
    using namespace volt;
    Fixture f;
    const auto module = f.circuit.define_module(
        ModuleSpec{.name = ModuleName{"Child"},
                   .template_nets = {TemplateNetDefinition{NetName{"N"}, NetKind::Signal}},
                   .ports = {ModulePortSpec{PortName{"N"}, NetName{"N"}, PortRole::Passive}}});
    const auto port = f.circuit.get(module).ports()[0];
    const auto template_net = f.circuit.get(module).template_nets()[0];
    const auto instance =
        f.circuit.instantiate_module(module, ModuleInstanceSpec{.name = ModuleInstanceName{"M"}});
    const auto internal = queries::concrete_net_for(f.circuit, instance, template_net).value();
    static_cast<void>(f.circuit.bind_port(instance, port, f.net));
    const auto track = f.board.add_track(BoardTrack{internal, f.front, {{15, 5}, {15, 15}}, 0.2});
    SECTION("same-group existing copper") { REQUIRE(f.report(f.spec()).committed); }
    SECTION("endpoint evidence still exact") {
        auto spec = f.spec();
        spec.steps = {RouteTo{RouteTrackRef{&f.board, track, RouteTrackEnd::First}}};
        CHECK_THROWS_AS(f.report(spec), KernelArgumentError);
    }
    SECTION("transient real NetId") {
        auto index = BoardSpatialIndex{f.view()};
        index.insert({BoardSpatialQueryShapeKind::Disc,
                      internal,
                      {f.front},
                      {{10, 10}},
                      0.35,
                      BoardClearanceKind::Via,
                      BoardKeepoutRestriction::Via});
        CHECK(index
                  .query_legality({BoardSpatialQueryShapeKind::Segment,
                                   f.net,
                                   {f.front},
                                   {{5, 10}, {20, 10}},
                                   0.1})
                  .legal);
    }
}
