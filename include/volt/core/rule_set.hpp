#pragma once

#include <functional>
#include <utility>
#include <vector>

#include <volt/core/diagnostics.hpp>

namespace volt {

/** Runs a typed set of validation rules over one model and one diagnostic report. */
template <class Model> class RuleSet {
  public:
    /** Callable validation rule for the model type. */
    using Rule = std::function<void(const Model &, DiagnosticReport &)>;

    /** Register a rule and return this set for fluent construction. */
    template <class RuleFunction> RuleSet &add(RuleFunction rule) {
        rules_.emplace_back(std::move(rule));
        return *this;
    }

    /** Run all registered rules in insertion order. */
    void run(const Model &model, DiagnosticReport &report) const {
        for (const auto &rule : rules_) {
            rule(model, report);
        }
    }

  private:
    std::vector<Rule> rules_;
};

} // namespace volt
