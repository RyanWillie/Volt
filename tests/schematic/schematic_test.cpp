#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <stdexcept>
#include <variant>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/circuit/instances.hpp>
#include <volt/schematic/geometry.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/symbols.hpp>
#include <volt/schematic/validation.hpp>

namespace {

volt::ComponentId add_resistor(volt::Circuit &circuit) {
    const auto first_pin =
        circuit.add_pin_definition(volt::PinDefinition{"1", "1", volt::PinRole::Passive});
    const auto second_pin =
        circuit.add_pin_definition(volt::PinDefinition{"2", "2", volt::PinRole::Passive});
    const auto definition = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    return circuit.instantiate_component(definition, volt::ReferenceDesignator{"R1"});
}

volt::NetId add_net(volt::Circuit &circuit) {
    return circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});
}

void connect_pin_by_number(volt::Circuit &circuit, volt::NetId net, volt::ComponentId component,
                           const std::string &number) {
    circuit.connect(net, circuit.pin_by_number(component, number).value());
}

volt::SymbolDefinition make_resistor_symbol() {
    auto symbol = volt::SymbolDefinition{"Resistor"};
    symbol.add_pin(
        volt::SymbolPin{"1", "1", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Left});
    symbol.add_pin(
        volt::SymbolPin{"2", "2", volt::Point{20.0, 0.0}, volt::SchematicOrientation::Right});
    symbol.add_primitive(volt::SymbolLine{volt::Point{0.0, 0.0}, volt::Point{20.0, 0.0}});
    return symbol;
}

} // namespace

TEST_CASE("Symbol definitions store structured drawing primitives and pins") {
    const auto symbol = make_resistor_symbol();

    CHECK(symbol.name() == "Resistor");
    REQUIRE(symbol.pins().size() == 2);
    CHECK(symbol.pins().front().name() == "1");
    CHECK(symbol.pins().front().number() == "1");
    CHECK(symbol.pins().front().anchor() == volt::Point{0.0, 0.0});
    CHECK(symbol.pins().front().orientation() == volt::SchematicOrientation::Left);

    REQUIRE(symbol.primitives().size() == 1);
    REQUIRE(std::holds_alternative<volt::SymbolLine>(symbol.primitives().front()));
    const auto &line = std::get<volt::SymbolLine>(symbol.primitives().front());
    CHECK(line.start() == volt::Point{0.0, 0.0});
    CHECK(line.end() == volt::Point{20.0, 0.0});
}

TEST_CASE("Symbol primitives store basic vector geometry") {
    const auto rectangle = volt::SymbolRectangle{volt::Point{0.0, -5.0}, volt::Point{10.0, 5.0}};
    CHECK(rectangle.first_corner() == volt::Point{0.0, -5.0});
    CHECK(rectangle.second_corner() == volt::Point{10.0, 5.0});

    const auto circle = volt::SymbolCircle{volt::Point{4.0, 2.0}, 3.0};
    CHECK(circle.center() == volt::Point{4.0, 2.0});
    CHECK(circle.radius() == 3.0);

    const auto arc = volt::SymbolArc{volt::Point{2.0, 2.0}, 4.0, 90.0, 180.0};
    CHECK(arc.center() == volt::Point{2.0, 2.0});
    CHECK(arc.radius() == 4.0);
    CHECK(arc.start_degrees() == 90.0);
    CHECK(arc.sweep_degrees() == 180.0);

    const auto text = volt::SymbolText{"U?", volt::Point{0.0, 12.0}};
    CHECK(text.text() == "U?");
    CHECK(text.anchor() == volt::Point{0.0, 12.0});
    CHECK(text.orientation() == volt::SchematicOrientation::Right);
    CHECK(text == volt::SymbolText{"U?", volt::Point{0.0, 12.0}});
}

