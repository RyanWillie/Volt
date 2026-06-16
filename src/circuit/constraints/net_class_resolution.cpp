#include <volt/circuit/constraints/net_class_resolution.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>

namespace volt {

namespace {

[[nodiscard]] std::optional<NetClassId> kind_default_net_class(const Circuit &circuit,
                                                               NetKind kind) {
    std::optional<NetClassId> best;
    for (std::size_t index = 0; index < circuit.net_class_count(); ++index) {
        const auto id = NetClassId{index};
        const auto &net_class = circuit.net_class(id);
        if (net_class.default_for_net_kind() != kind) {
            continue;
        }
        if (!best.has_value() ||
            net_class.priority() > circuit.net_class(best.value()).priority()) {
            best = id;
        }
    }
    return best;
}

} // namespace

[[nodiscard]] std::optional<NetClassId> resolve_net_class(const Circuit &circuit, NetId net) {
    const auto assigned = circuit.net_class_for_net(net);
    if (assigned.has_value()) {
        return assigned;
    }

    return kind_default_net_class(circuit, circuit.net(net).kind());
}

[[nodiscard]] ResolvedNetClassRules resolve_net_class_rules(const Circuit &circuit, NetId net) {
    auto rules = ResolvedNetClassRules{};
    rules.net_class = resolve_net_class(circuit, net);
    if (!rules.net_class.has_value()) {
        return rules;
    }

    const auto &net_class = circuit.net_class(rules.net_class.value());
    rules.maximum_net_voltage = net_class.maximum_net_voltage();
    rules.copper_clearance_mm = net_class.copper_clearance_mm();
    rules.derived_copper_clearance = net_class.derived_copper_clearance();
    if (!net_class.has_explicit_copper_clearance_mm() &&
        rules.derived_copper_clearance.has_value()) {
        rules.copper_clearance_derivation = rules.derived_copper_clearance->derivation;
    }
    rules.track_width_mm = net_class.track_width_mm();
    rules.derived_track_width = net_class.derived_track_width();
    if (!net_class.has_explicit_track_width_mm() && rules.derived_track_width.has_value()) {
        rules.track_width_derivation = rules.derived_track_width->derivation;
    }
    rules.via_drill_mm = net_class.via_drill_mm();
    rules.via_diameter_mm = net_class.via_diameter_mm();
    rules.layer_scope = net_class.layer_scope();
    rules.allowed_layer_names = net_class.allowed_layer_names();
    return rules;
}

[[nodiscard]] double resolve_copper_clearance_mm(const Circuit &circuit, NetId lhs, NetId rhs,
                                                 double minimum_clearance_mm) {
    const auto side_clearance = [&circuit](NetId net) {
        return resolve_net_class_rules(circuit, net).copper_clearance_mm.value_or(0.0);
    };

    return std::max(minimum_clearance_mm, std::max(side_clearance(lhs), side_clearance(rhs)));
}

} // namespace volt
