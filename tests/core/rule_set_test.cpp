#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <volt/core/diagnostics.hpp>
#include <volt/core/rule_set.hpp>

namespace {

struct RuleSetModel {
    int value;
};

} // namespace

TEST_CASE("RuleSet runs registered rules in registration order") {
    const auto model = RuleSetModel{10};
    auto observed = std::vector<int>{};
    auto report = volt::DiagnosticReport{};

    auto rules = volt::RuleSet<RuleSetModel>{};
    rules
        .add([&observed](const RuleSetModel &rule_model, volt::DiagnosticReport &) {
            observed.push_back(rule_model.value + 1);
        })
        .add([&observed](const RuleSetModel &rule_model, volt::DiagnosticReport &) {
            observed.push_back(rule_model.value + 2);
        });

    rules.run(model, report);

    CHECK(observed == std::vector{11, 12});
    CHECK(report.empty());
}

TEST_CASE("RuleSet lets rules append diagnostics to one shared report") {
    const auto model = RuleSetModel{7};
    auto report = volt::DiagnosticReport{};

    auto rules = volt::RuleSet<RuleSetModel>{};
    rules
        .add([](const RuleSetModel &, volt::DiagnosticReport &rule_report) {
            rule_report.add(volt::Diagnostic{volt::Severity::Warning,
                                             volt::DiagnosticCode{"FIRST_RULE"}, "first"});
        })
        .add([](const RuleSetModel &rule_model, volt::DiagnosticReport &rule_report) {
            rule_report.add(volt::Diagnostic{
                volt::Severity::Error,
                volt::DiagnosticCode{"SECOND_RULE"},
                std::to_string(rule_model.value),
            });
        });

    rules.run(model, report);

    REQUIRE(report.count() == 2);
    CHECK(report.diagnostics()[0].code() == volt::DiagnosticCode{"FIRST_RULE"});
    CHECK(report.diagnostics()[1].code() == volt::DiagnosticCode{"SECOND_RULE"});
    CHECK(report.diagnostics()[1].message() == "7");
}
