#pragma once

#include <optional>
#include <string>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/quantities.hpp>

namespace volt {

/**
 * Resolved per-net design rule values for one logical net.
 *
 * Responsibility: carries the outcome of net-class resolution so ERC, DRC, and future
 *   routing consume identical rule values through one path.
 * Invariants: values mirror the resolved class verbatim; absent values mean the net has
 *   no constraint from net classes and callers apply their own defaults.
 * Collaborators: produced by resolve_net_class_rules; consumed by circuit validation and
 *   the board DRC checks.
 */
struct ResolvedNetClassRules {
    /** Net class governing the net, if any resolved. */
    std::optional<NetClassId> net_class;
    /** Maximum allowed net voltage from the resolved class. */
    std::optional<Quantity> maximum_net_voltage;
    /** Required copper clearance from the resolved class. */
    std::optional<double> copper_clearance_mm;
    /** Required track width from the resolved class. */
    std::optional<double> track_width_mm;
    /** Required via drill diameter from the resolved class. */
    std::optional<double> via_drill_mm;
    /** Required via finished copper diameter from the resolved class. */
    std::optional<double> via_diameter_mm;
    /** Allowed copper layer names from the resolved class; empty means unrestricted. */
    std::vector<std::string> allowed_layer_names;
};

/**
 * Return the net class governing a net: an explicit assignment wins; otherwise the
 * highest-priority class declaring itself the intent-derived default for the net's kind,
 * with ties broken deterministically by lowest class ID.
 */
[[nodiscard]] std::optional<NetClassId> resolve_net_class(const Circuit &circuit, NetId net);

/** Return the resolved rule values for a net through the shared resolution path. */
[[nodiscard]] ResolvedNetClassRules resolve_net_class_rules(const Circuit &circuit, NetId net);

/**
 * Return the copper clearance required between two different nets: the larger of the two
 * resolved class clearances, never below the supplied minimum.
 */
[[nodiscard]] double resolve_copper_clearance_mm(const Circuit &circuit, NetId lhs, NetId rhs,
                                                 double minimum_clearance_mm);

} // namespace volt
