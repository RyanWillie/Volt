#include "schematic_test_helpers.hpp"

TEST_CASE("Schematic readability reports long local dogleg routes") {
    volt::Circuit circuit;
    const auto net = add_named_net(circuit, "OSC_IN");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{net,
                             std::vector{volt::Point{40.0, 40.0}, volt::Point{95.0, 40.0},
                                         volt::Point{95.0, 52.0}, volt::Point{42.0, 52.0}},
                             volt::RouteIntent::Orthogonal});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_LONG_LOCAL_DOGLEG");
    CHECK(diagnostic.severity() == volt::Severity::Warning);
    CHECK(diagnostic.entities() == std::vector{volt::EntityRef::sheet(sheet),
                                               volt::EntityRef::wire_run(wire),
                                               volt::EntityRef::net(net)});
}

TEST_CASE("Schematic readability reports misaligned repeated same-net labels in a local cluster") {
    volt::Circuit circuit;
    const auto net = add_named_net(circuit, "BOOT0");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto first = schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{50.0, 50.0}});
    const auto second =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{58.0, 55.0}});
    const auto third = schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{53.0, 63.0}});
    static_cast<void>(
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{160.0, 160.0}}));

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_MISALIGNED_LOCAL_LABELS");
    CHECK(diagnostic.severity() == volt::Severity::Warning);
    const auto &entities = diagnostic.entities();
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::net_label(first)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::net_label(second)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::net_label(third)) !=
          entities.end());
}

TEST_CASE("Schematic readability reports ambiguous same-net wire crossings") {
    volt::Circuit circuit;
    const auto net = add_named_net(circuit, "RESET");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto horizontal = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{40.0, 50.0}, volt::Point{70.0, 50.0}}});
    const auto vertical = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{55.0, 40.0}, volt::Point{55.0, 60.0}}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_AMBIGUOUS_SAME_NET_CROSSING");
    CHECK(diagnostic.severity() == volt::Severity::Warning);
    CHECK(diagnostic.entities() ==
          std::vector{volt::EntityRef::sheet(sheet), volt::EntityRef::wire_run(horizontal),
                      volt::EntityRef::wire_run(vertical), volt::EntityRef::net(net)});
}

TEST_CASE("Schematic readability reports one ambiguous same-net crossing per wire pair") {
    volt::Circuit circuit;
    const auto net = add_named_net(circuit, "RESET");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    [[maybe_unused]] const auto routed = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{40.0, 40.0}, volt::Point{80.0, 40.0},
                                              volt::Point{80.0, 60.0}, volt::Point{40.0, 60.0}}});
    [[maybe_unused]] const auto crossing = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{60.0, 30.0}, volt::Point{60.0, 70.0}}});

    const auto report = volt::validate_schematic_readability(schematic);

    CHECK(diagnostic_count(report, "SCHEMATIC_AMBIGUOUS_SAME_NET_CROSSING") == 1U);
}

TEST_CASE("Schematic readability reports visually dangling wire endpoints") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = add_named_net(circuit, "RESET");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    static_cast<void>(schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 20.0}}));
    const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{40.0, 20.0}, volt::Point{70.0, 20.0}}});

    const auto pins_before = circuit.net(net).pins();
    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_DANGLING_WIRE_ENDPOINT");
    CHECK(diagnostic.severity() == volt::Severity::Error);
    CHECK(diagnostic.entities() == std::vector{volt::EntityRef::sheet(sheet),
                                               volt::EntityRef::wire_run(wire),
                                               volt::EntityRef::net(net)});
    CHECK(circuit.net(net).pins() == pins_before);
}

TEST_CASE("Schematic readability reports exactly one diagnostic per dangling endpoint") {
    // Wire start lands on a connected symbol pin (anchored); only the far end is dangling.
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = add_named_net(circuit, "SIG");
    connect_pin_by_number(circuit, net, component, "1");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    static_cast<void>(schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 20.0}}));
    const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{40.0, 20.0}, volt::Point{70.0, 20.0}}});

    const auto report = volt::validate_schematic_readability(schematic);

    CHECK(diagnostic_count(report, "SCHEMATIC_DANGLING_WIRE_ENDPOINT") == 1);
    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_DANGLING_WIRE_ENDPOINT");
    CHECK(diagnostic.severity() == volt::Severity::Error);
    CHECK(diagnostic.entities() == std::vector{volt::EntityRef::sheet(sheet),
                                               volt::EntityRef::wire_run(wire),
                                               volt::EntityRef::net(net)});
}

TEST_CASE("Schematic readability reports wire endpoints anchored only by net label text") {
    volt::Circuit circuit;
    const auto net = add_named_net(circuit, "VCAP1");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{40.0, 40.0}, volt::Point{56.0, 40.0}}});
    static_cast<void>(schematic.add_junction(sheet, volt::Junction{net, volt::Point{40.0, 40.0}}));
    static_cast<void>(schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{58.0, 40.0}}));

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_DANGLING_WIRE_ENDPOINT");
    CHECK(diagnostic.severity() == volt::Severity::Error);
    CHECK(diagnostic.entities() == std::vector{volt::EntityRef::sheet(sheet),
                                               volt::EntityRef::wire_run(wire),
                                               volt::EntityRef::net(net)});
}

