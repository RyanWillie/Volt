#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/nets.hpp>
#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/quantities.hpp>

namespace volt {

class Circuit;

/** Semantic copper-layer restriction that scales with any board layer count. */
enum class NetClassLayerScope {
    AnyCopper,
    OuterOnly,
    InnerOnly,
    TopOnly,
    BottomOnly,
};

/** Copper environment used by IPC current-capacity trace-width calculators. */
enum class NetClassTraceEnvironment {
    External,
    Internal,
};

/** Dielectric-height spacing rule used by intent-derived clearance calculators. */
enum class NetClassDielectricSpacingRule {
    Stripline1H,
    Microstrip2H,
};

/** One named calculator input captured for derived design-rule provenance. */
struct NetClassDerivationInput {
    /** Stable input name used in serialized provenance. */
    std::string name;
    /** Numeric input value when the input is not textual. */
    double value = 0.0;
    /** Textual input value; empty means the numeric value is active. */
    std::string text_value;
    /** Unit label for the input value, or enum for symbolic inputs. */
    std::string unit;
};

/** Calculator identity and inputs for a derived net-class value. */
struct NetClassRuleDerivation {
    /** Stable calculator identifier used by diagnostics and serialization. */
    std::string calculator_id;
    /** Human-readable calculator name. */
    std::string calculator_name;
    /** Standard or fixture family the calculator is documented against. */
    std::string standard;
    /** Formula or fixture reference used by the calculator. */
    std::string reference;
    /** Ordered calculator inputs captured for explainable diagnostics. */
    std::vector<NetClassDerivationInput> inputs;
};

/** Millimeter-valued rule result plus the provenance that produced it. */
struct DerivedNetClassRuleValue {
    /** Derived rule value in millimeters. */
    double value_mm = 0.0;
    /** Calculator provenance that produced the derived value. */
    NetClassRuleDerivation derivation;
};

/** Derive trace width from current, temperature rise, and copper weight. */
[[nodiscard]] DerivedNetClassRuleValue
ipc2221_trace_width_from_current_mm(double current_a, double temperature_rise_c,
                                    double copper_weight_oz, NetClassTraceEnvironment environment);

/** Derive clearance from dielectric height using 1H stripline or 2H microstrip rules. */
[[nodiscard]] DerivedNetClassRuleValue
dielectric_height_spacing_mm(double dielectric_height_mm, NetClassDielectricSpacingRule rule);

/** Derive external-conductor voltage clearance from the IPC-2221 tabulated fixture. */
[[nodiscard]] DerivedNetClassRuleValue ipc2221_external_voltage_clearance_mm(double voltage_v);

/** Stable name for a kernel-owned net class. */
class NetClassName {
  public:
    /** Construct a non-empty net-class name. */
    explicit NetClassName(std::string value);

    /** Return the canonical net-class name text. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Compare net-class names by canonical text. */
    [[nodiscard]] friend bool operator==(const NetClassName &lhs,
                                         const NetClassName &rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

  private:
    std::string value_;
};

/** Reusable rule intent that may be assigned to logical nets. */
class NetClass {
  public:
    /** Construct a net class with a stable name. */
    explicit NetClass(NetClassName name);

    /** Return the stable net-class name. */
    [[nodiscard]] const NetClassName &name() const noexcept { return name_; }

    /** Set the maximum voltage allowed on assigned nets. */
    void set_maximum_net_voltage(Quantity voltage);

    /** Set the copper clearance required for assigned nets. */
    void set_copper_clearance_mm(double clearance_mm);

    /** Store a kernel-derived copper clearance; an explicit clearance still wins. */
    void derive_copper_clearance(DerivedNetClassRuleValue clearance);

    /** Set the track width required for assigned nets. */
    void set_track_width_mm(double width_mm);

    /** Store a kernel-derived track width; an explicit width still wins. */
    void derive_track_width(DerivedNetClassRuleValue width);

    /** Set the via drill and finished copper diameters required for assigned nets. */
    void set_via_size_mm(double drill_mm, double diameter_mm);

    /** Restrict assigned nets to a semantic copper-layer scope; exclusive with names. */
    void set_layer_scope(NetClassLayerScope scope);

    /** Restrict assigned nets to exact board-local layer names; exclusive with a scope. */
    void set_allowed_layer_names(std::vector<std::string> names);

    /** Set the resolution priority used when intent-derived defaults compete. */
    void set_priority(int priority) noexcept { priority_ = priority; }

    /** Mark this class as the intent-derived default for nets of one kind. */
    void set_default_for_net_kind(NetKind kind) noexcept { default_for_net_kind_ = kind; }

