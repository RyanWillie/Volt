#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
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

volt::ComponentId add_resistor(volt::Circuit &circuit, const std::string &reference) {
    const auto first_pin =
        circuit.add_pin_definition(volt::PinDefinition{"1", "1", volt::PinRole::Passive});
    const auto second_pin =
        circuit.add_pin_definition(volt::PinDefinition{"2", "2", volt::PinRole::Passive});
    const auto definition = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    return circuit.instantiate_component(definition, volt::ReferenceDesignator{reference});
}

volt::ComponentId add_resistor(volt::Circuit &circuit) { return add_resistor(circuit, "R1"); }

volt::ComponentId add_three_pin_component(volt::Circuit &circuit) {
    const auto first_pin =
        circuit.add_pin_definition(volt::PinDefinition{"A", "1", volt::PinRole::Passive});
    const auto second_pin =
        circuit.add_pin_definition(volt::PinDefinition{"B", "2", volt::PinRole::Passive});
    const auto third_pin =
        circuit.add_pin_definition(volt::PinDefinition{"C", "3", volt::PinRole::Passive});
    const auto definition = circuit.add_component_definition(
        volt::ComponentDefinition{"ThreePin", std::vector{first_pin, second_pin, third_pin}});
    return circuit.instantiate_component(definition, volt::ReferenceDesignator{"U1"});
}

volt::NetId add_net(volt::Circuit &circuit) {
    return circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});
}

volt::NetId add_named_net(volt::Circuit &circuit, std::string name) {
    return circuit.add_net(volt::Net{volt::NetName{std::move(name)}, volt::NetKind::Signal});
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

bool report_has_code(const volt::DiagnosticReport &report, const std::string &code) {
    return std::any_of(report.diagnostics().begin(), report.diagnostics().end(),
                       [&code](const volt::Diagnostic &diagnostic) {
                           return diagnostic.code() == volt::DiagnosticCode{code};
                       });
}

const volt::Diagnostic &require_diagnostic(const volt::DiagnosticReport &report,
                                           const std::string &code) {
    const auto it = std::find_if(report.diagnostics().begin(), report.diagnostics().end(),
                                 [&code](const volt::Diagnostic &diagnostic) {
                                     return diagnostic.code() == volt::DiagnosticCode{code};
                                 });
    REQUIRE(it != report.diagnostics().end());
    return *it;
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

TEST_CASE("Schematic stores professional primitives without changing logical connectivity") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto vcc = add_net(circuit);
    const auto gnd = circuit.add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});
    const auto no_connect_pin = circuit.pin_by_number(component, "2").value();
    circuit.mark_intentional_no_connect_pin(no_connect_pin);

    auto metadata = volt::SheetMetadata{
        "Power sheet",
        volt::SheetSize{420.0, 297.0},
        std::vector{volt::TitleBlockField{"Revision", "A"}, volt::TitleBlockField{"Owner", "Volt"}},
    };
    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Power", std::move(metadata)});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 20.0}});
    const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{vcc,
                             std::vector{volt::Point{10.0, 20.0}, volt::Point{30.0, 20.0},
                                         volt::Point{30.0, 40.0}},
                             volt::RouteIntent::Orthogonal});

    const auto junction =
        schematic.add_junction(sheet, volt::Junction{vcc, volt::Point{30.0, 20.0}});
    const auto power = schematic.add_power_port(
        sheet, volt::PowerPort{vcc, volt::PowerPortKind::Power, volt::Point{10.0, 16.0},
                               volt::SchematicOrientation::Up});
    const auto ground = schematic.add_power_port(
        sheet, volt::PowerPort{gnd, volt::PowerPortKind::Ground, volt::Point{50.0, 24.0},
                               volt::SchematicOrientation::Down});
    const auto no_connect = schematic.add_no_connect_marker(
        sheet, volt::NoConnectMarker{no_connect_pin, volt::Point{60.0, 20.0},
                                     volt::SchematicOrientation::Right, "factory option"});
    const auto sheet_port = schematic.add_sheet_port(
        sheet, volt::SheetPort{vcc, "VIN", volt::SheetPortKind::OffPage, volt::Point{5.0, 20.0},
                               volt::SchematicOrientation::Right});
    const auto field = schematic.add_symbol_field(
        sheet, volt::SymbolField{instance, "value", "10k", volt::Point{40.0, 32.0},
                                 volt::SchematicOrientation::Right});

    CHECK(schematic.sheet(sheet).metadata().title() == "Power sheet");
    CHECK(schematic.sheet(sheet).metadata().size().width() == 420.0);
    CHECK(schematic.sheet(sheet).metadata().size().height() == 297.0);
    REQUIRE(schematic.sheet(sheet).metadata().title_block().size() == 2);
    CHECK(schematic.sheet(sheet).metadata().title_block()[0].key() == "Revision");
    CHECK(schematic.sheet(sheet).metadata().title_block()[0].value() == "A");
    CHECK(schematic.wire_run(wire).route_intent() == volt::RouteIntent::Orthogonal);
    CHECK(junction == volt::JunctionId{0});
    CHECK(power == volt::PowerPortId{0});
    CHECK(ground == volt::PowerPortId{1});
    CHECK(no_connect == volt::NoConnectMarkerId{0});
    CHECK(sheet_port == volt::SheetPortId{0});
    CHECK(field == volt::SymbolFieldId{0});
    CHECK(schematic.sheet(sheet).junctions() == std::vector{junction});
    CHECK(schematic.sheet(sheet).power_ports() == std::vector{power, ground});
    CHECK(schematic.sheet(sheet).no_connect_markers() == std::vector{no_connect});
    CHECK(schematic.sheet(sheet).sheet_ports() == std::vector{sheet_port});
    CHECK(schematic.sheet(sheet).symbol_fields() == std::vector{field});
    CHECK(schematic.junction(junction).net() == vcc);
    CHECK(schematic.power_port(power).kind() == volt::PowerPortKind::Power);
    CHECK(schematic.power_port(ground).net() == gnd);
    CHECK(schematic.no_connect_marker(no_connect).pin() == no_connect_pin);
    CHECK(schematic.no_connect_marker(no_connect).reason() == "factory option");
    CHECK(schematic.sheet_port(sheet_port).name() == "VIN");
    CHECK(schematic.symbol_field(field).symbol_instance() == instance);
    CHECK(schematic.symbol_field(field).name() == "value");
    CHECK(schematic.symbol_field(field).value() == "10k");
    CHECK(circuit.net(vcc).pins().empty());
    CHECK(circuit.net(gnd).pins().empty());
    CHECK_FALSE(circuit.net_of(no_connect_pin).has_value());
}

