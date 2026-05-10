#include <catch2/catch_test_macros.hpp>

#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/io/schematic_reader.hpp>
#include <volt/io/schematic_writer.hpp>
#include <volt/schematic/schematic.hpp>
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

    auto schematic = volt::Schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    auto symbol = volt::SymbolDefinition{"Resistor"};
    symbol.add_pin(
        volt::SymbolPin{"1", "1", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Left});
    symbol.add_pin(
        volt::SymbolPin{"2", "2", volt::Point{20.0, 0.0}, volt::SchematicOrientation::Right});
    symbol.add_primitive(volt::SymbolLine{volt::Point{0.0, 0.0}, volt::Point{20.0, 0.0}});
    const auto symbol_id = schematic.add_symbol_definition(std::move(symbol));
    [[maybe_unused]] const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol_id, component, volt::Point{40.0, 20.0}});

    const auto output = volt::io::write_schematic(schematic);
    const auto loaded = volt::io::read_schematic_text(output, circuit);

    CHECK(volt::io::write_schematic(loaded) == output);
}
