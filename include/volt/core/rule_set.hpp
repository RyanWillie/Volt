#pragma once

#include <functional>
#include <utility>
#include <vector>

#include <volt/core/diagnostics.hpp>

namespace volt {

template <class Model> class RuleSet {
  public:
    using Rule = std::function<void(const Model &, DiagnosticReport &)>;

    template <class RuleFunction> RuleSet &add(RuleFunction rule) {
        rules_.emplace_back(std::move(rule));
        return *this;
    }

    void run(const Model &model, DiagnosticReport &report) const {
        for (const auto &rule : rules_) {
            rule(model, report);
        }
    }

  private:
    std::vector<Rule> rules_;
};

} // namespace volt
