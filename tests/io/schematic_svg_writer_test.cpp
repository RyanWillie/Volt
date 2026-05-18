#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>
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
    const auto sheet = schematic.add_sheet(volt::Sheet{
        "Main & Aux", volt::SheetMetadata{"Main & Aux", volt::SheetSize{},
                                          std::vector{volt::TitleBlockField{"Revision", "A"}}}});
    const auto symbol = schematic.add_symbol_definition(make_symbol());
    [[maybe_unused]] const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 20.0},
                                    volt::SchematicOrientation::Right});
    return schematic;
}

std::size_t require_contains(std::string_view text, std::string_view needle) {
    const auto position = text.find(needle);
    REQUIRE(position != std::string_view::npos);
    return position;
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
    const auto ground = circuit.add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});
    const auto no_connect_pin = circuit.pin_by_number(component, "2").value();
    circuit.mark_intentional_no_connect_pin(no_connect_pin);
    auto schematic = make_schematic_with_wires(circuit, component, net);
    [[maybe_unused]] const auto junction =
        schematic.add_junction(volt::SheetId{0}, volt::Junction{net, volt::Point{30.0, 20.0}});
    [[maybe_unused]] const auto power =
        schematic.add_power_port(volt::SheetId{0}, volt::PowerPort{net, volt::PowerPortKind::Power,
                                                                   volt::Point{10.0, 16.0}});
    [[maybe_unused]] const auto ground_port = schematic.add_power_port(
        volt::SheetId{0},
        volt::PowerPort{ground, volt::PowerPortKind::Ground, volt::Point{50.0, 24.0}});
    [[maybe_unused]] const auto no_connect = schematic.add_no_connect_marker(
        volt::SheetId{0}, volt::NoConnectMarker{no_connect_pin, volt::Point{60.0, 20.0}});
    [[maybe_unused]] const auto sheet_port = schematic.add_sheet_port(
        volt::SheetId{0},
        volt::SheetPort{net, "VIN", volt::SheetPortKind::OffPage, volt::Point{5.0, 20.0}});
    [[maybe_unused]] const auto field = schematic.add_symbol_field(
        volt::SheetId{0},
        volt::SymbolField{volt::SymbolInstanceId{0}, "value", "10k", volt::Point{40.0, 32.0}});

    const auto svg = volt::io::write_schematic_svg(schematic);

    CHECK(svg == volt::io::write_schematic_svg(schematic));
    CHECK(svg.find("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 297 210\"") !=
          std::string::npos);
    CHECK(svg.find("<rect class=\"document-background\" x=\"0\" y=\"0\" width=\"297\" "
                   "height=\"210\"/>") != std::string::npos);
    CHECK(svg.find("<rect class=\"sheet\" x=\"0\" y=\"0\" width=\"297\" height=\"210\"/>") !=
          std::string::npos);
    CHECK(svg.find("<rect class=\"sheet-border\" x=\"0\" y=\"0\" width=\"297\" height=\"210\"/>") !=
          std::string::npos);
    CHECK(svg.find(
              "<rect class=\"drawing-frame\" x=\"10\" y=\"10\" width=\"277\" height=\"190\"/>") !=
          std::string::npos);
    CHECK(svg.find("sheet-title-clip") == std::string::npos);
    CHECK(svg.find("<g class=\"title-block\" transform=\"translate(205 188)\"") !=
          std::string::npos);
    CHECK(svg.find("<text class=\"title-block-label\" x=\"2\" y=\"4.2\">Title</text>") !=
          std::string::npos);
    CHECK(svg.find("<text class=\"title-block-value sheet-title\" x=\"24\" "
                   "y=\"4.2\">Main &amp; Aux</text>") != std::string::npos);
    CHECK(svg.find("<text class=\"title-block-label\" x=\"2\" y=\"10.2\">Revision</text>") !=
          std::string::npos);
    CHECK(svg.find("<text class=\"title-block-value\" x=\"24\" y=\"10.2\">A</text>") !=
          std::string::npos);
    CHECK(svg.find(".wire-run{fill:none;stroke:#111;stroke-width:1}") != std::string::npos);
    CHECK(svg.find(".net-label{font:2.8px sans-serif;fill:#111;text-anchor:start}") !=
          std::string::npos);
    CHECK(svg.find("#0645ad") == std::string::npos);
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
    CHECK(svg.find("pin-anchor") == std::string::npos);
    CHECK(svg.find("pin-label") == std::string::npos);
    CHECK(svg.find("<text class=\"reference\" x=\"0\" y=\"-12\">R&amp;1</text>") !=
          std::string::npos);
    CHECK(
        svg.find("<circle class=\"junction\" data-net=\"net:0\" cx=\"30\" cy=\"20\" r=\"1.8\"/>") !=
        std::string::npos);
    CHECK(svg.find("<g class=\"power-port power\" data-net=\"net:0\"") != std::string::npos);
    CHECK(svg.find("<g class=\"power-port ground\" data-net=\"net:1\"") != std::string::npos);
    CHECK(svg.find("<g class=\"no-connect-marker\" data-pin=\"pin:1\"") != std::string::npos);
    CHECK(svg.find("<g class=\"sheet-port off-page\" data-net=\"net:0\"") != std::string::npos);
    CHECK(svg.find(">VIN</text>") != std::string::npos);
    CHECK(svg.find("<text class=\"symbol-field\" data-symbol-instance=\"symbol_instance:0\" "
                   "data-field=\"value\" x=\"40\" y=\"32\"") != std::string::npos);
    CHECK(svg.find(">10k</text>") != std::string::npos);

    const auto regions = require_contains(svg, "<g class=\"layer layer-regions\">");
    const auto symbols = require_contains(svg, "<g class=\"layer layer-symbols\">");
    const auto wires = require_contains(svg, "<g class=\"layer layer-wires\">");
    const auto junctions = require_contains(svg, "<g class=\"layer layer-junctions\">");
    const auto ports = require_contains(svg, "<g class=\"layer layer-ports\">");
    const auto labels = require_contains(svg, "<g class=\"layer layer-labels\">");
    const auto fields = require_contains(svg, "<g class=\"layer layer-fields\">");
    CHECK(regions < symbols);
    CHECK(symbols < wires);
    CHECK(wires < junctions);
    CHECK(junctions < ports);
    CHECK(ports < labels);
    CHECK(labels < fields);
    CHECK(svg.find("layer-debug") == std::string::npos);
}