TEST_CASE("Schematic stores sheets and symbol instances over logical components") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 20.0},
                                    volt::SchematicOrientation::Right});

    CHECK(sheet == volt::SheetId{0});
    CHECK(symbol == volt::SymbolDefId{0});
    CHECK(instance == volt::SymbolInstanceId{0});
    CHECK(schematic.sheet_count() == 1);
    CHECK(schematic.symbol_definition_count() == 1);
    CHECK(schematic.symbol_instance_count() == 1);
    CHECK(schematic.symbol_instance(instance).component() == component);
    CHECK(schematic.symbol_instance(instance).symbol_definition() == symbol);
    CHECK(schematic.symbol_instance(instance).position() == volt::Point{40.0, 20.0});
    CHECK(schematic.sheet(sheet).symbol_instances() == std::vector{instance});
    CHECK(circuit.net_count() == 0);
}

TEST_CASE("Schematic stores wire runs and labels over canonical logical nets") {
    volt::Circuit circuit;
    const auto net = add_net(circuit);

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{10.0, 20.0}, volt::Point{40.0, 20.0}}});
    const auto label = schematic.add_net_label(
        sheet, volt::NetLabel{net, volt::Point{12.0, 16.0}, volt::SchematicOrientation::Right});

    CHECK(wire == volt::WireRunId{0});
    CHECK(label == volt::NetLabelId{0});
    CHECK(schematic.wire_run_count() == 1);
    CHECK(schematic.net_label_count() == 1);
    CHECK(schematic.sheet(sheet).wire_runs() == std::vector{wire});
    CHECK(schematic.sheet(sheet).net_labels() == std::vector{label});
    CHECK(schematic.wire_run(wire).net() == net);
    CHECK(schematic.wire_run(wire).points() ==
          std::vector{volt::Point{10.0, 20.0}, volt::Point{40.0, 20.0}});
    CHECK(schematic.net_label(label).net() == net);
    CHECK(schematic.net_label(label).position() == volt::Point{12.0, 16.0});
    CHECK(schematic.net_label(label).orientation() == volt::SchematicOrientation::Right);
}

TEST_CASE("Schematic rejects empty presentation names") {
    CHECK_THROWS_AS(volt::SymbolDefinition{""}, std::invalid_argument);
    CHECK_THROWS_AS(volt::Sheet{""}, std::invalid_argument);
    CHECK_THROWS_AS(
        (volt::SymbolPin{"", "1", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Left}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        (volt::SymbolPin{"A", "", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Left}),
        std::invalid_argument);
}

TEST_CASE("Symbol definitions reject duplicate pin numbers") {
    auto symbol = volt::SymbolDefinition{"Connector"};
    symbol.add_pin(
        volt::SymbolPin{"A", "1", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Left});

    CHECK_THROWS_AS(symbol.add_pin(volt::SymbolPin{"B", "1", volt::Point{10.0, 0.0},
                                                   volt::SchematicOrientation::Right}),
                    std::logic_error);
}

TEST_CASE("Schematic rejects symbol placements with missing references") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());

    CHECK_THROWS_AS(
        schematic.place_symbol(volt::SheetId{99},
                               volt::SymbolInstance{symbol, component, volt::Point{0.0, 0.0}}),
        std::out_of_range);
    CHECK_THROWS_AS(
        schematic.place_symbol(
            sheet, volt::SymbolInstance{volt::SymbolDefId{99}, component, volt::Point{0.0, 0.0}}),
        std::out_of_range);
    CHECK_THROWS_AS(
        schematic.place_symbol(
            sheet, volt::SymbolInstance{symbol, volt::ComponentId{99}, volt::Point{0.0, 0.0}}),
        std::out_of_range);
}

