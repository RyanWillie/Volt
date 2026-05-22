#include "schematic_test_helpers.hpp"

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

TEST_CASE("Schematic readiness accepts coincident same-net symbol pins as visual coverage") {
    volt::Circuit circuit;
    const auto left = add_resistor(circuit, "R1");
    const auto right = add_resistor(circuit, "R2");
    const auto net = add_named_net(circuit, "LED_A");
    connect_pin_by_number(circuit, net, left, "2");
    connect_pin_by_number(circuit, net, right, "1");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    [[maybe_unused]] const auto left_instance =
        schematic.place_symbol(sheet, volt::SymbolInstance{symbol, left, volt::Point{0.0, 0.0}});
    [[maybe_unused]] const auto right_instance =
        schematic.place_symbol(sheet, volt::SymbolInstance{symbol, right, volt::Point{20.0, 0.0}});

    const auto report = volt::validate_schematic_readiness(schematic);

    CHECK(report.empty());
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
