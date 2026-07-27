#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <ranges>
#include <string>

#include "support/project_bundle_v2_board_test_support.hpp"

namespace {

using namespace volt::test::project_bundle_v2;

[[nodiscard]] std::string artifact_path(const OrderedJson &id, std::string_view extension) {
    const auto digest = volt::sha256_content_hash(id.dump()).value();
    return "artifacts/" + id.at("kind").get<std::string>() + "/" +
           digest.substr(std::string_view{"sha256:"}.size(), 20U) + std::string{extension};
}

} // namespace

TEST_CASE("ProjectBundle v2 rejects an unreachable vendored library artifact") {
    const auto fixture = board_fixture();
    const auto original = board_builder(fixture).build();
    const auto temporary = TempDirectory{};
    const auto root = temporary.path() / "orphan-library-artifact.volt";
    original.write(root);
    auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
    const auto footprint =
        std::ranges::find(manifest.at("artifacts"), "footprint_definition",
                          [](const auto &artifact) { return artifact.at("kind"); });
    REQUIRE(footprint != manifest.at("artifacts").end());
    auto orphan = *footprint;
    orphan["id"]["owner"]["value"]["key"] = "footprint:test.project/orphan";
    const auto orphan_path = artifact_path(orphan.at("id"), ".json");
    std::filesystem::copy_file(root / footprint->at("path").get<std::string>(), root / orphan_path);
    orphan["path"] = orphan_path;
    manifest["artifacts"].push_back(std::move(orphan));
    auto ordered_artifacts = std::vector<OrderedJson>{};
    for (const auto &artifact : manifest.at("artifacts")) {
        ordered_artifacts.push_back(artifact);
    }
    std::ranges::sort(ordered_artifacts, [](const auto &left, const auto &right) {
        return left.at("id").dump() < right.at("id").dump();
    });
    manifest["artifacts"] = OrderedJson::array();
    for (auto &artifact : ordered_artifacts) {
        manifest["artifacts"].push_back(std::move(artifact));
    }
    reseal_manifest(root, manifest);

    try {
        static_cast<void>(volt::io::ProjectBundle::open(root));
        FAIL("ProjectBundle open unexpectedly succeeded");
    } catch (const volt::io::ProjectBundleOpenError &error) {
        CHECK(error.code() == volt::io::ProjectBundleOpenErrorCode::OwnershipViolation);
    }
}