TEST_CASE("Schematic rejects empty presentation names") {
    CHECK_THROWS_AS(volt::SymbolDefinition{""}, std::invalid_argument);
    CHECK_THROWS_AS(volt::Sheet{""}, std::invalid_argument);
    CHECK_THROWS_AS(volt::SheetMetadata{""}, std::invalid_argument);
    CHECK_THROWS_AS((volt::SheetSize{0.0, 210.0}), std::invalid_argument);
    CHECK_THROWS_AS((volt::TitleBlockField{"", "A"}), std::invalid_argument);
    CHECK_THROWS_AS((volt::TitleBlockField{"Revision", ""}), std::invalid_argument);
    CHECK_THROWS_AS(
        (volt::SheetPort{volt::NetId{0}, "", volt::SheetPortKind::OffPage, volt::Point{0.0, 0.0}}),
        std::invalid_argument);
    CHECK_THROWS_AS((volt::SymbolField{volt::SymbolInstanceId{0}, "", "R1", volt::Point{0.0, 0.0}}),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        (volt::SymbolField{volt::SymbolInstanceId{0}, "reference", "", volt::Point{0.0, 0.0}}),
        std::invalid_argument);
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

TEST_CASE("Schematic rejects professional primitives with missing references") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = add_net(circuit);
    const auto pin = circuit.pin_by_number(component, "1").value();

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto other_sheet = schematic.add_sheet(volt::Sheet{"Other"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 20.0}});

    CHECK_THROWS_AS(
        schematic.add_junction(volt::SheetId{99}, volt::Junction{net, volt::Point{0.0, 0.0}}),
        std::out_of_range);
    CHECK_THROWS_AS(
        schematic.add_junction(sheet, volt::Junction{volt::NetId{99}, volt::Point{0.0, 0.0}}),
        std::out_of_range);
    CHECK_THROWS_AS(
        schematic.add_power_port(sheet, volt::PowerPort{volt::NetId{99}, volt::PowerPortKind::Power,
                                                        volt::Point{0.0, 0.0}}),
        std::out_of_range);
    CHECK_THROWS_AS(schematic.add_no_connect_marker(
                        sheet, volt::NoConnectMarker{volt::PinId{99}, volt::Point{0.0, 0.0}}),
                    std::out_of_range);
    CHECK_THROWS_AS(schematic.add_sheet_port(sheet, volt::SheetPort{volt::NetId{99}, "VIN",
                                                                    volt::SheetPortKind::OffPage,
                                                                    volt::Point{0.0, 0.0}}),
                    std::out_of_range);
    CHECK_THROWS_AS(
        schematic.add_symbol_field(sheet, volt::SymbolField{volt::SymbolInstanceId{99}, "reference",
                                                            "R1", volt::Point{0.0, 0.0}}),
        std::out_of_range);
    CHECK_THROWS_AS(
        schematic.add_symbol_field(
            other_sheet, volt::SymbolField{instance, "reference", "R1", volt::Point{0.0, 0.0}}),
        std::logic_error);
    CHECK_NOTHROW(
        schematic.add_no_connect_marker(sheet, volt::NoConnectMarker{pin, volt::Point{0.0, 0.0}}));
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

    const auto repeated_point_far_away = volt::classify_segment_relationship(
        volt::SchematicSegment{volt::Point{5.0, 10.0}, volt::Point{5.0, 10.0}}, horizontal);
    CHECK(repeated_point_far_away == volt::SchematicSegmentRelationship::Disjoint);

    const auto repeated_point_on_segment = volt::classify_segment_relationship(
        volt::SchematicSegment{volt::Point{5.0, 0.0}, volt::Point{5.0, 0.0}}, horizontal);
    CHECK(repeated_point_on_segment == volt::SchematicSegmentRelationship::EndpointTouch);
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
    CHECK_THROWS_AS(schematic.add_junction(sheet, volt::Junction{vcc, volt::Point{2.0, 0.0}}),
                    std::logic_error);
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
        sheet, volt::WireRun{net, std::vector{volt::Point{0.0, 12.0}, volt::Point{10.0, 12.0}}});
    [[maybe_unused]] const auto label =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{0.0, 10.0}});

    const auto report = volt::validate_schematic_readiness(schematic);

    REQUIRE(report.count() == 1);
    const auto &diagnostic = report.diagnostics().front();
    CHECK(diagnostic.severity() == volt::Severity::Error);
    CHECK(diagnostic.code() == volt::DiagnosticCode{"SCHEMATIC_PIN_NET_NOT_VISUALLY_COVERED"});
    CHECK(diagnostic.message() ==
          "Schematic sheet 'Main' omits visual net coverage for R1 pin 1 (1) on VCC");
    REQUIRE(diagnostic.entities().size() == 5);
    CHECK(diagnostic.entities()[0] == volt::EntityRef::sheet(sheet));
    CHECK(diagnostic.entities()[1] == volt::EntityRef::component(component));
    const auto pin = circuit.pin_by_number(component, "1").value();
    CHECK(diagnostic.entities()[2] == volt::EntityRef::pin(pin));
    CHECK(diagnostic.entities()[3] == volt::EntityRef::pin_def(circuit.pin(pin).definition()));
    CHECK(diagnostic.entities()[4] == volt::EntityRef::net(net));
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

