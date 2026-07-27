#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <ranges>
#include <string>

#include <volt/io/logical/logical_circuit_reader.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/io/parts/part_definition_reader.hpp>
#include <volt/io/parts/part_definition_writer.hpp>

#include "support/project_bundle_v2_board_test_support.hpp"

namespace {

using namespace volt::test::project_bundle_v2;

[[nodiscard]] std::string artifact_path(const OrderedJson &id, std::string_view extension) {
    const auto digest = volt::sha256_content_hash(id.dump()).value();
    return "artifacts/" + id.at("kind").get<std::string>() + "/" +
           digest.substr(std::string_view{"sha256:"}.size(), 20U) + std::string{extension};
}

[[nodiscard]] std::size_t replace_value(OrderedJson &value, const OrderedJson &from,
                                        const OrderedJson &to) {
    if (value == from) {
        value = to;
        return 1U;
    }
    auto replacements = std::size_t{0};
    if (value.is_object()) {
        for (auto &[unused, child] : value.items()) {
            static_cast<void>(unused);
            replacements += replace_value(child, from, to);
        }
    } else if (value.is_array()) {
        for (auto &child : value) {
            replacements += replace_value(child, from, to);
        }
    }
    return replacements;
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

TEST_CASE("ProjectBundle v2 binds a footprint payload to its selected typed reference") {
    const auto fixture = board_fixture(30.0, volt::FootprintRef{"test.project", "nested/R1"});
    auto builder = project_builder();
    builder.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.bundle);
    const auto original = builder.build();
    const auto temporary = TempDirectory{};
    const auto root = temporary.path() / "footprint-ref-collision.volt";
    original.write(root);
    auto manifest = OrderedJson::parse(read_bytes(root / "manifest.volt.json"));

    const auto component =
        std::ranges::find(manifest.at("artifacts"), "component_definition",
                          [](const auto &artifact) { return artifact.at("kind"); });
    const auto part = std::ranges::find(manifest.at("artifacts"), "part_definition",
                                        [](const auto &artifact) { return artifact.at("kind"); });
    const auto logical =
        std::ranges::find(manifest.at("artifacts"), "logical_model",
                          [](const auto &artifact) { return artifact.at("kind"); });
    REQUIRE(component != manifest.at("artifacts").end());
    REQUIRE(part != manifest.at("artifacts").end());
    REQUIRE(logical != manifest.at("artifacts").end());

    const auto component_document = volt::io::read_logical_circuit_text(
        read_bytes(root / component->at("path").get<std::string>()));
    const auto &component_definition = component_document.get(volt::ComponentDefId{0});
    const auto original_part = volt::io::read_part_definition_text(
        read_bytes(root / part->at("path").get<std::string>()), component_definition);
    const auto &physical = original_part.orderable_part();
    const auto altered_part = volt::PartDefinition{
        component_definition,
        original_part.identity(),
        original_part.electrical_records(),
        original_part.pin_terminal_mappings(),
        original_part.terminal_dispositions(),
        original_part.provenance(),
        original_part.schematic_assets(),
        volt::OrderablePart{
            physical.manufacturer_part(), physical.package(),
            volt::HashedFootprintReference{volt::FootprintRef{"test.project/nested", "R1"},
                                           physical.footprint().hash()},
            physical.footprint_pads(), physical.terminal_pad_mappings(),
            physical.approved_alternate_mpns(), physical.model_3d(), physical.footprint_courtyard(),
            physical.footprint_body(), physical.footprint_fabrication_outline(),
            physical.footprint_assembly_outline(), physical.footprint_markings()}};
    const auto altered_part_bytes = volt::io::write_part_definition(altered_part);

    const auto old_part_id = part->at("id");
    auto new_part_id = old_part_id;
    const auto old_part_digest =
        old_part_id.at("owner").at("value").at("part_digest").get<std::string>();
    const auto new_part_digest = altered_part.content_identity().value();
    new_part_id.at("owner").at("value")["part_digest"] = new_part_digest;
    const auto old_part_content_digest = part->at("content_digest").get<std::string>();
    const auto new_part_content_digest = volt::sha256_content_hash(altered_part_bytes).value();
    const auto old_part_path = part->at("path").get<std::string>();
    const auto new_part_path = artifact_path(new_part_id, ".json");

    auto logical_document =
        OrderedJson::parse(read_bytes(root / logical->at("path").get<std::string>()));
    REQUIRE(replace_value(logical_document, old_part_digest, new_part_digest) == 1U);
    const auto altered_logical = volt::io::read_logical_circuit_text(logical_document.dump());
    const auto altered_logical_bytes = volt::io::write_logical_circuit(altered_logical);
    const auto old_logical_content_digest = logical->at("content_digest").get<std::string>();
    const auto new_logical_content_digest =
        volt::sha256_content_hash(altered_logical_bytes).value();

    REQUIRE(replace_value(manifest, old_part_id, new_part_id) >= 2U);
    REQUIRE(replace_value(manifest, old_part_digest, new_part_digest) >= 1U);
    REQUIRE(replace_value(manifest, old_part_content_digest, new_part_content_digest) >= 2U);
    REQUIRE(replace_value(manifest, old_logical_content_digest, new_logical_content_digest) >= 2U);
    const auto updated_part =
        std::ranges::find(manifest.at("artifacts"), "part_definition",
                          [](const auto &artifact) { return artifact.at("kind"); });
    REQUIRE(updated_part != manifest.at("artifacts").end());
    updated_part->at("path") = new_part_path;

    std::filesystem::rename(root / old_part_path, root / new_part_path);
    write_bytes(root / new_part_path, altered_part_bytes);
    write_bytes(root / logical->at("path").get<std::string>(), altered_logical_bytes);
    reseal_manifest(root, manifest);

    try {
        static_cast<void>(volt::io::ProjectBundle::open(root));
        FAIL("ProjectBundle open unexpectedly succeeded");
    } catch (const volt::io::ProjectBundleOpenError &error) {
        CHECK(error.code() == volt::io::ProjectBundleOpenErrorCode::OwnershipViolation);
        CHECK(std::string{error.what()}.contains(
            "footprint payload reference disagrees with its selected part"));
    }
}
