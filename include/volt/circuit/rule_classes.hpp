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
    explicit RuleClassName(std::string value);

    [[nodiscard]] const std::string &value() const noexcept { return value_; }

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
    explicit RuleClass(RuleClassName name);

    [[nodiscard]] const RuleClassName &name() const noexcept { return name_; }

    void set_maximum_net_voltage(Quantity voltage);

    void set_copper_clearance_mm(double clearance_mm);

    [[nodiscard]] const std::optional<Quantity> &maximum_net_voltage() const noexcept {
        return maximum_net_voltage_;
    }

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
    [[nodiscard]] RuleClassId add_rule_class(RuleClass rule_class);

    [[nodiscard]] bool assign_net_rule_class(NetId net, RuleClassId rule_class);

    [[nodiscard]] const RuleClass &rule_class(RuleClassId id) const;

    [[nodiscard]] std::optional<RuleClassId> rule_class_by_name(const RuleClassName &name) const;

    [[nodiscard]] std::optional<RuleClassId> rule_class_for_net(NetId net) const noexcept;

    [[nodiscard]] const std::vector<std::pair<NetId, RuleClassId>> &
    net_rule_class_assignments() const noexcept;

    [[nodiscard]] std::size_t rule_class_count() const noexcept;

    void require_rule_class(RuleClassId rule_class) const;

  private:
    EntityTable<RuleClass, RuleClassId> rule_classes_;
    std::vector<std::pair<NetId, RuleClassId>> net_rule_class_assignments_;
};

} // namespace volt
