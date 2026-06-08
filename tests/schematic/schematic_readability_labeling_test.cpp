#include "schematic_test_helpers.hpp"

TEST_CASE("Schematic readability reports crowded repeated tag stacks") {
    volt::Circuit circuit;
    const auto first_net = add_named_net(circuit, "GPIO_A");
    const auto second_net = add_named_net(circuit, "GPIO_B");
    const auto third_net = add_named_net(circuit, "GPIO_C");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto first = schematic.add_sheet_port(sheet, volt::SheetPort{first_net, "GPIO_A",
                                                                       volt::SheetPortKind::OffPage,
                                                                       volt::Point{40.0, 40.0}});
    const auto second = schematic.add_sheet_port(
        sheet, volt::SheetPort{second_net, "GPIO_B", volt::SheetPortKind::OffPage,
                               volt::Point{40.0, 43.0}});
    const auto third = schematic.add_sheet_port(sheet, volt::SheetPort{third_net, "GPIO_C",
                                                                       volt::SheetPortKind::OffPage,
                                                                       volt::Point{40.0, 46.0}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_CROWDED_TAG_STACK");
    CHECK(diagnostic.severity() == volt::Severity::Warning);
    const auto &entities = diagnostic.entities();
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::sheet_port(first)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::sheet_port(second)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::sheet_port(third)) !=
          entities.end());
}

TEST_CASE("Schematic readability reports dense port tags in authored regions") {
    volt::Circuit circuit;

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto region = schematic.add_sheet_region(
        sheet, volt::SheetRegion{"mcu", "MCU", volt::SheetRegionBounds{20.0, 20.0, 120.0, 120.0}});

    auto ports = std::vector<volt::SheetPortId>{};
    for (std::size_t index = 0; index < 12U; ++index) {
        const auto net = add_named_net(circuit, "SIG_" + std::to_string(index));
        ports.push_back(schematic.add_sheet_port(
            sheet, volt::SheetPort{net, "S" + std::to_string(index), volt::SheetPortKind::OffPage,
                                   volt::Point{30.0, 25.0 + (static_cast<double>(index) * 7.0)},
                                   volt::SchematicOrientation::Right, region}));
    }

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_DENSE_PORT_TAGS");
    CHECK(diagnostic.severity() == volt::Severity::Warning);
    CHECK(diagnostic_count(report, "SCHEMATIC_DENSE_PORT_TAGS") == 1U);
    const auto &entities = diagnostic.entities();
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::sheet_port(ports.front())) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::sheet_port(ports.back())) !=
          entities.end());
}

TEST_CASE("Schematic readability reports labels crowding symbols") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = add_named_net(circuit, "SIG");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{60.0, 60.0}});
    const auto label = schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{62.0, 60.0}});

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_LABEL_CROWDS_SYMBOL");
    CHECK(diagnostic.severity() == volt::Severity::Warning);
    const auto &entities = diagnostic.entities();
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::net_label(label)) !=
          entities.end());
    CHECK(std::find(entities.begin(), entities.end(), volt::EntityRef::symbol_instance(instance)) !=
          entities.end());
}

TEST_CASE("Schematic readability accepts terminal markers attached to connected symbol pins") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto ground = circuit.add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});
    connect_pin_by_number(circuit, ground, component, "1");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    static_cast<void>(schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{60.0, 60.0}}));
    static_cast<void>(schematic.add_power_port(
        sheet, volt::PowerPort{ground, volt::PowerPortKind::Ground, volt::Point{60.0, 60.0}}));

    const auto report = volt::validate_schematic_readability(schematic);

    CHECK_FALSE(report_has_code(report, "SCHEMATIC_LABEL_CROWDS_SYMBOL"));
}

