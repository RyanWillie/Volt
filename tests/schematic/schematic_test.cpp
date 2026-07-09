#include "schematic_test_helpers.hpp"

#include <volt/circuit/connectivity/queries.hpp>

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

TEST_CASE("Schematic authoring mutations infer nets from logical pin endpoints") {
    volt::Circuit circuit;
    const auto first_component = add_resistor(circuit, "R1");
    const auto second_component = add_resistor(circuit, "R2");
    const auto net = add_named_net(circuit, "SIG");
    const auto first_pin = volt::queries::pin_by_number(circuit, first_component, "2").value();
    const auto second_pin = volt::queries::pin_by_number(circuit, second_component, "1").value();
    circuit.connect(net, first_pin);
    circuit.connect(net, second_pin);
    const auto net_count = circuit.net_count();

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto wire = schematic.add_wire_run_for_endpoints(
        sheet, std::nullopt, std::vector{volt::Point{20.0, 0.0}, volt::Point{40.0, 0.0}},
        std::vector{volt::SchematicEndpoint{volt::Point{20.0, 0.0}, first_pin},
                    volt::SchematicEndpoint{volt::Point{40.0, 0.0}, second_pin}},
        volt::RouteIntent::Direct);

    CHECK(schematic.wire_run(wire).net() == net);
    CHECK(schematic.wire_run(wire).points() ==
          std::vector{volt::Point{20.0, 0.0}, volt::Point{40.0, 0.0}});
    CHECK(circuit.net_count() == net_count);
}

TEST_CASE("Schematic authoring mutations reject unconnected and mismatched endpoints") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit, "R1");
    const auto first_net = add_named_net(circuit, "SIG");
    const auto second_net = add_named_net(circuit, "ALT");
    const auto connected_pin = volt::queries::pin_by_number(circuit, component, "1").value();
    const auto unconnected_pin = volt::queries::pin_by_number(circuit, component, "2").value();
    circuit.connect(first_net, connected_pin);

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});

    try {
        static_cast<void>(schematic.add_net_label_for_endpoint(
            sheet, std::nullopt, volt::SchematicEndpoint{volt::Point{0.0, 0.0}, unconnected_pin}));
        FAIL("unconnected pin endpoints must not infer schematic nets");
    } catch (const std::logic_error &error) {
        CHECK(std::string{error.what()}.find("not connected to any logical net") !=
              std::string::npos);
    }

    try {
        static_cast<void>(schematic.add_net_label_for_endpoint(
            sheet, second_net, volt::SchematicEndpoint{volt::Point{0.0, 0.0}, connected_pin}));
        FAIL("mismatched endpoint net intent must be rejected");
    } catch (const std::logic_error &error) {
        const auto message = std::string{error.what()};
        CHECK(message.find("belongs to SIG") != std::string::npos);
        CHECK(message.find("instead of ALT") != std::string::npos);
    }

    check_kernel_error(
        [&] {
            static_cast<void>(schematic.add_net_label_for_endpoint(
                sheet, std::nullopt,
                volt::SchematicEndpoint{volt::Point{0.0, 0.0}, unconnected_pin}));
        },
        volt::ErrorCode::InvalidState,
        "Schematic endpoint R1 pin 2 (2) is not connected to any logical net");
    check_kernel_error(
        [&] {
            static_cast<void>(schematic.add_net_label_for_endpoint(
                sheet, second_net, volt::SchematicEndpoint{volt::Point{0.0, 0.0}, connected_pin}));
        },
        volt::ErrorCode::CrossReferenceViolation,
        "Schematic endpoint R1 pin 1 (1): the pin belongs to SIG (net:0) instead of ALT "
        "(net:1)");
}

TEST_CASE("Schematic stores professional primitives without changing logical connectivity") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto vcc = add_net(circuit);
    const auto gnd =
        circuit.connectivity().add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});
    const auto no_connect_pin = volt::queries::pin_by_number(circuit, component, "2").value();
    circuit.intent().mark_intentional_no_connect_pin(no_connect_pin);

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
    CHECK_FALSE(volt::queries::net_of(circuit, no_connect_pin).has_value());
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
    CHECK_THROWS_AS(
        (volt::NetLabel{volt::NetId{0}, volt::Point{0.0, 0.0}, volt::SchematicOrientation::Right,
                        std::nullopt, std::string{""}}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        (volt::PowerPort{volt::NetId{0}, volt::PowerPortKind::Power, volt::Point{0.0, 0.0},
                         volt::SchematicOrientation::Up, std::nullopt, std::string{""}}),
        std::invalid_argument);

    check_kernel_error([] { static_cast<void>(volt::SymbolDefinition{""}); },
                       volt::ErrorCode::InvalidArgument,
                       "Symbol definition name must not be empty");
    check_kernel_error([] { static_cast<void>(volt::Sheet{""}); }, volt::ErrorCode::InvalidArgument,
                       "Sheet title must not be empty");
}

TEST_CASE("Symbol definitions reject duplicate pin numbers") {
    auto symbol = volt::SymbolDefinition{"Connector"};
    symbol.add_pin(
        volt::SymbolPin{"A", "1", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Left});

    CHECK_THROWS_AS(symbol.add_pin(volt::SymbolPin{"B", "1", volt::Point{10.0, 0.0},
                                                   volt::SchematicOrientation::Right}),
                    std::logic_error);
    check_kernel_error(
        [&] {
            symbol.add_pin(volt::SymbolPin{"B", "1", volt::Point{10.0, 0.0},
                                           volt::SchematicOrientation::Right});
        },
        volt::ErrorCode::DuplicateName, "Symbol pin number already exists");
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

    try {
        static_cast<void>(schematic.place_symbol(
            volt::SheetId{99}, volt::SymbolInstance{symbol, component, volt::Point{0.0, 0.0}}));
        FAIL("missing schematic sheet must throw a typed kernel error");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::UnknownEntity);
        CHECK(std::string{error.what()} == "Sheet ID does not belong to this schematic");
        REQUIRE(error.entity().has_value());
        CHECK(error.entity().value() == volt::EntityRef::sheet(volt::SheetId{99}));
    }
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
    const auto pin = volt::queries::pin_by_number(circuit, component, "1").value();

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
    check_kernel_error(
        [&] {
            static_cast<void>(schematic.add_symbol_field(
                other_sheet,
                volt::SymbolField{instance, "reference", "R1", volt::Point{0.0, 0.0}}));
        },
        volt::ErrorCode::CrossReferenceViolation,
        "Symbol field must be placed on the symbol instance sheet");
    CHECK_NOTHROW(
        schematic.add_no_connect_marker(sheet, volt::NoConnectMarker{pin, volt::Point{0.0, 0.0}}));
}

TEST_CASE("Schematic replacement must reference the same logical circuit") {
    auto circuit = volt::Circuit{};
    auto other_circuit = volt::Circuit{};
    auto schematic = volt::Schematic{circuit};

    CHECK_THROWS_AS(schematic.replace_with(volt::Schematic{other_circuit}), std::logic_error);
    check_kernel_error([&] { schematic.replace_with(volt::Schematic{other_circuit}); },
                       volt::ErrorCode::CrossReferenceViolation,
                       "Schematic replacement must reference the same logical circuit");
}
