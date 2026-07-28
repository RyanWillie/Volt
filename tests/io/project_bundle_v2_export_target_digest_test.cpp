#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <ranges>
#include <string>

#include "support/project_bundle_v2_board_test_support.hpp"

namespace volt::io::detail {

[[nodiscard]] std::string project_bundle_v2_artifact_path(const ArtifactId &id);

} // namespace volt::io::detail

namespace {

using namespace volt::test::project_bundle_v2;

} // namespace

TEST_CASE("ProjectBundle v2 binds an export target to its exact artifact digest") {
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
    const auto root = temporary.path() / "stale-export-target.volt";
    original.write(root);

    auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
    auto &request = manifest.at("export_selection").front();
    request.at("target").at("value").at("model")["content_digest"] =
        volt::sha256_content_hash("stale compiled board").value();
    const auto request_digest = volt::sha256_content_hash(request.dump()).value();

    const auto output = std::ranges::find(manifest.at("artifacts"), "board_svg",
                                          [](const auto &artifact) { return artifact.at("kind"); });
    REQUIRE(output != manifest.at("artifacts").end());
    const auto old_path = output->at("path").get<std::string>();
    output->at("id").at("owner").at("value")["request_digest"] = request_digest;
    const auto new_path = volt::io::detail::project_bundle_v2_artifact_path(volt::io::ArtifactId{
        volt::io::ArtifactKind::BoardSvg,
        volt::io::ExportArtifactIdentity{volt::ContentHash{request_digest},
                                         volt::io::PrimaryExportOutputKey::Primary}});
    std::filesystem::rename(root / old_path, root / new_path);
    output->at("path") = new_path;
    reseal_manifest(root, manifest);

    try {
        static_cast<void>(volt::io::ProjectBundle::open(root));
        FAIL("ProjectBundle open unexpectedly succeeded");
    } catch (const volt::io::ProjectBundleOpenError &error) {
        INFO(error.what());
        CHECK(error.code() == volt::io::ProjectBundleOpenErrorCode::DigestMismatch);
    }
}
