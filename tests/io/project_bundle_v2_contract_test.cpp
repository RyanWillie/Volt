#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <volt/core/errors.hpp>
#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/io/project_bundle.hpp>
#include <volt/io/project_bundle_writer.hpp>
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

  private:
    EmptyAssetResolver resolver_;
    volt::Circuit circuit_;
    volt::io::PartLibraryBundle bundle_;
};

} // namespace

TEST_CASE("ProjectBundle v2 publication is atomic immutable and representation stable") {
    const auto fixture = LogicalFixture{};
    auto schematic = volt::Schematic{fixture.circuit()};
    auto builder = fixture.builder();
    builder.add_schematic(volt::io::DesignKey{"main"}, volt::io::SchematicKey{"main"}, schematic);
    const auto bundle = builder.build();
    const auto temporary = TempDirectory{};
    const auto directory = temporary.path() / "fixture.volt";
    const auto archive = temporary.path() / "fixture.volt.zip";

    bundle.write(directory);
    const auto original = snapshot(directory);
    const auto reopened_directory = volt::io::ProjectBundle::open(directory);
    CHECK(reopened_directory.schema_version() == volt::io::ProjectBundleSchemaVersion::V2);
    CHECK(reopened_directory.integrity_status() == volt::io::BundleIntegrityStatus::Verified);
    const auto directory_view = reopened_directory.graph();
    CHECK(directory_view.build_id() == bundle.build_id());
    CHECK(directory_view.bundle_digest() == bundle.bundle_digest());
    CHECK(directory_view.artifacts().size() == bundle.artifacts().size());
    CHECK(directory_view.loaded_project().circuits().size() == 1U);
    CHECK(directory_view.loaded_project().schematics().size() == 1U);
    CHECK(original.size() == bundle.paths().size());
    CHECK_THROWS_AS(bundle.write(directory), volt::KernelError);
    CHECK(snapshot(directory) == original);

    bundle.write(archive, volt::io::ProjectBundleRepresentation::Zip);
    const auto reopened_archive = volt::io::ProjectBundle::open(archive);
    const auto archive_view = reopened_archive.graph();
    CHECK(archive_view.bundle_digest() == bundle.bundle_digest());
    CHECK(read_bytes(archive) == bundle.archive_bytes());
    CHECK_THROWS_AS(bundle.write(archive, volt::io::ProjectBundleRepresentation::Zip),
                    volt::KernelError);
    CHECK(read_bytes(archive) == bundle.archive_bytes());

    const auto empty = temporary.path() / "empty.volt";
    std::filesystem::create_directory(empty);
    CHECK_THROWS_AS(bundle.write(empty), volt::KernelError);
    CHECK(snapshot(empty).empty());

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
        (volt::io::ProjectBundleBuilder{
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
