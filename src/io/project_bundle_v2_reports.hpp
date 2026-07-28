#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace volt::io {

enum class ProjectStatus;

namespace detail {

struct ProjectDiagnosticReferenceFacts {
    std::string kind;
    std::uint64_t index;
};

struct ProjectDiagnosticFacts {
    std::string source;
    std::string report;
    std::optional<std::string> design;
    std::optional<std::string> board;
    std::vector<std::vector<ProjectDiagnosticReferenceFacts>> reference_groups;
};

struct ProjectDiagnosticsReportFacts {
    std::string status;
    std::uint64_t errors;
    std::uint64_t warnings;
    std::uint64_t infos;
    std::uint64_t count;
    bool policy_ok;
    std::vector<ProjectDiagnosticFacts> diagnostics;
};

struct ProjectTestsReportFacts {
    std::uint64_t passed;
    std::uint64_t failed;
};

struct ProjectReportsFacts {
    ProjectStatus status;
    bool ok;
    std::vector<ProjectDiagnosticFacts> diagnostics;
};

[[nodiscard]] ProjectDiagnosticsReportFacts read_project_diagnostics_report(std::string_view bytes);

[[nodiscard]] ProjectTestsReportFacts read_project_tests_report(std::string_view bytes);

[[nodiscard]] ProjectReportsFacts read_project_reports(std::string_view diagnostics,
                                                       std::string_view tests);

} // namespace detail
} // namespace volt::io
