#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/io/project_bundle.hpp>
#include <volt/io/project_bundle_v2_writer.hpp>
#include <volt/io/schematic/schematic_writer.hpp>

namespace {

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
                ("volt-project-bundle-v2-parity-" + std::to_string(stamp) + "-" +
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

[[nodiscard]] volt::io::ProjectBundleV2Builder project_builder(std::string name) {
    return volt::io::ProjectBundleV2Builder{
        volt::io::ProjectIdentity{std::move(name), std::nullopt, std::nullopt},
        volt::io::ProjectRunSummary{true, volt::io::ProjectStatus::Clean, "default", {"design"}},
        volt::io::LogicalInputName{"project.py"},
        {volt::io::AuthoringInput{volt::io::AuthoringInputKind::ProjectSource,
                                  volt::io::LogicalInputName{"project.py"}, "project source"}},
        volt::io::ProjectReport{
            R"({"status":"clean","summary":{"errors":0,"warnings":0,"infos":0},"diagnostics":[],"expected":[],"unexpected":[],"missing_expected":[]})"},
        volt::io::ProjectReport{R"({"summary":{"passed":0,"failed":0},"tests":[]})"}};
}

} // namespace

TEST_CASE("ProjectBundle v2 reader matches the writer instance-rooted component closure") {
    auto circuit = volt::Circuit{};
    static_cast<void>(circuit.define_component(volt::ComponentSpec{
        .name = "Unused imported definition",
        .pins = {volt::PinSpec{.name = "A", .number = "1"}},
        .source = volt::DefinitionSource{"external.library", "unused", "1"},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{"external.library/unused@1"},
                .pin_keys = {volt::PinKey{"A"}},
            },
    }));
    auto resolver = EmptyAssetResolver{};
    const auto library = volt::io::PartLibraryBundle::build(
        volt::PartLibraryBuilder{
            volt::PartLibraryIdentity{"test.empty", "1", volt::PartLibrarySchemaVersion::V1}},
        {}, resolver);
    auto builder = project_builder("unused-definition");
    builder.add_logical(volt::io::DesignKey{"main"}, circuit, library);
    const auto bundle = builder.build();
    const auto temporary = TempDirectory{};
    const auto path = temporary.path() / "unused-definition.volt";
    bundle.write(path);

    const auto reopened = volt::io::ProjectBundle::open(path);
    const auto circuits = reopened.require_v2().loaded_project().circuits();
    REQUIRE(circuits.size() == 1U);
    CHECK(circuits.front().model().all<volt::ComponentDefId>().size() == 1U);
    CHECK(circuits.front().model().all<volt::ComponentId>().size() == 0U);
    const auto logical_artifact = circuits.front().artifact();
    CHECK(logical_artifact.descriptor().dependencies().empty());
}

TEST_CASE("ProjectBundle v2 resolves equal component identities within each logical origin") {
    const auto spec = volt::ComponentSpec{
        .name = "Shared imported definition",
        .pins = {volt::PinSpec{.name = "A", .number = "1"}},
        .source = volt::DefinitionSource{"shared.source", "component", "1"},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{"shared.source/component@1"},
                .pin_keys = {volt::PinKey{"A"}},
            },
    };
    auto first = volt::Circuit{};
    const auto first_definition = first.define_component(spec);
    static_cast<void>(first.instantiate_component(
        first_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}}));
    auto second = volt::Circuit{};
    const auto second_definition = second.define_component(spec);
    static_cast<void>(second.instantiate_component(
        second_definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}}));

    auto resolver = EmptyAssetResolver{};
    auto first_library = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"library.first", "1", volt::PartLibrarySchemaVersion::V1}};
    first_library.add_component(spec);
    const auto component_roots = std::vector{volt::ComponentKey{"shared.source/component@1"}};
    const auto first_bundle = volt::io::PartLibraryBundle::build_with_component_roots(
        first_library, {}, component_roots, resolver);
    auto second_library = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"library.second", "1", volt::PartLibrarySchemaVersion::V1}};
    second_library.add_component(spec);
    const auto second_bundle = volt::io::PartLibraryBundle::build_with_component_roots(
        second_library, {}, component_roots, resolver);

    auto builder = project_builder("two-library-origins");
    builder.add_logical(volt::io::DesignKey{"first"}, first, first_bundle);
    builder.add_logical(volt::io::DesignKey{"second"}, second, second_bundle);
    const auto bundle = builder.build();
    const auto temporary = TempDirectory{};
    const auto path = temporary.path() / "two-library-origins.volt";
    bundle.write(path);

    const auto circuits =
        volt::io::ProjectBundle::open(path).require_v2().loaded_project().circuits();
    REQUIRE(circuits.size() == 2U);
    for (const auto &circuit : circuits) {
        const auto artifact = circuit.artifact();
        REQUIRE(artifact.descriptor().dependencies().size() == 1U);
        const auto &owner = std::get<volt::io::LibraryComponentRef>(
            artifact.descriptor().dependencies().front().artifact().owner());
        CHECK(owner.library_namespace == "library." + circuit.design().value());
    }
}

TEST_CASE("ProjectBundle v2 regenerates logical and Schematic exports on reopen") {
    auto circuit = volt::Circuit{};
    auto schematic = volt::Schematic{circuit};
    auto resolver = EmptyAssetResolver{};
    const auto library = volt::io::PartLibraryBundle::build(
        volt::PartLibraryBuilder{
            volt::PartLibraryIdentity{"test.empty", "1", volt::PartLibrarySchemaVersion::V1}},
        {}, resolver);
    auto baseline_builder = project_builder("logical-exports");
    baseline_builder.add_logical(volt::io::DesignKey{"main"}, circuit, library);
    baseline_builder.add_schematic(volt::io::DesignKey{"main"}, volt::io::SchematicKey{"main"},
                                   schematic);
    const auto baseline = baseline_builder.build();
    const auto logical =
        std::ranges::find(baseline.artifacts(), volt::io::ArtifactKind::LogicalModel,
                          &volt::io::ArtifactDescriptor::kind);
    const auto schematic_artifact =
        std::ranges::find(baseline.artifacts(), volt::io::ArtifactKind::SchematicModel,
                          &volt::io::ArtifactDescriptor::kind);
    REQUIRE(logical != baseline.artifacts().end());
    REQUIRE(schematic_artifact != baseline.artifacts().end());

    auto selected_builder = project_builder("logical-exports");
    selected_builder.add_logical(volt::io::DesignKey{"main"}, circuit, library);
    selected_builder.add_schematic(volt::io::DesignKey{"main"}, volt::io::SchematicKey{"main"},
                                   schematic);
    selected_builder.select_exports(
        {volt::io::ExportRequest{volt::io::ExportKind::Bom,
                                 volt::io::ModelExportTarget{volt::io::ArtifactRef{
                                     logical->id(), logical->content_digest()}},
                                 volt::io::ExportRequestSchema{}, volt::io::BomParameters{}},
         volt::io::ExportRequest{
             volt::io::ExportKind::SchematicSvg,
             volt::io::ModelExportTarget{volt::io::ArtifactRef{
                 schematic_artifact->id(), schematic_artifact->content_digest()}},
             volt::io::ExportRequestSchema{}, volt::io::SchematicSvgParameters{}}});
    const auto selected = selected_builder.build();
    const auto temporary = TempDirectory{};
    const auto path = temporary.path() / "logical-exports.volt";
    selected.write(path);

    CHECK(volt::io::ProjectBundle::open(path)
              .require_v2()
              .loaded_project()
              .selected_exports()
              .size() == 2U);
}