TEST_CASE("Schematic readability accepts terminal markers on connected symbol leads") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto supply = circuit.add_net(volt::Net{volt::NetName{"+3V3"}, volt::NetKind::Power});
    connect_pin_by_number(circuit, supply, component, "1");

    auto switch_symbol = volt::SymbolDefinition{"SwitchLike"};
    switch_symbol.add_pin(
        volt::SymbolPin{"A", "1", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Left});
    switch_symbol.add_pin(
        volt::SymbolPin{"B", "2", volt::Point{24.0, 8.0}, volt::SchematicOrientation::Right});
    switch_symbol.add_primitive(volt::SymbolLine{volt::Point{0.0, 0.0}, volt::Point{12.0, 0.0}});
    switch_symbol.add_primitive(volt::SymbolLine{volt::Point{12.0, 0.0}, volt::Point{24.0, 8.0}});

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    static_cast<void>(schematic.place_symbol(
        sheet, volt::SymbolInstance{schematic.add_symbol_definition(switch_symbol), component,
                                    volt::Point{60.0, 60.0}}));
    static_cast<void>(schematic.add_power_port(
        sheet, volt::PowerPort{supply, volt::PowerPortKind::Power, volt::Point{60.0, 60.0}}));

    const auto report = volt::validate_schematic_readability(schematic);

    CHECK_FALSE(report_has_code(report, "SCHEMATIC_TERMINAL_TOUCHES_SYMBOL"));
    CHECK_FALSE(report_has_code(report, "SCHEMATIC_VISUAL_COLLISION"));
}

TEST_CASE("Schematic readability reports duplicate visible reference labels on a sheet") {
    volt::Circuit circuit;
    const auto first_component = add_resistor(circuit, "R1");
    const auto second_component = add_resistor(circuit, "R2");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    const auto first_instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, first_component, volt::Point{50.0, 50.0}});
    const auto second_instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, second_component, volt::Point{100.0, 50.0}});
    static_cast<void>(schematic.add_symbol_field(
        sheet, volt::SymbolField{first_instance, "reference", "R1", volt::Point{50.0, 38.0}}));
    static_cast<void>(schematic.add_symbol_field(
        sheet, volt::SymbolField{second_instance, "reference", "R1", volt::Point{100.0, 38.0}}));

    const auto report = volt::validate_schematic_readability(schematic);

    const auto &diagnostic = require_diagnostic(report, "SCHEMATIC_DUPLICATE_REFERENCE_LABEL");
    CHECK(diagnostic.severity() == volt::Severity::Warning);
    CHECK(diagnostic.entities() == std::vector{volt::EntityRef::sheet(sheet),
                                               volt::EntityRef::symbol_instance(first_instance),
                                               volt::EntityRef::component(first_component),
                                               volt::EntityRef::symbol_instance(second_instance),
                                               volt::EntityRef::component(second_component)});
}

TEST_CASE("Schematic readability ignores hidden logical component references") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit, "RRESET");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    static_cast<void>(schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{50.0, 50.0}}));

    const auto report = volt::validate_schematic_readability(schematic);

    CHECK(diagnostic_count(report, "SCHEMATIC_UNCONVENTIONAL_REFERENCE_LABEL") == 0U);
}

