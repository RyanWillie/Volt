#pragma once

#include <volt/schematic/readability_validation_common.hpp>

namespace volt {

namespace detail {

[[nodiscard]] bool display_label_is_overlong_or_scoped(const std::string &label);

[[nodiscard]] bool ascii_upper_alpha(char character) noexcept;

[[nodiscard]] bool ascii_digit(char character) noexcept;

// Returns true if the label looks like a conventional EDA reference designator: 1–4 uppercase
// letters followed by one or more decimal digits (e.g. R1, C12, U3, CONN4, TP10).  Labels
// containing scope separators ('/', "::"), underscores, mixed-case suffixes, or all-letter
// tokens are treated as unconventional.  Prefixes longer than four characters are also flagged
// to keep designators compact and readable; use a shorter type prefix instead (e.g. "FB" for
// ferrite bead rather than "FBEAD").
[[nodiscard]] bool visible_reference_label_looks_conventional(std::string_view label) noexcept;

void validate_visible_reference_labels(const Schematic &schematic, SheetId sheet_id,
                                       const Sheet &sheet, DiagnosticReport &report);

void validate_label_readability(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                                DiagnosticReport &report);

void validate_symbol_field_ownership_distance(const Schematic &schematic, SheetId sheet_id,
                                              const Sheet &sheet, DiagnosticReport &report);

void validate_text_collisions(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                              DiagnosticReport &report);

} // namespace detail

} // namespace volt
