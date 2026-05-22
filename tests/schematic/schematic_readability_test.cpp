#include "schematic_test_helpers.hpp"

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

TEST_CASE("Schematic readability reports title-block text overflow") {
    volt::Circuit circuit;

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{
        "Main",
        volt::SheetMetadata{
            "Main",
            volt::SheetSize{100.0, 80.0},
            std::vector{volt::TitleBlockField{"File", "examples/timer_555_led_blinker/main.py"}},
            volt::SheetOrientation::Landscape,
            volt::SheetFrame{true, volt::SheetMargins{10.0, 10.0, 10.0, 10.0}},
        },
    });

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_TITLE_BLOCK_TEXT_OVERFLOW");
    CHECK(diagnostic.severity() == volt::Severity::Warning);
    CHECK(diagnostic.entities() == std::vector{volt::EntityRef::sheet(sheet)});
    CHECK_FALSE(report_has_code(volt::validate_schematic_readiness(schematic),
                                "SCHEMATIC_TITLE_BLOCK_TEXT_OVERFLOW"));
}

TEST_CASE("Schematic readability checks title-block text against the rendered block width") {
    volt::Circuit circuit;

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{
        "Main",
        volt::SheetMetadata{
            "Main",
            volt::SheetSize{30.0, 40.0},
            std::vector{volt::TitleBlockField{"Revision", "A"}},
            volt::SheetOrientation::Landscape,
            volt::SheetFrame{true, volt::SheetMargins{10.0, 10.0, 10.0, 10.0}},
        },
    });

    const auto report = volt::validate_schematic_readability(schematic);

    CHECK(report_has_code(report, "SCHEMATIC_TITLE_BLOCK_TEXT_OVERFLOW"));
    CHECK(report_has_code_and_entities(report, "SCHEMATIC_TITLE_BLOCK_TEXT_OVERFLOW",
                                       std::vector{volt::EntityRef::sheet(sheet)}));
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

TEST_CASE("Schematic readability reports overlapping authored region content bounds") {
    volt::Circuit circuit;
    const auto net = add_net(circuit);

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto first_region = schematic.add_sheet_region(
        sheet,
        volt::SheetRegion{"logic", "Logic", volt::SheetRegionBounds{10.0, 10.0, 40.0, 40.0}});
    const auto second_region = schematic.add_sheet_region(
        sheet, volt::SheetRegion{"connectors", "Connectors",
                                 volt::SheetRegionBounds{30.0, 10.0, 40.0, 40.0}});
    const auto first_label = schematic.add_net_label(
        sheet, volt::NetLabel{net, volt::Point{28.0, 20.0}, volt::SchematicOrientation::Right,
                              first_region, "AAAAAA"});
    const auto second_label = schematic.add_net_label(
        sheet, volt::NetLabel{net, volt::Point{30.0, 20.0}, volt::SchematicOrientation::Right,
                              second_region, "B"});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic =
        require_diagnostic(report, "SCHEMATIC_AUTHORED_REGION_CONTENT_OVERLAP");
    CHECK(diagnostic.severity() == volt::Severity::Error);
    CHECK(diagnostic.entities() ==
          std::vector{volt::EntityRef::sheet(sheet), volt::EntityRef::net_label(first_label),
                      volt::EntityRef::net(net), volt::EntityRef::net_label(second_label),
                      volt::EntityRef::net(net)});
}

TEST_CASE("Schematic readability accepts adjacent authored region content bounds") {
    volt::Circuit circuit;
    const auto net = add_net(circuit);

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto first_region = schematic.add_sheet_region(
        sheet,
        volt::SheetRegion{"logic", "Logic", volt::SheetRegionBounds{10.0, 10.0, 20.0, 20.0}});
    const auto second_region = schematic.add_sheet_region(
        sheet, volt::SheetRegion{"connectors", "Connectors",
                                 volt::SheetRegionBounds{40.0, 10.0, 20.0, 20.0}});
    static_cast<void>(schematic.add_net_label(
        sheet, volt::NetLabel{net, volt::Point{12.0, 20.0}, volt::SchematicOrientation::Right,
                              first_region, "A"}));
    static_cast<void>(schematic.add_net_label(
        sheet, volt::NetLabel{net, volt::Point{42.0, 20.0}, volt::SchematicOrientation::Right,
                              second_region, "B"}));

    const auto report = volt::validate_schematic_readability(schematic);

    CHECK_FALSE(report_has_code(report, "SCHEMATIC_AUTHORED_REGION_CONTENT_OVERLAP"));
}

