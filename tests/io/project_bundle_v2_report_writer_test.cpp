#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <volt/io/project_bundle_v2_writer.hpp>

#include "support/project_bundle_v2_board_test_support.hpp"

namespace {

using Json = nlohmann::json;
using namespace volt::test::project_bundle_v2;

[[nodiscard]] Json warning_diagnostic(std::string stage, std::string source, std::string report,
                                      std::optional<std::string> design,
                                      std::optional<std::string> board, Json entities) {
    auto expectation = Json{{"code", "WARN"},
                            {"severity", "warning"},
                            {"stage", stage},
                            {"source", source},
                            {"report", report}};
    auto design_value = Json(nullptr);
    auto board_value = Json(nullptr);
    if (design.has_value()) {
        expectation["design"] = *design;
        design_value = *design;
    }
    if (board.has_value()) {
        expectation["board"] = *board;
        board_value = *board;
    }
    return Json{{"stage", std::move(stage)},
                {"source", source},
                {"report", std::move(report)},
                {"severity", "warning"},
                {"category", "general"},
                {"code", "WARN"},
                {"message", "warning"},
                {"entities", std::move(entities)},
                {"overlays", Json::array()},
                {"measurement", nullptr},
                {"design", std::move(design_value)},
                {"board", std::move(board_value)},
                {"rule", nullptr},
                {"expect_diagnostic_kwargs", expectation}};
}

[[nodiscard]] Json warning_report(Json diagnostic) {
    return Json{{"status", "expected-diagnostics"},
                {"summary", {{"errors", 0}, {"warnings", 1}, {"infos", 0}}},
                {"diagnostics", Json::array({diagnostic})},
                {"expected", Json::array()},
                {"unexpected", Json::array({std::move(diagnostic)})},
                {"missing_expected", Json::array()}};
}

[[nodiscard]] std::string library_report_subject(const volt::io::PartLibraryBundle &bundle) {
    return "library:" + Json{{"namespace", bundle.identity().namespace_name()},
                             {"version", bundle.identity().version()},
                             {"library_bundle_digest", bundle.library_digest().value()}}
                            .dump();
}

[[nodiscard]] volt::io::ProjectBundleV2Builder
project_builder_with_diagnostics(std::string diagnostics) {
    return volt::io::ProjectBundleV2Builder{
        volt::io::ProjectIdentity{"board-fixture", std::nullopt, std::nullopt},
        volt::io::ProjectRunSummary{
            true, volt::io::ProjectStatus::ExpectedDiagnostics, "default", {"design", "board"}},
        volt::io::LogicalInputName{"project.py"},
        {volt::io::AuthoringInput{volt::io::AuthoringInputKind::ProjectSource,
                                  volt::io::LogicalInputName{"project.py"}, "project source"}},
        volt::io::ProjectReport{std::move(diagnostics)},
        volt::io::ProjectReport{R"({"summary":{"passed":0,"failed":0},"tests":[]})"}};
}

} // namespace

TEST_CASE("ProjectBundle v2 writer rejects report references that cannot reopen") {
    SECTION("missing logical entity") {
        auto fixture = board_fixture();
        const auto report = warning_report(warning_diagnostic(
            "design", "logical:main", "logical.default", std::optional<std::string>{"main"},
            std::nullopt, Json::array({{{"kind", "component"}, {"index", 999}}})));
        auto builder = project_builder_with_diagnostics(report.dump());
        builder.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.bundle);
        CHECK_THROWS_AS(builder.build(), volt::KernelError);
    }

    SECTION("foreign exact part") {
        auto fixture = board_fixture();
        const auto report = warning_report(warning_diagnostic(
            "library", "part:foreign", library_report_subject(fixture.bundle), std::nullopt,
            std::nullopt, Json::array({{{"kind", "part_definition"}, {"index", 0}}})));
        auto builder = project_builder_with_diagnostics(report.dump());
        builder.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.bundle);
        CHECK_THROWS_AS(builder.build(), volt::KernelError);
    }

    SECTION("footprint pad outside its paired selected part") {
        auto fixture = board_fixture();
        const auto report = warning_report(
            warning_diagnostic("board", "pcb:Main", "pcb.board", std::optional<std::string>{"main"},
                               std::optional<std::string>{"Main"},
                               Json::array({{{"kind", "component_placement"}, {"index", 0}},
                                            {{"kind", "footprint_pad"}, {"index", 1}}})));
        auto builder = project_builder_with_diagnostics(report.dump());
        builder.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.bundle);
        builder.add_board(volt::io::DesignKey{"main"}, fixture.board, fixture.compiled,
                          fixture.scene, fixture.bundle);
        CHECK_THROWS_AS(builder.build(), volt::KernelError);
    }
}
