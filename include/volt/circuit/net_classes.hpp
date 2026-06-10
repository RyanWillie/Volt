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

    /** Set the track width required for assigned nets. */
    void set_track_width_mm(double width_mm);

    /** Set the via drill and finished copper diameters required for assigned nets. */
    void set_via_size_mm(double drill_mm, double diameter_mm);

    /** Restrict assigned nets to copper layers with the given board-local names. */
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
    [[nodiscard]] std::optional<double> copper_clearance_mm() const noexcept {
        return copper_clearance_mm_;
    }

    /** Return the track width constraint, if present. */
    [[nodiscard]] std::optional<double> track_width_mm() const noexcept { return track_width_mm_; }

    /** Return the via drill diameter constraint, if present. */
    [[nodiscard]] std::optional<double> via_drill_mm() const noexcept { return via_drill_mm_; }

    /** Return the via finished copper diameter constraint, if present. */
    [[nodiscard]] std::optional<double> via_diameter_mm() const noexcept {
        return via_diameter_mm_;
    }

    /** Return allowed copper layer names; empty means unrestricted. */
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
    std::optional<double> track_width_mm_;
    std::optional<double> via_drill_mm_;
    std::optional<double> via_diameter_mm_;
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