TEST_CASE("Schematic SVG writer renders debug pin overlays only when enabled") {
    volt::Circuit circuit;
    const auto component = add_resistor(circuit);
    const auto net = add_net(circuit);
    auto schematic = make_schematic_with_wires(circuit, component, net);

    auto options = volt::io::SchematicSvgOptions{};
    options.debug_overlays = true;
    const auto svg = volt::io::write_schematic_svg(schematic, options);

    CHECK(svg.find("<g class=\"layer layer-debug\">") != std::string::npos);
    CHECK(svg.find(".pin-anchor{fill:#fff;stroke:#c2410c;stroke-width:0.8}") != std::string::npos);
    CHECK(svg.find("<circle class=\"pin-anchor\" cx=\"0\" cy=\"0\" r=\"1.5\"/>") !=
          std::string::npos);
    CHECK(svg.find("<text class=\"pin-label\" x=\"0\" y=\"4\">1&amp;</text>") != std::string::npos);
    CHECK(require_contains(svg, "<g class=\"layer layer-fields\">") <
          require_contains(svg, "<g class=\"layer layer-debug\">"));
}

TEST_CASE("Schematic SVG writer preserves scoped names by default") {
    volt::Circuit circuit;
    const auto first_pin =
        circuit.add_pin_definition(volt::PinDefinition{"IN", "1", volt::PinRole::Passive});
    const auto second_pin =
        circuit.add_pin_definition(volt::PinDefinition{"OUT", "2", volt::PinRole::Passive});
    const auto definition = circuit.add_component_definition(
        volt::ComponentDefinition{"Divider", std::vector{first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(definition, volt::ReferenceDesignator{"DIV_A/R1"});
    const auto net = circuit.add_net(volt::Net{volt::NetName{"DIV_A/VIN"}, volt::NetKind::Power});
    auto schematic = volt::Schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_symbol());

    [[maybe_unused]] const auto instance = schematic.place_symbol(
        sheet, volt::SymbolInstance{symbol, component, volt::Point{40.0, 20.0}});
    [[maybe_unused]] const auto label =
        schematic.add_net_label(sheet, volt::NetLabel{net, volt::Point{12.0, 16.0}});
    [[maybe_unused]] const auto power = schematic.add_power_port(
        sheet, volt::PowerPort{net, volt::PowerPortKind::Power, volt::Point{20.0, 12.0}});

    const auto svg = volt::io::write_schematic_svg(schematic);

    CHECK(svg.find(">DIV_A/R1</text>") != std::string::npos);
    CHECK(svg.find(">DIV_A/VIN</text>") != std::string::npos);
}

TEST_CASE("Schematic SVG writer renders explicit presentation labels") {
    volt::Circuit circuit;
    const auto first_pin =
        circuit.add_pin_definition(volt::PinDefinition{"IN", "1", volt::PinRole::Passive});
    const auto second_pin =
        circuit.add_pin_definition(volt::PinDefinition{"OUT", "2", volt::PinRole::Passive});
    const auto definition = circuit.add_component_definition(
        volt::ComponentDefinition{"Divider", std::vector{first_pin, second_pin}});
    const auto component =
        circuit.instantiate_component(definition, volt::ReferenceDesignator{"DIV_A/R1"});
    const auto net = circuit.add_net(volt::Net{volt::NetName{"DIV_A/VIN"}, volt::NetKind::Power});
    auto schematic = volt::Schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{"Main"});
    const auto symbol = schematic.add_symbol_definition(make_symbol());

    [[maybe_unused]] const auto instance = schematic.place_symbol(
        sheet,
        volt::SymbolInstance{symbol, component, volt::Point{40.0, 20.0},
                             volt::SchematicOrientation::Right, std::nullopt, std::string{"R1"}});
    [[maybe_unused]] const auto power = schematic.add_power_port(
        sheet, volt::PowerPort{net, volt::PowerPortKind::Power, volt::Point{20.0, 12.0},
                               volt::SchematicOrientation::Up, std::nullopt, std::string{"+VIN"}});

    const auto svg = volt::io::write_schematic_svg(schematic);

    CHECK(svg.find(">R1</text>") != std::string::npos);
    CHECK(svg.find(">+VIN</text>") != std::string::npos);
    CHECK(svg.find(">DIV_A/R1</text>") == std::string::npos);
    CHECK(svg.find(">DIV_A/VIN</text>") == std::string::npos);
}

TEST_CASE("Schematic SVG writer expands the root viewport to sheet metadata") {
    volt::Circuit circuit;
    auto schematic = volt::Schematic{circuit};
    [[maybe_unused]] const auto sheet = schematic.add_sheet(
        volt::Sheet{"Wide", volt::SheetMetadata{"Wide", volt::SheetSize{420.0, 297.0}}});

    const auto svg = volt::io::write_schematic_svg(schematic);

    CHECK(svg.find("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 420 297\"") !=
          std::string::npos);
    CHECK(svg.find("width=\"420\" height=\"297\"") != std::string::npos);
    CHECK(svg.find("<rect class=\"sheet\" x=\"0\" y=\"0\" width=\"420\" height=\"297\"/>") !=
          std::string::npos);
    CHECK(svg.find(
              "<rect class=\"drawing-frame\" x=\"10\" y=\"10\" width=\"400\" height=\"277\"/>") !=
          std::string::npos);
}

TEST_CASE("Schematic SVG writer renders professional page metadata") {
    volt::Circuit circuit;
    auto schematic = volt::Schematic{circuit};
    const auto sheet = schematic.add_sheet(volt::Sheet{
        "Power",
        volt::SheetMetadata{
            "Power & Control",
            volt::SheetSize{120.0, 90.0},
            std::vector{volt::TitleBlockField{"Project", "Volt"},
                        volt::TitleBlockField{"Revision", "B"}},
            volt::SheetOrientation::Landscape,
            volt::SheetFrame{true, volt::SheetMargins{12.0, 8.0, 10.0, 14.0}},
            volt::SheetCoordinateZones{4U, 3U},
            volt::SheetGrid{5.0, true},
        },
    });
    [[maybe_unused]] const auto region = schematic.add_sheet_region(
        sheet, volt::SheetRegion{
                   "power",
                   "Power Regulation",
                   volt::SheetRegionBounds{18.0, 20.0, 70.0, 30.0},
                   std::vector{volt::SheetRegionStyleField{"border", "dashed"}},
               });
    [[maybe_unused]] const auto solid_region = schematic.add_sheet_region(
        sheet, volt::SheetRegion{
                   "signals",
                   "Signals",
                   volt::SheetRegionBounds{22.0, 55.0, 60.0, 12.0},
                   std::vector{volt::SheetRegionStyleField{"accent", "dashed"}},
               });

    const auto svg = volt::io::write_schematic_svg(schematic);

    CHECK(svg.find("<rect class=\"drawing-frame\" x=\"12\" y=\"8\" width=\"98\" height=\"68\"/>") !=
          std::string::npos);
    CHECK(svg.find("<pattern id=\"grid-sheet-0\" x=\"12\" y=\"8\" width=\"5\" height=\"5\"") !=
          std::string::npos);
    CHECK(svg.find("<rect class=\"sheet-grid\" x=\"12\" y=\"8\" width=\"98\" height=\"68\"") !=
          std::string::npos);
    CHECK(svg.find("<g class=\"coordinate-zones\">") != std::string::npos);
    CHECK(svg.find("<text class=\"coordinate-zone-label column\" x=\"24.25\" y=\"4\">1</text>") !=
          std::string::npos);
    CHECK(svg.find("<text class=\"coordinate-zone-label column\" x=\"97.75\" y=\"83\">4</text>") !=
          std::string::npos);
    CHECK(
        svg.find(
            "<text class=\"coordinate-zone-label row\" x=\"6\" y=\"19.3333333333333\">A</text>") !=
        std::string::npos);
    CHECK(svg.find("<text class=\"coordinate-zone-label row\" x=\"115\" "
                   "y=\"64.6666666666667\">C</text>") != std::string::npos);
    CHECK(svg.find("<clipPath id=\"title-block-clip-sheet-0\">") != std::string::npos);
    CHECK(svg.find("<rect class=\"sheet-region-frame dashed\" data-region=\"power\" x=\"18\" "
                   "y=\"20\" width=\"70\" height=\"30\"/>") != std::string::npos);
    CHECK(svg.find("<rect class=\"sheet-region-frame\" data-region=\"signals\" x=\"22\" "
                   "y=\"55\" width=\"60\" height=\"12\"/>") != std::string::npos);
    CHECK(svg.find("<clipPath id=\"region-title-clip-sheet-0-0\">") != std::string::npos);
    CHECK(svg.find("<text class=\"sheet-region-title\" x=\"21\" y=\"26\" "
                   "clip-path=\"url(#region-title-clip-sheet-0-0)\">Power Regulation</text>") !=
          std::string::npos);
    CHECK(svg.find("<g class=\"title-block\" transform=\"translate(28 58)\"") != std::string::npos);
    CHECK(svg.find("<text class=\"title-block-value sheet-title\" x=\"24\" "
                   "y=\"4.2\">Power &amp; Control</text>") != std::string::npos);
    CHECK(svg.find("<text class=\"title-block-value\" x=\"24\" y=\"10.2\">Volt</text>") !=
          std::string::npos);
    CHECK(svg.find("<text class=\"title-block-value\" x=\"24\" y=\"16.2\">B</text>") !=
          std::string::npos);
}

TEST_CASE("Schematic SVG writer can export one SVG per sheet") {
    volt::Circuit circuit;
    auto schematic = volt::Schematic{circuit};
    [[maybe_unused]] const auto first = schematic.add_sheet(
        volt::Sheet{"Power", volt::SheetMetadata{"Power", volt::SheetSize{100.0, 80.0}}});
    [[maybe_unused]] const auto second = schematic.add_sheet(
        volt::Sheet{"MCU", volt::SheetMetadata{"MCU", volt::SheetSize{120.0, 90.0}}});

    const auto pages = volt::io::write_schematic_svg_pages(schematic);

    REQUIRE(pages.size() == 2U);
    CHECK(pages[0].sheet == volt::SheetId{0});
    CHECK(pages[0].name == "Power");
    CHECK(pages[0].svg.find("viewBox=\"0 0 100 80\"") != std::string::npos);
    CHECK(pages[0].svg.find("data-sheet=\"sheet:0\"") != std::string::npos);
    CHECK(pages[0].svg.find("data-sheet=\"sheet:1\"") == std::string::npos);
    CHECK(pages[1].sheet == volt::SheetId{1});
    CHECK(pages[1].name == "MCU");
    CHECK(pages[1].svg.find("viewBox=\"0 0 120 90\"") != std::string::npos);
    CHECK(pages[1].svg.find("data-sheet=\"sheet:1\"") != std::string::npos);
    CHECK(pages[1].svg.find("data-sheet=\"sheet:0\"") == std::string::npos);
    CHECK(volt::io::write_schematic_svg(schematic).find("viewBox=\"0 0 297 190\"") !=
          std::string::npos);
}