TEST_CASE("Schematic validation reports connected logical pins missing from a placed symbol") {
    volt::Circuit circuit;
    const auto component = add_three_pin_component(circuit);
    const auto net = add_net(circuit);
    connect_pin_by_number(circuit, net, component, "3");

    auto symbol = volt::SymbolDefinition{"TwoPinView"};
    symbol.add_pin(
        volt::SymbolPin{"A", "1", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Left});
    symbol.add_pin(
        volt::SymbolPin{"B", "2", volt::Point{20.0, 0.0}, volt::SchematicOrientation::Right});

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol_id = schematic.add_symbol_definition(std::move(symbol));
    const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol_id, component, volt::Point{40.0, 20.0}});

    const auto report = volt::validate_schematic_readiness(schematic);

    REQUIRE(report.count() == 1);
    const auto &diagnostic = report.diagnostics().front();
    CHECK(diagnostic.severity() == volt::Severity::Error);
    CHECK(diagnostic.code() == volt::DiagnosticCode{"SCHEMATIC_CONNECTED_PIN_MISSING_SYMBOL_PIN"});
    const auto pin = circuit.pin_by_number(component, "3").value();
    CHECK(diagnostic.entities() ==
          std::vector{volt::EntityRef::component(component), volt::EntityRef::pin(pin),
                      volt::EntityRef::pin_def(circuit.pin(pin).definition()),
                      volt::EntityRef::net(net), volt::EntityRef::symbol_instance(instance)});
}

