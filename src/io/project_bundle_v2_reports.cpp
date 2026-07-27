#include "project_bundle_v2_reports.hpp"

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>
#include <volt/io/project_bundle_v2_writer.hpp>

namespace volt::io::detail {
namespace {

using Json = nlohmann::json;

[[noreturn]] void reject_report(std::string message) {
    throw KernelArgumentError{ErrorCode::InvalidArgument, std::move(message)};
}

void require_report(bool condition, std::string message) {
    if (!condition) {
        reject_report(std::move(message));
    }
}

[[nodiscard]] Json parse_report(std::string_view bytes, std::string_view name) {
    try {
        return Json::parse(bytes.begin(), bytes.end());
    } catch (const std::exception &error) {
        reject_report(std::string{name} + " report is not valid JSON: " + error.what());
    }
}

void require_exact_keys(const Json &value, std::initializer_list<std::string_view> keys,
                        std::string_view context) {
    require_report(value.is_object(), std::string{context} + " must be an object");
    auto expected = std::set<std::string>{};
    for (const auto key : keys) {
        expected.emplace(key);
    }
    auto actual = std::set<std::string>{};
    for (const auto &[key, unused] : value.items()) {
        static_cast<void>(unused);
        actual.insert(key);
    }
    require_report(actual == expected, std::string{context} + " has an invalid field set");
}

[[nodiscard]] const Json &field(const Json &object, std::string_view name,
                                std::string_view context) {
    require_report(object.is_object(), std::string{context} + " must be an object");
    const auto match = object.find(name);
    require_report(match != object.end(),
                   std::string{context} + " is missing field " + std::string{name});
    return *match;
}

[[nodiscard]] const std::string &string_field(const Json &object, std::string_view name,
                                              std::string_view context, bool allow_empty = false) {
    const auto &value = field(object, name, context);
    require_report(value.is_string(),
                   std::string{context} + " field " + std::string{name} + " must be a string");
    const auto &result = value.get_ref<const std::string &>();
    require_report(allow_empty || !result.empty(),
                   std::string{context} + " field " + std::string{name} + " must not be empty");
    return result;
}

void require_nullable_string(const Json &object, std::string_view name, std::string_view context) {
    const auto &value = field(object, name, context);
    require_report(value.is_null() || value.is_string(), std::string{context} + " field " +
                                                             std::string{name} +
                                                             " must be null or a string");
}

[[nodiscard]] std::uint64_t unsigned_field(const Json &object, std::string_view name,
                                           std::string_view context) {
    const auto &value = field(object, name, context);
    require_report(value.is_number_unsigned(), std::string{context} + " field " +
                                                   std::string{name} +
                                                   " must be an unsigned integer");
    return value.get<std::uint64_t>();
}

void require_entity(const Json &entity, std::string_view context) {
    require_exact_keys(entity, {"kind", "index"}, context);
    static_cast<void>(string_field(entity, "kind", context));
    static_cast<void>(unsigned_field(entity, "index", context));
}

void require_entity_list(const Json &value, std::string_view context) {
    require_report(value.is_array(), std::string{context} + " must be an array");
    for (const auto &entity : value) {
        require_entity(entity, context);
    }
}

void require_overlay(const Json &overlay) {
    constexpr auto context = std::string_view{"diagnostic overlay"};
    require_exact_keys(overlay, {"kind", "points", "entities", "layers"}, context);
    static_cast<void>(string_field(overlay, "kind", context));
    const auto &points = field(overlay, "points", context);
    require_report(points.is_array(), "diagnostic overlay points must be an array");
    for (const auto &point : points) {
        require_report(point.is_array() && point.size() == 2U,
                       "diagnostic overlay point must be a coordinate pair");
        for (const auto &coordinate : point) {
            require_report(coordinate.is_number() && std::isfinite(coordinate.get<double>()),
                           "diagnostic overlay coordinates must be finite numbers");
        }
    }
    require_entity_list(field(overlay, "entities", context), "diagnostic overlay entities");
    require_entity_list(field(overlay, "layers", context), "diagnostic overlay layers");
}

void require_measurement(const Json &measurement) {
    if (measurement.is_null()) {
        return;
    }
    require_exact_keys(measurement, {"actual_mm", "required_mm"}, "diagnostic measurement");
    for (const auto name : {"actual_mm", "required_mm"}) {
        const auto &value = field(measurement, name, "diagnostic measurement");
        require_report(value.is_number() && std::isfinite(value.get<double>()),
                       "diagnostic measurement values must be finite numbers");
    }
}

[[nodiscard]] Json expected_kwargs(const Json &value) {
    auto result = Json::object();
    for (const auto name :
         {"code", "severity", "stage", "source", "report", "design", "board", "rule"}) {
        const auto &candidate = field(value, name, "diagnostic policy value");
        if (!candidate.is_null()) {
            result[name] = candidate;
        }
    }
    return result;
}

void require_diagnostic(const Json &diagnostic) {
    constexpr auto context = std::string_view{"project diagnostic"};
    require_exact_keys(diagnostic,
                       {"stage", "source", "report", "severity", "category", "code", "message",
                        "entities", "overlays", "measurement", "design", "board", "rule",
                        "expect_diagnostic_kwargs"},
                       context);
    static_cast<void>(string_field(diagnostic, "stage", context));
    static_cast<void>(string_field(diagnostic, "source", context));
    static_cast<void>(string_field(diagnostic, "report", context));
    const auto &severity = string_field(diagnostic, "severity", context);
    require_report(severity == "error" || severity == "warning" || severity == "info",
                   "project diagnostic severity is unsupported");
    static_cast<void>(string_field(diagnostic, "category", context));
    static_cast<void>(string_field(diagnostic, "code", context));
    static_cast<void>(string_field(diagnostic, "message", context, true));
    require_entity_list(field(diagnostic, "entities", context), "project diagnostic entities");
    const auto &overlays = field(diagnostic, "overlays", context);
    require_report(overlays.is_array(), "project diagnostic overlays must be an array");
    for (const auto &overlay : overlays) {
        require_overlay(overlay);
    }
    require_measurement(field(diagnostic, "measurement", context));
    require_nullable_string(diagnostic, "design", context);
    require_nullable_string(diagnostic, "board", context);
    require_nullable_string(diagnostic, "rule", context);
    require_report(field(diagnostic, "expect_diagnostic_kwargs", context) ==
                       expected_kwargs(diagnostic),
                   "project diagnostic expectation metadata is inconsistent");
}

void require_expected_diagnostic(const Json &expected) {
    constexpr auto context = std::string_view{"expected diagnostic"};
    require_exact_keys(expected,
                       {"code", "severity", "stage", "source", "report", "design", "board", "rule",
                        "matched", "expect_diagnostic_kwargs"},
                       context);
    static_cast<void>(string_field(expected, "code", context));
    for (const auto name : {"severity", "stage", "source", "report", "design", "board", "rule"}) {
        require_nullable_string(expected, name, context);
    }
    require_report(field(expected, "matched", context).is_boolean(),
                   "expected diagnostic matched flag must be boolean");
    require_report(field(expected, "expect_diagnostic_kwargs", context) ==
                       expected_kwargs(expected),
                   "expected diagnostic metadata is inconsistent");
}

[[nodiscard]] bool expected_matches(const Json &expected, const Json &diagnostic) {
    if (expected.at("code") != diagnostic.at("code")) {
        return false;
    }
    for (const auto name : {"severity", "stage", "source", "report", "design", "board", "rule"}) {
        if (!expected.at(name).is_null() && expected.at(name) != diagnostic.at(name)) {
            return false;
        }
    }
    return true;
}

} // namespace

ProjectDiagnosticsReportFacts read_project_diagnostics_report(std::string_view bytes) {
    const auto report = parse_report(bytes, "diagnostics");
    require_exact_keys(
        report, {"status", "summary", "diagnostics", "expected", "unexpected", "missing_expected"},
        "diagnostics report");
    const auto status = string_field(report, "status", "diagnostics report");
    require_report(status == "clean" || status == "expected-diagnostics" || status == "failed",
                   "diagnostics report status is unsupported");

    const auto &summary = field(report, "summary", "diagnostics report");
    require_exact_keys(summary, {"errors", "warnings", "infos"}, "diagnostics summary");
    const auto recorded_errors = unsigned_field(summary, "errors", "diagnostics summary");
    const auto recorded_warnings = unsigned_field(summary, "warnings", "diagnostics summary");
    const auto recorded_infos = unsigned_field(summary, "infos", "diagnostics summary");

    const auto &diagnostics = field(report, "diagnostics", "diagnostics report");
    require_report(diagnostics.is_array(), "diagnostics report diagnostics must be an array");
    auto errors = std::uint64_t{0};
    auto warnings = std::uint64_t{0};
    auto infos = std::uint64_t{0};
    for (const auto &diagnostic : diagnostics) {
        require_diagnostic(diagnostic);
        const auto &severity = diagnostic.at("severity").get_ref<const std::string &>();
        if (severity == "error") {
            ++errors;
        } else if (severity == "warning") {
            ++warnings;
        } else {
            ++infos;
        }
    }
    require_report(errors == recorded_errors && warnings == recorded_warnings &&
                       infos == recorded_infos,
                   "diagnostics summary does not equal the decoded diagnostics");

    const auto &expected = field(report, "expected", "diagnostics report");
    require_report(expected.is_array(), "diagnostics expected list must be an array");
    for (const auto &entry : expected) {
        require_expected_diagnostic(entry);
        const auto matched = std::ranges::any_of(diagnostics, [&](const auto &diagnostic) {
            return expected_matches(entry, diagnostic);
        });
        require_report(entry.at("matched").get<bool>() == matched,
                       "expected diagnostic match result is inconsistent");
    }

    auto expected_unexpected = Json::array();
    for (const auto &diagnostic : diagnostics) {
        const auto matched = std::ranges::any_of(
            expected, [&](const auto &entry) { return expected_matches(entry, diagnostic); });
        if (!matched) {
            expected_unexpected.push_back(diagnostic);
        }
    }
    const auto &unexpected = field(report, "unexpected", "diagnostics report");
    require_report(unexpected.is_array(), "diagnostics unexpected list must be an array");
    for (const auto &diagnostic : unexpected) {
        require_diagnostic(diagnostic);
    }
    require_report(unexpected == expected_unexpected,
                   "diagnostics unexpected list is inconsistent");

    auto expected_missing = Json::array();
    for (const auto &entry : expected) {
        if (!entry.at("matched").get<bool>()) {
            expected_missing.push_back(entry);
        }
    }
    const auto &missing = field(report, "missing_expected", "diagnostics report");
    require_report(missing.is_array(), "diagnostics missing-expected list must be an array");
    for (const auto &entry : missing) {
        require_expected_diagnostic(entry);
    }
    require_report(missing == expected_missing,
                   "diagnostics missing-expected list is inconsistent");

    const auto count = errors + warnings + infos;
    const auto policy_ok = expected.empty() ? errors == 0U : unexpected.empty() && missing.empty();
    return ProjectDiagnosticsReportFacts{status, errors, warnings, infos, count, policy_ok};
}

ProjectTestsReportFacts read_project_tests_report(std::string_view bytes) {
    const auto report = parse_report(bytes, "project tests");
    require_exact_keys(report, {"summary", "tests"}, "project tests report");
    const auto &summary = field(report, "summary", "project tests report");
    require_exact_keys(summary, {"passed", "failed"}, "project tests summary");
    const auto recorded_passed = unsigned_field(summary, "passed", "project tests summary");
    const auto recorded_failed = unsigned_field(summary, "failed", "project tests summary");

    const auto &tests = field(report, "tests", "project tests report");
    require_report(tests.is_array(), "project tests list must be an array");
    auto passed = std::uint64_t{0};
    auto failed = std::uint64_t{0};
    for (const auto &test : tests) {
        require_exact_keys(test, {"stage", "name", "ok", "message"}, "project test");
        static_cast<void>(string_field(test, "stage", "project test"));
        static_cast<void>(string_field(test, "name", "project test"));
        require_report(field(test, "ok", "project test").is_boolean(),
                       "project test ok field must be boolean");
        static_cast<void>(string_field(test, "message", "project test", true));
        if (test.at("ok").get<bool>()) {
            ++passed;
        } else {
            ++failed;
        }
    }
    require_report(passed == recorded_passed && failed == recorded_failed,
                   "project tests summary does not equal the decoded tests");
    return ProjectTestsReportFacts{passed, failed};
}

ProjectReportsFacts read_project_reports(std::string_view diagnostics, std::string_view tests) {
    const auto diagnostic_facts = read_project_diagnostics_report(diagnostics);
    const auto test_facts = read_project_tests_report(tests);
    const auto status =
        diagnostic_facts.count == 0U && test_facts.failed == 0U ? ProjectStatus::Clean
        : diagnostic_facts.policy_ok && test_facts.failed == 0U ? ProjectStatus::ExpectedDiagnostics
                                                                : ProjectStatus::Failed;
    const auto status_text = status == ProjectStatus::Clean                 ? "clean"
                             : status == ProjectStatus::ExpectedDiagnostics ? "expected-diagnostics"
                                                                            : "failed";
    require_report(diagnostic_facts.status == status_text,
                   "diagnostics status does not equal the decoded report outcome");
    return ProjectReportsFacts{status, status != ProjectStatus::Failed};
}

} // namespace volt::io::detail
