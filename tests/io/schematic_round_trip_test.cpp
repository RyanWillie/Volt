#include <catch2/catch_test_macros.hpp>

#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/io/schematic_reader.hpp>
#include <volt/io/schematic_writer.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/schematic_document.hpp>
#include <volt/schematic/symbols.hpp>

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

} // namespace

TEST_CASE("Schematic JSON round-trips deterministically") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});

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
    const auto no_connect_pin = circuit.pin_by_number(component, "2").value();
    circuit.mark_intentional_no_connect_pin(no_connect_pin);
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

TEST_CASE("Schematic document round-trips as a project artifact") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = circuit.add_net(volt::Net{volt::NetName{"VCC"}, volt::NetKind::Power});

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