TEST_CASE("Schematic readability keeps region spills distinct from region content overlap") {
    volt::Circuit circuit;
    const auto net = add_net(circuit);

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto first_region = schematic.add_sheet_region(
        sheet,
        volt::SheetRegion{"logic", "Logic", volt::SheetRegionBounds{10.0, 10.0, 20.0, 20.0}});
    const auto second_region = schematic.add_sheet_region(
        sheet, volt::SheetRegion{"connectors", "Connectors",
                                 volt::SheetRegionBounds{70.0, 10.0, 20.0, 20.0}});
    static_cast<void>(schematic.add_net_label(
        sheet, volt::NetLabel{net, volt::Point{35.0, 20.0}, volt::SchematicOrientation::Right,
                              first_region, "A"}));
    static_cast<void>(schematic.add_net_label(
        sheet, volt::NetLabel{net, volt::Point{72.0, 20.0}, volt::SchematicOrientation::Right,
                              second_region, "B"}));

    const auto report = volt::validate_schematic_readability(schematic);

    CHECK(report_has_code(report, "SCHEMATIC_OBJECT_OUTSIDE_AUTHORED_REGION"));
    CHECK_FALSE(report_has_code(report, "SCHEMATIC_AUTHORED_REGION_CONTENT_OVERLAP"));
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
    CHECK(collision.severity() == volt::Severity::Error);
    const auto &entities = collision.entities();
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::net_label(first)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::net_label(second)) !=
          entities.end());
}

TEST_CASE("Schematic text layout moves net labels away from wire keepouts") {
    volt::Circuit circuit;
    const auto net = add_named_net(circuit, "VCC");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto label = schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{40.0, 40.0}});
    const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{42.0, 39.4}, volt::Point{58.0, 39.4}}});
    const auto wire_points_before = schematic.wire_run(wire).points();

    volt::layout_schematic_text(schematic);

    CHECK(schematic.wire_run(wire).points() == wire_points_before);
    CHECK(schematic.net_label(label).position() == volt::Point{40.0, 40.0});
    CHECK_FALSE(schematic.net_label(label).text_position() == volt::Point{40.0, 40.0});
    CHECK_FALSE(report_has_code(volt::validate_schematic_readability(schematic),
                                "SCHEMATIC_TEXT_TOUCHES_WIRE"));
}

TEST_CASE("Schematic text layout moves symbol fields away from their owning symbol body") {
    volt::Circuit circuit;
    const auto component = add_four_pin_component(circuit, "U1");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_four_pin_ic_symbol());
    const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{50.0, 50.0}});
    const auto field = schematic.add_symbol_field(
        sheet, volt::SymbolField{instance, "value", "STM32F405", volt::Point{54.0, 50.0}});

    volt::layout_schematic_text(schematic);

    CHECK_FALSE(schematic.symbol_field(field).position() == volt::Point{54.0, 50.0});
    CHECK_FALSE(report_has_code(volt::validate_schematic_readability(schematic),
                                "SCHEMATIC_TEXT_TOUCHES_SYMBOL"));
}