TEST_CASE("Schematic placement rejects symbol pins that do not map to logical component pins") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);

    auto symbol = make_resistor_symbol();
    symbol.add_pin(
        volt::SymbolPin{"EXTRA", "99", volt::Point{40.0, 0.0}, volt::SchematicOrientation::Right});

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol_id = schematic.add_symbol_definition(std::move(symbol));

    CHECK_THROWS_AS(schematic.place_symbol(
                        sheet, volt::SymbolInstance{symbol_id, component, volt::Point{0.0, 0.0}}),
                    std::logic_error);
}

TEST_CASE("Schematic validation reports duplicate placements and unplaced connected components") {
    volt::Circuit circuit;
    const auto first = add_resistor(circuit, "R1");
    const auto second = add_resistor(circuit, "R2");
    const auto net = add_net(circuit);
    connect_pin_by_number(circuit, net, first, "1");
    connect_pin_by_number(circuit, net, second, "1");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    [[maybe_unused]] const auto first_instance =
        schematic.place_symbol(sheet, volt::SymbolInstance{symbol, first, volt::Point{40.0, 20.0}});
    [[maybe_unused]] const auto second_instance =
        schematic.place_symbol(sheet, volt::SymbolInstance{symbol, first, volt::Point{80.0, 20.0}});
    [[maybe_unused]] const auto first_label =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{40.0, 20.0}});
    [[maybe_unused]] const auto second_label =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{80.0, 20.0}});

    const auto report = volt::validate_schematic_readiness(schematic);

    CHECK(report_has_code(report, "SCHEMATIC_COMPONENT_DUPLICATE_PLACEMENT"));
    CHECK(report_has_code(report, "SCHEMATIC_COMPONENT_NOT_PLACED"));
}

TEST_CASE("Schematic validation does not require unplaced mechanical components") {
    volt::Circuit circuit;
    const auto pin_def =
        circuit.add_pin_definition(volt::PinDefinition{"1", "1", volt::PinRole::Ground});
    auto properties = volt::PropertyMap{};
    properties.set(volt::PropertyKey{"category"}, volt::PropertyValue{"mechanical"});
    const auto definition = circuit.add_component_definition(
        volt::ComponentDefinition{"MountingHole_Pad", std::vector{pin_def}, std::move(properties)});
    const auto hole = circuit.instantiate_component(definition, volt::ReferenceDesignator{"H1"});
    const auto net = circuit.add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});
    connect_pin_by_number(circuit, net, hole, "1");

    volt::Schematic schematic{circuit};
    [[maybe_unused]] const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});

    const auto report = volt::validate_schematic_readiness(schematic);

    CHECK_FALSE(report_has_code(report, "SCHEMATIC_COMPONENT_NOT_PLACED"));
}

TEST_CASE("Schematic validation reports fragmented pin-label coverage on multi-pin nets") {
    volt::Circuit circuit;
    const auto first = add_resistor(circuit, "R1");
    const auto second = add_resistor(circuit, "R2");
    const auto third = add_resistor(circuit, "R3");
    const auto net = add_net(circuit);
    connect_pin_by_number(circuit, net, first, "1");
    connect_pin_by_number(circuit, net, second, "1");
    connect_pin_by_number(circuit, net, third, "1");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    [[maybe_unused]] const auto first_instance =
        schematic.place_symbol(sheet, volt::SymbolInstance{symbol, first, volt::Point{40.0, 20.0}});
    [[maybe_unused]] const auto second_instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, second, volt::Point{80.0, 20.0}});
    [[maybe_unused]] const auto third_instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, third, volt::Point{120.0, 20.0}});
    [[maybe_unused]] const auto first_label =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{40.0, 20.0}});
    [[maybe_unused]] const auto second_label =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{80.0, 20.0}});
    [[maybe_unused]] const auto third_label =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{120.0, 20.0}});

    const auto report = volt::validate_schematic_readiness(schematic);

    REQUIRE(report.count() == 1);
    CHECK(report.diagnostics().front().severity() == volt::Severity::Warning);
    CHECK(report.diagnostics().front().code() ==
          volt::DiagnosticCode{"SCHEMATIC_NET_FRAGMENTED_PIN_LABELS"});
}

