#pragma once

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>
#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/io/pcb/compiled_board_consumers.hpp>
#include <volt/io/project_bundle.hpp>
#include <volt/io/project_bundle_v2_writer.hpp>

#include "support/compiled_board_export_helpers.hpp"

namespace volt::test::project_bundle_v2 {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

class TempDirectory final {
  public:
    TempDirectory() {
        static auto counter = std::atomic<std::uint64_t>{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()) /
                ("volt-project-bundle-v2-" + std::to_string(stamp) + "-" +
                 std::to_string(counter.fetch_add(1U)));
        std::filesystem::create_directories(path_);
    }

    TempDirectory(const TempDirectory &) = delete;
    TempDirectory &operator=(const TempDirectory &) = delete;

    ~TempDirectory() {
        auto ignored = std::error_code{};
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path &path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
};

[[nodiscard]] inline std::string read_bytes(const std::filesystem::path &path) {
    auto input = std::ifstream{path, std::ios::binary};
    REQUIRE(input);
    return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

inline void write_bytes(const std::filesystem::path &path, std::string_view bytes) {
    auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
    REQUIRE(output);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
}

[[nodiscard]] inline OrderedJson canonical_build_artifact(const OrderedJson &artifact) {
    return OrderedJson{{"id", artifact.at("id")},
                       {"role", artifact.at("role")},
                       {"kind", artifact.at("kind")},
                       {"schema", artifact.at("schema")},
                       {"media_type", artifact.at("media_type")},
                       {"content_digest", artifact.at("content_digest")},
                       {"depends_on", artifact.at("depends_on")},
                       {"producer_name", artifact.at("producer").at("name")},
                       {"producer_version", artifact.at("producer").at("version")}};
}

inline void reseal_manifest(const std::filesystem::path &root, OrderedJson &manifest) {
    auto required = OrderedJson::array();
    for (const auto &artifact : manifest.at("artifacts")) {
        if (artifact.at("id").at("owner").at("type") != "export_artifact_identity") {
            required.push_back(canonical_build_artifact(artifact));
        }
    }
    const auto build_identity =
        OrderedJson{{"format", manifest.at("format")},
                    {"schema_version", manifest.at("schema_version")},
                    {"project", manifest.at("project")},
                    {"authoring_inputs_digest", manifest.at("authoring_inputs").at("digest")},
                    {"dependency_lock", manifest.at("dependency_lock")},
                    {"artifacts", std::move(required)}};
    manifest["build_id"] = volt::sha256_content_hash(build_identity.dump()).value();
    for (auto &artifact : manifest.at("artifacts")) {
        if (artifact.at("producer").at("name") == "volt.project-bundle") {
            artifact["producer"]["build"] = manifest.at("build_id");
        }
    }
    const auto core = OrderedJson{{"format", manifest.at("format")},
                                  {"schema_version", manifest.at("schema_version")},
                                  {"project", manifest.at("project")},
                                  {"run", manifest.at("run")},
                                  {"authoring_inputs", manifest.at("authoring_inputs")},
                                  {"build_id", manifest.at("build_id")},
                                  {"dependency_lock", manifest.at("dependency_lock")},
                                  {"export_selection", manifest.at("export_selection")},
                                  {"artifacts", manifest.at("artifacts")}};
    manifest["bundle_digest"] = volt::sha256_content_hash(core.dump()).value();
    write_bytes(root / "manifest.volt.json", manifest.dump());
}

struct BoardFixture {
    std::unique_ptr<volt::Circuit> circuit;
    volt::io::PartLibraryBundle bundle;
    volt::Board board;
    volt::CompiledBoard compiled;
    volt::BoardScene scene;
};

[[nodiscard]] inline BoardFixture board_fixture(double width = 30.0) {
    auto circuit = std::make_unique<volt::Circuit>();
    const auto spec = volt::ComponentSpec{
        .name = "Project resistor",
        .pins = {volt::PinSpec{.name = "A", .number = "1"}},
        .source = volt::DefinitionSource{"test.project", "resistor", "1"},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{"test.project/resistor@1"},
                .pin_keys = {volt::PinKey{"A"}},
            },
    };
    const auto definition = circuit->define_component(spec);
    const auto component = circuit->instantiate_component(
        definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    const auto footprint = volt::FootprintDefinition{
        volt::FootprintRef{"test.project", "R1"},
        std::vector{volt::FootprintPad::surface_mount(
            "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{0.0, 0.0},
            volt::FootprintSize{1.0, 1.0}, volt::FootprintLayerSet::front_smd())}};
    const auto physical = volt::PhysicalPart{
        volt::ManufacturerPart{"Volt", "PROJECT-R1"}, volt::PackageRef{"0603"}, footprint.ref(),
        std::vector{volt::PinPadMapping{circuit->get(definition).pins().front(), "1"}}};
    auto library = volt::test::make_export_fixture_library(
        {{spec, physical, footprint, volt::PartKey{"resistor"}}});
    circuit->update(component, volt::SelectLibraryPart{
                                   library.bundle, library.bundle.require(library.keys.front())});

    auto board = volt::Board{*circuit, volt::BoardName{"Main"}};
    board.set_capability_profile(volt::test::export_fixture_profile());
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    board.set_layer_stack(volt::LayerStack{{front}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{width, 20.0}));
    static_cast<void>(board.place_component(
        volt::ComponentPlacement{component, volt::BoardPoint{5.0, 5.0},
                                 volt::BoardRotation::degrees(0.0), volt::BoardSide::Top, false}));

    auto compiled = volt::test::compile_export_fixture(*circuit, board, library.bundle);
    auto scene = volt::prepare_board_scene(compiled);
    return BoardFixture{std::move(circuit), std::move(library.bundle), std::move(board),
                        std::move(compiled), std::move(scene)};
}

[[nodiscard]] inline volt::io::ProjectBundleV2Builder project_builder() {
    return volt::io::ProjectBundleV2Builder{
        volt::io::ProjectIdentity{"board-fixture", std::nullopt, std::nullopt},
        volt::io::ProjectRunSummary{
            true, volt::io::ProjectStatus::Clean, "default", {"design", "board"}},
        volt::io::LogicalInputName{"project.py"},
        {volt::io::AuthoringInput{volt::io::AuthoringInputKind::ProjectSource,
                                  volt::io::LogicalInputName{"project.py"}, "project source"}},
        volt::io::ProjectReport{
            R"({"status":"clean","summary":{"errors":0,"warnings":0,"infos":0},"diagnostics":[],"expected":[],"unexpected":[],"missing_expected":[]})"},
        volt::io::ProjectReport{R"({"summary":{"passed":0,"failed":0},"tests":[]})"}};
}

[[nodiscard]] inline volt::io::ProjectBundleV2Builder board_builder(const BoardFixture &fixture) {
    auto builder = project_builder();
    builder.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.bundle);
    builder.add_board(volt::io::DesignKey{"main"}, fixture.board, fixture.compiled, fixture.scene,
                      fixture.bundle);
    return builder;
}

[[nodiscard]] inline const volt::io::ArtifactDescriptor &
descriptor(const volt::io::ProjectBundleV2 &bundle, volt::io::ArtifactKind kind) {
    const auto match =
        std::ranges::find(bundle.artifacts(), kind, &volt::io::ArtifactDescriptor::kind);
    REQUIRE(match != bundle.artifacts().end());
    return *match;
}

} // namespace volt::test::project_bundle_v2