TEST_CASE("Schematic rejects wire and label projections with missing references") {
    volt::Circuit circuit;
    const auto net = add_net(circuit);

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});

    CHECK_THROWS_AS(schematic.add_wire_run(volt::SheetId{99},
                                           volt::WireRun{net, std::vector{volt::Point{0.0, 0.0},
                                                                          volt::Point{10.0, 0.0}}}),
                    std::out_of_range);
    CHECK_THROWS_AS(schematic.add_wire_run(
                        sheet, volt::WireRun{volt::NetId{99}, std::vector{volt::Point{0.0, 0.0},
                                                                          volt::Point{10.0, 0.0}}}),
                    std::out_of_range);
    CHECK_THROWS_AS(
        schematic.add_net_label(sheet, volt::NetLabel{volt::NetId{99}, volt::Point{0.0, 0.0}}),
        std::out_of_range);
    CHECK_THROWS_AS((volt::WireRun{net, std::vector{volt::Point{0.0, 0.0}}}),
                    std::invalid_argument);
}

TEST_CASE("Schematic geometry transforms symbol points by orientation") {
    const auto local = volt::Point{2.0, 3.0};
    const auto origin = volt::Point{10.0, 20.0};

    CHECK(volt::transform_schematic_point(local, origin, volt::SchematicOrientation::Right) ==
          volt::Point{12.0, 23.0});
    CHECK(volt::transform_schematic_point(local, origin, volt::SchematicOrientation::Down) ==
          volt::Point{7.0, 22.0});
    CHECK(volt::transform_schematic_point(local, origin, volt::SchematicOrientation::Left) ==
          volt::Point{8.0, 17.0});
    CHECK(volt::transform_schematic_point(local, origin, volt::SchematicOrientation::Up) ==
          volt::Point{13.0, 18.0});
}

TEST_CASE("Schematic geometry classifies segment relationships and junction semantics") {
    const auto horizontal = volt::SchematicSegment{volt::Point{0.0, 0.0}, volt::Point{10.0, 0.0}};

    const auto crossing = volt::classify_segment_relationship(
        horizontal, volt::SchematicSegment{volt::Point{5.0, -5.0}, volt::Point{5.0, 5.0}});
    CHECK(crossing == volt::SchematicSegmentRelationship::Crossing);
    CHECK_FALSE(volt::same_net_segments_join(crossing, volt::SchematicJunction::Absent));
    CHECK(volt::same_net_segments_join(crossing, volt::SchematicJunction::Present));
    CHECK_FALSE(volt::different_net_segments_collide(crossing, volt::SchematicJunction::Absent));
    CHECK(volt::different_net_segments_collide(crossing, volt::SchematicJunction::Present));

    const auto endpoint_touch = volt::classify_segment_relationship(
        horizontal, volt::SchematicSegment{volt::Point{10.0, 0.0}, volt::Point{20.0, 0.0}});
    CHECK(endpoint_touch == volt::SchematicSegmentRelationship::EndpointTouch);
    CHECK(volt::same_net_segments_join(endpoint_touch, volt::SchematicJunction::Absent));
    CHECK(volt::different_net_segments_collide(endpoint_touch, volt::SchematicJunction::Absent));

    const auto overlap = volt::classify_segment_relationship(
        horizontal, volt::SchematicSegment{volt::Point{5.0, 0.0}, volt::Point{15.0, 0.0}});
    CHECK(overlap == volt::SchematicSegmentRelationship::Overlap);
    CHECK(volt::same_net_segments_join(overlap, volt::SchematicJunction::Absent));
    CHECK(volt::different_net_segments_collide(overlap, volt::SchematicJunction::Absent));

    const auto disjoint = volt::classify_segment_relationship(
        horizontal, volt::SchematicSegment{volt::Point{0.0, 2.0}, volt::Point{10.0, 2.0}});
    CHECK(disjoint == volt::SchematicSegmentRelationship::Disjoint);
    CHECK_FALSE(volt::same_net_segments_join(disjoint, volt::SchematicJunction::Present));
    CHECK_FALSE(volt::different_net_segments_collide(disjoint, volt::SchematicJunction::Present));
}

