#include <volt/circuit/rule_classes.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace volt {

RuleClassName::RuleClassName(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw std::invalid_argument{"Rule class name must not be empty"};
    }
}

RuleClass::RuleClass(RuleClassName name) : name_{std::move(name)} {}

void RuleClass::set_maximum_net_voltage(Quantity voltage) {
    if (voltage.dimension() != UnitDimension::Voltage) {
        throw std::invalid_argument{"Rule class maximum net voltage must use voltage units"};
    }
    if (voltage.value() < 0.0) {
        throw std::invalid_argument{"Rule class maximum net voltage must not be negative"};
    }

    maximum_net_voltage_ = voltage;
}

void RuleClass::set_copper_clearance_mm(double clearance_mm) {
    if (!std::isfinite(clearance_mm)) {
        throw std::invalid_argument{"Rule class copper clearance must be finite"};
    }
    if (clearance_mm < 0.0) {
        throw std::invalid_argument{"Rule class copper clearance must not be negative"};
    }

    copper_clearance_mm_ = clearance_mm;
}

[[nodiscard]] RuleClassId RuleClasses::add_rule_class(RuleClass rule_class) {
    if (rule_class_by_name(rule_class.name()).has_value()) {
        throw std::logic_error{"Rule class name already exists"};
    }

    return rule_classes_.insert(std::move(rule_class));
}

[[nodiscard]] bool RuleClasses::assign_net_rule_class(NetId net, RuleClassId rule_class) {
    require_rule_class(rule_class);
    const auto existing =
        std::find_if(net_rule_class_assignments_.begin(), net_rule_class_assignments_.end(),
                     [net](const auto &assignment) { return assignment.first == net; });
    if (existing == net_rule_class_assignments_.end()) {
        net_rule_class_assignments_.emplace_back(net, rule_class);
        return true;
    }
    if (existing->second == rule_class) {
        return false;
    }

    existing->second = rule_class;
    return true;
}

[[nodiscard]] const RuleClass &RuleClasses::rule_class(RuleClassId id) const {
    return rule_classes_.get(id);
}

[[nodiscard]] std::optional<RuleClassId>
RuleClasses::rule_class_by_name(const RuleClassName &name) const {
    for (std::size_t index = 0; index < rule_classes_.size(); ++index) {
        const auto id = RuleClassId{index};
        if (rule_classes_.get(id).name() == name) {
            return id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<RuleClassId> RuleClasses::rule_class_for_net(NetId net) const noexcept {
    const auto match =
        std::find_if(net_rule_class_assignments_.begin(), net_rule_class_assignments_.end(),
                     [net](const auto &assignment) { return assignment.first == net; });
    if (match == net_rule_class_assignments_.end()) {
        return std::nullopt;
    }

    return match->second;
}

[[nodiscard]] const std::vector<std::pair<NetId, RuleClassId>> &
RuleClasses::net_rule_class_assignments() const noexcept {
    return net_rule_class_assignments_;
}

[[nodiscard]] std::size_t RuleClasses::rule_class_count() const noexcept {
    return rule_classes_.size();
}

void RuleClasses::require_rule_class(RuleClassId rule_class) const {
    if (!rule_classes_.contains(rule_class)) {
        throw std::out_of_range{"Rule class ID is out of range"};
    }
}

} // namespace volt