TEST_CASE("Schematic validation reports repeated labels past the sheet threshold") {
    volt::Circuit circuit;
    const auto net = add_net(circuit);

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    [[maybe_unused]] const auto first =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{10.0, 10.0}});
    [[maybe_unused]] const auto second =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{20.0, 10.0}});
    [[maybe_unused]] const auto third =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{30.0, 10.0}});
    [[maybe_unused]] const auto fourth =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{40.0, 10.0}});
    [[maybe_unused]] const auto fifth =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{50.0, 10.0}});

    const auto report = volt::validate_schematic_readiness(schematic);

    REQUIRE(report.count() == 1);
    CHECK(report.diagnostics().front().severity() == volt::Severity::Warning);
    CHECK(report.diagnostics().front().code() ==
          volt::DiagnosticCode{"SCHEMATIC_REPEATED_NET_LABELS"});
}

TEST_CASE("Schematic validation reports off-sheet presentation objects") {
    volt::Circuit circuit;
    const auto net = add_net(circuit);

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(
        volt::Sheet{"Main", volt::SheetMetadata{"Main", volt::SheetSize{20.0, 20.0}}});
    const auto label = schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{25.0, 10.0}});

    const auto report = volt::validate_schematic_readiness(schematic);

    REQUIRE(report.count() == 1);
    CHECK(report.diagnostics().front().severity() == volt::Severity::Warning);
    CHECK(report.diagnostics().front().code() ==
          volt::DiagnosticCode{"SCHEMATIC_OBJECT_OUTSIDE_SHEET_BOUNDS"});
    CHECK(report.diagnostics().front().entities() == std::vector{volt::EntityRef::sheet(sheet),
                                                                 volt::EntityRef::net_label(label),
                                                                 volt::EntityRef::net(net)});
}

TEST_CASE("Schematic validation reports same-net crossings without explicit junctions") {
    volt::Circuit circuit;
    const auto net = add_net(circuit);

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    [[maybe_unused]] const auto horizontal = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{0.0, 10.0}, volt::Point{20.0, 10.0}}});
    [[maybe_unused]] const auto vertical = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{10.0, 0.0}, volt::Point{10.0, 20.0}}});

    const auto report = volt::validate_schematic_readiness(schematic);

    REQUIRE(report.count() == 1);
    CHECK(report.diagnostics().front().severity() == volt::Severity::Warning);
    CHECK(report.diagnostics().front().code() ==
          volt::DiagnosticCode{"SCHEMATIC_WIRE_CROSSING_WITHOUT_JUNCTION"});
}

TEST_CASE("Schematic validation reports no-connect markers without kernel-owned intent") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto pin = circuit.pin_by_number(component, "1").value();

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    [[maybe_unused]] const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 20.0}});
    const auto marker =
        schematic.add_no_connect_marker(sheet, volt::NoConnectMarker{pin, volt::Point{40.0, 20.0}});

    const auto report = volt::validate_schematic_readiness(schematic);

    REQUIRE(report.count() == 1);
    CHECK(report.diagnostics().front().severity() == volt::Severity::Error);
    CHECK(report.diagnostics().front().code() ==
          volt::DiagnosticCode{"SCHEMATIC_NO_CONNECT_MARKER_WITHOUT_INTENT"});
    CHECK(report.diagnostics().front().entities() ==
          std::vector{volt::EntityRef::no_connect_marker(marker), volt::EntityRef::pin(pin),
                      volt::EntityRef::component(component)});
}

TEST_CASE("Schematic readability reports usable-area and title-block layout issues") {
    volt::Circuit circuit;
    const auto net = add_net(circuit);

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{
        "Main",
        volt::SheetMetadata{
            "Main",
            volt::SheetSize{100.0, 80.0},
            std::vector{volt::TitleBlockField{"Revision", "A"}},
            volt::SheetOrientation::Landscape,
            volt::SheetFrame{true, volt::SheetMargins{10.0, 10.0, 10.0, 10.0}},
        },
    });
    const auto margin_label =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{5.0, 20.0}});
    const auto title_label =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{20.0, 65.0}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &outside = require_diagnostic(report, "SCHEMATIC_OBJECT_OUTSIDE_USABLE_AREA");
    CHECK(outside.severity() == volt::Severity::Error);
    CHECK(outside.entities() == std::vector{volt::EntityRef::sheet(sheet),
                                            volt::EntityRef::net_label(margin_label),
                                            volt::EntityRef::net(net)});

    const auto &title = require_diagnostic(report, "SCHEMATIC_OBJECT_OVERLAPS_TITLE_BLOCK");
    CHECK(title.severity() == volt::Severity::Warning);
    CHECK(title.entities() == std::vector{volt::EntityRef::sheet(sheet),
                                          volt::EntityRef::net_label(title_label),
                                          volt::EntityRef::net(net)});
}

