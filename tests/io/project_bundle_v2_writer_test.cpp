#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>
#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/io/pcb/compiled_board_consumers.hpp>
#include <volt/io/project_bundle_v2_writer.hpp>

#include "support/compiled_board_export_helpers.hpp"

namespace {

using Json = nlohmann::json;

class EmptyAssetResolver final : public volt::PartAssetResolver {
  public:
    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &) const override {
        return std::nullopt;
    }
};

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

[[nodiscard]] std::map<std::string, std::string> snapshot(const std::filesystem::path &root) {
    auto result = std::map<std::string, std::string>{};
    for (const auto &entry : std::filesystem::recursive_directory_iterator{root}) {
        if (entry.is_regular_file()) {
            result.emplace(entry.path().lexically_relative(root).generic_string(),
                           read_bytes(entry.path()));
        }
    }
    return result;
}

class LogicalFixture final {
  public:
    LogicalFixture()
        : bundle_{volt::io::PartLibraryBundle::build(
              volt::PartLibraryBuilder{
                  volt::PartLibraryIdentity{"test.empty", "1", volt::PartLibrarySchemaVersion::V1}},
              {}, resolver_)} {}

    [[nodiscard]] volt::io::ProjectBundleV2Builder
    builder(std::string source = "project source",
            std::vector<volt::io::AuthoringInput> extra_inputs = {}) const {
        auto inputs = std::vector<volt::io::AuthoringInput>{};
        inputs.emplace_back(volt::io::AuthoringInputKind::ProjectSource,
                            volt::io::LogicalInputName{"project.py"}, std::move(source));
        inputs.insert(inputs.end(), std::make_move_iterator(extra_inputs.begin()),
                      std::make_move_iterator(extra_inputs.end()));
        auto result = volt::io::ProjectBundleV2Builder{
            volt::io::ProjectIdentity{"fixture", std::optional{"1.0"},
                                      std::optional{"fixture project"}},
            volt::io::ProjectRunSummary{
                true, volt::io::ProjectStatus::Clean, "default", {"design"}},
            volt::io::LogicalInputName{"project.py"},
            std::move(inputs),
            volt::io::ProjectReport{
                R"({"status":"clean","summary":{"errors":0,"warnings":0,"infos":0},"diagnostics":[],"expected":[],"unexpected":[],"missing_expected":[]})"},
            volt::io::ProjectReport{R"({"summary":{"passed":0,"failed":0},"tests":[]})"}};
        result.add_logical(volt::io::DesignKey{"main"}, circuit_, bundle_);
        return result;
    }

  private:
    EmptyAssetResolver resolver_;
    volt::Circuit circuit_;
    volt::io::PartLibraryBundle bundle_;
};

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

TEST_CASE("ProjectBundle v2 writer is deterministic and identity sensitive") {
    const auto fixture = LogicalFixture{};
    auto first = fixture.builder().build();
    auto second = fixture.builder().build();
    auto ordered_inputs =
        fixture
            .builder("project source",
                     {volt::io::AuthoringInput{volt::io::AuthoringInputKind::DeclaredInput,
                                               volt::io::LogicalInputName{"inputs/a.json"}, "a"},
                      volt::io::AuthoringInput{volt::io::AuthoringInputKind::DeclaredInput,
                                               volt::io::LogicalInputName{"inputs/b.json"}, "b"}})
            .build();
    auto reversed_inputs =
        fixture
            .builder("project source",
                     {volt::io::AuthoringInput{volt::io::AuthoringInputKind::DeclaredInput,
                                               volt::io::LogicalInputName{"inputs/b.json"}, "b"},
                      volt::io::AuthoringInput{volt::io::AuthoringInputKind::DeclaredInput,
                                               volt::io::LogicalInputName{"inputs/a.json"}, "a"}})
            .build();

    CHECK(first.build_id() == second.build_id());
    CHECK(first.bundle_digest() == second.bundle_digest());
    CHECK(std::string{first.manifest_bytes()} == std::string{second.manifest_bytes()});
    CHECK(first.paths() == second.paths());
    CHECK(first.archive_bytes() == second.archive_bytes());
    CHECK(ordered_inputs.build_id() == reversed_inputs.build_id());
    CHECK(ordered_inputs.bundle_digest() == reversed_inputs.bundle_digest());
    CHECK(std::string{ordered_inputs.manifest_bytes()} ==
          std::string{reversed_inputs.manifest_bytes()});
    CHECK(ordered_inputs.archive_bytes() == reversed_inputs.archive_bytes());
    const auto ordered_manifest = Json::parse(ordered_inputs.manifest_bytes());
    CHECK(ordered_manifest.at("authoring_inputs").at("records").at(0).at("kind") ==
          "declared_input");
    CHECK(ordered_manifest.at("authoring_inputs").at("records").at(1).at("kind") ==
          "declared_input");
    CHECK(ordered_manifest.at("authoring_inputs").at("records").at(2).at("kind") ==
          "project_source");

    const auto manifest = Json::parse(first.manifest_bytes());
    CHECK(manifest.at("schema_version") == 2);
    CHECK(manifest.at("export_selection").empty());
    CHECK(manifest.at("artifacts").size() == 3U);
    CHECK(descriptor(first, volt::io::ArtifactKind::LogicalModel).dependencies().empty());
    CHECK(descriptor(first, volt::io::ArtifactKind::Diagnostics).dependencies().size() == 1U);
    CHECK(descriptor(first, volt::io::ArtifactKind::ProjectTests).dependencies().size() == 1U);

    const auto historical_manifest = std::string{first.manifest_bytes()};
    auto changed = fixture.builder("changed project source").build();
    CHECK(changed.build_id() != first.build_id());
    CHECK(changed.bundle_digest() != first.bundle_digest());
    CHECK(std::string{first.manifest_bytes()} == historical_manifest);
}