TEST_CASE("Schematic readability reports oversized sheet and off-page tags") {
    volt::Circuit circuit;
    const auto net = add_named_net(circuit, "A_VERY_WIDE_DEBUG_SIGNAL_NAME");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto port = schematic.add_sheet_port(
        sheet, volt::SheetPort{net, "A_VERY_WIDE_DEBUG_SIGNAL_NAME", volt::SheetPortKind::OffPage,
                               volt::Point{40.0, 50.0}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_OVERSIZED_PORT_TAG");
    CHECK(diagnostic.severity() == volt::Severity::Warning);
    CHECK(diagnostic.entities() == std::vector{volt::EntityRef::sheet(sheet),
                                               volt::EntityRef::sheet_port(port),
                                               volt::EntityRef::net(net)});
}

TEST_CASE("Schematic readability reports oversized power-port labels") {
    volt::Circuit circuit;
    const auto net = add_named_net(circuit, "POWER_DEBUG_RAIL_WITH_A_LONG_NAME");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto port = schematic.add_power_port(
        sheet, volt::PowerPort{net, volt::PowerPortKind::Power, volt::Point{40.0, 50.0},
                               volt::SchematicOrientation::Up, std::nullopt,
                               std::optional<std::string>{"POWER_DEBUG_RAIL_WITH_A_LONG_NAME"}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_OVERSIZED_PORT_TAG");
    CHECK(diagnostic.severity() == volt::Severity::Warning);
    CHECK(diagnostic.entities() == std::vector{volt::EntityRef::sheet(sheet),
                                               volt::EntityRef::power_port(port),
                                               volt::EntityRef::net(net)});
}

TEST_CASE("Schematic readability reports symbol fields far from their owning symbol") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{50.0, 50.0}});
    const auto field = schematic.add_symbol_field(
        sheet, volt::SymbolField{instance, "Value", "10k", volt::Point{110.0, 90.0}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_SYMBOL_FIELD_FAR_FROM_SYMBOL");
    CHECK(diagnostic.severity() == volt::Severity::Warning);
    CHECK(diagnostic.entities() == std::vector{volt::EntityRef::sheet(sheet),
                                               volt::EntityRef::symbol_field(field),
                                               volt::EntityRef::symbol_instance(instance)});
}

TEST_CASE("Schematic readability reports text crossing wire geometry") {
    volt::Circuit circuit;
    const auto net = add_named_net(circuit, "VCC");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto label = schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{40.0, 40.0}});
    const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{42.0, 39.4}, volt::Point{58.0, 39.4}}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_TEXT_TOUCHES_WIRE");
    CHECK(diagnostic.severity() == volt::Severity::Error);
    const auto &entities = diagnostic.entities();
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::net_label(label)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::wire_run(wire)) !=
          entities.end());
}

TEST_CASE("Schematic readability reports power labels crossing wire geometry") {
    volt::Circuit circuit;
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto port = schematic.add_power_port(
        sheet, volt::PowerPort{net, volt::PowerPortKind::Power, volt::Point{50.0, 50.0}});
    const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{48.0, 40.6}, volt::Point{52.0, 40.6}}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_TEXT_TOUCHES_WIRE");
    CHECK(diagnostic.severity() == volt::Severity::Error);
    const auto &entities = diagnostic.entities();
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::power_port(port)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::wire_run(wire)) !=
          entities.end());
}

TEST_CASE("Schematic readability reports symbol text crossing wire geometry") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = add_named_net(circuit, "TRACE");

    auto text_symbol = volt::SymbolDefinition{"TextSymbol"};
    text_symbol.add_primitive(volt::SymbolText{"PIN", volt::Point{50.0, 50.0}});

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(text_symbol);
    const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{0.0, 0.0}});
    const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{48.0, 49.0}, volt::Point{58.0, 49.0}}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_TEXT_TOUCHES_WIRE");
    CHECK(diagnostic.severity() == volt::Severity::Error);
    const auto &entities = diagnostic.entities();
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::symbol_instance(instance)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::wire_run(wire)) !=
          entities.end());
}

TEST_CASE("Schematic readability reports labels crowding unrelated wire geometry") {
    volt::Circuit circuit;
    const auto label_net = circuit.add_net(volt::Net{volt::NetName{"+3V3"}, volt::NetKind::Power});
    const auto wire_net = circuit.add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto port = schematic.add_power_port(
        sheet, volt::PowerPort{label_net, volt::PowerPortKind::Power, volt::Point{20.0, 20.0},
                               volt::SchematicOrientation::Up, std::nullopt,
                               std::optional<std::string>{"+3V3"}, volt::Point{50.0, 50.0}});
    const auto wire = schematic.add_wire_run(
        sheet,
        volt::WireRun{wire_net, std::vector{volt::Point{49.0, 52.6}, volt::Point{62.0, 52.6}}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_TEXT_TOUCHES_WIRE");
    CHECK(diagnostic.severity() == volt::Severity::Error);
    const auto &entities = diagnostic.entities();
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::power_port(port)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::wire_run(wire)) !=
          entities.end());
}

