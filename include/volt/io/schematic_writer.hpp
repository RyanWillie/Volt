#pragma once

#include <algorithm>
#include <cstddef>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

#include <volt/io/logical_circuit_writer.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/symbols.hpp>

namespace volt::io {

/** Return the canonical v1 schematic projection format name. */
[[nodiscard]] inline constexpr std::string_view schematic_format_name() noexcept {
    return "volt.schematic";
}

/** Return the canonical schematic projection format version written by this library. */
[[nodiscard]] inline constexpr int schematic_format_version() noexcept { return 1; }

namespace detail {

[[nodiscard]] inline std::string symbol_def_id(SymbolDefId id) {
    return "symbol_def:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string sheet_id(SheetId id) {
    return "sheet:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string symbol_instance_id(SymbolInstanceId id) {
    return "symbol_instance:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string wire_run_id(WireRunId id) {
    return "wire_run:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string net_label_id(NetLabelId id) {
    return "net_label:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string schematic_orientation_name(SchematicOrientation orientation) {
    switch (orientation) {
    case SchematicOrientation::Right:
        return "Right";
    case SchematicOrientation::Down:
        return "Down";
    case SchematicOrientation::Left:
        return "Left";
    case SchematicOrientation::Up:
        return "Up";
    }
    throw std::logic_error{"Unhandled schematic orientation"};
}

inline void write_point(std::ostream &out, Point point) {
    out << "{ \"x\": ";
    write_json_number(out, point.x());
    out << ", \"y\": ";
    write_json_number(out, point.y());
    out << " }";
}

inline void write_symbol_primitive(std::ostream &out, const SymbolPrimitive &primitive) {
    if (std::holds_alternative<SymbolLine>(primitive)) {
        const auto &line = std::get<SymbolLine>(primitive);
        out << "{ \"type\": \"line\", \"start\": ";
        write_point(out, line.start());
        out << ", \"end\": ";
        write_point(out, line.end());
        out << " }";
        return;
    }
    if (std::holds_alternative<SymbolRectangle>(primitive)) {
        const auto &rectangle = std::get<SymbolRectangle>(primitive);
        out << "{ \"type\": \"rectangle\", \"first_corner\": ";
        write_point(out, rectangle.first_corner());
        out << ", \"second_corner\": ";
        write_point(out, rectangle.second_corner());
        out << " }";
        return;
    }
    if (std::holds_alternative<SymbolCircle>(primitive)) {
        const auto &circle = std::get<SymbolCircle>(primitive);
        out << "{ \"type\": \"circle\", \"center\": ";
        write_point(out, circle.center());
        out << ", \"radius\": ";
        write_json_number(out, circle.radius());
        out << " }";
        return;
    }
    if (std::holds_alternative<SymbolArc>(primitive)) {
        const auto &arc = std::get<SymbolArc>(primitive);
        out << "{ \"type\": \"arc\", \"center\": ";
        write_point(out, arc.center());
        out << ", \"radius\": ";
        write_json_number(out, arc.radius());
        out << ", \"start_degrees\": ";
        write_json_number(out, arc.start_degrees());
        out << ", \"sweep_degrees\": ";
        write_json_number(out, arc.sweep_degrees());
        out << " }";
        return;
    }

    const auto &text = std::get<SymbolText>(primitive);
    out << "{ \"type\": \"text\", \"text\": " << json_string(text.text()) << ", \"anchor\": ";
    write_point(out, text.anchor());
    out << ", \"orientation\": " << json_string(schematic_orientation_name(text.orientation()))
        << " }";
}

[[nodiscard]] inline SheetId sheet_for_symbol_instance(const Schematic &schematic,
                                                       SymbolInstanceId instance) {
    for (std::size_t sheet_index = 0; sheet_index < schematic.sheet_count(); ++sheet_index) {
        const auto sheet = SheetId{sheet_index};
        const auto &instances = schematic.sheet(sheet).symbol_instances();
        if (std::find(instances.begin(), instances.end(), instance) != instances.end()) {
            return sheet;
        }
    }
    throw std::logic_error{"Symbol instance is not placed on a schematic sheet"};
}

[[nodiscard]] inline SheetId sheet_for_wire_run(const Schematic &schematic, WireRunId wire) {
    for (std::size_t sheet_index = 0; sheet_index < schematic.sheet_count(); ++sheet_index) {
        const auto sheet = SheetId{sheet_index};
        const auto &wires = schematic.sheet(sheet).wire_runs();
        if (std::find(wires.begin(), wires.end(), wire) != wires.end()) {
            return sheet;
        }
    }
    throw std::logic_error{"Wire run is not placed on a schematic sheet"};
}

[[nodiscard]] inline SheetId sheet_for_net_label(const Schematic &schematic, NetLabelId label) {
    for (std::size_t sheet_index = 0; sheet_index < schematic.sheet_count(); ++sheet_index) {
        const auto sheet = SheetId{sheet_index};
        const auto &labels = schematic.sheet(sheet).net_labels();
        if (std::find(labels.begin(), labels.end(), label) != labels.end()) {
            return sheet;
        }
    }
    throw std::logic_error{"Net label is not placed on a schematic sheet"};
}

} // namespace detail

/** Write a deterministic JSON representation of a schematic projection to an output stream. */
inline void write_schematic(std::ostream &out, const Schematic &schematic) {
    out << "{\n";
    out << "  \"format\": " << detail::json_string(schematic_format_name()) << ",\n";
    out << "  \"version\": " << schematic_format_version() << ",\n";

    out << "  \"symbol_definitions\": [\n";
    for (std::size_t index = 0; index < schematic.symbol_definition_count(); ++index) {
        const auto id = SymbolDefId{index};
        const auto &symbol = schematic.symbol_definition(id);
        out << "    { \"id\": " << detail::json_string(detail::symbol_def_id(id))
            << ", \"name\": " << detail::json_string(symbol.name()) << ", \"pins\": [";
        for (std::size_t pin_index = 0; pin_index < symbol.pins().size(); ++pin_index) {
            const auto &pin = symbol.pins()[pin_index];
            if (pin_index != 0) {
                out << ", ";
            }
            out << "{ \"name\": " << detail::json_string(pin.name())
                << ", \"number\": " << detail::json_string(pin.number()) << ", \"anchor\": ";
            detail::write_point(out, pin.anchor());
            out << ", \"orientation\": "
                << detail::json_string(detail::schematic_orientation_name(pin.orientation()))
                << " }";
        }
        out << "], \"primitives\": [";
        for (std::size_t primitive_index = 0; primitive_index < symbol.primitives().size();
             ++primitive_index) {
            if (primitive_index != 0) {
                out << ", ";
            }
            detail::write_symbol_primitive(out, symbol.primitives()[primitive_index]);
        }
        out << "] }";
        if (index + 1 != schematic.symbol_definition_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"sheets\": [\n";
    for (std::size_t index = 0; index < schematic.sheet_count(); ++index) {
        const auto id = SheetId{index};
        const auto &sheet = schematic.sheet(id);
        out << "    { \"id\": " << detail::json_string(detail::sheet_id(id))
            << ", \"name\": " << detail::json_string(sheet.name()) << ", \"symbol_instances\": [";
        for (std::size_t instance_index = 0; instance_index < sheet.symbol_instances().size();
             ++instance_index) {
            if (instance_index != 0) {
                out << ", ";
            }
            out << detail::json_string(
                detail::symbol_instance_id(sheet.symbol_instances()[instance_index]));
        }
        out << "], \"wire_runs\": [";
        for (std::size_t wire_index = 0; wire_index < sheet.wire_runs().size(); ++wire_index) {
            if (wire_index != 0) {
                out << ", ";
            }
            out << detail::json_string(detail::wire_run_id(sheet.wire_runs()[wire_index]));
        }
        out << "], \"net_labels\": [";
        for (std::size_t label_index = 0; label_index < sheet.net_labels().size(); ++label_index) {
            if (label_index != 0) {
                out << ", ";
            }
            out << detail::json_string(detail::net_label_id(sheet.net_labels()[label_index]));
        }
        out << "] }";
        if (index + 1 != schematic.sheet_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"symbol_instances\": [\n";
    for (std::size_t index = 0; index < schematic.symbol_instance_count(); ++index) {
        const auto id = SymbolInstanceId{index};
        const auto &instance = schematic.symbol_instance(id);
        out << "    { \"id\": " << detail::json_string(detail::symbol_instance_id(id))
            << ", \"sheet\": "
            << detail::json_string(
                   detail::sheet_id(detail::sheet_for_symbol_instance(schematic, id)))
            << ", \"symbol_definition\": "
            << detail::json_string(detail::symbol_def_id(instance.symbol_definition()))
            << ", \"component\": "
            << detail::json_string(detail::component_id(instance.component()))
            << ", \"position\": ";
        detail::write_point(out, instance.position());
        out << ", \"orientation\": "
            << detail::json_string(detail::schematic_orientation_name(instance.orientation()))
            << " }";
        if (index + 1 != schematic.symbol_instance_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"wire_runs\": [\n";
    for (std::size_t index = 0; index < schematic.wire_run_count(); ++index) {
        const auto id = WireRunId{index};
        const auto &wire = schematic.wire_run(id);
        out << "    { \"id\": " << detail::json_string(detail::wire_run_id(id)) << ", \"sheet\": "
            << detail::json_string(detail::sheet_id(detail::sheet_for_wire_run(schematic, id)))
            << ", \"net\": " << detail::json_string(detail::net_id(wire.net()))
            << ", \"points\": [";
        for (std::size_t point_index = 0; point_index < wire.points().size(); ++point_index) {
            if (point_index != 0) {
                out << ", ";
            }
            detail::write_point(out, wire.points()[point_index]);
        }
        out << "] }";
        if (index + 1 != schematic.wire_run_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"net_labels\": [\n";
    for (std::size_t index = 0; index < schematic.net_label_count(); ++index) {
        const auto id = NetLabelId{index};
        const auto &label = schematic.net_label(id);
        out << "    { \"id\": " << detail::json_string(detail::net_label_id(id)) << ", \"sheet\": "
            << detail::json_string(detail::sheet_id(detail::sheet_for_net_label(schematic, id)))
            << ", \"net\": " << detail::json_string(detail::net_id(label.net()))
            << ", \"position\": ";
        detail::write_point(out, label.position());
        out << ", \"orientation\": "
            << detail::json_string(detail::schematic_orientation_name(label.orientation())) << " }";
        if (index + 1 != schematic.net_label_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ]\n";
    out << "}\n";
}

/** Return a deterministic JSON representation of a schematic projection. */
[[nodiscard]] inline std::string write_schematic(const Schematic &schematic) {
    auto out = std::ostringstream{};
    write_schematic(out, schematic);
    return out.str();
}

} // namespace volt::io
