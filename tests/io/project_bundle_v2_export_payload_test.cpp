#include <catch2/catch_test_macros.hpp>

#include <ranges>
#include <string>

#include "support/project_bundle_v2_board_test_support.hpp"

namespace {

using namespace volt::test::project_bundle_v2;

} // namespace

TEST_CASE("ProjectBundle v2 regenerates selected export bytes before publication") {
    const auto fixture = board_fixture();
    const auto baseline = board_builder(fixture).build();
    const auto &compiled = descriptor(baseline, volt::io::ArtifactKind::CompiledBoard);
    const auto compiled_ref = volt::io::ArtifactRef{compiled.id(), compiled.content_digest()};
    auto builder = board_builder(fixture);
    builder.select_exports({volt::io::ExportRequest{
        volt::io::ExportKind::BoardSvg, volt::io::ModelExportTarget{compiled_ref},
        volt::io::ExportRequestSchema{}, volt::io::BoardSvgParameters{}}});
    const auto original = builder.build();
    const auto temporary = TempDirectory{};
    const auto root = temporary.path() / "tampered-export.volt";
    original.write(root);
    auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
    const auto output = std::ranges::find(manifest.at("artifacts"), "board_svg",
                                          [](const auto &artifact) { return artifact.at("kind"); });
    REQUIRE(output != manifest.at("artifacts").end());
    const auto bytes = std::string{"<svg>tampered but digest-consistent</svg>"};
    write_bytes(root / output->at("path").get<std::string>(), bytes);
    output->at("content_digest") = volt::sha256_content_hash(bytes).value();
    reseal_manifest(root, manifest);

    try {
        static_cast<void>(volt::io::ProjectBundle::open(root));
        FAIL("ProjectBundle open unexpectedly succeeded");
    } catch (const volt::io::ProjectBundleOpenError &error) {
        CHECK(error.code() == volt::io::ProjectBundleOpenErrorCode::ModelDecodeFailure);
    }
}
