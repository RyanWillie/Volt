#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <volt/circuit/parts/parts.hpp>
#include <volt/circuit/parts/selected_part.hpp>

namespace volt {

class Board;
class Circuit;
class Schematic;

} // namespace volt

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
    std::vector<ProjectDiagnosticFacts> expectations;
};

struct ProjectTestsReportFacts {
    std::uint64_t passed;
    std::uint64_t failed;
};

struct ProjectReportsFacts {
    ProjectStatus status;
    bool ok;
    std::vector<ProjectDiagnosticFacts> diagnostics;
    std::vector<ProjectDiagnosticFacts> expectations;
};

struct ProjectReportLogicalContext {
    std::string design;
    const Circuit *model;
};

struct ProjectReportSchematicContext {
    std::string design;
    std::string schematic;
    const Schematic *model;
};

struct ProjectReportBoardContext {
    std::string design;
    std::string board;
    const Board *model;
};

struct ProjectReportSelectedPartContext {
    LibraryPartRef reference;
    std::size_t footprint_pad_count;
};

struct ProjectReportReferenceContext {
    std::vector<ProjectReportLogicalContext> logicals;
    std::vector<ProjectReportSchematicContext> schematics;
    std::vector<ProjectReportBoardContext> boards;
    std::vector<ProjectReportSelectedPartContext> selected_parts;
};

[[nodiscard]] ProjectDiagnosticsReportFacts read_project_diagnostics_report(std::string_view bytes);

[[nodiscard]] ProjectTestsReportFacts read_project_tests_report(std::string_view bytes);

[[nodiscard]] ProjectReportsFacts read_project_reports(std::string_view diagnostics,
                                                       std::string_view tests);

[[nodiscard]] std::string project_library_report_subject(std::string_view library_namespace,
                                                         std::string_view library_version,
                                                         const ContentHash &library_digest);

[[nodiscard]] std::string project_library_report_subject(const LibraryPartRef &reference);

[[nodiscard]] std::optional<std::string>
project_report_reference_error(const ProjectReportsFacts &reports,
                               const ProjectReportReferenceContext &context);

} // namespace detail
} // namespace volt::io
