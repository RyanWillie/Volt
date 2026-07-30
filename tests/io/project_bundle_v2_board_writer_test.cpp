#include <catch2/catch_test_macros.hpp>

#include <ranges>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>

#include "support/project_bundle_v2_board_test_support.hpp"

namespace {

using Json = nlohmann::json;
using namespace volt::test::project_bundle_v2;

} // namespace

TEST_CASE("ProjectBundle v2 derives one exact complete Board graph and rejects stale pairings") {
    const auto fixture = board_fixture();
    const auto bundle = board_builder(fixture).build();
    const auto temporary = TempDirectory{};
    const auto path = temporary.path() / "board.volt";
    bundle.write(path);
    const auto reopened = volt::io::ProjectBundle::open(path);
    const auto reopened_project = reopened.graph().loaded_project();
    CHECK(reopened_project.circuits().size() == 1U);
    CHECK(reopened_project.boards().size() == 1U);
    CHECK(reopened_project.compiled_boards().size() == 1U);
    CHECK(reopened_project.board_scenes().size() == 1U);
    const auto reopened_scenes = reopened_project.board_scenes();
    const auto reopened_compiled = reopened_scenes.front().compiled_board();
    CHECK(reopened_compiled.identity() == fixture.compiled.identity());
    const auto manifest = Json::parse(bundle.manifest_bytes());
    auto kind_values = std::vector<std::string>{};
    for (const auto &artifact : manifest.at("artifacts")) {
        kind_values.push_back(artifact.at("kind").get<std::string>());
    }

    CHECK(std::ranges::count(kind_values, "logical_model") == 1);
    CHECK(std::ranges::count(kind_values, "board_model") == 1);
    CHECK(std::ranges::count(kind_values, "compiled_board") == 1);
    CHECK(std::ranges::count(kind_values, "board_scene") == 1);
    CHECK(std::ranges::count(kind_values, "component_definition") == 1);
    CHECK(std::ranges::count(kind_values, "part_definition") == 1);
    CHECK(std::ranges::count(kind_values, "footprint_definition") == 1);
    CHECK(std::ranges::count(kind_values, "glb_asset") == 0);
    CHECK(bundle.dependency_lock().libraries().size() == 1U);
    CHECK(bundle.dependency_lock().selected_parts().size() == 1U);

    const auto dependency_kinds = [&](volt::io::ArtifactKind kind) {
        auto result = std::vector<volt::io::ArtifactKind>{};
        for (const auto &dependency : descriptor(bundle, kind).dependencies()) {
            result.push_back(dependency.artifact().kind());
        }
        std::ranges::sort(result);
        return result;
    };
    CHECK(dependency_kinds(volt::io::ArtifactKind::BoardModel) ==
          std::vector{volt::io::ArtifactKind::LogicalModel});
    CHECK(dependency_kinds(volt::io::ArtifactKind::PartDefinition) ==
          (std::vector{volt::io::ArtifactKind::ComponentDefinition,
                       volt::io::ArtifactKind::FootprintDefinition}));
    CHECK(dependency_kinds(volt::io::ArtifactKind::LogicalModel) ==
          (std::vector{volt::io::ArtifactKind::ComponentDefinition,
                       volt::io::ArtifactKind::PartDefinition}));
    CHECK(dependency_kinds(volt::io::ArtifactKind::CompiledBoard) ==
          (std::vector{volt::io::ArtifactKind::LogicalModel, volt::io::ArtifactKind::BoardModel,
                       volt::io::ArtifactKind::ComponentDefinition,
                       volt::io::ArtifactKind::PartDefinition,
                       volt::io::ArtifactKind::FootprintDefinition}));
    CHECK(dependency_kinds(volt::io::ArtifactKind::BoardScene) ==
          std::vector{volt::io::ArtifactKind::CompiledBoard});
    const auto evaluated_models =
        std::vector{volt::io::ArtifactKind::LogicalModel, volt::io::ArtifactKind::BoardModel};
    CHECK(dependency_kinds(volt::io::ArtifactKind::Diagnostics) == evaluated_models);
    CHECK(dependency_kinds(volt::io::ArtifactKind::ProjectTests) == evaluated_models);

    const auto changed = board_fixture(31.0);
    const auto historical_manifest = std::string{bundle.manifest_bytes()};
    const auto changed_bundle = board_builder(changed).build();
    CHECK(changed_bundle.build_id() != bundle.build_id());
    CHECK(descriptor(changed_bundle, volt::io::ArtifactKind::BoardModel).id() ==
          descriptor(bundle, volt::io::ArtifactKind::BoardModel).id());
    CHECK(descriptor(changed_bundle, volt::io::ArtifactKind::BoardModel).content_digest() !=
          descriptor(bundle, volt::io::ArtifactKind::BoardModel).content_digest());
    CHECK(descriptor(changed_bundle, volt::io::ArtifactKind::CompiledBoard).id() !=
          descriptor(bundle, volt::io::ArtifactKind::CompiledBoard).id());
    CHECK(std::string{bundle.manifest_bytes()} == historical_manifest);

    auto stale_compiled = project_builder();
    stale_compiled.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.bundle);
    stale_compiled.add_board(volt::io::DesignKey{"main"}, fixture.board, changed.compiled,
                             changed.scene, fixture.bundle);
    CHECK_THROWS_AS(stale_compiled.build(), volt::KernelError);

    auto stale_scene = project_builder();
    stale_scene.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.bundle);
    stale_scene.add_board(volt::io::DesignKey{"main"}, fixture.board, fixture.compiled,
                          changed.scene, fixture.bundle);
    CHECK_THROWS_AS(stale_scene.build(), volt::KernelError);

    static_cast<void>(
        fixture.circuit->add_net(volt::NetSpec{.name = volt::NetName{"POST_COMPILE"}}));
    auto stale_logical = project_builder();
    stale_logical.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.bundle);
    stale_logical.add_board(volt::io::DesignKey{"main"}, fixture.board, fixture.compiled,
                            fixture.scene, fixture.bundle);
    CHECK_THROWS_AS(stale_logical.build(), volt::KernelError);
}
