#pragma once

#include <volt/schematic/readability_validation_common.hpp>

namespace volt {

namespace detail {

void validate_port_tag_scale(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                             DiagnosticReport &report);

[[nodiscard]] bool label_or_tag_crowds_symbols(ReadabilityObjectKind kind) noexcept;

void validate_symbol_crowding(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                              DiagnosticReport &report);

[[nodiscard]] double tag_stack_primary_position(const ReadabilityTagObject &tag) noexcept;

[[nodiscard]] double tag_stack_cross_position(const ReadabilityTagObject &tag) noexcept;

void add_crowded_tag_stack_diagnostic(DiagnosticReport &report, SheetId sheet_id,
                                      const std::vector<ReadabilityTagObject> &tags,
                                      const std::vector<std::size_t> &cluster);

void validate_crowded_tag_stacks(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                                 DiagnosticReport &report);

void validate_dense_region_port_tags(const Schematic &schematic, SheetId sheet_id,
                                     const Sheet &sheet, DiagnosticReport &report);

[[nodiscard]] bool component_definition_is_passive(CircuitView circuit,
                                                   ComponentDefId definition_id);

[[nodiscard]] bool component_has_known_value(const ComponentInstance &component);

[[nodiscard]] bool symbol_instance_has_value_field(const Schematic &schematic, const Sheet &sheet,
                                                   SymbolInstanceId instance);

void validate_missing_passive_value_fields(const Schematic &schematic, SheetId sheet_id,
                                           const Sheet &sheet, DiagnosticReport &report);

[[nodiscard]] double squared_distance(Point lhs, Point rhs) noexcept;

void validate_terminal_marker_net_kind_mismatch(const Schematic &schematic, SheetId sheet_id,
                                                const Sheet &sheet, DiagnosticReport &report);

void validate_dense_no_connect_clusters(const Schematic &schematic, SheetId sheet_id,
                                        const Sheet &sheet, DiagnosticReport &report);

} // namespace detail

} // namespace volt