TEST_CASE("Schematic readability uses label-dependent sheet-port bounds") {
    volt::Circuit circuit;
    const auto net = add_net(circuit);

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{
        "Main",
        volt::SheetMetadata{
            "Main",
            volt::SheetSize{70.0, 40.0},
            {},
            volt::SheetOrientation::Landscape,
            volt::SheetFrame{true, volt::SheetMargins{10.0, 10.0, 10.0, 10.0}},
        },
    });
    const auto port = schematic.add_sheet_port(sheet, volt::SheetPort{net, "LONG_PORT_LABEL",
                                                                      volt::SheetPortKind::OffPage,
                                                                      volt::Point{38.0, 20.0}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &outside = require_diagnostic(report, "SCHEMATIC_OBJECT_OUTSIDE_USABLE_AREA");
    CHECK(outside.severity() == volt::Severity::Error);
    CHECK(outside.entities() == std::vector{volt::EntityRef::sheet(sheet),
                                            volt::EntityRef::sheet_port(port),
                                            volt::EntityRef::net(net)});
}

TEST_CASE("Schematic readability reports objects outside their authored region") {
    volt::Circuit circuit;
    const auto net = add_net(circuit);

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto region = schematic.add_sheet_region(
        sheet,
        volt::SheetRegion{"power", "Power", volt::SheetRegionBounds{10.0, 10.0, 20.0, 20.0}});
    const auto label =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{35.0, 15.0},
                                                      volt::SchematicOrientation::Right, region});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_OBJECT_OUTSIDE_AUTHORED_REGION");
    CHECK(diagnostic.severity() == volt::Severity::Error);
    CHECK(diagnostic.entities() == std::vector{volt::EntityRef::sheet(sheet),
                                               volt::EntityRef::net_label(label),
                                               volt::EntityRef::net(net)});
}

TEST_CASE("Schematic readability reports duplicate junctions and hard-to-read labels") {
    volt::Circuit circuit;
    const auto scoped = add_named_net(circuit, "PWR/OUT_3V3");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto first =
        schematic.add_junction(sheet, volt::Junction{scoped, volt::Point{30.0, 20.0}});
    const auto second =
        schematic.add_junction(sheet, volt::Junction{scoped, volt::Point{30.0, 20.0}});
    const auto label = schematic.add_net_label(
        sheet, volt::NetLabel{scoped, volt::Point{40.0, 20.0}, volt::SchematicOrientation::Down});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &duplicate = require_diagnostic(report, "SCHEMATIC_DUPLICATE_JUNCTION_MARKERS");
    CHECK(duplicate.severity() == volt::Severity::Warning);
    CHECK(duplicate.entities() ==
          std::vector{volt::EntityRef::sheet(sheet), volt::EntityRef::net(scoped),
                      volt::EntityRef::junction(first), volt::EntityRef::junction(second)});

    const auto &rotated = require_diagnostic(report, "SCHEMATIC_TEXT_NOT_HORIZONTAL");
    CHECK(rotated.entities() == std::vector{volt::EntityRef::sheet(sheet),
                                            volt::EntityRef::net_label(label),
                                            volt::EntityRef::net(scoped)});

    const auto &long_label = require_diagnostic(report, "SCHEMATIC_OVERLONG_DISPLAY_LABEL");
    CHECK(long_label.entities() == std::vector{volt::EntityRef::sheet(sheet),
                                               volt::EntityRef::net_label(label),
                                               volt::EntityRef::net(scoped)});
}