TEST_CASE("Schematic readability reports text touching symbol outlines") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = add_named_net(circuit, "BOOT0");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{70.0, 70.0}});
    const auto label = schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{74.0, 70.0}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_TEXT_TOUCHES_SYMBOL");
    CHECK(diagnostic.severity() == volt::Severity::Error);
    const auto &entities = diagnostic.entities();
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::net_label(label)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::symbol_instance(instance)) !=
          entities.end());
}

TEST_CASE("Schematic readability reports overlapping symbol bodies") {
    volt::Circuit circuit;
    const auto first_component = add_four_pin_component(circuit, "U1");
    const auto second_component = add_four_pin_component(circuit, "U2");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_four_pin_ic_symbol());
    const auto first_instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, first_component, volt::Point{40.0, 40.0}});
    const auto second_instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, second_component, volt::Point{48.0, 40.0}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_SYMBOL_OVERLAP");
    CHECK(diagnostic.severity() == volt::Severity::Error);
    const auto &entities = diagnostic.entities();
    CHECK(std::find(entities.begin(), entities.end(),
                    volt::EntityRef::symbol_instance(first_instance)) != entities.end());
    CHECK(std::find(entities.begin(), entities.end(),
                    volt::EntityRef::symbol_instance(second_instance)) != entities.end());
}

TEST_CASE("Schematic readability accepts adjacent symbols sharing a same-net pin point") {
    volt::Circuit circuit;
    const auto first_component = add_resistor(circuit, "R1");
    const auto second_component = add_resistor(circuit, "R2");
    const auto net = add_named_net(circuit, "MID");
    connect_pin_by_number(circuit, net, first_component, "2");
    connect_pin_by_number(circuit, net, second_component, "1");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    static_cast<void>(schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, first_component, volt::Point{40.0, 40.0}}));
    static_cast<void>(schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, second_component, volt::Point{60.0, 40.0}}));

    const auto report = volt::validate_schematic_readability(schematic);

    CHECK_FALSE(report_has_code(report, "SCHEMATIC_SYMBOL_OVERLAP"));
}

TEST_CASE("Schematic readability reports wires crossing unrelated symbol bodies") {
    volt::Circuit circuit;
    const auto component = add_four_pin_component(circuit, "U1");
    const auto net = add_named_net(circuit, "BUS");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_four_pin_ic_symbol());
    const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 40.0}});
    const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{45.0, 20.0}, volt::Point{45.0, 60.0}}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_WIRE_CROSSES_SYMBOL");
    CHECK(diagnostic.severity() == volt::Severity::Error);
    const auto &entities = diagnostic.entities();
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::wire_run(wire)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::symbol_instance(instance)) !=
          entities.end());
}

TEST_CASE("Schematic readability accepts wires that leave their own symbol pins") {
    volt::Circuit circuit;
    const auto component = add_four_pin_component(circuit, "U1");
    const auto net = add_named_net(circuit, "NRST");
    connect_pin_by_number(circuit, net, component, "2");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_four_pin_ic_symbol());
    static_cast<void>(schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 40.0}}));
    static_cast<void>(schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{40.0, 40.0}, volt::Point{28.0, 40.0}}}));

    const auto report = volt::validate_schematic_readability(schematic);

    CHECK_FALSE(report_has_code(report, "SCHEMATIC_WIRE_CROSSES_SYMBOL"));
}

TEST_CASE("Schematic readability reports wire continuations routed through a symbol body") {
    volt::Circuit circuit;
    const auto component = add_four_pin_component(circuit, "U1");
    const auto net = add_named_net(circuit, "NRST");
    connect_pin_by_number(circuit, net, component, "2");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_four_pin_ic_symbol());
    const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 40.0}});
    const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{40.0, 40.0}, volt::Point{28.0, 40.0},
                                              volt::Point{28.0, 55.0}, volt::Point{45.0, 55.0}}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_WIRE_CROSSES_SYMBOL");
    const auto &entities = diagnostic.entities();
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::wire_run(wire)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::symbol_instance(instance)) !=
          entities.end());
}

