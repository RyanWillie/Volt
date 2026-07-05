#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <volt/adapters/kicad/schematic_writer.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/core/errors.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/symbols.hpp>

namespace {

std::string read_fixture(const std::string &name) {
    auto input = std::ifstream{std::string{VOLT_TEST_FIXTURE_DIR} + "/" + name};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

volt::SymbolDefinition make_resistor_symbol() {
    auto symbol = volt::SymbolDefinition{"Resistor"};
    symbol.add_pin(
        volt::SymbolPin{"A", "1", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Left});
    symbol.add_pin(
        volt::SymbolPin{"B", "2", volt::Point{20.0, 0.0}, volt::SchematicOrientation::Right});
    symbol.add_primitive(volt::SymbolRectangle{volt::Point{4.0, -3.0}, volt::Point{16.0, 3.0}});
    symbol.add_primitive(volt::SymbolLine{volt::Point{0.0, 0.0}, volt::Point{4.0, 0.0}});
    symbol.add_primitive(volt::SymbolLine{volt::Point{16.0, 0.0}, volt::Point{20.0, 0.0}});
    symbol.add_primitive(volt::SymbolText{"R", volt::Point{10.0, -6.0}});
    return symbol;
}

volt::ComponentId add_resistor(volt::Circuit &circuit) {
    const auto first_pin = circuit.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto second_pin = circuit.add_pin_definition(volt::PinDefinition{
        "B", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto definition = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(definition, volt::ReferenceDesignator{"R1"});
    circuit.set_component_property(component, volt::PropertyKey{"Value"},
                                   volt::PropertyValue{"10k"});
    circuit.set_component_property(component, volt::PropertyKey{"tolerance"},
                                   volt::PropertyValue{"1%"});
    return component;
}

volt::Schematic make_flat_schematic(const volt::Circuit &circuit, volt::ComponentId component,
                                    volt::NetId net) {
    auto schematic = volt::Schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_resistor_symbol());
    [[maybe_unused]] const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 20.0}});
    [[maybe_unused]] const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{10.0, 20.0}, volt::Point{30.0, 20.0},
                                              volt::Point{30.0, 50.0}}});
    [[maybe_unused]] const auto label =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{12.0, 16.0}});
    return schematic;
}

} // namespace

TEST_CASE("KiCad schematic writer exports a deterministic flat schematic subset") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});
    const auto schematic = make_flat_schematic(circuit, component, net);

    const auto result = volt::adapters::kicad::write_flat_schematic(schematic);

    CHECK_FALSE(result.loss_report.has_warnings());
    CHECK(result.text == read_fixture("kicad_flat_resistor.kicad_sch"));
    CHECK(result.text == volt::adapters::kicad::write_flat_schematic(schematic).text);
}

TEST_CASE("KiCad schematic writer reports unsupported out-of-subset constructs") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});
    auto schematic = make_flat_schematic(circuit, component, net);
    [[maybe_unused]] const auto extra_sheet = schematic.add_sheet(volt::Sheet{"Second"});

    auto unsupported_symbol = volt::SymbolDefinition{"Indicator"};
    unsupported_symbol.add_pin(
        volt::SymbolPin{"A", "1", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Left});
    unsupported_symbol.add_primitive(volt::SymbolCircle{volt::Point{0.0, 0.0}, 2.0});
    [[maybe_unused]] const auto unsupported_symbol_id =
        schematic.add_symbol_definition(std::move(unsupported_symbol));

    const auto result = volt::adapters::kicad::write_flat_schematic(schematic);

    REQUIRE(result.loss_report.warnings().size() == 2);
    CHECK(result.loss_report.warnings().at(0).kind ==
          volt::adapters::kicad::LossKind::UnsupportedConstruct);
    CHECK(result.loss_report.warnings().at(0).construct == "sheet");
    CHECK(result.loss_report.warnings().at(1).kind ==
          volt::adapters::kicad::LossKind::UnsupportedConstruct);
    CHECK(result.loss_report.warnings().at(1).construct == "symbol.circle");
}

TEST_CASE("KiCad schematic writer preserves canonical net label names") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net =
        circuit.add_net(volt::Net{volt::NetName{"SUPPORT/SWDIO"}, volt::NetKind::Signal});
    auto schematic = make_flat_schematic(circuit, component, net);
    [[maybe_unused]] const auto label = schematic.add_net_label(
        volt::SheetId{0},
        volt::NetLabel{net, volt::Point{14.0, 16.0}, volt::SchematicOrientation::Right,
                       std::nullopt, std::string{"SWDIO"}});

    const auto result = volt::adapters::kicad::write_flat_schematic(schematic);

    CHECK(result.text.find("(label \"SUPPORT/SWDIO\"") != std::string::npos);
    CHECK(result.text.find("(label \"SWDIO\"") == std::string::npos);
}

TEST_CASE("KiCad schematic writer rejects non-finite numeric property values") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    circuit.set_component_property(component, volt::PropertyKey{"Value"},
                                   volt::PropertyValue{std::numeric_limits<double>::infinity()});
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});
    const auto schematic = make_flat_schematic(circuit, component, net);

    CHECK_THROWS_AS(volt::adapters::kicad::write_flat_schematic(schematic), std::invalid_argument);

    try {
        [[maybe_unused]] const auto result = volt::adapters::kicad::write_flat_schematic(schematic);
        FAIL("expected KiCad finite-number rejection");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::InvalidArgument);
        CHECK(std::string{error.what()} == "KiCad numeric values must be finite");
        CHECK_FALSE(error.entity().has_value());
    }
}