TEST_CASE("ProjectBundle v2 export selection is explicit typed and exact") {
    const auto fixture = LogicalFixture{};
    auto baseline = fixture.builder().build();
    const auto &logical = descriptor(baseline, volt::io::ArtifactKind::LogicalModel);
    const auto logical_ref = volt::io::ArtifactRef{logical.id(), logical.content_digest()};
    const auto request =
        volt::io::ExportRequest{volt::io::ExportKind::Bom, volt::io::ModelExportTarget{logical_ref},
                                volt::io::ExportRequestSchema{}, volt::io::BomParameters{}};

    auto selected_builder = fixture.builder();
    selected_builder.select_exports({request});
    auto selected = selected_builder.build();

    CHECK(selected.build_id() == baseline.build_id());
    CHECK(selected.bundle_digest() != baseline.bundle_digest());
    CHECK(Json::parse(selected.manifest_bytes()).at("export_selection").size() == 1U);
    const auto &bom = descriptor(selected, volt::io::ArtifactKind::Bom);
    REQUIRE(bom.dependencies().size() == 1U);
    CHECK(bom.dependencies().front() == logical_ref);

    auto duplicate = fixture.builder();
    duplicate.select_exports({request, request});
    CHECK_THROWS_AS(duplicate.build(), volt::KernelError);

    auto stale = fixture.builder();
    stale.select_exports(
        {volt::io::ExportRequest{volt::io::ExportKind::Bom,
                                 volt::io::ModelExportTarget{volt::io::ArtifactRef{
                                     logical.id(), volt::sha256_content_hash("stale")}},
                                 volt::io::ExportRequestSchema{}, volt::io::BomParameters{}}});
    CHECK_THROWS_AS(stale.build(), volt::KernelError);
}

TEST_CASE("ProjectBundle v2 derives one exact complete Board graph and rejects stale pairings") {
    const auto fixture = board_fixture();
    const auto bundle = board_builder(fixture).build();
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
}

TEST_CASE("ProjectBundle v2 publication is atomic immutable and representation stable") {
    const auto fixture = LogicalFixture{};
    const auto bundle = fixture.builder().build();
    const auto temporary = TempDirectory{};
    const auto directory = temporary.path() / "fixture.volt";
    const auto archive = temporary.path() / "fixture.volt.zip";

    bundle.write(directory);
    const auto original = snapshot(directory);
    CHECK(original.size() == bundle.paths().size());
    CHECK_THROWS_AS(bundle.write(directory), volt::KernelError);
    CHECK(snapshot(directory) == original);

    bundle.write(archive, volt::io::ProjectBundleV2Representation::Zip);
    CHECK(read_bytes(archive) == bundle.archive_bytes());
    CHECK_THROWS_AS(bundle.write(archive, volt::io::ProjectBundleV2Representation::Zip),
                    volt::KernelError);
    CHECK(read_bytes(archive) == bundle.archive_bytes());

    const auto empty = temporary.path() / "empty.volt";
    std::filesystem::create_directory(empty);
    bundle.write(empty);
    CHECK(snapshot(empty) == original);

    const auto real_parent = temporary.path() / "real-parent";
    const auto linked_parent = temporary.path() / "linked-parent";
    std::filesystem::create_directory(real_parent);
    auto symlink_error = std::error_code{};
    std::filesystem::create_directory_symlink(real_parent, linked_parent, symlink_error);
    if (symlink_error) {
        CHECK((symlink_error == std::errc::function_not_supported ||
               symlink_error == std::errc::operation_not_supported));
    } else {
        CHECK_THROWS_AS(bundle.write(linked_parent / "linked.volt"), volt::KernelError);
        CHECK_FALSE(std::filesystem::exists(real_parent / "linked.volt"));
    }
}

