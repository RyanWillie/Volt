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
[[nodiscard]] DiagnosticReport validate_schematic_readability(const Schematic &schematic);

} // namespace volt
