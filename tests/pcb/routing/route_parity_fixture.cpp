#include "support/resolved_board_test_parts.hpp"
#include "support/route_report_snapshot.hpp"

#include <iostream>
#include <limits>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/io/pcb/pcb_writer.hpp>
#include <volt/pcb/routing/board_router.hpp>

int main(int argc, char **argv) {
    using namespace volt;
    const auto mode = argc > 1 ? std::string{argv[1]} : "blocked";
    Circuit circuit;
    const auto net = circuit.add_net(NetSpec{NetName{"SIG"}, NetKind::Signal});
    const auto other = circuit.add_net(NetSpec{NetName{"OTHER"}, NetKind::Signal});
    Board board{circuit};
    const auto front =
        board.add_layer(BoardLayer{"F.Cu", BoardLayerRole::Copper, BoardLayerSide::Top});
    const auto back =
        board.add_layer(BoardLayer{"B.Cu", BoardLayerRole::Copper, BoardLayerSide::Bottom});
    board.set_layer_stack(LayerStack{{front, back}, 1.6});
    board.set_design_rules(BoardDesignRules{0.2, 0.2, 0.3, 0.7, 0.25});
    test::ResolvedBoardTestParts parts;
    auto spec = RouteSpec{&board,
                          RouteNetRef{&circuit, net},
                          RoutePoint{{10, 10}},
                          {&board, front},
                          {RouteTo{RoutePoint{{30, 10}}}}};
    if (mode == "blocked")
        static_cast<void>(board.add_track(BoardTrack{other, front, {{25, 5}, {25, 15}}, 0.2}));
    else if (mode == "width")
        spec.width_mm = 0.1;
    else if (mode == "geometry") {
        const auto max = std::numeric_limits<double>::max();
        spec.start = RoutePoint{{-max, 0}};
        spec.steps = {RouteTo{RoutePoint{{max, 2}}}};
    } else if (mode == "legal") {
        spec.steps.insert(spec.steps.begin(), RouteVia{{&board, back}});
    } else if (mode == "unconnected") {
        const auto definition = circuit.define_component(ComponentSpec{
            .name = "Resistor",
            .pins = {PinSpec{.name = "A", .number = "1"}, PinSpec{.name = "B", .number = "2"}}});
        const auto component = circuit.instantiate_component(
            definition, ComponentInstanceSpec{.reference = ReferenceDesignator{"R1"}});
        const auto &pins = circuit.get(definition).pins();
        parts.set(circuit, component,
                  PhysicalPart{ManufacturerPart{"Test", "R"},
                               PackageRef{"0603"},
                               passive_0603_footprint().ref(),
                               {{pins[0], "1"}, {pins[1], "2"}}});
        const auto placement = board.place_component(
            ComponentPlacement{component, {10, 10}, BoardRotation::degrees(0)});
        spec.start = RoutePad{&board, placement, passive_0603_footprint(), FootprintPadId{0}};
    } else
        return 2;
    const auto view = parts.view_builtin(board);
    const auto bytes = io::write_pcb_board(view);
    auto router = BoardRouter{board, view};
    try {
        const auto result = router.route(spec);
        if (mode != "legal" || !result.committed)
            return 3;
        std::cout << test::route_snapshot(result).dump() << '\n';
    } catch (const RouteRejected &error) {
        if (mode == "legal" || error.what() != error.result().message ||
            io::write_pcb_board(view) != bytes)
            return 4;
        const auto snapshot = test::route_snapshot(error.result());
        const auto report = router.route(spec, RoutePolicy::Report);
        if (test::route_snapshot(report) != snapshot)
            return 5;
        std::cout << snapshot.dump() << '\n';
    }
}
