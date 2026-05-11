#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/io/schematic_svg_writer.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/symbols.hpp>

namespace {

volt::ComponentId add_resistor(volt::Circuit &circuit) {
    const auto first_pin =
        circuit.add_pin_definition(volt::PinDefinition{"1&", "1", volt::PinRole::Passive});
    const auto second_pin =
        circuit.add_pin_definition(volt::PinDefinition{"2<", "2", volt::PinRole::Passive});
    const auto definition = circuit.add_component_definition(
        volt::ComponentDefinition{"Resistor", std::vector{first_pin, second_pin}});
    return circuit.instantiate_component(definition, volt::ReferenceDesignator{"R&1"});
}

volt::NetId add_net(volt::Circuit &circuit) {
    return circuit.add_net(volt::Net{volt::NetName{"V&CC"}, volt::NetKind::Power});
}

volt::SymbolDefinition make_symbol() {
    auto symbol = volt::SymbolDefinition{"Resistor"};
    symbol.add_pin(
        volt::SymbolPin{"1&", "1", volt::Point{0.0, 0.0}, volt::SchematicOrientation::Left});
    symbol.add_pin(
        volt::SymbolPin{"2<", "2", volt::Point{20.0, 0.0}, volt::SchematicOrientation::Right});
    symbol.add_primitive(volt::SymbolLine{volt::Point{0.0, 0.0}, volt::Point{20.0, 0.0}});
    symbol.add_primitive(volt::SymbolRectangle{volt::Point{4.0, -3.0}, volt::Point{16.0, 3.0}});
    symbol.add_primitive(volt::SymbolCircle{volt::Point{10.0, 0.0}, 2.0});
    symbol.add_primitive(volt::SymbolArc{volt::Point{10.0, 0.0}, 5.0, 0.0, 180.0});
    symbol.add_primitive(volt::SymbolArc{volt::Point{30.0, 0.0}, 5.0, 0.0, 360.0});
    symbol.add_primitive(volt::SymbolText{"R<&", volt::Point{10.0, -8.0}});
    return symbol;
}

volt::Schematic make_schematic(const volt::Circuit &circuit, volt::ComponentId component) {
    auto schematic = volt::Schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main & Aux"});
    const auto symbol = schematic.add_symbol_definition(make_symbol());
    [[maybe_unused]] const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 20.0},
                                    volt::SchematicOrientation::Right});
    return schematic;
}

volt::Schematic make_schematic_with_wires(const volt::Circuit &circuit, volt::ComponentId component,
                                          volt::NetId net) {
    auto schematic = make_schematic(circuit, component);
    [[maybe_unused]] const auto wire = schematic.add_wire_run(
        volt::SheetId{0},
        volt::WireRun{net, std::vector{volt::Point{10.0, 20.0}, volt::Point{30.0, 20.0}}});
    [[maybe_unused]] const auto label =
        schematic.add_net_label(volt::SheetId{0}, volt::NetLabel{net, volt::Point{12.0, 16.0}});
    return schematic;
}

} // namespace

TEST_CASE("Schematic SVG writer renders placed symbols deterministically") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = add_net(circuit);
    const auto schematic = make_schematic_with_wires(circuit, component, net);

    const auto svg = volt::io::write_schematic_svg(schematic);

    CHECK(svg == volt::io::write_schematic_svg(schematic));
    CHECK(svg.find("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 297 210\"") !=
          std::string::npos);
    CHECK(svg.find("<rect class=\"sheet\" x=\"0\" y=\"0\" width=\"297\" height=\"210\"/>") !=
          std::string::npos);
    CHECK(svg.find("<text class=\"sheet-title\" x=\"10\" y=\"16\">Main &amp; Aux</text>") !=
          std::string::npos);
    CHECK(svg.find("<polyline class=\"wire-run\" data-net=\"net:0\" points=\"10,20 30,20\"/>") !=
          std::string::npos);
    CHECK(svg.find("<text class=\"net-label\" data-net=\"net:0\" x=\"12\" y=\"16\"") !=
          std::string::npos);
    CHECK(svg.find(">V&amp;CC</text>") != std::string::npos);
    CHECK(svg.find("data-component=\"component:0\"") != std::string::npos);
    CHECK(svg.find("data-symbol-definition=\"symbol_def:0\"") != std::string::npos);
    CHECK(svg.find("transform=\"translate(40 20) rotate(0)\"") != std::string::npos);
    CHECK(svg.find("<line class=\"symbol-line\" x1=\"0\" y1=\"0\" x2=\"20\" y2=\"0\"/>") !=
          std::string::npos);
    CHECK(
        svg.find("<rect class=\"symbol-rectangle\" x=\"4\" y=\"-3\" width=\"12\" height=\"6\"/>") !=
        std::string::npos);
    CHECK(svg.find("<circle class=\"symbol-circle\" cx=\"10\" cy=\"0\" r=\"2\"/>") !=
          std::string::npos);
    CHECK(svg.find("<path class=\"symbol-arc\" d=\"M 15 0 A 5 5 0 0 1 5 ") != std::string::npos);
    CHECK(svg.find("<path class=\"symbol-arc\" d=\"M 35 0 A 5 5 0 0 1 25 0 A 5 5 0 0 1 35 0\"/>") !=
          std::string::npos);
    CHECK(svg.find("<text class=\"symbol-text\" x=\"10\" y=\"-8\"") != std::string::npos);
    CHECK(svg.find(">R&lt;&amp;</text>") != std::string::npos);
    CHECK(svg.find("<circle class=\"pin-anchor\" cx=\"0\" cy=\"0\" r=\"1.5\"/>") !=
          std::string::npos);
    CHECK(svg.find("<text class=\"pin-label\" x=\"0\" y=\"4\">1&amp;</text>") != std::string::npos);
    CHECK(svg.find("<text class=\"reference\" x=\"0\" y=\"-12\">R&amp;1</text>") !=
          std::string::npos);
}
