#pragma once

#include <optional>
#include <string>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/net_classes.hpp>
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
    /** Provenance for the effective copper clearance when it came from a derivation. */
    std::optional<NetClassRuleDerivation> copper_clearance_derivation;
    /** Stored derived copper clearance, even when a hand-set value overrides it. */
    std::optional<DerivedNetClassRuleValue> derived_copper_clearance;
    /** Required track width from the resolved class. */
    std::optional<double> track_width_mm;
    /** Provenance for the effective track width when it came from a derivation. */
    std::optional<NetClassRuleDerivation> track_width_derivation;
    /** Stored derived track width, even when a hand-set value overrides it. */
    std::optional<DerivedNetClassRuleValue> derived_track_width;
    /** Required via drill diameter from the resolved class. */
    std::optional<double> via_drill_mm;
    /** Required via finished copper diameter from the resolved class. */
    std::optional<double> via_diameter_mm;
    /** Semantic copper-layer scope from the resolved class. */
    NetClassLayerScope layer_scope = NetClassLayerScope::AnyCopper;
    /** Allowed copper layer names from the resolved class; empty means no name restriction. */
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
