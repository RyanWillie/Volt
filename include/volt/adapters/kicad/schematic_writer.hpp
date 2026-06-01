#pragma once

#include <cmath>
#include <cstddef>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

#include <volt/adapters/kicad/loss_report.hpp>
#include <volt/core/properties.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/symbols.hpp>

namespace volt::adapters::kicad {

/** Result of exporting Volt schematic projection data to a KiCad schematic document. */
struct SchematicExportResult {
    /** Deterministic `.kicad_sch` text for the supported flat schematic subset. */
    std::string text;

    /** Structured warnings for constructs intentionally omitted by the first adapter subset. */
    LossReport loss_report;
};

namespace detail {

[[nodiscard]] std::string sexpr_string(std::string_view value);

void write_number(std::ostream &out, double value);

void write_xy(std::ostream &out, Point point);

void write_at(std::ostream &out, Point point, SchematicOrientation orientation);

[[nodiscard]] std::string stable_uuid(std::size_t value);

[[nodiscard]] std::string component_id(ComponentId id);

[[nodiscard]] std::string symbol_library_name(const SymbolDefinition &symbol);

[[nodiscard]] std::string property_value_to_string(const PropertyValue &value);

void write_effects(std::ostream &out, bool hidden = false);

[[nodiscard]] std::string component_value(const ComponentInstance &component,
                                          const ComponentDefinition &definition);

void write_symbol_property(std::ostream &out, std::string_view name, std::string_view value,
                           Point at, bool hidden = false);

void write_library_property(std::ostream &out, std::string_view name, std::string_view value,
                            Point at);

void report_unsupported_primitives(const SymbolDefinition &symbol, LossReport &loss_report);

void write_symbol_primitive(std::ostream &out, const SymbolPrimitive &primitive);

void write_symbol_pin(std::ostream &out, const SymbolPin &pin);

void write_library_symbol(std::ostream &out, const SymbolDefinition &symbol);

void write_wire(std::ostream &out, const WireRun &wire, std::size_t index);

void write_label(std::ostream &out, const Schematic &schematic, const NetLabel &label,
                 std::size_t index);

void write_symbol_instance(std::ostream &out, const Schematic &schematic, SymbolInstanceId id,
                           std::size_t index);

} // namespace detail

/** Write one flat KiCad schematic sheet from Volt-owned logical and schematic data. */
[[nodiscard]] SchematicExportResult write_flat_schematic(const Schematic &schematic);

} // namespace volt::adapters::kicad
