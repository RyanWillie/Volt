#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/io/project_bundle.hpp>
#include <volt/io/project_bundle_v2_writer.hpp>

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
    auto builder = volt::io::ProjectBundleV2Builder{
        volt::io::ProjectIdentity{"unused-definition", std::nullopt, std::nullopt},
        volt::io::ProjectRunSummary{true, volt::io::ProjectStatus::Clean, "default", {"design"}},
        volt::io::LogicalInputName{"project.py"},
        {volt::io::AuthoringInput{volt::io::AuthoringInputKind::ProjectSource,
                                  volt::io::LogicalInputName{"project.py"}, "project source"}},
        volt::io::ProjectReport{
            R"({"status":"clean","summary":{"errors":0,"warnings":0,"infos":0},"diagnostics":[],"expected":[],"unexpected":[],"missing_expected":[]})"},
        volt::io::ProjectReport{R"({"summary":{"passed":0,"failed":0},"tests":[]})"}};
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