TEST_CASE("Schematic readability reports non-professional visible reference labels") {
    struct Placement {
        std::string reference;
        volt::Point position;
        bool warns;
    };

    const auto placements = std::vector<Placement>{
        {"PWR/RRESET", volt::Point{30.0, 40.0}, true}, {"RRESET", volt::Point{70.0, 40.0}, true},
        {"CVCAP1", volt::Point{110.0, 40.0}, true},    {"VIN_SRC", volt::Point{150.0, 40.0}, true},
        {"U3V3", volt::Point{190.0, 40.0}, true},      {"R1", volt::Point{30.0, 90.0}, false},
        {"C12", volt::Point{70.0, 90.0}, false},       {"U3", volt::Point{110.0, 90.0}, false},
        {"J1", volt::Point{150.0, 90.0}, false},       {"SW1", volt::Point{190.0, 90.0}, false},
        {"Y1", volt::Point{30.0, 140.0}, false},       {"D2", volt::Point{70.0, 140.0}, false},
        {"FB1", volt::Point{110.0, 140.0}, false},     {"TP10", volt::Point{150.0, 140.0}, false},
        {"CONN1", volt::Point{190.0, 140.0}, false},
    };

    volt::Circuit circuit;
    auto components = std::vector<volt::ComponentId>{};
    for (const auto &placement : placements) {
        components.push_back(add_resistor(circuit, placement.reference));
    }

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    auto instances = std::vector<volt::SymbolInstanceId>{};
    for (std::size_t index = 0; index < placements.size(); ++index) {
        instances.push_back(schematic.place_symbol(
            sheet, volt::SymbolInstance{symbol, components[index], placements[index].position}));
        static_cast<void>(schematic.add_symbol_field(
            sheet, volt::SymbolField{instances.back(), "reference", placements[index].reference,
                                     volt::Point{placements[index].position.x(),
                                                 placements[index].position.y() - 12.0}}));
    }

    const auto report = volt::validate_schematic_readability(schematic);

    CHECK(diagnostic_count(report, "SCHEMATIC_UNCONVENTIONAL_REFERENCE_LABEL") == 5U);
    for (std::size_t index = 0; index < placements.size(); ++index) {
        const auto expected_entities = std::vector{
            volt::EntityRef::sheet(sheet), volt::EntityRef::symbol_instance(instances[index]),
            volt::EntityRef::component(components[index])};
        CHECK(report_has_code_and_entities(report, "SCHEMATIC_UNCONVENTIONAL_REFERENCE_LABEL",
                                           expected_entities) == placements[index].warns);
    }
}

TEST_CASE(
    "Schematic readability reports both unconventional and duplicate for a repeated bad label") {
    // A label that is both unconventional (all-letter, no digit suffix) and shared by two
    // instances must produce an UNCONVENTIONAL diagnostic for each instance AND a DUPLICATE
    // diagnostic covering both.
    volt::Circuit circuit;
    const auto first_component = add_resistor(circuit, "RRESET");
    const auto second_component = add_resistor(circuit, "R2");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    const auto first_instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, first_component, volt::Point{50.0, 50.0}});
    const auto second_instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, second_component, volt::Point{100.0, 50.0}});
    static_cast<void>(schematic.add_symbol_field(
        sheet, volt::SymbolField{first_instance, "reference", "RRESET", volt::Point{50.0, 38.0}}));
    static_cast<void>(
        schematic.add_symbol_field(sheet, volt::SymbolField{second_instance, "reference", "RRESET",
                                                            volt::Point{100.0, 38.0}}));

    const auto report = volt::validate_schematic_readability(schematic);

    CHECK(diagnostic_count(report, "SCHEMATIC_UNCONVENTIONAL_REFERENCE_LABEL") == 2U);
    CHECK(diagnostic_count(report, "SCHEMATIC_DUPLICATE_REFERENCE_LABEL") == 1U);
    CHECK(report_has_code_and_entities(report, "SCHEMATIC_UNCONVENTIONAL_REFERENCE_LABEL",
                                       std::vector{volt::EntityRef::sheet(sheet),
                                                   volt::EntityRef::symbol_instance(first_instance),
                                                   volt::EntityRef::component(first_component)}));
    CHECK(
        report_has_code_and_entities(report, "SCHEMATIC_UNCONVENTIONAL_REFERENCE_LABEL",
                                     std::vector{volt::EntityRef::sheet(sheet),
                                                 volt::EntityRef::symbol_instance(second_instance),
                                                 volt::EntityRef::component(second_component)}));
    CHECK(report_has_code_and_entities(
        report, "SCHEMATIC_DUPLICATE_REFERENCE_LABEL",
        std::vector{volt::EntityRef::sheet(sheet), volt::EntityRef::symbol_instance(first_instance),
                    volt::EntityRef::component(first_component),
                    volt::EntityRef::symbol_instance(second_instance),
                    volt::EntityRef::component(second_component)}));
}