TEST_CASE("Schematic readability accepts wire endpoints with explicit visual anchors") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto pin_net = add_named_net(circuit, "PIN");
    const auto terminal_net = add_named_net(circuit, "PWR");
    const auto sheet_port_net = add_named_net(circuit, "OFFPAGE");
    const auto junction_net = add_named_net(circuit, "NODE");
    const auto endpoint_net = add_named_net(circuit, "CHAIN");
    connect_pin_by_number(circuit, pin_net, component, "1");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    static_cast<void>(schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 20.0}}));

    static_cast<void>(schematic.add_wire_run(
        sheet,
        volt::WireRun{pin_net, std::vector{volt::Point{40.0, 20.0}, volt::Point{50.0, 20.0}}}));
    static_cast<void>(
        schematic.add_junction(sheet, volt::Junction{pin_net, volt::Point{50.0, 20.0}}));

    static_cast<void>(schematic.add_power_port(
        sheet, volt::PowerPort{terminal_net, volt::PowerPortKind::Power, volt::Point{40.0, 40.0}}));
    static_cast<void>(schematic.add_wire_run(
        sheet, volt::WireRun{terminal_net,
                             std::vector{volt::Point{40.0, 40.0}, volt::Point{50.0, 40.0}}}));
    static_cast<void>(
        schematic.add_junction(sheet, volt::Junction{terminal_net, volt::Point{50.0, 40.0}}));

    static_cast<void>(schematic.add_sheet_port(sheet, volt::SheetPort{sheet_port_net, "OFFPAGE",
                                                                      volt::SheetPortKind::OffPage,
                                                                      volt::Point{40.0, 60.0}}));
    static_cast<void>(schematic.add_wire_run(
        sheet, volt::WireRun{sheet_port_net,
                             std::vector{volt::Point{40.0, 60.0}, volt::Point{50.0, 60.0}}}));
    static_cast<void>(
        schematic.add_junction(sheet, volt::Junction{sheet_port_net, volt::Point{50.0, 60.0}}));

    static_cast<void>(
        schematic.add_junction(sheet, volt::Junction{junction_net, volt::Point{40.0, 80.0}}));
    static_cast<void>(
        schematic.add_junction(sheet, volt::Junction{junction_net, volt::Point{50.0, 80.0}}));
    static_cast<void>(schematic.add_wire_run(
        sheet, volt::WireRun{junction_net,
                             std::vector{volt::Point{40.0, 80.0}, volt::Point{50.0, 80.0}}}));

    static_cast<void>(schematic.add_wire_run(
        sheet, volt::WireRun{endpoint_net,
                             std::vector{volt::Point{40.0, 100.0}, volt::Point{50.0, 100.0}}}));
    static_cast<void>(schematic.add_wire_run(
        sheet, volt::WireRun{endpoint_net,
                             std::vector{volt::Point{50.0, 100.0}, volt::Point{60.0, 100.0}}}));
    static_cast<void>(
        schematic.add_junction(sheet, volt::Junction{endpoint_net, volt::Point{40.0, 100.0}}));
    static_cast<void>(
        schematic.add_junction(sheet, volt::Junction{endpoint_net, volt::Point{60.0, 100.0}}));

    const auto report = volt::validate_schematic_readability(schematic);

    CHECK_FALSE(report_has_code(report, "SCHEMATIC_DANGLING_WIRE_ENDPOINT"));
}

TEST_CASE("Schematic readability reports floating-looking local stub clusters") {
    volt::Circuit circuit;
    const auto net = add_named_net(circuit, "BOOT0");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    auto wires = std::vector<volt::WireRunId>{};
    for (std::size_t index = 0; index < 3U; ++index) {
        const auto y = 50.0 + (static_cast<double>(index) * 6.0);
        wires.push_back(schematic.add_wire_run(
            sheet, volt::WireRun{net, std::vector{volt::Point{50.0, y}, volt::Point{56.0, y}}}));
        static_cast<void>(
            schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{56.0, y}}));
    }
    static_cast<void>(schematic.add_wire_run(
        sheet,
        volt::WireRun{net, std::vector{volt::Point{160.0, 50.0}, volt::Point{166.0, 50.0}}}));
    static_cast<void>(
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{166.0, 50.0}}));

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_FLOATING_STUB_CLUSTER");
    CHECK(diagnostic.severity() == volt::Severity::Warning);
    const auto &entities = diagnostic.entities();
    for (const auto wire : wires) {
        CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::wire_run(wire)) !=
              entities.end());
    }
}

TEST_CASE(
    "Schematic readability does not report floating stub cluster when stubs attach to trunk") {
    volt::Circuit circuit;
    const auto net = add_named_net(circuit, "BOOT0");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    static_cast<void>(schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{50.0, 50.0}, volt::Point{50.0, 66.0}}}));
    for (std::size_t index = 0; index < 3U; ++index) {
        const auto y = 50.0 + (static_cast<double>(index) * 6.0);
        static_cast<void>(schematic.add_wire_run(
            sheet, volt::WireRun{net, std::vector{volt::Point{50.0, y}, volt::Point{56.0, y}}}));
        static_cast<void>(
            schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{56.0, y}}));
    }

    const auto report = volt::validate_schematic_readability(schematic);

    CHECK_FALSE(report_has_code(report, "SCHEMATIC_FLOATING_STUB_CLUSTER"));
}
