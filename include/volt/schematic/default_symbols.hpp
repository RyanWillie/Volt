#pragma once

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <volt/schematic/symbols.hpp>

namespace volt {
namespace default_symbol_detail {

// Production defaults keep identity in fields or explicitly authored text, not internal
// debug glyphs embedded in generic element geometry.

void add_pin(SymbolDefinition &symbol, std::string name, std::string number, Point anchor,
             SchematicOrientation orientation);

void add_two_pin_anchors(SymbolDefinition &symbol, std::string left_name, std::string left_number,
                         std::string right_name, std::string right_number);

[[nodiscard]] SymbolDefinition resistor_symbol(std::string_view name);

[[nodiscard]] SymbolDefinition capacitor_symbol(std::string_view name);

[[nodiscard]] SymbolDefinition polarized_capacitor_symbol(std::string_view name);

[[nodiscard]] SymbolDefinition inductor_symbol(std::string_view name);

[[nodiscard]] SymbolDefinition diode_symbol(std::string_view name);

[[nodiscard]] SymbolDefinition led_symbol(std::string_view name);

[[nodiscard]] SymbolDefinition switch_symbol(std::string_view name);

[[nodiscard]] SymbolDefinition crystal_symbol(std::string_view name);

[[nodiscard]] SymbolDefinition test_point_symbol(std::string_view name);

[[nodiscard]] SymbolDefinition connector_symbol(std::string_view name,
                                                std::initializer_list<std::string_view> pin_names);

[[nodiscard]] SymbolDefinition regulator_symbol(std::string_view name);

[[nodiscard]] SymbolDefinition op_amp_symbol(std::string_view name);

} // namespace default_symbol_detail

/** Return a named built-in schematic symbol definition, if it is in the default catalog. */
[[nodiscard]] std::optional<SymbolDefinition> default_schematic_symbol(std::string_view name);

} // namespace volt
