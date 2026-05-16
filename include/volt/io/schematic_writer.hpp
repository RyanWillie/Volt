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
#include <volt/schematic/schematic_document.hpp>
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

[[nodiscard]] inline std::string junction_id(JunctionId id) {
    return "junction:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string power_port_id(PowerPortId id) {
    return "power_port:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string no_connect_marker_id(NoConnectMarkerId id) {
    return "no_connect_marker:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string sheet_port_id(SheetPortId id) {
    return "sheet_port:" + std::to_string(id.index());
}

[[nodiscard]] inline std::string symbol_field_id(SymbolFieldId id) {
    return "symbol_field:" + std::to_string(id.index());
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

[[nodiscard]] inline std::string route_intent_name(RouteIntent intent) {
    switch (intent) {
    case RouteIntent::Direct:
        return "Direct";
    case RouteIntent::Orthogonal:
        return "Orthogonal";
    }
    throw std::logic_error{"Unhandled route intent"};
}

[[nodiscard]] inline std::string power_port_kind_name(PowerPortKind kind) {
    switch (kind) {
    case PowerPortKind::Power:
        return "Power";
    case PowerPortKind::Ground:
        return "Ground";
    }
    throw std::logic_error{"Unhandled power port kind"};
}

[[nodiscard]] inline std::string sheet_port_kind_name(SheetPortKind kind) {
    switch (kind) {
    case SheetPortKind::Input:
        return "Input";
    case SheetPortKind::Output:
        return "Output";
    case SheetPortKind::Bidirectional:
        return "Bidirectional";
    case SheetPortKind::OffPage:
        return "OffPage";
    }
    throw std::logic_error{"Unhandled sheet port kind"};
}

inline void write_point(std::ostream &out, Point point) {
    out << "{ \"x\": ";
    write_json_number(out, point.x());
    out << ", \"y\": ";
    write_json_number(out, point.y());
    out << " }";
}

inline void write_sheet_metadata(std::ostream &out, const SheetMetadata &metadata) {
    out << "{ \"title\": " << json_string(metadata.title()) << ", \"size\": { \"width\": ";
    write_json_number(out, metadata.size().width());
    out << ", \"height\": ";
    write_json_number(out, metadata.size().height());
    out << " }, \"title_block\": [";
    for (std::size_t index = 0; index < metadata.title_block().size(); ++index) {
        const auto &field = metadata.title_block()[index];
        if (index != 0) {
            out << ", ";
        }
        out << "{ \"key\": " << json_string(field.key())
            << ", \"value\": " << json_string(field.value()) << " }";
    }
    out << "] }";
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

[[nodiscard]] inline SheetId sheet_for_junction(const Schematic &schematic, JunctionId junction) {
    for (std::size_t sheet_index = 0; sheet_index < schematic.sheet_count(); ++sheet_index) {
        const auto sheet = SheetId{sheet_index};
        const auto &junctions = schematic.sheet(sheet).junctions();
        if (std::find(junctions.begin(), junctions.end(), junction) != junctions.end()) {
            return sheet;
        }
    }
    throw std::logic_error{"Junction is not placed on a schematic sheet"};
}

[[nodiscard]] inline SheetId sheet_for_power_port(const Schematic &schematic, PowerPortId port) {
    for (std::size_t sheet_index = 0; sheet_index < schematic.sheet_count(); ++sheet_index) {
        const auto sheet = SheetId{sheet_index};
        const auto &ports = schematic.sheet(sheet).power_ports();
        if (std::find(ports.begin(), ports.end(), port) != ports.end()) {
            return sheet;
        }
    }
    throw std::logic_error{"Power port is not placed on a schematic sheet"};
}

[[nodiscard]] inline SheetId sheet_for_no_connect_marker(const Schematic &schematic,
                                                         NoConnectMarkerId marker) {
    for (std::size_t sheet_index = 0; sheet_index < schematic.sheet_count(); ++sheet_index) {
        const auto sheet = SheetId{sheet_index};
        const auto &markers = schematic.sheet(sheet).no_connect_markers();
        if (std::find(markers.begin(), markers.end(), marker) != markers.end()) {
            return sheet;
        }
    }
    throw std::logic_error{"No-connect marker is not placed on a schematic sheet"};
}

[[nodiscard]] inline SheetId sheet_for_sheet_port(const Schematic &schematic, SheetPortId port) {
    for (std::size_t sheet_index = 0; sheet_index < schematic.sheet_count(); ++sheet_index) {
        const auto sheet = SheetId{sheet_index};
        const auto &ports = schematic.sheet(sheet).sheet_ports();
        if (std::find(ports.begin(), ports.end(), port) != ports.end()) {
            return sheet;
        }
    }
    throw std::logic_error{"Sheet port is not placed on a schematic sheet"};
}

[[nodiscard]] inline SheetId sheet_for_symbol_field(const Schematic &schematic,
                                                    SymbolFieldId field) {
    for (std::size_t sheet_index = 0; sheet_index < schematic.sheet_count(); ++sheet_index) {
        const auto sheet = SheetId{sheet_index};
        const auto &fields = schematic.sheet(sheet).symbol_fields();
        if (std::find(fields.begin(), fields.end(), field) != fields.end()) {
            return sheet;
        }
    }
    throw std::logic_error{"Symbol field is not placed on a schematic sheet"};
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
            << ", \"name\": " << detail::json_string(sheet.name()) << ", \"metadata\": ";
        detail::write_sheet_metadata(out, sheet.metadata());
        out << ", \"symbol_instances\": [";
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
        out << "], \"junctions\": [";
        for (std::size_t junction_index = 0; junction_index < sheet.junctions().size();
             ++junction_index) {
            if (junction_index != 0) {
                out << ", ";
            }
            out << detail::json_string(detail::junction_id(sheet.junctions()[junction_index]));
        }
        out << "], \"power_ports\": [";
        for (std::size_t port_index = 0; port_index < sheet.power_ports().size(); ++port_index) {
            if (port_index != 0) {
                out << ", ";
            }
            out << detail::json_string(detail::power_port_id(sheet.power_ports()[port_index]));
        }
        out << "], \"no_connect_markers\": [";
        for (std::size_t marker_index = 0; marker_index < sheet.no_connect_markers().size();
             ++marker_index) {
            if (marker_index != 0) {
                out << ", ";
            }
            out << detail::json_string(
                detail::no_connect_marker_id(sheet.no_connect_markers()[marker_index]));
        }
        out << "], \"sheet_ports\": [";
        for (std::size_t port_index = 0; port_index < sheet.sheet_ports().size(); ++port_index) {
            if (port_index != 0) {
                out << ", ";
            }
            out << detail::json_string(detail::sheet_port_id(sheet.sheet_ports()[port_index]));
        }
        out << "], \"symbol_fields\": [";
        for (std::size_t field_index = 0; field_index < sheet.symbol_fields().size();
             ++field_index) {
            if (field_index != 0) {
                out << ", ";
            }
            out << detail::json_string(detail::symbol_field_id(sheet.symbol_fields()[field_index]));
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
        out << "], \"route_intent\": "
            << detail::json_string(detail::route_intent_name(wire.route_intent())) << " }";
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
    out << "  ],\n";

    out << "  \"junctions\": [\n";
    for (std::size_t index = 0; index < schematic.junction_count(); ++index) {
        const auto id = JunctionId{index};
        const auto &junction = schematic.junction(id);
        out << "    { \"id\": " << detail::json_string(detail::junction_id(id)) << ", \"sheet\": "
            << detail::json_string(detail::sheet_id(detail::sheet_for_junction(schematic, id)))
            << ", \"net\": " << detail::json_string(detail::net_id(junction.net()))
            << ", \"position\": ";
        detail::write_point(out, junction.position());
        out << " }";
        if (index + 1 != schematic.junction_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"power_ports\": [\n";
    for (std::size_t index = 0; index < schematic.power_port_count(); ++index) {
        const auto id = PowerPortId{index};
        const auto &port = schematic.power_port(id);
        out << "    { \"id\": " << detail::json_string(detail::power_port_id(id)) << ", \"sheet\": "
            << detail::json_string(detail::sheet_id(detail::sheet_for_power_port(schematic, id)))
            << ", \"net\": " << detail::json_string(detail::net_id(port.net()))
            << ", \"kind\": " << detail::json_string(detail::power_port_kind_name(port.kind()))
            << ", \"position\": ";
        detail::write_point(out, port.position());
        out << ", \"orientation\": "
            << detail::json_string(detail::schematic_orientation_name(port.orientation())) << " }";
        if (index + 1 != schematic.power_port_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"no_connect_markers\": [\n";
    for (std::size_t index = 0; index < schematic.no_connect_marker_count(); ++index) {
        const auto id = NoConnectMarkerId{index};
        const auto &marker = schematic.no_connect_marker(id);
        out << "    { \"id\": " << detail::json_string(detail::no_connect_marker_id(id))
            << ", \"sheet\": "
            << detail::json_string(
                   detail::sheet_id(detail::sheet_for_no_connect_marker(schematic, id)))
            << ", \"pin\": " << detail::json_string(detail::pin_id(marker.pin()))
            << ", \"position\": ";
        detail::write_point(out, marker.position());
        out << ", \"orientation\": "
            << detail::json_string(detail::schematic_orientation_name(marker.orientation()));
        if (!marker.reason().empty()) {
            out << ", \"reason\": " << detail::json_string(marker.reason());
        }
        out << " }";
        if (index + 1 != schematic.no_connect_marker_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"sheet_ports\": [\n";
    for (std::size_t index = 0; index < schematic.sheet_port_count(); ++index) {
        const auto id = SheetPortId{index};
        const auto &port = schematic.sheet_port(id);
        out << "    { \"id\": " << detail::json_string(detail::sheet_port_id(id)) << ", \"sheet\": "
            << detail::json_string(detail::sheet_id(detail::sheet_for_sheet_port(schematic, id)))
            << ", \"net\": " << detail::json_string(detail::net_id(port.net()))
            << ", \"name\": " << detail::json_string(port.name())
            << ", \"kind\": " << detail::json_string(detail::sheet_port_kind_name(port.kind()))
            << ", \"position\": ";
        detail::write_point(out, port.position());
        out << ", \"orientation\": "
            << detail::json_string(detail::schematic_orientation_name(port.orientation())) << " }";
        if (index + 1 != schematic.sheet_port_count()) {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n";

    out << "  \"symbol_fields\": [\n";
    for (std::size_t index = 0; index < schematic.symbol_field_count(); ++index) {
        const auto id = SymbolFieldId{index};
        const auto &field = schematic.symbol_field(id);
        out << "    { \"id\": " << detail::json_string(detail::symbol_field_id(id))
            << ", \"sheet\": "
            << detail::json_string(detail::sheet_id(detail::sheet_for_symbol_field(schematic, id)))
            << ", \"symbol_instance\": "
            << detail::json_string(detail::symbol_instance_id(field.symbol_instance()))
            << ", \"name\": " << detail::json_string(field.name())
            << ", \"value\": " << detail::json_string(field.value()) << ", \"position\": ";
        detail::write_point(out, field.position());
        out << ", \"orientation\": "
            << detail::json_string(detail::schematic_orientation_name(field.orientation())) << " }";
        if (index + 1 != schematic.symbol_field_count()) {
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

/** Write a deterministic JSON representation of a schematic document. */
inline void write_schematic(std::ostream &out, const SchematicDocument &document) {
    write_schematic(out, document.schematic());
}

/** Return a deterministic JSON representation of a schematic document. */
[[nodiscard]] inline std::string write_schematic(const SchematicDocument &document) {
    return write_schematic(document.schematic());
}

} // namespace volt::io
