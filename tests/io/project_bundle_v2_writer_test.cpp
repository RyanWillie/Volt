#include <catch2/catch_test_macros.hpp>

#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/core/errors.hpp>
#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/io/project_bundle_writer.hpp>

namespace {

using Json = nlohmann::json;

class EmptyAssetResolver final : public volt::PartAssetResolver {
  public:
    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &) const override {
        return std::nullopt;
    }
};

class LogicalFixture final {
  public:
    LogicalFixture()
        : bundle_{volt::io::PartLibraryBundle::build(
              volt::PartLibraryBuilder{
                  volt::PartLibraryIdentity{"test.empty", "1", volt::PartLibrarySchemaVersion::V1}},
              {}, resolver_)} {}

    [[nodiscard]] volt::io::ProjectBundleBuilder
    builder(std::string source = "project source",
            std::vector<volt::io::AuthoringInput> extra_inputs = {}) const {
        auto inputs = std::vector<volt::io::AuthoringInput>{};
        inputs.emplace_back(volt::io::AuthoringInputKind::ProjectSource,
                            volt::io::LogicalInputName{"project.py"}, std::move(source));
        inputs.insert(inputs.end(), std::make_move_iterator(extra_inputs.begin()),
                      std::make_move_iterator(extra_inputs.end()));
        auto result = volt::io::ProjectBundleBuilder{
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

    [[nodiscard]] const volt::Circuit &circuit() const noexcept { return circuit_; }

    [[nodiscard]] const volt::io::PartLibraryBundle &bundle() const noexcept { return bundle_; }

  private:
    EmptyAssetResolver resolver_;
    volt::Circuit circuit_;
    volt::io::PartLibraryBundle bundle_;
};

[[nodiscard]] const volt::io::ArtifactDescriptor &
descriptor(const volt::io::ProjectBundlePublication &bundle, volt::io::ArtifactKind kind) {
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

TEST_CASE("ProjectBundle v2 writer derives run status from decoded reports") {
    const auto fixture = LogicalFixture{};
    auto builder = volt::io::ProjectBundleBuilder{
        volt::io::ProjectIdentity{"fixture", std::nullopt, std::nullopt},
        volt::io::ProjectRunSummary{true, volt::io::ProjectStatus::Clean, "default", {"design"}},
        volt::io::LogicalInputName{"project.py"},
        {volt::io::AuthoringInput{volt::io::AuthoringInputKind::ProjectSource,
                                  volt::io::LogicalInputName{"project.py"}, "project source"}},
        volt::io::ProjectReport{
            R"({"status":"clean","summary":{"errors":0,"warnings":1,"infos":0},"diagnostics":[{"stage":"design","source":"test","report":"logical.default","severity":"warning","category":"general","code":"WARN","message":"warning","entities":[],"overlays":[],"measurement":null,"design":"main","board":null,"rule":null,"expect_diagnostic_kwargs":{"code":"WARN","severity":"warning","stage":"design","source":"test","report":"logical.default","design":"main"}}],"expected":[],"unexpected":[{"stage":"design","source":"test","report":"logical.default","severity":"warning","category":"general","code":"WARN","message":"warning","entities":[],"overlays":[],"measurement":null,"design":"main","board":null,"rule":null,"expect_diagnostic_kwargs":{"code":"WARN","severity":"warning","stage":"design","source":"test","report":"logical.default","design":"main"}}],"missing_expected":[]})"},
        volt::io::ProjectReport{R"({"summary":{"passed":0,"failed":0},"tests":[]})"}};
    builder.add_logical(volt::io::DesignKey{"main"}, fixture.circuit(), fixture.bundle());

    CHECK_THROWS_AS(builder.build(), volt::KernelError);
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
