#include <catch2/catch_test_macros.hpp>

#include "support/compiled_board_export_helpers.hpp"

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

volt::ComponentSpec resistor_component_spec() {
    const auto first_pin = volt::PinSpec{"A",
                                         "1",
                                         volt::ConnectionRequirement::Required,
                                         volt::ElectricalTerminalKind::Passive,
                                         volt::ElectricalDirection::Passive,
                                         volt::ElectricalSignalDomain::Unspecified,
                                         volt::ElectricalDriveKind::Passive};
    const auto second_pin = volt::PinSpec{"B",
                                          "2",
                                          volt::ConnectionRequirement::Required,
                                          volt::ElectricalTerminalKind::Passive,
                                          volt::ElectricalDirection::Passive,
                                          volt::ElectricalSignalDomain::Unspecified,
                                          volt::ElectricalDriveKind::Passive};
    return volt::ComponentSpec{.name = "Resistor", .pins = {first_pin, second_pin}};
}

volt::ComponentId add_resistor(volt::Circuit &circuit) {
    const auto definition = circuit.define_component(resistor_component_spec());
    const auto component = circuit.instantiate_component(
        definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    circuit.update(component, volt::SetComponentProperty{volt::PropertyKey{"Value"},
                                                         volt::PropertyValue{"10k"}});
    circuit.update(component, volt::SetComponentProperty{volt::PropertyKey{"tolerance"},
                                                         volt::PropertyValue{"1%"}});
    return component;
}

volt::test::ExportFixtureLibrary select_resistor_part(volt::Circuit &circuit,
                                                      volt::ComponentId component) {
    const auto &definition = circuit.get(circuit.get(component).definition());
    const auto footprint = volt::passive_0603_footprint();
    const auto part =
        volt::PhysicalPart{volt::ManufacturerPart{"Yageo", "RC0603FR-0710KL"},
                           volt::PackageRef{"0603"}, footprint.ref(),
                           std::vector{volt::PinPadMapping{definition.pins()[0], "1"},
                                       volt::PinPadMapping{definition.pins()[1], "2"}}};
    auto library = volt::test::make_export_fixture_library(
        {{resistor_component_spec(), part, footprint, volt::PartKey{"resistor"}}});
    circuit.update(component, volt::SelectLibraryPart{library.bundle,
                                                      library.bundle.require(library.keys[0])});
    return library;
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
    const auto net =
        circuit.add_net(volt::NetSpec{.name = volt::NetName{"VCC"}, .kind = volt::NetKind::Power});
    const auto schematic = make_flat_schematic(circuit, component, net);
    const auto exact_parts = volt::test::make_export_fixture_library({});

    const auto result = volt::adapters::kicad::write_flat_schematic(schematic, exact_parts.bundle);

    CHECK_FALSE(result.loss_report.has_warnings());
    CHECK(result.text == read_fixture("kicad_flat_resistor.kicad_sch"));
    CHECK(result.text ==
          volt::adapters::kicad::write_flat_schematic(schematic, exact_parts.bundle).text);
}

TEST_CASE("KiCad schematic writer resolves selected footprints from their exact owner") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto exact_parts = select_resistor_part(circuit, component);
    const auto net =
        circuit.add_net(volt::NetSpec{.name = volt::NetName{"VCC"}, .kind = volt::NetKind::Power});
    const auto schematic = make_flat_schematic(circuit, component, net);
    const auto foreign_parts = volt::test::make_export_fixture_library({});

    const auto result = volt::adapters::kicad::write_flat_schematic(schematic, exact_parts.bundle);

    CHECK_FALSE(result.loss_report.has_warnings());
    CHECK(result.text.find(R"((property "Footprint" "passives:R_0603_1608Metric")") !=
          std::string::npos);
    CHECK_THROWS(volt::adapters::kicad::write_flat_schematic(schematic, foreign_parts.bundle));
}

TEST_CASE("KiCad schematic writer reports unsupported out-of-subset constructs") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net =
        circuit.add_net(volt::NetSpec{.name = volt::NetName{"VCC"}, .kind = volt::NetKind::Power});
    auto schematic = make_flat_schematic(circuit, component, net);
    [[maybe_unused]] const auto extra_sheet = schematic.add_sheet(volt::Sheet{"Second"});

    auto unsupported_symbol = volt::SymbolDefinition{"Indicator"};
    unsupported_symbol.add_pin(
        volt::SymbolPin{"A", "1", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Left});
    unsupported_symbol.add_primitive(volt::SymbolCircle{volt::Point{0.0, 0.0}, 2.0});
    [[maybe_unused]] const auto unsupported_symbol_id =
        schematic.add_symbol_definition(std::move(unsupported_symbol));
    const auto exact_parts = volt::test::make_export_fixture_library({});

    const auto result = volt::adapters::kicad::write_flat_schematic(schematic, exact_parts.bundle);

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
    const auto net = circuit.add_net(
        volt::NetSpec{.name = volt::NetName{"SUPPORT/SWDIO"}, .kind = volt::NetKind::Signal});
    auto schematic = make_flat_schematic(circuit, component, net);
    [[maybe_unused]] const auto label = schematic.add_net_label(
        volt::SheetId{0},
        volt::NetLabel{net, volt::Point{14.0, 16.0}, volt::SchematicOrientation::Right,
                       std::nullopt, std::string{"SWDIO"}});
    const auto exact_parts = volt::test::make_export_fixture_library({});

    const auto result = volt::adapters::kicad::write_flat_schematic(schematic, exact_parts.bundle);

    CHECK(result.text.find("(label \"SUPPORT/SWDIO\"") != std::string::npos);
    CHECK(result.text.find("(label \"SWDIO\"") == std::string::npos);
}

TEST_CASE("KiCad schematic writer rejects non-finite numeric property values") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    circuit.update(component, volt::SetComponentProperty{
                                  volt::PropertyKey{"Value"},
                                  volt::PropertyValue{std::numeric_limits<double>::infinity()}});
    const auto net =
        circuit.add_net(volt::NetSpec{.name = volt::NetName{"VCC"}, .kind = volt::NetKind::Power});
    const auto schematic = make_flat_schematic(circuit, component, net);
    const auto exact_parts = volt::test::make_export_fixture_library({});

    CHECK_THROWS_AS(volt::adapters::kicad::write_flat_schematic(schematic, exact_parts.bundle),
                    std::invalid_argument);

    try {
        [[maybe_unused]] const auto result =
            volt::adapters::kicad::write_flat_schematic(schematic, exact_parts.bundle);
        FAIL("expected KiCad finite-number rejection");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::InvalidArgument);
        CHECK(std::string{error.what()} == "KiCad numeric values must be finite");
        CHECK_FALSE(error.entity().has_value());
    }
}