TEST_CASE("Schematic readability reports missing passive values and dense no-connect clusters") {
    volt::Circuit circuit;
    const auto resistor = add_resistor(circuit);
    circuit.set_component_property(resistor, volt::PropertyKey{"value"},
                                   volt::PropertyValue{"10k"});

    auto pin_definitions = std::vector<volt::PinDefId>{};
    for (auto index = 1; index <= 6; ++index) {
        pin_definitions.push_back(circuit.add_pin_definition(
            volt::PinDefinition{"NC" + std::to_string(index), std::to_string(index),
                                volt::PinRole::NoConnect, volt::ConnectionRequirement::Optional}));
    }
    const auto connector_definition =
        circuit.add_component_definition(volt::ComponentDefinition{"Header", pin_definitions});
    const auto connector =
        circuit.instantiate_component(connector_definition, volt::ReferenceDesignator{"J1"});

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto resistor_symbol = schematic.add_symbol_definition(make_resistor_symbol());
    const auto resistor_instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{resistor_symbol, resistor, volt::Point{40.0, 20.0}});
    const auto connector_symbol = schematic.add_symbol_definition(volt::SymbolDefinition{"Header"});
    const auto connector_instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{connector_symbol, connector, volt::Point{80.0, 40.0}});
    auto markers = std::vector<volt::NoConnectMarkerId>{};
    for (auto index = 1; index <= 6; ++index) {
        markers.push_back(schematic.add_no_connect_marker(
            sheet,
            volt::NoConnectMarker{circuit.pin_by_number(connector, std::to_string(index)).value(),
                                  volt::Point{80.0 + static_cast<double>(index), 40.0}}));
    }

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &missing_value = require_diagnostic(report, "SCHEMATIC_PASSIVE_VALUE_FIELD_MISSING");
    CHECK(missing_value.entities() ==
          std::vector{volt::EntityRef::sheet(sheet), volt::EntityRef::component(resistor),
                      volt::EntityRef::symbol_instance(resistor_instance)});
    static_cast<void>(schematic.add_symbol_field(
        sheet, volt::SymbolField{resistor_instance, "Value", "10k", volt::Point{40.0, 14.0}}));
    const auto visible_value_report = volt::validate_schematic_readability(schematic);
    CHECK_FALSE(report_has_code(visible_value_report, "SCHEMATIC_PASSIVE_VALUE_FIELD_MISSING"));

    const auto &cluster = require_diagnostic(report, "SCHEMATIC_DENSE_NO_CONNECT_MARKERS");
    CHECK(cluster.severity() == volt::Severity::Warning);
    CHECK(cluster.entities().front() == volt::EntityRef::sheet(sheet));
    CHECK(cluster.entities()[1] == volt::EntityRef::symbol_instance(connector_instance));
    for (std::size_t index = 0; index < markers.size(); ++index) {
        CHECK(cluster.entities()[index + 2U] == volt::EntityRef::no_connect_marker(markers[index]));
    }
}

TEST_CASE("Schematic readability reports overlapping text bounds") {
    volt::Circuit circuit;
    const auto net = add_named_net(circuit, "VCC");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto first = schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{40.0, 20.0}});
    const auto second =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{42.0, 20.0}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &collision = require_diagnostic(report, "SCHEMATIC_TEXT_COLLISION");
    CHECK(collision.severity() == volt::Severity::Warning);
    const auto &entities = collision.entities();
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::net_label(first)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::net_label(second)) !=
          entities.end());
}

TEST_CASE("Schematic validation accepts no-connect markers on no-connect pin definitions") {
    volt::Circuit circuit;
    const auto pin_definition = circuit.add_pin_definition(volt::PinDefinition{
        "NC", "1", volt::PinRole::NoConnect, volt::ConnectionRequirement::Optional});
    const auto component_definition = circuit.add_component_definition(
        volt::ComponentDefinition{"NoConnectPad", std::vector{pin_definition}});
    const auto component =
        circuit.instantiate_component(component_definition, volt::ReferenceDesignator{"TP1"});
    const auto pin = circuit.pin_by_number(component, "1").value();

    auto symbol_definition = volt::SymbolDefinition{"NoConnectPad"};
    symbol_definition.add_pin(
        volt::SymbolPin{"NC", "1", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Right});

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(std::move(symbol_definition));
    [[maybe_unused]] const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 20.0}});
    [[maybe_unused]] const auto marker =
        schematic.add_no_connect_marker(sheet, volt::NoConnectMarker{pin, volt::Point{40.0, 20.0}});

    const auto report = volt::validate_schematic_readiness(schematic);

    CHECK_FALSE(report.has_errors());
    CHECK(report.count() == 0);
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
