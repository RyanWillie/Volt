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

#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/schematic/readability_geometry.hpp>
#include <volt/schematic/schematic.hpp>
#include <volt/schematic/wire_topology.hpp>

namespace volt {

namespace detail {

void add_outside_sheet_diagnostic(DiagnosticReport &report, SheetId sheet_id, EntityRef object,
                                  std::vector<EntityRef> entities);

void validate_component_placement_coverage(const Schematic &schematic, const Sheet &sheet,
                                           SheetId sheet_id, SymbolInstanceId instance_id,
                                           DiagnosticReport &report);

void validate_component_placements(const Schematic &schematic, DiagnosticReport &report);

void validate_repeated_labels(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                              DiagnosticReport &report);

void validate_fragmented_pin_labels_for_net(const Schematic &schematic, SheetId sheet_id,
                                            const Sheet &sheet, NetId net_id,
                                            DiagnosticReport &report);

void validate_fragmented_pin_labels(const Schematic &schematic, SheetId sheet_id,
                                    const Sheet &sheet, DiagnosticReport &report);

void validate_outside_sheet_objects(const Schematic &schematic, SheetId sheet_id,
                                    const Sheet &sheet, DiagnosticReport &report);

void validate_same_net_crossings(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                                 DiagnosticReport &report);

[[nodiscard]] bool schematic_has_no_connect_marker_for_pin(const Schematic &schematic, PinId pin);

void validate_no_connect_markers(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                                 DiagnosticReport &report);

void validate_missing_no_connect_markers(const Schematic &schematic, DiagnosticReport &report);

} // namespace detail

/** Validate that placed connected schematic pins have visible net geometry on their sheet. */
[[nodiscard]] DiagnosticReport validate_schematic_readiness(const Schematic &schematic);

} // namespace volt
