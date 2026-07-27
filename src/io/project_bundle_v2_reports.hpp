#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace volt::io {

enum class ProjectStatus;

namespace detail {

struct ProjectDiagnosticsReportFacts {
    std::string status;
    std::uint64_t errors;
    std::uint64_t warnings;
    std::uint64_t infos;
    std::uint64_t count;
    bool policy_ok;
};

struct ProjectTestsReportFacts {
    std::uint64_t passed;
    std::uint64_t failed;
};

struct ProjectReportsFacts {
    ProjectStatus status;
    bool ok;
};

[[nodiscard]] ProjectDiagnosticsReportFacts read_project_diagnostics_report(std::string_view bytes);

[[nodiscard]] ProjectTestsReportFacts read_project_tests_report(std::string_view bytes);

[[nodiscard]] ProjectReportsFacts read_project_reports(std::string_view diagnostics,
                                                       std::string_view tests);

} // namespace detail
} // namespace volt::io
