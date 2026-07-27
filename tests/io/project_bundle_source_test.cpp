#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
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

void append_u16(std::string &bytes, std::uint16_t value) {
    bytes.push_back(static_cast<char>(value & 0xffU));
    bytes.push_back(static_cast<char>((value >> 8U) & 0xffU));
}

void append_u32(std::string &bytes, std::uint32_t value) {
    for (auto index = 0U; index < 4U; ++index) {
        bytes.push_back(static_cast<char>((value >> (8U * index)) & 0xffU));
    }
}

[[nodiscard]] std::string empty_directory_zip(std::string_view path) {
    auto bytes = std::string{};
    append_u32(bytes, 0x04034b50U);
    append_u16(bytes, 20U);
    append_u16(bytes, 0x0800U);
    append_u16(bytes, 0U);
    append_u16(bytes, 0U);
    append_u16(bytes, 0U);
    append_u32(bytes, 0U);
    append_u32(bytes, 0U);
    append_u32(bytes, 0U);
    append_u16(bytes, static_cast<std::uint16_t>(path.size()));
    append_u16(bytes, 0U);
    bytes.append(path);

    const auto central_offset = static_cast<std::uint32_t>(bytes.size());
    append_u32(bytes, 0x02014b50U);
    append_u16(bytes, static_cast<std::uint16_t>((3U << 8U) | 20U));
    append_u16(bytes, 20U);
    append_u16(bytes, 0x0800U);
    append_u16(bytes, 0U);
    append_u16(bytes, 0U);
    append_u16(bytes, 0U);
    append_u32(bytes, 0U);
    append_u32(bytes, 0U);
    append_u32(bytes, 0U);
    append_u16(bytes, static_cast<std::uint16_t>(path.size()));
    append_u16(bytes, 0U);
    append_u16(bytes, 0U);
    append_u16(bytes, 0U);
    append_u16(bytes, 0U);
    append_u32(bytes, static_cast<std::uint32_t>(0040755U) << 16U);
    append_u32(bytes, 0U);
    bytes.append(path);

    const auto central_size = static_cast<std::uint32_t>(bytes.size()) - central_offset;
    append_u32(bytes, 0x06054b50U);
    append_u16(bytes, 0U);
    append_u16(bytes, 0U);
    append_u16(bytes, 1U);
    append_u16(bytes, 1U);
    append_u32(bytes, central_size);
    append_u32(bytes, central_offset);
    append_u16(bytes, 0U);
    return bytes;
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

TEST_CASE("ZIP bundle inventory retains explicit empty directory entries") {
    const auto temporary = TempDirectory{};
    const auto archive = temporary.path() / "bundle.volt.zip";
    write_file(archive, empty_directory_zip("empty/"));

    const auto source = volt::io::detail::open_project_bundle_source(archive);
    CHECK(source->paths() == std::vector<std::string>{"empty/"});
}
