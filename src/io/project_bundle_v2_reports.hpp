#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace volt::io::detail {

struct ProjectDiagnosticsReportFacts {
    std::string status;
    std::uint64_t errors;
    std::uint64_t warnings;
    std::uint64_t infos;
};

struct ProjectTestsReportFacts {
    std::uint64_t passed;
    std::uint64_t failed;
};

[[nodiscard]] ProjectDiagnosticsReportFacts read_project_diagnostics_report(std::string_view bytes);

[[nodiscard]] ProjectTestsReportFacts read_project_tests_report(std::string_view bytes);

} // namespace volt::io::detail
