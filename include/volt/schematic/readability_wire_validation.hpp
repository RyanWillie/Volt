#pragma once

#include <volt/schematic/readability_validation_common.hpp>
#include <volt/schematic/wire_topology.hpp>

namespace volt {

namespace detail {

[[nodiscard]] double wire_run_length(const WireRun &wire) noexcept;

void validate_long_local_doglegs(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                                 DiagnosticReport &report);

[[nodiscard]] bool points_align_as_stack(const std::vector<Point> &points) noexcept;

void validate_misaligned_local_labels(const Schematic &schematic, SheetId sheet_id,
                                      const Sheet &sheet, DiagnosticReport &report);

void validate_ambiguous_same_net_crossings(const Schematic &schematic, SheetId sheet_id,
                                           const Sheet &sheet, DiagnosticReport &report);

void validate_different_net_wire_crossings(const Schematic &schematic, SheetId sheet_id,
                                           const Sheet &sheet, DiagnosticReport &report);

[[nodiscard]] bool sheet_has_same_net_tag_at_point(const Schematic &schematic, const Sheet &sheet,
                                                   NetId net, Point point);

[[nodiscard]] bool sheet_has_symbol_pin_for_net_at_point(const Schematic &schematic,
                                                         const Sheet &sheet, NetId net,
                                                         Point point);

[[nodiscard]] bool sheet_has_terminal_or_sheet_port_for_net_at_point(const Schematic &schematic,
                                                                     const Sheet &sheet, NetId net,
                                                                     Point point);

[[nodiscard]] bool sheet_has_junction_for_net_at_point(const Schematic &schematic,
                                                       const Sheet &sheet, NetId net, Point point);

[[nodiscard]] bool wire_has_endpoint_at_point(const WireRun &wire, Point point) noexcept;

[[nodiscard]] bool sheet_has_other_same_net_wire_endpoint_at_point(const Schematic &schematic,
                                                                   const Sheet &sheet, NetId net,
                                                                   Point point,
                                                                   WireRunId excluded_wire);

[[nodiscard]] bool wire_endpoint_has_readable_anchor(const Schematic &schematic, const Sheet &sheet,
                                                     NetId net, Point point, WireRunId wire_id);

void validate_dangling_wire_endpoints(const Schematic &schematic, SheetId sheet_id,
                                      const Sheet &sheet, DiagnosticReport &report);

[[nodiscard]] bool sheet_has_other_same_net_wire_at_point(const Schematic &schematic,
                                                          const Sheet &sheet, NetId net,
                                                          Point point, WireRunId excluded_wire);

/** Short tagged wire run that may read as a floating local stub. */
struct FloatingStubCandidate {
    /** Wire run that forms the short stub. */
    WireRunId wire;
    /** Sheet-space midpoint used to cluster nearby stubs deterministically. */
    Point center;
};

void validate_floating_stub_clusters(const Schematic &schematic, SheetId sheet_id,
                                     const Sheet &sheet, DiagnosticReport &report);

void validate_duplicate_junctions(const Schematic &schematic, SheetId sheet_id, const Sheet &sheet,
                                  DiagnosticReport &report);

} // namespace detail

} // namespace volt
