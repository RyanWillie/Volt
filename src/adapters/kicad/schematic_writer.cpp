#include <volt/adapters/kicad/schematic_writer.hpp>

#include "format.hpp"

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/errors.hpp>

namespace volt::adapters::kicad::detail {

void write_xy(std::ostream &out, Point point) {
    out << "(xy ";
    write_number(out, point.x());
    out << ' ';
    write_number(out, point.y());
    out << ')';
}

void write_at(std::ostream &out, Point point, SchematicOrientation orientation) {
    out << "(at ";
    write_number(out, point.x());
    out << ' ';
    write_number(out, point.y());
    out << ' ';
    write_number(out, [&orientation] {
        switch (orientation) {
        case SchematicOrientation::Right:
            return 0.0;
        case SchematicOrientation::Down:
            return 90.0;
        case SchematicOrientation::Left:
            return 180.0;
        case SchematicOrientation::Up:
            return 270.0;
        }
        throw KernelLogicError{ErrorCode::InvalidArgument, "Unhandled schematic orientation"};
    }());
    out << ')';
}

[[nodiscard]] std::string stable_uuid(std::size_t value) {
    auto out = std::ostringstream{};
    out << "00000000-0000-0000-0000-" << std::setw(12) << std::setfill('0') << value;
    return out.str();
}

[[nodiscard]] std::string component_id(ComponentId id) {
    return "component:" + std::to_string(id.index());
}

[[nodiscard]] std::string symbol_library_name(const SymbolDefinition &symbol) {
    return "Volt:" + symbol.name();
}

void write_effects(std::ostream &out, bool hidden) {
    out << "(effects (font (size 1.27 1.27))";
    if (hidden) {
        out << " (hide yes)";
    }
    out << ')';
}

[[nodiscard]] std::string component_value(const ComponentInstance &component,
                                          const ComponentDefinition &definition) {
    const auto value_key = PropertyKey{"Value"};
    if (component.properties().contains(value_key)) {
        return property_value_to_string(component.properties().get(value_key));
    }
    return definition.name();
}

void write_symbol_property(std::ostream &out, std::string_view name, std::string_view value,
                           Point at, bool hidden) {
    out << "    (property " << sexpr_string(name) << ' ' << sexpr_string(value) << " ";
    write_at(out, at, SchematicOrientation::Right);
    out << "\n";
    out << "      ";
    write_effects(out, hidden);
    out << "\n";
    out << "    )\n";
}

void write_library_property(std::ostream &out, std::string_view name, std::string_view value,
                            Point at) {
    out << "      (property " << sexpr_string(name) << ' ' << sexpr_string(value) << " ";
    write_at(out, at, SchematicOrientation::Right);
    out << "\n";
    out << "        ";
    write_effects(out);
    out << "\n";
    out << "      )\n";
}

void report_unsupported_primitives(const SymbolDefinition &symbol, LossReport &loss_report) {
    for (const auto &primitive : symbol.primitives()) {
        if (std::holds_alternative<SymbolCircle>(primitive)) {
            loss_report.add_warning(LossKind::UnsupportedConstruct, "symbol.circle",
                                    "The first KiCad writer subset omits symbol circles");
        } else if (std::holds_alternative<SymbolArc>(primitive)) {
            loss_report.add_warning(LossKind::UnsupportedConstruct, "symbol.arc",
                                    "The first KiCad writer subset omits symbol arcs");
        }
    }
}

void write_symbol_primitive(std::ostream &out, const SymbolPrimitive &primitive) {
    if (std::holds_alternative<SymbolLine>(primitive)) {
        const auto &line = std::get<SymbolLine>(primitive);
        out << "        (polyline\n";
        out << "          (pts ";
        write_xy(out, line.start());
        out << ' ';
        write_xy(out, line.end());
        out << ")\n";
        out << "          (stroke (width 0.15) (type default))\n";
        out << "          (fill (type none))\n";
        out << "        )\n";
        return;
    }
    if (std::holds_alternative<SymbolRectangle>(primitive)) {
        const auto &rectangle = std::get<SymbolRectangle>(primitive);
        out << "        (rectangle (start ";
        write_number(out, rectangle.first_corner().x());
        out << ' ';
        write_number(out, rectangle.first_corner().y());
        out << ") (end ";
        write_number(out, rectangle.second_corner().x());
        out << ' ';
        write_number(out, rectangle.second_corner().y());
        out << ")\n";
        out << "          (stroke (width 0.15) (type default))\n";
        out << "          (fill (type none))\n";
        out << "        )\n";
        return;
    }
    if (std::holds_alternative<SymbolText>(primitive)) {
        const auto &text = std::get<SymbolText>(primitive);
        out << "        (text " << sexpr_string(text.text()) << ' ';
        write_at(out, text.anchor(), text.orientation());
        out << "\n";
        out << "          ";
        write_effects(out);
        out << "\n";
        out << "        )\n";
    }
}

void write_symbol_pin(std::ostream &out, const SymbolPin &pin) {
    out << "        (pin passive line ";
    write_at(out, pin.anchor(), pin.orientation());
    out << " (length 2.54)\n";
    out << "          (name " << sexpr_string(pin.name()) << " ";
    write_effects(out);
    out << ")\n";
    out << "          (number " << sexpr_string(pin.number()) << " ";
    write_effects(out);
    out << ")\n";
    out << "        )\n";
}

void write_library_symbol(std::ostream &out, const SymbolDefinition &symbol) {
    out << "    (symbol " << sexpr_string(symbol_library_name(symbol)) << "\n";
    out << "      (pin_names (offset 0))\n";
    out << "      (exclude_from_sim no)\n";
    out << "      (in_bom yes)\n";
    out << "      (on_board yes)\n";
    write_library_property(out, "Reference", "R", Point{0.0, -7.0});
    write_library_property(out, "Value", symbol.name(), Point{0.0, 7.0});
    out << "      (symbol " << sexpr_string(symbol.name() + "_0_1") << "\n";
    for (const auto &primitive : symbol.primitives()) {
        write_symbol_primitive(out, primitive);
    }
    for (const auto &pin : symbol.pins()) {
        write_symbol_pin(out, pin);
    }
    out << "      )\n";
    out << "    )\n";
}

void write_wire(std::ostream &out, const WireRun &wire, std::size_t index) {
    out << "  (wire\n";
    out << "    (pts";
    for (const auto point : wire.points()) {
        out << ' ';
        write_xy(out, point);
    }
    out << ")\n";
    out << "    (stroke (width 0) (type default))\n";
    out << "    (uuid " << sexpr_string(stable_uuid(200U + index)) << ")\n";
    out << "  )\n";
}

void write_label(std::ostream &out, const Schematic &schematic, const NetLabel &label,
                 std::size_t index) {
    const auto &net = schematic.circuit().get(label.net());
    out << "  (label " << sexpr_string(net.name().value()) << ' ';
    write_at(out, label.position(), label.orientation());
    out << "\n";
    out << "    (effects (font (size 1.27 1.27)) (justify left bottom))\n";
    out << "    (uuid " << sexpr_string(stable_uuid(300U + index)) << ")\n";
    out << "  )\n";
}

void write_symbol_instance(std::ostream &out, const Schematic &schematic, SymbolInstanceId id,
                           std::size_t index) {
    const auto &instance = schematic.symbol_instance(id);
    const auto &symbol = schematic.symbol_definition(instance.symbol_definition());
    const auto &component = schematic.circuit().get(instance.component());
    const auto &definition = schematic.circuit().get(component.definition());

    out << "  (symbol\n";
    out << "    (lib_id " << sexpr_string(symbol_library_name(symbol)) << ")\n";
    out << "    ";
    write_at(out, instance.position(), instance.orientation());
    out << "\n";
    out << "    (unit 1)\n";
    out << "    (exclude_from_sim no)\n";
    out << "    (in_bom yes)\n";
    out << "    (on_board yes)\n";
    out << "    (uuid " << sexpr_string(stable_uuid(100U + index)) << ")\n";
    write_symbol_property(out, "Reference", component.reference().value(),
                          Point{instance.position().x(), instance.position().y() - 7.0});
    write_symbol_property(out, "Value", component_value(component, definition),
                          Point{instance.position().x(), instance.position().y() + 7.0});
    write_symbol_property(out, "VoltComponentId", component_id(instance.component()),
                          Point{instance.position().x(), instance.position().y() + 14.0}, true);

    auto property_y = instance.position().y() + 21.0;
    for (const auto &[key, value] : component.properties().entries()) {
        if (key.value() == "Reference" || key.value() == "Value") {
            continue;
        }
        write_symbol_property(out, key.value(), property_value_to_string(value),
                              Point{instance.position().x(), property_y});
        property_y += 7.0;
    }

    const auto &selected_part =
        volt::queries::selected_physical_part(schematic.circuit(), instance.component());
    if (selected_part.has_value()) {
        const auto &footprint = selected_part->footprint();
        write_symbol_property(out, "Footprint", footprint.library() + ":" + footprint.name(),
                              Point{instance.position().x(), property_y});
    }

    for (std::size_t pin_index = 0; pin_index < symbol.pins().size(); ++pin_index) {
        out << "    (pin " << sexpr_string(symbol.pins()[pin_index].number()) << "\n";
        out << "      (uuid " << sexpr_string(stable_uuid(400U + (index * 100U) + pin_index))
            << ")\n";
        out << "    )\n";
    }

    out << "    (instances\n";
    out << "      (project \"Volt\"\n";
    out << "        (path \"/00000000-0000-0000-0000-000000000000\"\n";
    out << "          (reference " << sexpr_string(component.reference().value()) << ")\n";
    out << "          (unit 1)\n";
    out << "        )\n";
    out << "      )\n";
    out << "    )\n";
    out << "  )\n";
}

} // namespace volt::adapters::kicad::detail

