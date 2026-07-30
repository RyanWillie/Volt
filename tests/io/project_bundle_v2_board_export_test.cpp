#include <catch2/catch_test_macros.hpp>

#include <ranges>

#include "support/project_bundle_v2_board_test_support.hpp"

namespace {

using namespace volt::test::project_bundle_v2;

} // namespace

TEST_CASE("ProjectBundle v2 Board exports are opt-in typed and fail before publication") {
    const auto fixture = board_fixture();
    const auto baseline = board_builder(fixture).build();
    const auto &compiled = descriptor(baseline, volt::io::ArtifactKind::CompiledBoard);
    const auto compiled_ref = volt::io::ArtifactRef{compiled.id(), compiled.content_digest()};
    const auto board_svg = volt::io::ExportRequest{
        volt::io::ExportKind::BoardSvg, volt::io::ModelExportTarget{compiled_ref},
        volt::io::ExportRequestSchema{}, volt::io::BoardSvgParameters{}};
    const auto front_layer = volt::io::ExportRequest{
        volt::io::ExportKind::BoardLayerImage,
        volt::io::BoardLayerExportTarget{compiled_ref, volt::io::BoardLayerKey{"F.Cu"}},
        volt::io::ExportRequestSchema{}, volt::io::BoardLayerImageParameters{}};

    auto selected_builder = board_builder(fixture);
    selected_builder.select_exports({front_layer, board_svg});
    const auto selected = selected_builder.build();
    const auto temporary = TempDirectory{};
    const auto path = temporary.path() / "selected.volt";
    selected.write(path);
    const auto reopened = volt::io::ProjectBundle::open(path);
    CHECK(reopened.graph().loaded_project().selected_exports().size() == 2U);
    const auto selected_manifest = Json::parse(selected.manifest_bytes());
    CHECK(selected.build_id() == baseline.build_id());
    CHECK(selected_manifest.at("export_selection").size() == 2U);
    CHECK(std::ranges::count(selected.artifacts(), volt::io::ArtifactKind::BoardSvg,
                             &volt::io::ArtifactDescriptor::kind) == 1);
    CHECK(std::ranges::count(selected.artifacts(), volt::io::ArtifactKind::BoardLayerImage,
                             &volt::io::ArtifactDescriptor::kind) == 1);
    for (const auto kind :
         {volt::io::ArtifactKind::BoardSvg, volt::io::ArtifactKind::BoardLayerImage}) {
        const auto &output = descriptor(selected, kind);
        REQUIRE(output.dependencies().size() == 1U);
        CHECK(output.dependencies().front() == compiled_ref);
    }
}