TEST_CASE("ProjectBundle v2 strong identities reject unsafe or cross-kind values") {
    CHECK_THROWS_AS(volt::io::LogicalInputName{"../project.py"}, volt::KernelError);
    CHECK_THROWS_AS(volt::io::BoardLayerKey{""}, volt::KernelError);
    CHECK_THROWS_AS(
        volt::io::ExportRequestSchema{static_cast<volt::io::ExportRequestSchemaVersion>(99)},
        volt::KernelError);
    CHECK_THROWS_AS(
        (volt::io::ProjectBundleV2Builder{
            volt::io::ProjectIdentity{"fixture", std::nullopt, std::nullopt},
            volt::io::ProjectRunSummary{
                true, volt::io::ProjectStatus::Clean, "default", {"design", "design"}},
            volt::io::LogicalInputName{"project.py"},
            {volt::io::AuthoringInput{volt::io::AuthoringInputKind::ProjectSource,
                                      volt::io::LogicalInputName{"project.py"}, "source"}},
            volt::io::ProjectReport{
                R"({"status":"clean","summary":{"errors":0,"warnings":0,"infos":0},"diagnostics":[],"expected":[],"unexpected":[],"missing_expected":[]})"},
            volt::io::ProjectReport{R"({"summary":{"passed":0,"failed":0},"tests":[]})"}}),
        volt::KernelError);
    for (const auto &path :
         {std::string{"/absolute.json"}, std::string{"../escape.json"},
          std::string{"C:/drive.json"}, std::string{"unc\\path.json"},
          std::string{"bad//path.json"}, std::string{"trailing./file.json"},
          std::string{"NUL.json"}, std::string{"artifacts/COM1.bin"},
          std::string{"unicode/\xc3\xa9.json"}, std::string{"nul\0byte.json", 13U}}) {
        CHECK_THROWS_AS(volt::io::RelativeBundlePath{path}, volt::KernelError);
    }
    CHECK_THROWS_AS(
        (volt::io::ArtifactId{volt::io::ArtifactKind::BoardModel,
                              volt::io::LogicalArtifactIdentity{volt::io::DesignKey{"main"}}}),
        volt::KernelError);

    const auto id =
        volt::io::ArtifactId{volt::io::ArtifactKind::LogicalModel,
                             volt::io::LogicalArtifactIdentity{volt::io::DesignKey{"main"}}};
    CHECK_THROWS_AS((volt::io::ArtifactDescriptor{id,
                                                  volt::io::ArtifactRole::Model,
                                                  volt::io::ArtifactKind::LogicalModel,
                                                  "wrong.format",
                                                  1,
                                                  "application/json",
                                                  volt::io::RelativeBundlePath{"../escape"},
                                                  volt::sha256_content_hash(""),
                                                  {},
                                                  "producer",
                                                  1,
                                                  volt::sha256_content_hash("producer")}),
                    volt::KernelError);

    CHECK_THROWS_AS(
        (volt::io::ArtifactDescriptor{id,
                                      volt::io::ArtifactRole::Model,
                                      volt::io::ArtifactKind::LogicalModel,
                                      "volt.logical_circuit",
                                      1,
                                      "application/vnd.volt.logical+json",
                                      volt::io::RelativeBundlePath{"MANIFEST.VOLT.JSON"},
                                      volt::sha256_content_hash(""),
                                      {},
                                      "producer",
                                      1,
                                      volt::sha256_content_hash("producer")}),
        volt::KernelError);
}
