#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/io/project_bundle_writer.hpp>

#include "support/project_bundle_v2_board_test_support.hpp"

namespace {

using Json = nlohmann::json;
using namespace volt::test::project_bundle_v2;

class BundleAssetResolver final : public volt::PartAssetResolver {
  public:
    explicit BundleAssetResolver(const volt::io::PartLibraryBundle &bundle) : bundle_{bundle} {}

    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &reference) const override {
        const auto bytes = bundle_.asset(reference);
        return bytes.has_value() ? std::optional{std::string{*bytes}} : std::nullopt;
    }

  private:
    const volt::io::PartLibraryBundle &bundle_;
};

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

[[nodiscard]] Json missing_expectation_report(std::string design) {
    const auto source = "logical:" + design;
    const auto expectation = Json{{"code", "WARN"},
                                  {"severity", nullptr},
                                  {"stage", nullptr},
                                  {"source", source},
                                  {"report", nullptr},
                                  {"design", design},
                                  {"board", nullptr},
                                  {"rule", nullptr},
                                  {"matched", false},
                                  {"expect_diagnostic_kwargs",
                                   Json{{"code", "WARN"}, {"source", source}, {"design", design}}}};
    const auto diagnostic =
        warning_diagnostic("design", "logical:main", "logical.default",
                           std::optional<std::string>{"main"}, std::nullopt, Json::array());
    return Json{{"status", "failed"},
                {"summary", {{"errors", 0}, {"warnings", 1}, {"infos", 0}}},
                {"diagnostics", Json::array({diagnostic})},
                {"expected", Json::array({expectation})},
                {"unexpected", Json::array({diagnostic})},
                {"missing_expected", Json::array({expectation})}};
}

[[nodiscard]] std::string library_report_subject(const volt::io::PartLibraryBundle &bundle) {
    return "library:" + Json{{"namespace", bundle.identity().namespace_name()},
                             {"version", bundle.identity().version()},
                             {"library_bundle_digest", bundle.library_digest().value()}}
                            .dump();
}

[[nodiscard]] volt::io::ProjectBundleBuilder project_builder_with_diagnostics(
    std::string diagnostics,
    volt::io::ProjectStatus status = volt::io::ProjectStatus::ExpectedDiagnostics) {
    return volt::io::ProjectBundleBuilder{
        volt::io::ProjectIdentity{"board-fixture", std::nullopt, std::nullopt},
        volt::io::ProjectRunSummary{
            status != volt::io::ProjectStatus::Failed, status, "default", {"design", "board"}},
        volt::io::LogicalInputName{"project.py"},
        {volt::io::AuthoringInput{volt::io::AuthoringInputKind::ProjectSource,
                                  volt::io::LogicalInputName{"project.py"}, "project source"}},
        volt::io::ProjectReport{std::move(diagnostics)},
        volt::io::ProjectReport{R"({"summary":{"passed":0,"failed":0},"tests":[]})"}};
}

[[nodiscard]] volt::io::PartLibraryBundle
library_with_extra_summary_pad(const volt::io::PartLibraryBundle &original) {
    REQUIRE(original.library().parts().size() == 1U);
    REQUIRE(original.library().components().size() == 1U);
    const auto spec = volt::ComponentSpec{
        .name = "Project resistor",
        .pins = {volt::PinSpec{.name = "A", .number = "1"}},
        .source = volt::DefinitionSource{"test.project", "resistor", "1"},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{"test.project/resistor@1"},
                .pin_keys = {volt::PinKey{"A"}},
            },
    };
    const auto &part = original.library().parts().front();
    const auto &physical = part.orderable_part();
    auto pads = physical.footprint_pads();
    pads.emplace_back("M", 2.0, 0.0, 1.0, 1.0, volt::PartFootprintPadRole::Mechanical);
    const auto altered = volt::PartDefinition{
        original.library().components().front(),
        part.identity(),
        part.electrical_records(),
        part.pin_terminal_mappings(),
        part.terminal_dispositions(),
        part.provenance(),
        part.schematic_assets(),
        volt::OrderablePart{physical.manufacturer_part(), physical.package(), physical.footprint(),
                            std::move(pads), physical.terminal_pad_mappings(),
                            physical.approved_alternate_mpns(), physical.model_3d(),
                            physical.footprint_courtyard(), physical.footprint_body(),
                            physical.footprint_fabrication_outline(),
                            physical.footprint_assembly_outline(), physical.footprint_markings()}};
    auto builder = volt::PartLibraryBuilder{original.identity()};
    builder.add_component(spec).add_part(altered);
    const auto key = volt::PartKey{part.identity().name()};
    return volt::io::PartLibraryBundle::build(builder, std::vector{key},
                                              BundleAssetResolver{original});
}

void check_build_error(volt::io::ProjectBundleBuilder &builder, std::string_view expected) {
    try {
        static_cast<void>(builder.build());
        FAIL("ProjectBundle v2 build unexpectedly succeeded");
    } catch (const volt::KernelError &error) {
        CHECK(std::string{error.what()}.contains(expected));
    }
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

    SECTION("footprint pad from another placement") {
        auto fixture = mixed_footprint_board_fixture();
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

    SECTION("footprint pad absent from the vendored payload") {
        auto fixture = board_fixture();
        auto mismatched = library_with_extra_summary_pad(fixture.bundle);
        const auto key = volt::PartKey{"resistor"};
        fixture.circuit->update(volt::ComponentId{0},
                                volt::SelectLibraryPart{mismatched, mismatched.require(key)});
        const auto report = warning_report(
            warning_diagnostic("board", "pcb:Main", "pcb.board", std::optional<std::string>{"main"},
                               std::optional<std::string>{"Main"},
                               Json::array({{{"kind", "component_placement"}, {"index", 0}},
                                            {{"kind", "footprint_pad"}, {"index", 1}}})));
        auto builder = project_builder_with_diagnostics(report.dump());
        builder.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, mismatched);
        builder.add_board(volt::io::DesignKey{"main"}, fixture.board, fixture.compiled,
                          fixture.scene, mismatched);
        check_build_error(builder, "footprint pad is outside its paired placement");
    }

    SECTION("unmatched expectation names a foreign design") {
        auto fixture = board_fixture();
        const auto report = missing_expectation_report("foreign");
        auto builder =
            project_builder_with_diagnostics(report.dump(), volt::io::ProjectStatus::Failed);
        builder.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.bundle);
        check_build_error(builder, "project diagnostic names a missing logical design");
    }
}
