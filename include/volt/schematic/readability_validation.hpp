#pragma once

#include <volt/core/diagnostics.hpp>
#include <volt/schematic/readability_bounds_validation.hpp>
#include <volt/schematic/readability_collision_validation.hpp>
#include <volt/schematic/readability_density_validation.hpp>
#include <volt/schematic/readability_label_validation.hpp>
#include <volt/schematic/readability_validation_common.hpp>
#include <volt/schematic/readability_wire_validation.hpp>
#include <volt/schematic/schematic.hpp>

namespace volt {

/**
 * Validate schematic document readability and presentation quality.
 *
 * This layer is separate from logical netlist correctness and schematic readiness. Text
 * collision diagnostics use deterministic conservative bounding boxes rather than renderer
 * font measurement, so they are suitable for tests and agent feedback but may over-report.
 */
[[nodiscard]] inline DiagnosticReport validate_schematic_readability(const Schematic &schematic) {
    auto report = DiagnosticReport{};

    for (std::size_t sheet_index = 0; sheet_index < schematic.sheet_count(); ++sheet_index) {
        const auto sheet_id = SheetId{sheet_index};
        const auto &sheet = schematic.sheet(sheet_id);
        detail::validate_readability_bounds(schematic, sheet_id, sheet, report);
        detail::validate_authored_region_content_overlaps(schematic, sheet_id, sheet, report);
        detail::validate_title_block_text_overflow(sheet_id, sheet, report);
        detail::validate_duplicate_junctions(schematic, sheet_id, sheet, report);
        detail::validate_visible_reference_labels(schematic, sheet_id, sheet, report);
        detail::validate_label_readability(schematic, sheet_id, sheet, report);
        detail::validate_symbol_field_ownership_distance(schematic, sheet_id, sheet, report);
        detail::validate_port_tag_scale(schematic, sheet_id, sheet, report);
        detail::validate_text_wire_collisions(schematic, sheet_id, sheet, report);
        detail::validate_text_symbol_collisions(schematic, sheet_id, sheet, report);
        detail::validate_symbol_overlaps(schematic, sheet_id, sheet, report);
        detail::validate_wire_symbol_collisions(schematic, sheet_id, sheet, report);
        detail::validate_terminal_wire_collisions(schematic, sheet_id, sheet, report);
        detail::validate_terminal_symbol_collisions(schematic, sheet_id, sheet, report);
        detail::validate_visual_element_collisions(schematic, sheet_id, sheet, report);
        detail::validate_long_local_doglegs(schematic, sheet_id, sheet, report);
        detail::validate_misaligned_local_labels(schematic, sheet_id, sheet, report);
        detail::validate_ambiguous_same_net_crossings(schematic, sheet_id, sheet, report);
        detail::validate_different_net_wire_crossings(schematic, sheet_id, sheet, report);
        detail::validate_dangling_wire_endpoints(schematic, sheet_id, sheet, report);
        detail::validate_floating_stub_clusters(schematic, sheet_id, sheet, report);
        detail::validate_symbol_crowding(schematic, sheet_id, sheet, report);
        detail::validate_crowded_tag_stacks(schematic, sheet_id, sheet, report);
        detail::validate_dense_region_port_tags(schematic, sheet_id, sheet, report);
        detail::validate_missing_passive_value_fields(schematic, sheet_id, sheet, report);
        detail::validate_terminal_marker_net_kind_mismatch(schematic, sheet_id, sheet, report);
        detail::validate_dense_no_connect_clusters(schematic, sheet_id, sheet, report);
        detail::validate_text_collisions(schematic, sheet_id, sheet, report);
    }

    return report;
}

} // namespace volt
