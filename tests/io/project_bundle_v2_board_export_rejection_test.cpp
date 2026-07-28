#include <catch2/catch_test_macros.hpp>

#include "support/project_bundle_v2_board_test_support.hpp"

namespace {

using namespace volt::test::project_bundle_v2;

} // namespace

TEST_CASE("ProjectBundle v2 Board export requests fail before publication") {
    const auto fixture = board_fixture();
    const auto baseline = board_builder(fixture).build();
    const auto &compiled = descriptor(baseline, volt::io::ArtifactKind::CompiledBoard);
    const auto compiled_ref = volt::io::ArtifactRef{compiled.id(), compiled.content_digest()};

    auto bad_layer = board_builder(fixture);
    bad_layer.select_exports({volt::io::ExportRequest{
        volt::io::ExportKind::BoardLayerImage,
        volt::io::BoardLayerExportTarget{compiled_ref, volt::io::BoardLayerKey{"Missing.Cu"}},
        volt::io::ExportRequestSchema{}, volt::io::BoardLayerImageParameters{}}});
    CHECK_THROWS_AS(bad_layer.build(), volt::KernelError);

    auto bad_parameters = board_builder(fixture);
    bad_parameters.select_exports({volt::io::ExportRequest{
        volt::io::ExportKind::BoardSvg, volt::io::ModelExportTarget{compiled_ref},
        volt::io::ExportRequestSchema{}, volt::io::BomParameters{}}});
    CHECK_THROWS_AS(bad_parameters.build(), volt::KernelError);

    auto unsupported = board_builder(fixture);
    unsupported.select_exports({volt::io::ExportRequest{
        volt::io::ExportKind::KicadPcb, volt::io::ModelExportTarget{compiled_ref},
        volt::io::ExportRequestSchema{}, volt::io::KicadPcbParameters{}}});
    CHECK_THROWS_AS(unsupported.build(), volt::KernelError);
}