TEST_CASE("Schematic allows same-net joins but rejects different-net wire collisions") {
    volt::Circuit circuit;
    const auto vcc = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});
    const auto gnd = circuit.add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    [[maybe_unused]] const auto first = schematic.add_wire_run(
        sheet, volt::WireRun{vcc, std::vector{volt::Point{0.0, 0.0}, volt::Point{10.0, 0.0}}});

    CHECK_NOTHROW(schematic.add_wire_run(
        sheet, volt::WireRun{vcc, std::vector{volt::Point{10.0, 0.0}, volt::Point{20.0, 0.0}}}));
    CHECK_NOTHROW(schematic.add_wire_run(
        sheet, volt::WireRun{vcc, std::vector{volt::Point{5.0, 0.0}, volt::Point{15.0, 0.0}}}));
    CHECK_NOTHROW(schematic.add_wire_run(
        sheet, volt::WireRun{gnd, std::vector{volt::Point{2.0, -5.0}, volt::Point{2.0, 5.0}}}));
    CHECK_THROWS_AS(
        schematic.add_wire_run(
            sheet, volt::WireRun{gnd, std::vector{volt::Point{10.0, 0.0}, volt::Point{20.0, 0.0}}}),
        std::logic_error);
    CHECK_THROWS_AS(
        schematic.add_wire_run(
            sheet, volt::WireRun{gnd, std::vector{volt::Point{0.0, 0.0}, volt::Point{5.0, 0.0}}}),
        std::logic_error);
}

TEST_CASE("Schematic readiness reports connected symbol pins without visual net coverage") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = add_net(circuit);
    connect_pin_by_number(circuit, net, component, "1");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    [[maybe_unused]] const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 20.0}});
    [[maybe_unused]] const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{0.0, 0.0}, volt::Point{10.0, 0.0}}});
    [[maybe_unused]] const auto label =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{0.0, -2.0}});

    const auto report = volt::validate_schematic_readiness(schematic);

    REQUIRE(report.count() == 1);
    const auto &diagnostic = report.diagnostics().front();
    CHECK(diagnostic.severity() == volt::Severity::Error);
    CHECK(diagnostic.code() == volt::DiagnosticCode{"SCHEMATIC_PIN_NET_NOT_VISUALLY_COVERED"});
    CHECK(diagnostic.message() == "Schematic omits visual net coverage for R1 pin 1 (1) on VCC");
    REQUIRE(diagnostic.entities().size() == 4);
    CHECK(diagnostic.entities()[0] == volt::EntityRef::component(component));
    const auto pin = circuit.pin_by_number(component, "1").value();
    CHECK(diagnostic.entities()[1] == volt::EntityRef::pin(pin));
    CHECK(diagnostic.entities()[2] == volt::EntityRef::pin_def(circuit.pin(pin).definition()));
    CHECK(diagnostic.entities()[3] == volt::EntityRef::net(net));
}

TEST_CASE("Schematic readiness accepts wires and labels at connected symbol pin anchors") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = add_net(circuit);
    connect_pin_by_number(circuit, net, component, "1");
    connect_pin_by_number(circuit, net, component, "2");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    [[maybe_unused]] const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 20.0}});
    [[maybe_unused]] const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{40.0, 20.0}, volt::Point{30.0, 20.0}}});
    [[maybe_unused]] const auto label =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{60.0, 20.0}});

    const auto pins_before = circuit.net(net).pins();
    const auto report = volt::validate_schematic_readiness(schematic);

    CHECK(report.empty());
    CHECK(circuit.net(net).pins() == pins_before);
}

TEST_CASE("Schematic geometry rejects non-finite coordinates") {
    CHECK_THROWS_AS(volt::Point(0.0, std::numeric_limits<double>::infinity()),
                    std::invalid_argument);
    CHECK_THROWS_AS(volt::SymbolCircle(volt::Point{0.0, 0.0}, -1.0), std::invalid_argument);
    CHECK_THROWS_AS(
        volt::SymbolArc(volt::Point{0.0, 0.0}, 1.0, std::numeric_limits<double>::infinity(), 90.0),
        std::invalid_argument);
    CHECK_THROWS_AS(volt::SymbolText("", volt::Point{0.0, 0.0}), std::invalid_argument);
}
