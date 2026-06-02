#include <volt/schematic/default_symbols.hpp>

namespace volt::default_symbol_detail {

void add_pin(SymbolDefinition &symbol, std::string name, std::string number, Point anchor,
             SchematicOrientation orientation) {
    symbol.add_pin(SymbolPin{std::move(name), std::move(number), anchor, orientation});
}

void add_two_pin_anchors(SymbolDefinition &symbol, std::string left_name, std::string left_number,
                         std::string right_name, std::string right_number) {
    add_pin(symbol, std::move(left_name), std::move(left_number), Point{0.0, 0.0},
            SchematicOrientation::Left);
    add_pin(symbol, std::move(right_name), std::move(right_number), Point{20.0, 0.0},
            SchematicOrientation::Right);
}

[[nodiscard]] SymbolDefinition resistor_symbol(std::string_view name) {
    auto symbol = SymbolDefinition{std::string{name}};
    add_two_pin_anchors(symbol, "1", "1", "2", "2");
    symbol.add_primitive(
        SymbolLine{Point{0.0, 0.0}, Point{5.0, 0.0}, SymbolLineRole::TerminalLeadStart});
    symbol.add_primitive(SymbolLine{Point{5.0, 0.0}, Point{6.5, -3.0}});
    symbol.add_primitive(SymbolLine{Point{6.5, -3.0}, Point{8.0, 3.0}});
    symbol.add_primitive(SymbolLine{Point{8.0, 3.0}, Point{9.5, -3.0}});
    symbol.add_primitive(SymbolLine{Point{9.5, -3.0}, Point{11.0, 3.0}});
    symbol.add_primitive(SymbolLine{Point{11.0, 3.0}, Point{12.5, -3.0}});
    symbol.add_primitive(SymbolLine{Point{12.5, -3.0}, Point{14.0, 3.0}});
    symbol.add_primitive(SymbolLine{Point{14.0, 3.0}, Point{15.0, 0.0}});
    symbol.add_primitive(
        SymbolLine{Point{15.0, 0.0}, Point{20.0, 0.0}, SymbolLineRole::TerminalLeadEnd});
    return symbol;
}

[[nodiscard]] SymbolDefinition capacitor_symbol(std::string_view name) {
    auto symbol = SymbolDefinition{std::string{name}};
    add_two_pin_anchors(symbol, "1", "1", "2", "2");
    symbol.add_primitive(
        SymbolLine{Point{0.0, 0.0}, Point{8.0, 0.0}, SymbolLineRole::TerminalLeadStart});
    symbol.add_primitive(SymbolLine{Point{8.0, -5.0}, Point{8.0, 5.0}});
    symbol.add_primitive(SymbolLine{Point{12.0, -5.0}, Point{12.0, 5.0}});
    symbol.add_primitive(
        SymbolLine{Point{12.0, 0.0}, Point{20.0, 0.0}, SymbolLineRole::TerminalLeadEnd});
    return symbol;
}

[[nodiscard]] SymbolDefinition polarized_capacitor_symbol(std::string_view name) {
    auto symbol = SymbolDefinition{std::string{name}};
    add_two_pin_anchors(symbol, "+", "1", "-", "2");
    symbol.add_primitive(
        SymbolLine{Point{0.0, 0.0}, Point{8.0, 0.0}, SymbolLineRole::TerminalLeadStart});
    symbol.add_primitive(SymbolLine{Point{8.0, -5.0}, Point{8.0, 5.0}});
    symbol.add_primitive(SymbolLine{Point{12.0, -5.0}, Point{12.0, 5.0}});
    symbol.add_primitive(
        SymbolLine{Point{12.0, 0.0}, Point{20.0, 0.0}, SymbolLineRole::TerminalLeadEnd});
    symbol.add_primitive(SymbolText{"+", Point{6.0, -8.0}});
    return symbol;
}

[[nodiscard]] SymbolDefinition inductor_symbol(std::string_view name) {
    auto symbol = SymbolDefinition{std::string{name}};
    add_two_pin_anchors(symbol, "1", "1", "2", "2");
    symbol.add_primitive(
        SymbolLine{Point{0.0, 0.0}, Point{4.0, 0.0}, SymbolLineRole::TerminalLeadStart});
    symbol.add_primitive(SymbolArc{Point{6.0, 0.0}, 2.0, 180.0, -180.0});
    symbol.add_primitive(SymbolArc{Point{10.0, 0.0}, 2.0, 180.0, -180.0});
    symbol.add_primitive(SymbolArc{Point{14.0, 0.0}, 2.0, 180.0, -180.0});
    symbol.add_primitive(
        SymbolLine{Point{16.0, 0.0}, Point{20.0, 0.0}, SymbolLineRole::TerminalLeadEnd});
    return symbol;
}

[[nodiscard]] SymbolDefinition diode_symbol(std::string_view name) {
    auto symbol = SymbolDefinition{std::string{name}};
    add_two_pin_anchors(symbol, "K", "1", "A", "2");
    symbol.add_primitive(
        SymbolLine{Point{0.0, 0.0}, Point{7.0, 0.0}, SymbolLineRole::TerminalLeadStart});
    symbol.add_primitive(SymbolLine{Point{7.0, -5.0}, Point{7.0, 5.0}});
    symbol.add_primitive(SymbolLine{Point{7.0, -5.0}, Point{13.0, 0.0}});
    symbol.add_primitive(SymbolLine{Point{7.0, 5.0}, Point{13.0, 0.0}});
    symbol.add_primitive(
        SymbolLine{Point{13.0, 0.0}, Point{20.0, 0.0}, SymbolLineRole::TerminalLeadEnd});
    return symbol;
}

[[nodiscard]] SymbolDefinition led_symbol(std::string_view name) {
    auto symbol = diode_symbol(name);
    symbol.add_primitive(SymbolLine{Point{13.0, -6.0}, Point{17.0, -10.0}});
    symbol.add_primitive(SymbolLine{Point{15.0, -4.0}, Point{19.0, -8.0}});
    return symbol;
}

[[nodiscard]] SymbolDefinition switch_symbol(std::string_view name) {
    auto symbol = SymbolDefinition{std::string{name}};
    add_two_pin_anchors(symbol, "A", "1", "B", "2");
    symbol.add_primitive(
        SymbolLine{Point{0.0, 0.0}, Point{7.0, 0.0}, SymbolLineRole::TerminalLeadStart});
    symbol.add_primitive(
        SymbolLine{Point{13.0, 0.0}, Point{20.0, 0.0}, SymbolLineRole::TerminalLeadEnd});
    symbol.add_primitive(SymbolCircle{Point{8.0, 0.0}, 1.0});
    symbol.add_primitive(SymbolCircle{Point{12.0, 0.0}, 1.0});
    symbol.add_primitive(SymbolLine{Point{8.0, 0.0}, Point{14.0, -5.0}});
    symbol.add_primitive(SymbolText{"SW", Point{10.0, -10.0}});
    return symbol;
}

[[nodiscard]] SymbolDefinition crystal_symbol(std::string_view name) {
    auto symbol = SymbolDefinition{std::string{name}};
    add_two_pin_anchors(symbol, "1", "1", "2", "2");
    symbol.add_primitive(
        SymbolLine{Point{0.0, 0.0}, Point{7.0, 0.0}, SymbolLineRole::TerminalLeadStart});
    symbol.add_primitive(
        SymbolLine{Point{13.0, 0.0}, Point{20.0, 0.0}, SymbolLineRole::TerminalLeadEnd});
    symbol.add_primitive(SymbolLine{Point{8.0, -6.0}, Point{8.0, 6.0}});
    symbol.add_primitive(SymbolLine{Point{12.0, -6.0}, Point{12.0, 6.0}});
    symbol.add_primitive(SymbolRectangle{Point{7.0, -4.0}, Point{13.0, 4.0}});
    symbol.add_primitive(SymbolText{"Y", Point{10.0, -11.0}});
    return symbol;
}

[[nodiscard]] SymbolDefinition test_point_symbol(std::string_view name) {
    auto symbol = SymbolDefinition{std::string{name}};
    add_pin(symbol, "TP", "1", Point{0.0, 0.0}, SchematicOrientation::Left);
    symbol.add_primitive(SymbolLine{Point{0.0, 0.0}, Point{4.0, 0.0}});
    symbol.add_primitive(SymbolCircle{Point{7.0, 0.0}, 3.0});
    symbol.add_primitive(SymbolText{"TP", Point{7.0, -8.0}});
    return symbol;
}

[[nodiscard]] SymbolDefinition connector_symbol(std::string_view name,
                                                std::initializer_list<std::string_view> pin_names) {
    auto symbol = SymbolDefinition{std::string{name}};
    const auto pin_count = static_cast<int>(pin_names.size());
    const auto height = static_cast<double>(pin_count - 1) * 8.0;
    symbol.add_primitive(SymbolRectangle{Point{8.0, -4.0}, Point{22.0, height + 4.0}});
    symbol.add_primitive(SymbolText{"J", Point{15.0, -10.0}});

    auto index = 0;
    for (const auto pin_name : pin_names) {
        const auto number = std::to_string(index + 1);
        const auto y = static_cast<double>(index) * 8.0;
        add_pin(symbol, std::string{pin_name}, number, Point{0.0, y}, SchematicOrientation::Left);
        symbol.add_primitive(SymbolLine{Point{0.0, y}, Point{8.0, y}});
        ++index;
    }
    return symbol;
}

[[nodiscard]] SymbolDefinition regulator_symbol(std::string_view name) {
    auto symbol = SymbolDefinition{std::string{name}};
    add_pin(symbol, "IN", "3", Point{0.0, 10.0}, SchematicOrientation::Left);
    add_pin(symbol, "OUT", "2", Point{50.0, 10.0}, SchematicOrientation::Right);
    add_pin(symbol, "GND", "1", Point{25.0, 30.0}, SchematicOrientation::Down);
    symbol.add_primitive(SymbolRectangle{Point{10.0, 0.0}, Point{40.0, 25.0}});
    symbol.add_primitive(SymbolLine{Point{0.0, 10.0}, Point{10.0, 10.0}});
    symbol.add_primitive(SymbolLine{Point{40.0, 10.0}, Point{50.0, 10.0}});
    symbol.add_primitive(SymbolLine{Point{25.0, 25.0}, Point{25.0, 30.0}});
    symbol.add_primitive(SymbolText{"REG", Point{25.0, 14.0}});
    return symbol;
}

[[nodiscard]] SymbolDefinition op_amp_symbol(std::string_view name) {
    auto symbol = SymbolDefinition{std::string{name}};
    add_pin(symbol, "IN+", "3", Point{0.0, 16.0}, SchematicOrientation::Left);
    add_pin(symbol, "IN-", "2", Point{0.0, 4.0}, SchematicOrientation::Left);
    add_pin(symbol, "OUT", "1", Point{45.0, 10.0}, SchematicOrientation::Right);
    add_pin(symbol, "V+", "5", Point{20.0, -8.0}, SchematicOrientation::Up);
    add_pin(symbol, "V-", "4", Point{20.0, 28.0}, SchematicOrientation::Down);
    symbol.add_primitive(SymbolLine{Point{0.0, 4.0}, Point{10.0, 4.0}});
    symbol.add_primitive(SymbolLine{Point{0.0, 16.0}, Point{10.0, 16.0}});
    symbol.add_primitive(SymbolLine{Point{35.0, 10.0}, Point{45.0, 10.0}});
    symbol.add_primitive(SymbolLine{Point{20.0, -8.0}, Point{20.0, 2.0}});
    symbol.add_primitive(SymbolLine{Point{20.0, 18.0}, Point{20.0, 28.0}});
    symbol.add_primitive(SymbolLine{Point{10.0, 0.0}, Point{10.0, 20.0}});
    symbol.add_primitive(SymbolLine{Point{10.0, 0.0}, Point{35.0, 10.0}});
    symbol.add_primitive(SymbolLine{Point{10.0, 20.0}, Point{35.0, 10.0}});
    symbol.add_primitive(SymbolText{"-", Point{13.0, 6.0}});
    symbol.add_primitive(SymbolText{"+", Point{13.0, 17.0}});
    return symbol;
}

} // namespace volt::default_symbol_detail

