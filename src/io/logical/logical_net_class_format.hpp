#pragma once

#include <iosfwd>

#include <nlohmann/json.hpp>

#include <volt/circuit/constraints/net_classes.hpp>

namespace volt::io::detail {

[[nodiscard]] DerivedNetClassRuleValue
read_derived_net_class_rule_value(const nlohmann::json &object);

void write_derived_net_class_rule_value(std::ostream &out, const DerivedNetClassRuleValue &value);

} // namespace volt::io::detail
