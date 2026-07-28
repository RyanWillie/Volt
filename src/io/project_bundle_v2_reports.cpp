#include "project_bundle_v2_reports.hpp"

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/errors.hpp>
#include <volt/io/project_bundle_v2_writer.hpp>
#include <volt/pcb/queries/board_queries.hpp>

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
    auto object_keys = std::vector<std::set<std::string>>{};
    const auto callback = [&](int, Json::parse_event_t event, Json &parsed) {
        if (event == Json::parse_event_t::object_start) {
            object_keys.emplace_back();
        } else if (event == Json::parse_event_t::key) {
            if (object_keys.empty() ||
                !object_keys.back().insert(parsed.get<std::string>()).second) {
                reject_report(std::string{name} + " report contains a duplicate JSON object key");
            }
        } else if (event == Json::parse_event_t::object_end) {
            object_keys.pop_back();
        }
        return true;
    };
    try {
        return Json::parse(bytes.begin(), bytes.end(), callback);
    } catch (const KernelArgumentError &) {
        throw;
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

[[nodiscard]] ProjectDiagnosticReferenceFacts require_entity(const Json &entity,
                                                             std::string_view context) {
    require_exact_keys(entity, {"kind", "index"}, context);
    return ProjectDiagnosticReferenceFacts{string_field(entity, "kind", context),
                                           unsigned_field(entity, "index", context)};
}

[[nodiscard]] std::vector<ProjectDiagnosticReferenceFacts>
require_entity_list(const Json &value, std::string_view context) {
    require_report(value.is_array(), std::string{context} + " must be an array");
    auto result = std::vector<ProjectDiagnosticReferenceFacts>{};
    result.reserve(value.size());
    for (const auto &entity : value) {
        result.push_back(require_entity(entity, context));
    }
    return result;
}

[[nodiscard]] std::vector<ProjectDiagnosticReferenceFacts> require_overlay(const Json &overlay) {
    constexpr auto context = std::string_view{"diagnostic overlay"};
    require_exact_keys(overlay, {"kind", "points", "entities", "layers"}, context);
    const auto &kind = string_field(overlay, "kind", context);
    const auto &points = field(overlay, "points", context);
    require_report(points.is_array(), "diagnostic overlay points must be an array");
    const auto valid_point_count = (kind == "bounding_box" || kind == "segment")
                                       ? points.size() == 2U
                                   : kind == "point"   ? points.size() == 1U
                                   : kind == "polygon" ? points.size() >= 3U
                                                       : false;
    require_report(valid_point_count, "diagnostic overlay kind or point count is invalid");
    for (const auto &point : points) {
        require_report(point.is_array() && point.size() == 2U,
                       "diagnostic overlay point must be a coordinate pair");
        for (const auto &coordinate : point) {
            require_report(coordinate.is_number() && std::isfinite(coordinate.get<double>()),
                           "diagnostic overlay coordinates must be finite numbers");
        }
    }
    auto result =
        require_entity_list(field(overlay, "entities", context), "diagnostic overlay entities");
    auto layers =
        require_entity_list(field(overlay, "layers", context), "diagnostic overlay layers");
    require_report(
        std::ranges::all_of(layers, [](const auto &layer) { return layer.kind == "board_layer"; }),
        "diagnostic overlay layers must be board_layer references");
    result.insert(result.end(), std::make_move_iterator(layers.begin()),
                  std::make_move_iterator(layers.end()));
    return result;
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

[[nodiscard]] ProjectDiagnosticFacts require_diagnostic(const Json &diagnostic) {
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
    auto reference_groups = std::vector<std::vector<ProjectDiagnosticReferenceFacts>>{};
    reference_groups.push_back(
        require_entity_list(field(diagnostic, "entities", context), "project diagnostic entities"));
    const auto &overlays = field(diagnostic, "overlays", context);
    require_report(overlays.is_array(), "project diagnostic overlays must be an array");
    for (const auto &overlay : overlays) {
        reference_groups.push_back(require_overlay(overlay));
    }
    require_measurement(field(diagnostic, "measurement", context));
    require_nullable_string(diagnostic, "design", context);
    require_nullable_string(diagnostic, "board", context);
    require_nullable_string(diagnostic, "rule", context);
    require_report(field(diagnostic, "expect_diagnostic_kwargs", context) ==
                       expected_kwargs(diagnostic),
                   "project diagnostic expectation metadata is inconsistent");
    const auto nullable_value = [&](std::string_view name) -> std::optional<std::string> {
        const auto &value = field(diagnostic, name, context);
        return value.is_null() ? std::nullopt : std::optional{value.get_ref<const std::string &>()};
    };
    return ProjectDiagnosticFacts{
        string_field(diagnostic, "source", context), string_field(diagnostic, "report", context),
        nullable_value("design"), nullable_value("board"), std::move(reference_groups)};
}

[[nodiscard]] ProjectDiagnosticFacts require_expected_diagnostic(const Json &expected) {
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
    const auto nullable_value = [&](std::string_view name) -> std::optional<std::string> {
        const auto &value = field(expected, name, context);
        return value.is_null() ? std::nullopt : std::optional{value.get_ref<const std::string &>()};
    };
    return ProjectDiagnosticFacts{nullable_value("source").value_or(""),
                                  nullable_value("report").value_or(""),
                                  nullable_value("design"),
                                  nullable_value("board"),
                                  {}};
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
    auto diagnostic_facts = std::vector<ProjectDiagnosticFacts>{};
    diagnostic_facts.reserve(diagnostics.size());
    for (const auto &diagnostic : diagnostics) {
        diagnostic_facts.push_back(require_diagnostic(diagnostic));
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
    auto expectation_facts = std::vector<ProjectDiagnosticFacts>{};
    expectation_facts.reserve(expected.size());
    for (const auto &entry : expected) {
        expectation_facts.push_back(require_expected_diagnostic(entry));
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
        static_cast<void>(require_diagnostic(diagnostic));
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
        static_cast<void>(require_expected_diagnostic(entry));
    }
    require_report(missing == expected_missing,
                   "diagnostics missing-expected list is inconsistent");

    const auto count = errors + warnings + infos;
    const auto policy_ok = expected.empty() ? errors == 0U : unexpected.empty() && missing.empty();
    return ProjectDiagnosticsReportFacts{status,
                                         errors,
                                         warnings,
                                         infos,
                                         count,
                                         policy_ok,
                                         std::move(diagnostic_facts),
                                         std::move(expectation_facts)};
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
    return ProjectReportsFacts{status, diagnostic_facts.policy_ok && test_facts.failed == 0U,
                               std::move(diagnostic_facts.diagnostics),
                               std::move(diagnostic_facts.expectations)};
}

std::string project_library_report_subject(std::string_view library_namespace,
                                           std::string_view library_version,
                                           const ContentHash &library_digest) {
    return "library:" + Json{{"namespace", library_namespace},
                             {"version", library_version},
                             {"library_bundle_digest", library_digest.value()}}
                            .dump();
}

std::string project_library_report_subject(const LibraryPartRef &reference) {
    return project_library_report_subject(reference.library_namespace(),
                                          reference.library_version(), reference.library_digest());
}

namespace {

template <typename Id, typename Model>
[[nodiscard]] bool report_index_exists(const Model &model, std::uint64_t index) {
    return index < model.template all<Id>().size();
}

struct DiagnosticModelContext {
    const Circuit *circuit = nullptr;
    const Schematic *schematic = nullptr;
    const Board *board = nullptr;
};

[[nodiscard]] std::optional<std::string>
diagnostic_model_context(const ProjectReportReferenceContext &models,
                         const ProjectDiagnosticFacts &diagnostic, DiagnosticModelContext &result) {
    if (diagnostic.design.has_value()) {
        const auto circuit =
            std::ranges::find(models.logicals, *diagnostic.design,
                              [](const auto &candidate) { return candidate.design; });
        if (circuit == models.logicals.end()) {
            return "project diagnostic names a missing logical design";
        }
        result.circuit = circuit->model;
    }
    if (diagnostic.board.has_value()) {
        if (!diagnostic.design.has_value()) {
            return "project diagnostic names a Board without a logical design";
        }
        const auto board = std::ranges::find_if(models.boards, [&](const auto &candidate) {
            return candidate.design == *diagnostic.design && candidate.board == *diagnostic.board;
        });
        if (board == models.boards.end()) {
            return "project diagnostic names a missing Board";
        }
        result.board = board->model;
    }
    if (diagnostic.source.starts_with("logical:")) {
        if (!diagnostic.design.has_value() ||
            diagnostic.source.substr(std::string_view{"logical:"}.size()) != *diagnostic.design) {
            return "project diagnostic logical source disagrees with its design";
        }
    } else if (diagnostic.source.starts_with("schematic:")) {
        if (!diagnostic.design.has_value()) {
            return "project diagnostic Schematic source has no logical design";
        }
        const auto name = diagnostic.source.substr(std::string_view{"schematic:"}.size());
        const auto schematic = std::ranges::find_if(models.schematics, [&](const auto &candidate) {
            return candidate.design == *diagnostic.design && candidate.schematic == name;
        });
        if (schematic == models.schematics.end()) {
            return "project diagnostic names a missing Schematic";
        }
        result.schematic = schematic->model;
    } else if (diagnostic.source.starts_with("pcb:") &&
               (!diagnostic.board.has_value() ||
                diagnostic.source.substr(std::string_view{"pcb:"}.size()) != *diagnostic.board)) {
        return "project diagnostic PCB source disagrees with its Board";
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
part_definition_reference_error(const ProjectReportReferenceContext &models,
                                const ProjectDiagnosticFacts &diagnostic, std::uint64_t index) {
    if (index != 0U || !diagnostic.source.starts_with("part:")) {
        return "project diagnostic part reference has no exact library context";
    }
    const auto part = diagnostic.source.substr(std::string_view{"part:"}.size());
    if (!std::ranges::any_of(models.selected_parts, [&](const auto &selected) {
            return diagnostic.report == project_library_report_subject(selected.reference) &&
                   selected.reference.part_key().value() == part;
        })) {
        return "project diagnostic references an absent selected PartDefinition";
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
diagnostic_subject_error(const ProjectReportReferenceContext &models,
                         const ProjectDiagnosticFacts &diagnostic) {
    const auto exact_library = diagnostic.report.starts_with("library:");
    const auto exact_part = diagnostic.source.starts_with("part:");
    if (exact_library && !std::ranges::any_of(models.selected_parts, [&](const auto &selected) {
            return project_library_report_subject(selected.reference) == diagnostic.report;
        })) {
        return "project diagnostic references an absent selected library release";
    }
    if (exact_part) {
        const auto part = diagnostic.source.substr(std::string_view{"part:"}.size());
        if (!std::ranges::any_of(models.selected_parts, [&](const auto &selected) {
                return selected.reference.part_key().value() == part &&
                       (!exact_library ||
                        project_library_report_subject(selected.reference) == diagnostic.report);
            })) {
            return "project diagnostic references an absent selected PartDefinition";
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
expectation_model_error(const ProjectReportReferenceContext &models,
                        const ProjectDiagnosticFacts &expectation) {
    if (expectation.design.has_value() &&
        std::ranges::none_of(models.logicals, [&](const auto &candidate) {
            return candidate.design == *expectation.design;
        })) {
        return "project diagnostic names a missing logical design";
    }
    if (expectation.board.has_value() &&
        std::ranges::none_of(models.boards, [&](const auto &candidate) {
            return candidate.board == *expectation.board &&
                   (!expectation.design.has_value() || candidate.design == *expectation.design);
        })) {
        return "project diagnostic names a missing Board";
    }
    if (expectation.source.starts_with("logical:")) {
        const auto design = expectation.source.substr(std::string_view{"logical:"}.size());
        if ((expectation.design.has_value() && design != *expectation.design) ||
            std::ranges::none_of(models.logicals, [&](const auto &candidate) {
                return candidate.design == design;
            })) {
            return "project diagnostic logical source disagrees with its design";
        }
    } else if (expectation.source.starts_with("schematic:")) {
        const auto schematic = expectation.source.substr(std::string_view{"schematic:"}.size());
        if (std::ranges::none_of(models.schematics, [&](const auto &candidate) {
                return candidate.schematic == schematic &&
                       (!expectation.design.has_value() || candidate.design == *expectation.design);
            })) {
            return "project diagnostic names a missing Schematic";
        }
    } else if (expectation.source.starts_with("pcb:")) {
        const auto board = expectation.source.substr(std::string_view{"pcb:"}.size());
        if ((expectation.board.has_value() && board != *expectation.board) ||
            std::ranges::none_of(models.boards, [&](const auto &candidate) {
                return candidate.board == board &&
                       (!expectation.design.has_value() || candidate.design == *expectation.design);
            })) {
            return "project diagnostic PCB source disagrees with its Board";
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
footprint_pad_reference_error(const ProjectReportReferenceContext &models, const Board &board,
                              std::uint64_t placement_index, std::uint64_t pad_index) {
    if (!report_index_exists<ComponentPlacementId>(board, placement_index)) {
        return "project diagnostic footprint pad names a missing component placement";
    }
    const auto &placement = board.get(ComponentPlacementId{placement_index});
    const auto &selected_physical =
        queries::selected_physical_part(board.circuit(), placement.component());
    if (selected_physical.has_value()) {
        const auto footprint =
            queries::footprint_definition_id(board, selected_physical->footprint());
        if (!footprint.has_value() || pad_index >= board.get(*footprint).pad_count()) {
            return "project diagnostic footprint pad is outside its paired placement";
        }
        return std::nullopt;
    }
    const auto &selected_library =
        queries::selected_library_part_ref(board.circuit(), placement.component());
    if (!selected_library.has_value()) {
        return "project diagnostic footprint pad placement has no exact selected footprint";
    }
    const auto selected =
        std::ranges::find(models.selected_parts, *selected_library,
                          [](const auto &candidate) { return candidate.reference; });
    if (selected == models.selected_parts.end()) {
        return "project diagnostic footprint pad placement has no exact selected footprint";
    }
    if (pad_index >= selected->footprint_pad_count) {
        return "project diagnostic footprint pad is outside its paired placement";
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> diagnostic_reference_error(
    const ProjectReportReferenceContext &models, const ProjectDiagnosticFacts &diagnostic,
    const DiagnosticModelContext &context, const ProjectDiagnosticReferenceFacts &reference) {
    const auto &kind = reference.kind;
    const auto index = reference.index;
    auto valid = false;
    if (kind == "component_definition") {
        valid = context.circuit != nullptr &&
                report_index_exists<ComponentDefId>(*context.circuit, index);
    } else if (kind == "component") {
        valid =
            context.circuit != nullptr && report_index_exists<ComponentId>(*context.circuit, index);
    } else if (kind == "pin_definition") {
        valid =
            context.circuit != nullptr && report_index_exists<PinDefId>(*context.circuit, index);
    } else if (kind == "pin") {
        valid = context.circuit != nullptr && report_index_exists<PinId>(*context.circuit, index);
    } else if (kind == "net") {
        valid = context.circuit != nullptr && report_index_exists<NetId>(*context.circuit, index);
    } else if (kind == "net_class") {
        valid =
            context.circuit != nullptr && report_index_exists<NetClassId>(*context.circuit, index);
    } else if (kind == "module_definition") {
        valid =
            context.circuit != nullptr && report_index_exists<ModuleDefId>(*context.circuit, index);
    } else if (kind == "template_net_definition") {
        valid = context.circuit != nullptr &&
                report_index_exists<TemplateNetDefId>(*context.circuit, index);
    } else if (kind == "module_component") {
        valid = context.circuit != nullptr &&
                report_index_exists<ModuleComponentId>(*context.circuit, index);
    } else if (kind == "module_instance") {
        valid = context.circuit != nullptr &&
                report_index_exists<ModuleInstanceId>(*context.circuit, index);
    } else if (kind == "port_definition") {
        valid =
            context.circuit != nullptr && report_index_exists<PortDefId>(*context.circuit, index);
    } else if (kind == "symbol_definition") {
        valid = context.schematic != nullptr &&
                report_index_exists<SymbolDefId>(*context.schematic, index);
    } else if (kind == "sheet") {
        valid =
            context.schematic != nullptr && report_index_exists<SheetId>(*context.schematic, index);
    } else if (kind == "symbol_instance") {
        valid = context.schematic != nullptr &&
                report_index_exists<SymbolInstanceId>(*context.schematic, index);
    } else if (kind == "wire_run") {
        valid = context.schematic != nullptr &&
                report_index_exists<WireRunId>(*context.schematic, index);
    } else if (kind == "net_label") {
        valid = context.schematic != nullptr &&
                report_index_exists<NetLabelId>(*context.schematic, index);
    } else if (kind == "junction") {
        valid = context.schematic != nullptr &&
                report_index_exists<JunctionId>(*context.schematic, index);
    } else if (kind == "power_port") {
        valid = context.schematic != nullptr &&
                report_index_exists<PowerPortId>(*context.schematic, index);
    } else if (kind == "no_connect_marker") {
        valid = context.schematic != nullptr &&
                report_index_exists<NoConnectMarkerId>(*context.schematic, index);
    } else if (kind == "sheet_port") {
        valid = context.schematic != nullptr &&
                report_index_exists<SheetPortId>(*context.schematic, index);
    } else if (kind == "symbol_field") {
        valid = context.schematic != nullptr &&
                report_index_exists<SymbolFieldId>(*context.schematic, index);
    } else if (kind == "board") {
        valid = context.board != nullptr && index == 0U;
    } else if (kind == "board_layer") {
        valid =
            context.board != nullptr && report_index_exists<BoardLayerId>(*context.board, index);
    } else if (kind == "board_feature") {
        valid =
            context.board != nullptr && report_index_exists<BoardFeatureId>(*context.board, index);
    } else if (kind == "board_track") {
        valid =
            context.board != nullptr && report_index_exists<BoardTrackId>(*context.board, index);
    } else if (kind == "board_via") {
        valid = context.board != nullptr && report_index_exists<BoardViaId>(*context.board, index);
    } else if (kind == "board_zone") {
        valid = context.board != nullptr && report_index_exists<BoardZoneId>(*context.board, index);
    } else if (kind == "board_keepout") {
        valid =
            context.board != nullptr && report_index_exists<BoardKeepoutId>(*context.board, index);
    } else if (kind == "board_room") {
        valid = context.board != nullptr && report_index_exists<BoardRoomId>(*context.board, index);
    } else if (kind == "board_text") {
        valid = context.board != nullptr && report_index_exists<BoardTextId>(*context.board, index);
    } else if (kind == "footprint_def") {
        valid =
            context.board != nullptr && report_index_exists<FootprintDefId>(*context.board, index);
    } else if (kind == "component_placement") {
        valid = context.board != nullptr &&
                report_index_exists<ComponentPlacementId>(*context.board, index);
    } else if (kind == "part_definition") {
        return part_definition_reference_error(models, diagnostic, index);
    } else {
        return "project diagnostic uses an unknown entity kind";
    }
    if (!valid) {
        return "project diagnostic references an entity outside its exact model context";
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string>
project_report_reference_error(const ProjectReportsFacts &reports,
                               const ProjectReportReferenceContext &models) {
    const auto diagnostic_error =
        [&](const ProjectDiagnosticFacts &diagnostic) -> std::optional<std::string> {
        if (auto error = diagnostic_subject_error(models, diagnostic); error.has_value()) {
            return error;
        }
        auto context = DiagnosticModelContext{};
        if (auto error = diagnostic_model_context(models, diagnostic, context); error.has_value()) {
            return error;
        }
        for (const auto &group : diagnostic.reference_groups) {
            auto placement = std::optional<std::uint64_t>{};
            for (const auto &reference : group) {
                if (reference.kind == "component_placement") {
                    if (auto error =
                            diagnostic_reference_error(models, diagnostic, context, reference);
                        error.has_value()) {
                        return error;
                    }
                    placement = reference.index;
                } else if (reference.kind == "footprint_pad") {
                    if (context.board == nullptr || !placement.has_value()) {
                        return "project diagnostic footprint pad has no paired component placement";
                    }
                    if (auto error = footprint_pad_reference_error(models, *context.board,
                                                                   *placement, reference.index);
                        error.has_value()) {
                        return error;
                    }
                } else if (auto error =
                               diagnostic_reference_error(models, diagnostic, context, reference);
                           error.has_value()) {
                    return error;
                }
            }
        }
        return std::nullopt;
    };
    for (const auto &diagnostic : reports.diagnostics) {
        if (auto error = diagnostic_error(diagnostic); error.has_value()) {
            return error;
        }
    }
    for (const auto &expectation : reports.expectations) {
        if (auto error = diagnostic_subject_error(models, expectation); error.has_value()) {
            return error;
        }
        if (auto error = expectation_model_error(models, expectation); error.has_value()) {
            return error;
        }
    }
    return std::nullopt;
}

} // namespace volt::io::detail
