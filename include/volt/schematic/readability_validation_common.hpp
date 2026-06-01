#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <volt/circuit/definitions.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/schematic/readability_geometry.hpp>
#include <volt/schematic/schematic.hpp>

namespace volt {

namespace detail {

void add_readability_diagnostic(DiagnosticReport &report, Severity severity,
                                const std::string &code, std::string message, SheetId sheet_id,
                                EntityRef object, const std::vector<EntityRef> &context);

[[nodiscard]] std::vector<ReadabilityObject>
readability_objects_for_sheet(const Schematic &schematic, const Sheet &sheet);

[[nodiscard]] std::vector<ReadabilityTagObject>
readability_tags_for_sheet(const Schematic &schematic, const Sheet &sheet);

[[nodiscard]] std::vector<ReadabilityTextObject>
readability_texts_for_sheet(const Schematic &schematic, const Sheet &sheet);

[[nodiscard]] std::vector<ReadabilityCollisionObject>
readability_collision_objects_for_sheet(const Schematic &schematic, const Sheet &sheet);

[[nodiscard]] bool power_port_attaches_to_symbol_pin(const Schematic &schematic,
                                                     const PowerPort &port,
                                                     SymbolInstanceId symbol_id);

[[nodiscard]] bool terminal_marker_attaches_to_symbol_pin(const Schematic &schematic,
                                                          const ReadabilityObject &symbol,
                                                          const ReadabilityObject &object);

} // namespace detail

} // namespace volt