TEST_CASE("Schematic readability reports terminal markers touching unrelated wires") {
    volt::Circuit circuit;
    const auto power_net = circuit.add_net(volt::Net{volt::NetName{"VDDA"}, volt::NetKind::Power});
    const auto ground_net = circuit.add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto port = schematic.add_power_port(
        sheet, volt::PowerPort{power_net, volt::PowerPortKind::Power, volt::Point{50.0, 50.0}});
    const auto wire = schematic.add_wire_run(
        sheet,
        volt::WireRun{ground_net, std::vector{volt::Point{46.0, 48.0}, volt::Point{54.0, 48.0}}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic =
        require_diagnostic(report, "SCHEMATIC_TERMINAL_TOUCHES_UNRELATED_WIRE");
    CHECK(diagnostic.severity() == volt::Severity::Error);
    const auto &entities = diagnostic.entities();
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::power_port(port)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::wire_run(wire)) !=
          entities.end());
}

TEST_CASE("Schematic readability reports terminal markers touching symbol bodies") {
    volt::Circuit circuit;
    const auto component = add_four_pin_component(circuit, "U1");
    const auto power_net = circuit.add_net(volt::Net{volt::NetName{"+3V3"}, volt::NetKind::Power});

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_four_pin_ic_symbol());
    const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 40.0}});
    const auto port = schematic.add_power_port(
        sheet, volt::PowerPort{power_net, volt::PowerPortKind::Power, volt::Point{62.0, 55.0}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_TERMINAL_TOUCHES_SYMBOL");
    CHECK(diagnostic.severity() == volt::Severity::Error);
    const auto &entities = diagnostic.entities();
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::power_port(port)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::symbol_instance(instance)) !=
          entities.end());
}

TEST_CASE("Schematic readability reports generic visual element collisions") {
    volt::Circuit circuit;
    const auto first_net = circuit.add_net(volt::Net{volt::NetName{"+3V3"}, volt::NetKind::Power});
    const auto second_net = circuit.add_net(volt::Net{volt::NetName{"+5V"}, volt::NetKind::Power});

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto first = schematic.add_power_port(
        sheet, volt::PowerPort{first_net, volt::PowerPortKind::Power, volt::Point{50.0, 50.0}});
    const auto second = schematic.add_power_port(
        sheet, volt::PowerPort{second_net, volt::PowerPortKind::Power, volt::Point{50.0, 50.0}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_VISUAL_COLLISION");
    CHECK(diagnostic.severity() == volt::Severity::Error);
    const auto &entities = diagnostic.entities();
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::power_port(first)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::power_port(second)) !=
          entities.end());
}

TEST_CASE("Schematic readability reports no-connect markers crowding wire geometry") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto pin = circuit.pin_by_number(component, "1").value();
    const auto net = add_named_net(circuit, "BOOT0");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto marker = schematic.add_no_connect_marker(
        sheet,
        volt::NoConnectMarker{pin, volt::Point{50.0, 50.0}, volt::SchematicOrientation::Right});
    const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{45.0, 45.2}, volt::Point{55.0, 45.2}}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_VISUAL_COLLISION");
    CHECK(diagnostic.severity() == volt::Severity::Error);
    const auto &entities = diagnostic.entities();
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::no_connect_marker(marker)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::wire_run(wire)) !=
          entities.end());
}

TEST_CASE("Schematic readability reports different-net visual wire crossings") {
    volt::Circuit circuit;
    const auto first_net = add_named_net(circuit, "ROW");
    const auto second_net = add_named_net(circuit, "COL");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto first_wire = schematic.add_wire_run(
        sheet,
        volt::WireRun{first_net, std::vector{volt::Point{40.0, 50.0}, volt::Point{80.0, 50.0}}});
    const auto second_wire = schematic.add_wire_run(
        sheet,
        volt::WireRun{second_net, std::vector{volt::Point{60.0, 30.0}, volt::Point{60.0, 70.0}}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_DIFFERENT_NET_WIRE_CROSSING");
    CHECK(diagnostic.severity() == volt::Severity::Error);
    const auto &entities = diagnostic.entities();
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::wire_run(first_wire)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::wire_run(second_wire)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::net(first_net)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::net(second_net)) !=
          entities.end());
}