namespace volt {

[[nodiscard]] std::optional<SymbolDefinition> default_schematic_symbol(std::string_view name) {
    using namespace default_symbol_detail;

    if (name == "resistor" || name == "volt.passives:resistor") {
        return resistor_symbol(name);
    }
    if (name == "capacitor" || name == "volt.passives:capacitor") {
        return capacitor_symbol(name);
    }
    if (name == "volt.passives:capacitor_polarized") {
        return polarized_capacitor_symbol(name);
    }
    if (name == "volt.passives:inductor") {
        return inductor_symbol(name);
    }
    if (name == "volt.discretes:diode") {
        return diode_symbol(name);
    }
    if (name == "led" || name == "volt.optos:led") {
        return led_symbol(name);
    }
    if (name == "volt.switches:switch_spst") {
        return switch_symbol(name);
    }
    if (name == "volt.frequency:crystal_2pin") {
        return crystal_symbol(name);
    }
    if (name == "volt.testpoints:test_point") {
        return test_point_symbol(name);
    }
    if (name == "volt.connectors:connector_1x01") {
        return connector_symbol(name, {"1"});
    }
    if (name == "connector_1x02" || name == "volt.connectors:connector_1x02") {
        return connector_symbol(name, {"+", "-"});
    }
    if (name == "volt.connectors:connector_1x03") {
        return connector_symbol(name, {"1", "2", "3"});
    }
    if (name == "volt.power:regulator_3pin") {
        return regulator_symbol(name);
    }
    if (name == "volt.analog:op_amp_5pin") {
        return op_amp_symbol(name);
    }

    return std::nullopt;
}

} // namespace volt