    /** Return the maximum net voltage constraint, if present. */
    [[nodiscard]] const std::optional<Quantity> &maximum_net_voltage() const noexcept {
        return maximum_net_voltage_;
    }

    /** Return the copper clearance constraint, if present. */
    [[nodiscard]] std::optional<double> copper_clearance_mm() const noexcept;

    /** Return whether a hand-set copper clearance overrides any derived clearance. */
    [[nodiscard]] bool has_explicit_copper_clearance_mm() const noexcept {
        return copper_clearance_mm_.has_value();
    }

    /** Return the derived copper clearance and provenance, if present. */
    [[nodiscard]] const std::optional<DerivedNetClassRuleValue> &
    derived_copper_clearance() const noexcept {
        return derived_copper_clearance_;
    }

    /** Return the track width constraint, if present. */
    [[nodiscard]] std::optional<double> track_width_mm() const noexcept;

    /** Return whether a hand-set track width overrides any derived width. */
    [[nodiscard]] bool has_explicit_track_width_mm() const noexcept {
        return track_width_mm_.has_value();
    }

    /** Return the derived track width and provenance, if present. */
    [[nodiscard]] const std::optional<DerivedNetClassRuleValue> &
    derived_track_width() const noexcept {
        return derived_track_width_;
    }

    /** Return the via drill diameter constraint, if present. */
    [[nodiscard]] std::optional<double> via_drill_mm() const noexcept { return via_drill_mm_; }

    /** Return the via finished copper diameter constraint, if present. */
    [[nodiscard]] std::optional<double> via_diameter_mm() const noexcept {
        return via_diameter_mm_;
    }

    /** Return the semantic copper-layer scope; AnyCopper means unrestricted. */
    [[nodiscard]] NetClassLayerScope layer_scope() const noexcept { return layer_scope_; }

    /** Return allowed copper layer names; empty means no exact-name restriction. */
    [[nodiscard]] const std::vector<std::string> &allowed_layer_names() const noexcept {
        return allowed_layer_names_;
    }

    /** Return the resolution priority for intent-derived defaults. */
    [[nodiscard]] int priority() const noexcept { return priority_; }

    /** Return the net kind this class is the intent-derived default for, if any. */
    [[nodiscard]] std::optional<NetKind> default_for_net_kind() const noexcept {
        return default_for_net_kind_;
    }

  private:
    NetClassName name_;
    std::optional<Quantity> maximum_net_voltage_;
    std::optional<double> copper_clearance_mm_;
    std::optional<DerivedNetClassRuleValue> derived_copper_clearance_;
    std::optional<double> track_width_mm_;
    std::optional<DerivedNetClassRuleValue> derived_track_width_;
    std::optional<double> via_drill_mm_;
    std::optional<double> via_diameter_mm_;
    NetClassLayerScope layer_scope_ = NetClassLayerScope::AnyCopper;
    std::vector<std::string> allowed_layer_names_;
    int priority_ = 0;
    std::optional<NetKind> default_for_net_kind_;
};

/**
 * Owns net classes and deterministic net-to-net-class assignments.
 *
 * Responsibility: stores kernel-owned net-class (netclass) intent — named electrical/physical
 *   constraint parameters — and which logical nets they apply to.
 * Invariants: net-class names are stable and unique; assignments reference existing nets.
 * Collaborators: composed by Circuit; the constraint parameters are read by both ERC (voltage)
 *   and DRC (clearance) checkers; persisted through logical-circuit IO; acyclic.
 */
class NetClasses {
  public:
    /** Add a reusable net class and return its stable ID. */
    [[nodiscard]] NetClassId add_net_class(NetClass net_class);

    /** Return a net class by stable ID. */
    [[nodiscard]] const NetClass &net_class(NetClassId id) const;

    /** Return the net class with the requested name, if present. */
    [[nodiscard]] std::optional<NetClassId> net_class_by_name(const NetClassName &name) const;

    /** Return the net class assigned to a logical net, if present. */
    [[nodiscard]] std::optional<NetClassId> net_class_for_net(NetId net) const noexcept;

    /** Return deterministic net-to-net-class assignments. */
    [[nodiscard]] const std::vector<std::pair<NetId, NetClassId>> &
    net_class_assignments() const noexcept;

    /** Return the number of net classes. */
    [[nodiscard]] std::size_t net_class_count() const noexcept;

    /** Require that a net class ID belongs to this model. */
    void require_net_class(NetClassId net_class) const;

  private:
    friend class Circuit;

    /** Assign a net class to a logical net. */
    [[nodiscard]] bool assign_net_class(NetId net, NetClassId net_class);

    EntityTable<NetClass, NetClassId> net_classes_;
    std::vector<std::pair<NetId, NetClassId>> net_class_assignments_;
};

} // namespace volt
