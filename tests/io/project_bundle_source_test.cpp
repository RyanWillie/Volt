#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../../src/io/project_bundle_source.hpp"

namespace {

class TempDirectory final {
  public:
    TempDirectory() {
        static auto counter = std::atomic<std::uint64_t>{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("volt-project-bundle-source-" + std::to_string(stamp) + "-" +
                 std::to_string(counter.fetch_add(1U)));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() {
        auto ignored = std::error_code{};
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path &path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
};

void write_file(const std::filesystem::path &path, std::string_view bytes) {
    auto output = std::ofstream{path, std::ios::binary};
    REQUIRE(output);
    output << bytes;
    REQUIRE(output.good());
}

} // namespace

#ifndef _WIN32
TEST_CASE("Directory bundle inventory remains pinned after its pathname is replaced") {
    const auto temporary = TempDirectory{};
    const auto root = temporary.path() / "bundle.volt";
    const auto retained = temporary.path() / "retained.volt";
    std::filesystem::create_directory(root);
    write_file(root / "original.txt", "original");
    const auto source = volt::io::detail::open_project_bundle_source(root);

    std::filesystem::rename(root, retained);
    std::filesystem::create_directory(root);
    write_file(root / "replacement.txt", "replacement");

    CHECK(source->paths() == std::vector<std::string>{"original.txt"});
    CHECK(source->read("original.txt").bytes == "original");
}
#endif
