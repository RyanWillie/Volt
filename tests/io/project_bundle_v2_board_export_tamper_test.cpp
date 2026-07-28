#include <catch2/catch_test_macros.hpp>

#include <ranges>

#include "support/project_bundle_v2_board_test_support.hpp"

namespace {

using namespace volt::test::project_bundle_v2;

} // namespace

TEST_CASE("ProjectBundle v2 Board export manifest tampering rejects the whole open") {
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

    const auto wrong_shape = temporary.path() / "wrong-export-shape.volt";
    selected.write(wrong_shape);
    auto wrong_shape_manifest = OrderedJson::parse(read_bytes(wrong_shape / "manifest.volt.json"));
    wrong_shape_manifest["export_selection"][0]["parameters"]["type"] = "bom_parameters";
    reseal_manifest(wrong_shape, wrong_shape_manifest);
    CHECK_THROWS_AS(volt::io::ProjectBundle::open(wrong_shape), volt::io::ProjectBundleOpenError);

    const auto wrong_edges = temporary.path() / "wrong-export-edges.volt";
    selected.write(wrong_edges);
    auto wrong_edges_manifest = OrderedJson::parse(read_bytes(wrong_edges / "manifest.volt.json"));
    const auto output =
        std::ranges::find_if(wrong_edges_manifest.at("artifacts"), [](const auto &artifact) {
            return artifact.at("id").at("owner").at("type") == "export_artifact_identity";
        });
    REQUIRE(output != wrong_edges_manifest.at("artifacts").end());
    output->at("depends_on").clear();
    reseal_manifest(wrong_edges, wrong_edges_manifest);
    CHECK_THROWS_AS(volt::io::ProjectBundle::open(wrong_edges), volt::io::ProjectBundleOpenError);
}
