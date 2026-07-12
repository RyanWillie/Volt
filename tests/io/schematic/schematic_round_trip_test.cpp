#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/io/schematic/schematic_reader.hpp>
#include <volt/io/schematic/schematic_writer.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/schematic_document.hpp>
#include <volt/schematic/symbols.hpp>

#include "../../support/circuit_test_helpers.hpp"

namespace {

volt::ComponentId add_resistor(volt::Circuit &circuit) {
    const auto definition = volt::test::define_component(
        circuit, "Resistor",
        {volt::test::passive_pin("1", "1"), volt::test::passive_pin("2", "2")});
    return volt::test::instantiate_component(circuit, definition, "R1");
}

} // namespace

TEST_CASE("Schematic JSON round-trips deterministically") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"VCC"}, volt::NetKind::Power});

    auto schematic = volt::Schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    auto symbol = volt::SymbolDefinition{"Resistor"};
    symbol.add_pin(
        volt::SymbolPin{"1", "1", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Left});
    symbol.add_pin(
        volt::SymbolPin{"2", "2", volt::Point{20.0, 0.0}, volt::SchematicOrientation::Right});
    symbol.add_primitive(volt::SymbolLine{volt::Point{0.0, 0.0}, volt::Point{20.0, 0.0}});
    const auto symbol_id = schematic.add_symbol_definition(std::move(symbol));
    const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol_id, component, volt::Point{40.0, 20.0}});
    [[maybe_unused]] const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{20.0, 20.0}, volt::Point{40.0, 20.0}},
                             volt::RouteIntent::Orthogonal});
    [[maybe_unused]] const auto label =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{20.0, 16.0}});
    [[maybe_unused]] const auto junction =
        schematic.add_junction(sheet, volt::Junction{net, volt::Point{40.0, 20.0}});
    [[maybe_unused]] const auto port = schematic.add_power_port(
        sheet, volt::PowerPort{net, volt::PowerPortKind::Power, volt::Point{20.0, 12.0}});
    const auto no_connect_pin = volt::queries::pin_by_number(circuit, component, "2").value();
    circuit.mark_no_connect(no_connect_pin);
    [[maybe_unused]] const auto marker = schematic.add_no_connect_marker(
        sheet, volt::NoConnectMarker{no_connect_pin, volt::Point{64.0, 20.0}});
    [[maybe_unused]] const auto sheet_port = schematic.add_sheet_port(
        sheet, volt::SheetPort{net, "VIN", volt::SheetPortKind::OffPage, volt::Point{10.0, 20.0}});
    [[maybe_unused]] const auto field = schematic.add_symbol_field(
        sheet, volt::SymbolField{instance, "value", "10k", volt::Point{40.0, 32.0}});

    const auto output = volt::io::write_schematic(schematic);
    const auto loaded = volt::io::read_schematic_text(output, circuit);

    CHECK(volt::io::write_schematic(loaded) == output);
    CHECK(circuit.net(net).pins().empty());
}

TEST_CASE("Schematic JSON round-trips terminal lead line roles") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);

    auto schematic = volt::Schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    auto symbol = volt::SymbolDefinition{"LeadAware"};
    symbol.add_pin(
        volt::SymbolPin{"1", "1", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Left});
    symbol.add_pin(
        volt::SymbolPin{"2", "2", volt::Point{20.0, 0.0}, volt::SchematicOrientation::Right});
    symbol.add_primitive(volt::SymbolLine{volt::Point{0.0, 0.0}, volt::Point{4.0, 0.0},
                                          volt::SymbolLineRole::TerminalLeadStart});
    symbol.add_primitive(volt::SymbolRectangle{volt::Point{4.0, -3.0}, volt::Point{16.0, 3.0}});
    symbol.add_primitive(volt::SymbolLine{volt::Point{16.0, 0.0}, volt::Point{20.0, 0.0},
                                          volt::SymbolLineRole::TerminalLeadEnd});
    const auto symbol_id = schematic.add_symbol_definition(std::move(symbol));
    [[maybe_unused]] const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol_id, component, volt::Point{40.0, 20.0}});

    const auto output = volt::io::write_schematic(schematic);
    const auto output_json = nlohmann::json::parse(output);
    const auto loaded = volt::io::read_schematic_text(output, circuit);

    CHECK(output_json["symbol_definitions"][0]["primitives"][0]["role"] == "TerminalLeadStart");
    CHECK(output_json["symbol_definitions"][0]["primitives"][2]["role"] == "TerminalLeadEnd");
    CHECK(volt::io::write_schematic(loaded) == output);
}

TEST_CASE("Schematic document round-trips as a project artifact") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = circuit.add_net(volt::NetSpec{volt::NetName{"VCC"}, volt::NetKind::Power});

    auto document = volt::SchematicDocument{circuit};
    auto &schematic = document.schematic();
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    auto symbol = volt::SymbolDefinition{"Resistor"};
    symbol.add_pin(
        volt::SymbolPin{"1", "1", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Left});
    symbol.add_pin(
        volt::SymbolPin{"2", "2", volt::Point{20.0, 0.0}, volt::SchematicOrientation::Right});
    const auto symbol_id = schematic.add_symbol_definition(std::move(symbol));
    [[maybe_unused]] const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol_id, component, volt::Point{40.0, 20.0}});
    [[maybe_unused]] const auto wire = schematic.add_wire_run(
        sheet, volt::WireRun{net, std::vector{volt::Point{20.0, 20.0}, volt::Point{40.0, 20.0}}});

    const auto output = volt::io::write_schematic(document);
    const auto loaded = volt::io::read_schematic_document_text(output, circuit);

    CHECK(&document.circuit() == &circuit);
    CHECK(&loaded.circuit() == &circuit);
    CHECK(volt::io::write_schematic(loaded) == output);
}
