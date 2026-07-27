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

namespace {

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

[[nodiscard]] std::string read_bytes(const std::filesystem::path &path) {
    auto input = std::ifstream{path, std::ios::binary};
    REQUIRE(input);
    return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void write_bytes(const std::filesystem::path &path, std::string_view bytes) {
    auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
    REQUIRE(output);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
}

[[nodiscard]] OrderedJson canonical_build_artifact(const OrderedJson &artifact) {
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

void reseal_manifest(const std::filesystem::path &root, OrderedJson &manifest) {
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

[[nodiscard]] BoardFixture board_fixture(double width = 30.0) {
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

[[nodiscard]] volt::io::ProjectBundleV2Builder project_builder() {
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

[[nodiscard]] volt::io::ProjectBundleV2Builder board_builder(const BoardFixture &fixture) {
    auto builder = project_builder();
    builder.add_logical(volt::io::DesignKey{"main"}, *fixture.circuit, fixture.bundle);
    builder.add_board(volt::io::DesignKey{"main"}, fixture.board, fixture.compiled, fixture.scene,
                      fixture.bundle);
    return builder;
}

[[nodiscard]] const volt::io::ArtifactDescriptor &
descriptor(const volt::io::ProjectBundleV2 &bundle, volt::io::ArtifactKind kind) {
    const auto match =
        std::ranges::find(bundle.artifacts(), kind, &volt::io::ArtifactDescriptor::kind);
    REQUIRE(match != bundle.artifacts().end());
    return *match;
}

} // namespace

TEST_CASE("ProjectBundle v2 derives one exact complete Board graph and rejects stale pairings") {
    const auto fixture = board_fixture();
    const auto bundle = board_builder(fixture).build();
    const auto temporary = TempDirectory{};
    const auto path = temporary.path() / "board.volt";
    bundle.write(path);
    const auto reopened = volt::io::ProjectBundle::open(path);
    const auto reopened_project = reopened.require_v2().loaded_project();
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

TEST_CASE("ProjectBundle v2 Board exports are opt-in typed and fail before publication") {
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
    const auto path = temporary.path() / "selected.volt";
    selected.write(path);
    const auto reopened = volt::io::ProjectBundle::open(path);
    CHECK(reopened.require_v2().loaded_project().selected_exports().size() == 2U);
    const auto selected_manifest = Json::parse(selected.manifest_bytes());
    CHECK(selected.build_id() == baseline.build_id());
    CHECK(selected_manifest.at("export_selection").size() == 2U);
    CHECK(std::ranges::count(selected.artifacts(), volt::io::ArtifactKind::BoardSvg,
                             &volt::io::ArtifactDescriptor::kind) == 1);
    CHECK(std::ranges::count(selected.artifacts(), volt::io::ArtifactKind::BoardLayerImage,
                             &volt::io::ArtifactDescriptor::kind) == 1);
    for (const auto kind :
         {volt::io::ArtifactKind::BoardSvg, volt::io::ArtifactKind::BoardLayerImage}) {
        const auto &output = descriptor(selected, kind);
        REQUIRE(output.dependencies().size() == 1U);
        CHECK(output.dependencies().front() == compiled_ref);
    }

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
