#pragma once

#include <volt/schematic/readability_validation_common.hpp>

namespace volt {

namespace detail {

[[nodiscard]] bool text_anchor_intentionally_attaches_to_wire(const ReadabilityTextObject &text,
                                                              const WireRun &wire);

void validate_text_wire_collisions(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                                   DiagnosticReport &report);

void validate_text_symbol_collisions(const Schematic &schematic, SheetId sheet_id,
                                     const Sheet &sheet, DiagnosticReport &report);

void validate_symbol_overlaps(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                              DiagnosticReport &report);

[[nodiscard]] bool segment_visually_attaches_to_symbol_pin(const Schematic &schematic,
                                                           const WireRun &wire,
                                                           SchematicSegment segment,
                                                           SymbolInstanceId instance_id);

void validate_wire_symbol_collisions(const Schematic &schematic, SheetId sheet_id,
                                     const Sheet &sheet, DiagnosticReport &report);

void validate_terminal_wire_collisions(const Schematic &schematic, SheetId sheet_id,
                                       const Sheet &sheet, DiagnosticReport &report);

void validate_terminal_symbol_collisions(const Schematic &schematic, SheetId sheet_id,
                                         const Sheet &sheet, DiagnosticReport &report);

[[nodiscard]] bool readability_collision_kind_pair(ReadabilityCollisionKind first,
                                                   ReadabilityCollisionKind second,
                                                   ReadabilityCollisionKind lhs,
                                                   ReadabilityCollisionKind rhs) noexcept;

[[nodiscard]] bool
readability_collision_is_handled_by_specific_validator(const ReadabilityCollisionObject &first,
                                                       const ReadabilityCollisionObject &second);

[[nodiscard]] bool
readability_collision_is_intentional_wire_contact(const ReadabilityCollisionObject &first,
                                                  const ReadabilityCollisionObject &second);

[[nodiscard]] bool readability_collision_is_intentional_owning_symbol_contact(
    const Schematic &schematic, const ReadabilityCollisionObject &first,
    const ReadabilityCollisionObject &second);

[[nodiscard]] bool
readability_collision_is_intentional_junction_contact(const ReadabilityCollisionObject &first,
                                                      const ReadabilityCollisionObject &second);

[[nodiscard]] bool readability_collision_is_intentional_junction_symbol_pin_contact(
    const Schematic &schematic, const ReadabilityCollisionObject &first,
    const ReadabilityCollisionObject &second);

[[nodiscard]] bool
readability_collision_is_intentional_contact(const Schematic &schematic,
                                             const ReadabilityCollisionObject &first,
                                             const ReadabilityCollisionObject &second);

[[nodiscard]] bool readability_collision_shapes_intersect(const ReadabilityCollisionObject &first,
                                                          const ReadabilityCollisionObject &second);

[[nodiscard]] bool readability_collision_pair_reported(
    const std::vector<std::pair<EntityRef, EntityRef>> &reported_pairs, EntityRef first,
    EntityRef second);

void validate_visual_element_collisions(const Schematic &schematic, SheetId sheet_id,
                                        const Sheet &sheet, DiagnosticReport &report);

} // namespace detail

} // namespace volt
