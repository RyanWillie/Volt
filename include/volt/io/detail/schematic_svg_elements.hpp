#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ostream>
#include <variant>

#include <volt/io/detail/schematic_svg_common.hpp>

namespace volt::io::detail {

void write_symbol_arc_svg(std::ostream &out, const SymbolArc &arc);

[[nodiscard]] bool is_symbol_text(const SymbolPrimitive &primitive);

void write_symbol_primitive_svg(std::ostream &out, const SymbolPrimitive &primitive);

void write_symbol_pin_svg(std::ostream &out, const SymbolPin &pin);

void write_symbol_instance_svg(std::ostream &out, const Schematic &schematic, SymbolInstanceId id);

void write_symbol_text_instance_svg(std::ostream &out, const Schematic &schematic,
                                    SymbolInstanceId id);

void write_symbol_debug_overlay_svg(std::ostream &out, const Schematic &schematic,
                                    SymbolInstanceId id);

void write_wire_run_svg(std::ostream &out, const Schematic &schematic, WireRunId id);

void write_net_label_svg(std::ostream &out, const Schematic &schematic, NetLabelId id);

void write_junction_svg(std::ostream &out, const Schematic &schematic, JunctionId id);

void write_power_port_svg(std::ostream &out, const Schematic &schematic, PowerPortId id);

void write_no_connect_marker_svg(std::ostream &out, const Schematic &schematic,
                                 NoConnectMarkerId id);

void write_sheet_port_svg(std::ostream &out, const Schematic &schematic, SheetPortId id);

void write_symbol_field_svg(std::ostream &out, const Schematic &schematic, SymbolFieldId id);

} // namespace volt::io::detail