TEST_CASE("Schematic readability accepts compact labels and spaced tag ports") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto signal = add_named_net(circuit, "SIG");
    const auto status = add_named_net(circuit, "STAT");
    const auto enable = add_named_net(circuit, "EN");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    static_cast<void>(schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{70.0, 70.0}}));
    static_cast<void>(
        schematic.add_net_label(sheet, volt::NetLabel{signal, volt::Point{115.0, 72.0}}));
    static_cast<void>(schematic.add_sheet_port(
        sheet,
        volt::SheetPort{status, "STAT", volt::SheetPortKind::OffPage, volt::Point{30.0, 50.0}}));
    static_cast<void>(
        schematic.add_sheet_port(sheet, volt::SheetPort{enable, "EN", volt::SheetPortKind::OffPage,
                                                        volt::Point{30.0, 62.0}}));

    const auto report = volt::validate_schematic_readability(schematic);

    CHECK(report.empty());
}

TEST_CASE("Schematic readability accepts a clean local oscillator reset boot fixture") {
    volt::Circuit circuit;
    auto pin_definitions = std::vector<volt::PinDefId>{};
    pin_definitions.push_back(circuit.add_pin_definition(volt::PinDefinition{
        "OSC_IN", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Signal,
        volt::ElectricalDirection::Input, volt::ElectricalSignalDomain::Digital}));
    pin_definitions.push_back(circuit.add_pin_definition(volt::PinDefinition{
        "NRST", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Signal,
        volt::ElectricalDirection::Input, volt::ElectricalSignalDomain::Digital}));
    pin_definitions.push_back(circuit.add_pin_definition(volt::PinDefinition{
        "BOOT0", "3", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Signal,
        volt::ElectricalDirection::Input, volt::ElectricalSignalDomain::Digital}));
    pin_definitions.push_back(circuit.add_pin_definition(volt::PinDefinition{
        "VSS", "4", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Ground,
        volt::ElectricalDirection::Passive}));
    const auto mcu_definition =
        circuit.add_component_definition(volt::ComponentDefinition{"MCU", pin_definitions});
    const auto mcu = circuit.instantiate_component(mcu_definition, volt::ReferenceDesignator{"U1"});
    const auto crystal = add_resistor(circuit, "Y1");
    const auto reset_pullup = add_resistor(circuit, "R1");
    const auto boot_resistor = add_resistor(circuit, "R2");
    const auto osc = add_named_net(circuit, "OSC_IN");
    const auto reset = add_named_net(circuit, "NRST");
    const auto boot = add_named_net(circuit, "BOOT0");
    const auto vcc = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});
    const auto gnd = circuit.add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});
    connect_pin_by_number(circuit, osc, mcu, "1");
    connect_pin_by_number(circuit, reset, mcu, "2");
    connect_pin_by_number(circuit, boot, mcu, "3");
    connect_pin_by_number(circuit, osc, crystal, "2");
    connect_pin_by_number(circuit, reset, reset_pullup, "2");
    connect_pin_by_number(circuit, boot, boot_resistor, "2");

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto mcu_symbol = schematic.add_symbol_definition(make_four_pin_ic_symbol());
    const auto passive_symbol = schematic.add_symbol_definition(make_resistor_symbol());
    const auto mcu_instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{mcu_symbol, mcu, volt::Point{90.0, 70.0}});
    const auto crystal_instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{passive_symbol, crystal, volt::Point{42.0, 60.0}});
    const auto reset_instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{passive_symbol, reset_pullup, volt::Point{42.0, 70.0}});
    const auto boot_instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{passive_symbol, boot_resistor, volt::Point{42.0, 82.0}});

    static_cast<void>(schematic.add_wire_run(
        sheet, volt::WireRun{osc, std::vector{volt::Point{62.0, 60.0}, volt::Point{90.0, 60.0}}}));
    static_cast<void>(schematic.add_wire_run(
        sheet,
        volt::WireRun{reset, std::vector{volt::Point{62.0, 70.0}, volt::Point{90.0, 70.0}}}));
    static_cast<void>(schematic.add_wire_run(
        sheet, volt::WireRun{boot, std::vector{volt::Point{62.0, 82.0}, volt::Point{90.0, 80.0}}}));
    static_cast<void>(schematic.add_net_label(sheet, volt::NetLabel{osc, volt::Point{66.0, 56.0}}));
    static_cast<void>(
        schematic.add_net_label(sheet, volt::NetLabel{reset, volt::Point{66.0, 66.0}}));
    static_cast<void>(
        schematic.add_net_label(sheet, volt::NetLabel{boot, volt::Point{66.0, 78.0}}));
    static_cast<void>(schematic.add_power_port(
        sheet, volt::PowerPort{vcc, volt::PowerPortKind::Power, volt::Point{26.0, 42.0}}));
    static_cast<void>(schematic.add_power_port(
        sheet, volt::PowerPort{gnd, volt::PowerPortKind::Ground, volt::Point{132.0, 98.0},
                               volt::SchematicOrientation::Down}));
    static_cast<void>(schematic.add_symbol_field(
        sheet, volt::SymbolField{crystal_instance, "Value", "8MHz", volt::Point{52.0, 54.0}}));
    static_cast<void>(schematic.add_symbol_field(
        sheet, volt::SymbolField{reset_instance, "Value", "10k", volt::Point{52.0, 64.0}}));
    static_cast<void>(schematic.add_symbol_field(
        sheet, volt::SymbolField{boot_instance, "Value", "100k", volt::Point{52.0, 76.0}}));
    static_cast<void>(mcu_instance);

    const auto report = volt::validate_schematic_readability(schematic);

    CHECK(report.empty());
}

