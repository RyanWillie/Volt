#pragma once

#include <volt/schematic/readability_validation_common.hpp>

namespace volt {

namespace detail {

void validate_readability_bounds(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                                 DiagnosticReport &report);

void add_region_content_object_refs(std::vector<EntityRef> &refs,
                                    const std::vector<const ReadabilityObject *> &objects,
                                    SchematicBounds comparison_bounds);

void validate_authored_region_content_overlaps(const Schematic &schematic, SheetId sheet_id,
                                               const Sheet &sheet, DiagnosticReport &report);

void add_title_block_overflow_diagnostic(DiagnosticReport &report, SheetId sheet_id,
                                         std::string_view column, std::string_view row_label);

void validate_title_block_text_overflow(SheetId sheet_id, const Sheet &sheet,
                                        DiagnosticReport &report);

} // namespace detail

} // namespace volt
