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
    const auto component =
        std::ranges::find(manifest.at("artifacts"), "component_definition",
                          [](const auto &artifact) { return artifact.at("kind"); });
    REQUIRE(component != manifest.at("artifacts").end());
    auto orphan = *component;
    const auto orphan_digest = volt::sha256_content_hash("orphan library").value();
    orphan["id"]["owner"]["value"]["library_namespace"] = "orphan.library";
    orphan["id"]["owner"]["value"]["library_version"] = "1";
    orphan["id"]["owner"]["value"]["library_bundle_digest"] = orphan_digest;
    orphan["producer"]["build"] = orphan_digest;
    const auto orphan_path = artifact_path(orphan.at("id"), ".json");
    std::filesystem::copy_file(root / component->at("path").get<std::string>(), root / orphan_path);
    orphan["path"] = orphan_path;
    manifest["artifacts"].push_back(std::move(orphan));
    auto libraries = OrderedJson::array({OrderedJson{{"library", "orphan.library"},
                                                     {"version", "1"},
                                                     {"library_bundle_digest", orphan_digest}}});
    for (const auto &library : manifest.at("dependency_lock").at("libraries")) {
        libraries.push_back(library);
    }
    manifest["dependency_lock"]["libraries"] = std::move(libraries);
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

TEST_CASE("ProjectBundle v2 binds a footprint owner key to its decoded reference") {
    const auto fixture = board_fixture();
    const auto original = board_builder(fixture).build();
    const auto temporary = TempDirectory{};
    const auto root = temporary.path() / "footprint-owner.volt";
    original.write(root);
    auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));
    const auto footprint =
        std::ranges::find(manifest.at("artifacts"), "footprint_definition",
                          [](const auto &artifact) { return artifact.at("kind"); });
    REQUIRE(footprint != manifest.at("artifacts").end());
    const auto old_id = footprint->at("id");
    const auto old_path = footprint->at("path").get<std::string>();
    footprint->at("id").at("owner").at("value").at("key") = "footprint:test.project/contradictory";
    const auto new_id = footprint->at("id");
    const auto new_path = artifact_path(new_id, ".json");
    std::filesystem::rename(root / old_path, root / new_path);
    footprint->at("path") = new_path;
    for (auto &artifact : manifest.at("artifacts")) {
        for (auto &dependency : artifact.at("depends_on")) {
            if (dependency.at("artifact") == old_id) {
                dependency["artifact"] = new_id;
            }
        }
    }
    reseal_manifest(root, manifest);

    try {
        static_cast<void>(volt::io::ProjectBundle::open(root));
        FAIL("ProjectBundle open unexpectedly succeeded");
    } catch (const volt::io::ProjectBundleOpenError &error) {
        CHECK(error.code() == volt::io::ProjectBundleOpenErrorCode::OwnershipViolation);
        CHECK(std::string{error.what()}.contains(
            "footprint payload identity disagrees with its owner"));
    }
}
