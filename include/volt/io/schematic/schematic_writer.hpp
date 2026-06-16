#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

#include <volt/io/detail/typed_id.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/io/schematic/schematic_schema.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/schematic_document.hpp>
#include <volt/schematic/symbols.hpp>

namespace volt::io {

namespace detail {

[[nodiscard]] inline std::string symbol_def_id(SymbolDefId id) { return encode_local_id(id); }

[[nodiscard]] inline std::string sheet_id(SheetId id) { return encode_local_id(id); }

[[nodiscard]] std::string symbol_instance_id(SymbolInstanceId id);

[[nodiscard]] inline std::string wire_run_id(WireRunId id) { return encode_local_id(id); }

[[nodiscard]] inline std::string net_label_id(NetLabelId id) { return encode_local_id(id); }

[[nodiscard]] inline std::string junction_id(JunctionId id) { return encode_local_id(id); }

[[nodiscard]] inline std::string power_port_id(PowerPortId id) { return encode_local_id(id); }

[[nodiscard]] std::string no_connect_marker_id(NoConnectMarkerId id);

[[nodiscard]] inline std::string sheet_port_id(SheetPortId id) { return encode_local_id(id); }

[[nodiscard]] inline std::string symbol_field_id(SymbolFieldId id) { return encode_local_id(id); }

void write_point(std::ostream &out, Point point);

void write_sheet_margins(std::ostream &out, SheetMargins margins);

void write_sheet_frame(std::ostream &out, SheetFrame frame);

void write_sheet_coordinate_zones(std::ostream &out, SheetCoordinateZones zones);

void write_sheet_grid(std::ostream &out, SheetGrid grid);

void write_sheet_region_style(std::ostream &out, const std::vector<SheetRegionStyleField> &style);

void write_sheet_region(std::ostream &out, const SheetRegion &region);

void write_authored_region(std::ostream &out, const Sheet &sheet,
                           const std::optional<std::size_t> &region);

void write_text_style_fields(std::ostream &out, SchematicTextStyle style,
                             SchematicTextStyle defaults);

void write_sheet_metadata(std::ostream &out, const SheetMetadata &metadata);

void write_symbol_primitive(std::ostream &out, const SymbolPrimitive &primitive);

[[nodiscard]] SheetId sheet_for_symbol_instance(const Schematic &schematic,
                                                SymbolInstanceId instance);

[[nodiscard]] SheetId sheet_for_wire_run(const Schematic &schematic, WireRunId wire);

[[nodiscard]] SheetId sheet_for_net_label(const Schematic &schematic, NetLabelId label);

[[nodiscard]] SheetId sheet_for_junction(const Schematic &schematic, JunctionId junction);

[[nodiscard]] SheetId sheet_for_power_port(const Schematic &schematic, PowerPortId port);

[[nodiscard]] SheetId sheet_for_no_connect_marker(const Schematic &schematic,
                                                  NoConnectMarkerId marker);

[[nodiscard]] SheetId sheet_for_sheet_port(const Schematic &schematic, SheetPortId port);

[[nodiscard]] SheetId sheet_for_symbol_field(const Schematic &schematic, SymbolFieldId field);

} // namespace detail

/** Write a deterministic JSON representation of a schematic projection to an output stream. */
void write_schematic(std::ostream &out, const Schematic &schematic);

/** Return a deterministic JSON representation of a schematic projection. */
[[nodiscard]] std::string write_schematic(const Schematic &schematic);

/** Write a deterministic JSON representation of a schematic document. */
void write_schematic(std::ostream &out, const SchematicDocument &document);

/** Return a deterministic JSON representation of a schematic document. */
[[nodiscard]] std::string write_schematic(const SchematicDocument &document);

} // namespace volt::io