TEST_CASE("Schematic readability warns when power marker is placed on ground net") {
    volt::Circuit circuit;
    const auto gnd = circuit.add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    static_cast<void>(schematic.add_power_port(
        sheet, volt::PowerPort{gnd, volt::PowerPortKind::Power, volt::Point{50.0, 50.0},
                               volt::SchematicOrientation::Up}));

    const auto report = volt::validate_schematic_readability(schematic);

    REQUIRE_FALSE(report.empty());
    CHECK(report.count() == 1);
    CHECK(report.diagnostics().front().code() ==
          volt::DiagnosticCode{"SCHEMATIC_POWER_MARKER_ON_GROUND_NET"});
    CHECK(report.diagnostics().front().severity() == volt::Severity::Warning);
}

TEST_CASE("Schematic readability warns when ground marker is placed on power net") {
    volt::Circuit circuit;
    const auto vcc = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    static_cast<void>(schematic.add_power_port(
        sheet, volt::PowerPort{vcc, volt::PowerPortKind::Ground, volt::Point{50.0, 50.0},
                               volt::SchematicOrientation::Down}));

    const auto report = volt::validate_schematic_readability(schematic);

    REQUIRE_FALSE(report.empty());
    CHECK(report.count() == 1);
    CHECK(report.diagnostics().front().code() ==
          volt::DiagnosticCode{"SCHEMATIC_GROUND_MARKER_ON_POWER_NET"});
    CHECK(report.diagnostics().front().severity() == volt::Severity::Warning);
}

TEST_CASE("Schematic readability accepts power marker on power net") {
    volt::Circuit circuit;
    const auto vcc = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});

    volt::Schematic schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    static_cast<void>(schematic.add_power_port(
        sheet, volt::PowerPort{vcc, volt::PowerPortKind::Power, volt::Point{50.0, 50.0},
                               volt::SchematicOrientation::Up}));

    const auto report = volt::validate_schematic_readability(schematic);

    CHECK(report.empty());
}
