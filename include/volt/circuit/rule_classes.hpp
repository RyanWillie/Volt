#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/quantities.hpp>

namespace volt {

/** Stable name for a kernel-owned rule class. */
class RuleClassName {
  public:
    /** Construct a non-empty rule-class name. */
    explicit RuleClassName(std::string value);

    /** Return the canonical rule-class name text. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Compare rule-class names by canonical text. */
    [[nodiscard]] friend bool operator==(const RuleClassName &lhs,
                                         const RuleClassName &rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

  private:
    std::string value_;
};

/** Reusable rule intent that may be assigned to logical nets. */
class RuleClass {
  public:
    /** Construct a rule class with a stable name. */
    explicit RuleClass(RuleClassName name);

    /** Return the stable rule-class name. */
    [[nodiscard]] const RuleClassName &name() const noexcept { return name_; }

    /** Set the maximum voltage allowed on assigned nets. */
    void set_maximum_net_voltage(Quantity voltage);

    /** Set the copper clearance required for assigned nets. */
    void set_copper_clearance_mm(double clearance_mm);

    /** Return the maximum net voltage constraint, if present. */
    [[nodiscard]] const std::optional<Quantity> &maximum_net_voltage() const noexcept {
        return maximum_net_voltage_;
    }

    /** Return the copper clearance constraint, if present. */
    [[nodiscard]] std::optional<double> copper_clearance_mm() const noexcept {
        return copper_clearance_mm_;
    }

  private:
    RuleClassName name_;
    std::optional<Quantity> maximum_net_voltage_;
    std::optional<double> copper_clearance_mm_;
};

/** Owns rule classes and deterministic net-to-rule-class assignments. */
class RuleClasses {
  public:
    /** Add a reusable rule class and return its stable ID. */
    [[nodiscard]] RuleClassId add_rule_class(RuleClass rule_class);

    /** Assign a rule class to a logical net. */
    [[nodiscard]] bool assign_net_rule_class(NetId net, RuleClassId rule_class);

    /** Return a rule class by stable ID. */
    [[nodiscard]] const RuleClass &rule_class(RuleClassId id) const;

    /** Return the rule class with the requested name, if present. */
    [[nodiscard]] std::optional<RuleClassId> rule_class_by_name(const RuleClassName &name) const;

    /** Return the rule class assigned to a logical net, if present. */
    [[nodiscard]] std::optional<RuleClassId> rule_class_for_net(NetId net) const noexcept;

    /** Return deterministic net-to-rule-class assignments. */
    [[nodiscard]] const std::vector<std::pair<NetId, RuleClassId>> &
    net_rule_class_assignments() const noexcept;

    /** Return the number of rule classes. */
    [[nodiscard]] std::size_t rule_class_count() const noexcept;

    /** Require that a rule class ID belongs to this model. */
    void require_rule_class(RuleClassId rule_class) const;

  private:
    EntityTable<RuleClass, RuleClassId> rule_classes_;
    std::vector<std::pair<NetId, RuleClassId>> net_rule_class_assignments_;
};

} // namespace volt