namespace volt::adapters::kicad {

[[nodiscard]] SchematicExportResult write_flat_schematic(const Schematic &schematic) {
    auto result = SchematicExportResult{};
    if (schematic.sheet_count() > 1U) {
        result.loss_report.add_warning(
            LossKind::UnsupportedConstruct, "sheet",
            "The first KiCad writer subset exports only the first flat schematic sheet");
    }

    for (std::size_t index = 0; index < schematic.symbol_definition_count(); ++index) {
        detail::report_unsupported_primitives(schematic.symbol_definition(SymbolDefId{index}),
                                              result.loss_report);
    }

    auto out = std::ostringstream{};
    out << "(kicad_sch\n";
    out << "  (version 20231120)\n";
    out << "  (generator \"Volt\")\n";
    out << "  (uuid \"00000000-0000-0000-0000-000000000000\")\n";
    out << "  (paper \"A4\")\n";
    out << "  (lib_symbols\n";
    for (std::size_t index = 0; index < schematic.symbol_definition_count(); ++index) {
        detail::write_library_symbol(out, schematic.symbol_definition(SymbolDefId{index}));
    }
    out << "  )\n";

    if (schematic.sheet_count() != 0U) {
        const auto &sheet = schematic.sheet(SheetId{0});
        for (std::size_t index = 0; index < sheet.wire_runs().size(); ++index) {
            detail::write_wire(out, schematic.wire_run(sheet.wire_runs()[index]), index);
        }
        for (std::size_t index = 0; index < sheet.net_labels().size(); ++index) {
            detail::write_label(out, schematic, schematic.net_label(sheet.net_labels()[index]),
                                index);
        }
        for (std::size_t index = 0; index < sheet.symbol_instances().size(); ++index) {
            detail::write_symbol_instance(out, schematic, sheet.symbol_instances()[index], index);
        }
    }

    out << "  (sheet_instances\n";
    out << "    (path \"/\"\n";
    out << "      (page \"1\")\n";
    out << "    )\n";
    out << "  )\n";
    out << "  (embedded_fonts no)\n";
    out << ")\n";
    result.text = out.str();
    return result;
}

} // namespace volt::adapters::kicad
