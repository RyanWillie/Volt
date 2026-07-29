#include <catch2/catch_test_macros.hpp>

#include "support/project_bundle_v2_board_test_support.hpp"

namespace {

using namespace volt::test::project_bundle_v2;

} // namespace

TEST_CASE("ProjectBundle v2 regenerates a selected CPL before publishing it") {
    const auto fixture = board_fixture();
    const auto baseline = board_builder(fixture).build();
    const auto &compiled = descriptor(baseline, volt::io::ArtifactKind::CompiledBoard);
    auto builder = board_builder(fixture);
    builder.select_exports(
        {volt::io::ExportRequest{volt::io::ExportKind::Cpl,
                                 volt::io::ModelExportTarget{volt::io::ArtifactRef{
                                     compiled.id(), compiled.content_digest()}},
                                 volt::io::ExportRequestSchema{}, volt::io::CplParameters{}}});
    const auto selected = builder.build();
    const auto temporary = TempDirectory{};
    const auto path = temporary.path() / "cpl.volt";
    selected.write(path);

    CHECK(volt::io::ProjectBundle::open(path).graph().loaded_project().selected_exports().size() ==
          1U);
}
