#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <zlib.h>

#include <volt/io/project_bundle.hpp>

namespace {

using Json = nlohmann::json;

class TempDirectory final {
  public:
    TempDirectory() {
        static auto counter = std::atomic<std::uint64_t>{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("volt-project-bundle-" + std::to_string(stamp) + "-" +
                 std::to_string(counter.fetch_add(1U)));
        std::filesystem::create_directories(path_);
    }

    TempDirectory(const TempDirectory &) = delete;
    TempDirectory &operator=(const TempDirectory &) = delete;

    ~TempDirectory() {
        auto unused = std::error_code{};
        std::filesystem::remove_all(path_, unused);
    }

    [[nodiscard]] const std::filesystem::path &path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
};

[[nodiscard]] std::filesystem::path fixture_root() {
    return std::filesystem::path{VOLT_TEST_FIXTURE_DIR}.parent_path().parent_path() / "examples" /
           "pcb_led_board" / "artifacts" / "pcb_led_board.volt";
}

[[nodiscard]] std::vector<std::filesystem::path> existing_fixture_roots() {
    const auto examples =
        std::filesystem::path{VOLT_TEST_FIXTURE_DIR}.parent_path().parent_path() / "examples";
    return {
        examples / "pcb_led_board" / "artifacts" / "pcb_led_board.volt",
        examples / "stm32_usb_buck" / "artifacts" / "stm32_usb_buck.volt",
        examples / "timer_555_led_blinker" / "artifacts" / "timer_555_led_blinker.volt",
    };
}

[[nodiscard]] std::string read_bytes(const std::filesystem::path &path) {
    auto input = std::ifstream{path, std::ios::binary};
    REQUIRE(input);
    return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void write_bytes(const std::filesystem::path &path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
    REQUIRE(output);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
}

[[nodiscard]] Json read_json(const std::filesystem::path &path) {
    return Json::parse(read_bytes(path));
}

void write_json(const std::filesystem::path &path, const Json &value) {
    write_bytes(path, value.dump(2) + "\n");
}

[[nodiscard]] std::filesystem::path copy_fixture(const TempDirectory &temporary) {
    const auto destination = temporary.path() / "fixture.volt";
    std::filesystem::copy(fixture_root(), destination, std::filesystem::copy_options::recursive);
    return destination;
}

[[nodiscard]] std::map<std::string, std::string>
regular_file_snapshot(const std::filesystem::path &root) {
    auto result = std::map<std::string, std::string>{};
    for (const auto &entry : std::filesystem::recursive_directory_iterator{root}) {
        if (entry.is_regular_file()) {
            result.emplace(entry.path().lexically_relative(root).generic_string(),
                           read_bytes(entry.path()));
        }
    }
    return result;
}

[[nodiscard]] std::optional<volt::io::ProjectBundleOpenErrorCode>
open_error(const std::filesystem::path &path) {
    try {
        static_cast<void>(volt::io::ProjectBundle::open(path));
    } catch (const volt::io::ProjectBundleOpenError &error) {
        return error.code();
    }
    return std::nullopt;
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

void overwrite_u32(std::string &bytes, std::size_t offset, std::uint32_t value) {
    REQUIRE(offset + 4U <= bytes.size());
    for (auto index = 0U; index < 4U; ++index) {
        bytes[offset + index] = static_cast<char>((value >> (8U * index)) & 0xffU);
    }
}

[[nodiscard]] std::uint32_t crc32_bytes(std::string_view bytes) {
    auto result = std::uint32_t{0xffffffffU};
    for (const auto value : bytes) {
        result ^= static_cast<unsigned char>(value);
        for (auto bit = 0U; bit < 8U; ++bit) {
            const auto mask = static_cast<std::uint32_t>(-static_cast<std::int32_t>(result & 1U));
            result = (result >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~result;
}

struct ZipEntry {
    std::string path;
    std::string bytes;
    std::uint16_t unix_mode = 0100644U;
};

[[nodiscard]] std::string deflate_raw(std::string_view bytes) {
    auto stream = z_stream{};
    REQUIRE(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                         Z_DEFAULT_STRATEGY) == Z_OK);
    auto compressed = std::string(deflateBound(&stream, static_cast<uLong>(bytes.size())), '\0');
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(bytes.data()));
    stream.avail_in = static_cast<uInt>(bytes.size());
    stream.next_out = reinterpret_cast<Bytef *>(compressed.data());
    stream.avail_out = static_cast<uInt>(compressed.size());
    REQUIRE(deflate(&stream, Z_FINISH) == Z_STREAM_END);
    compressed.resize(stream.total_out);
    REQUIRE(deflateEnd(&stream) == Z_OK);
    return compressed;
}

[[nodiscard]] std::vector<ZipEntry> fixture_zip_entries(const std::filesystem::path &root) {
    auto result = std::vector<ZipEntry>{};
    for (const auto &entry : std::filesystem::recursive_directory_iterator{root}) {
        if (entry.is_regular_file()) {
            result.push_back(ZipEntry{entry.path().lexically_relative(root).generic_string(),
                                      read_bytes(entry.path())});
        }
    }
    std::ranges::sort(result, {}, &ZipEntry::path);
    return result;
}

[[nodiscard]] std::string encode_zip(const std::vector<ZipEntry> &entries,
                                     bool use_deflate = false) {
    struct CentralRecord {
        ZipEntry entry;
        std::string compressed;
        std::uint32_t crc;
        std::uint32_t local_offset;
    };

    auto bytes = std::string{};
    auto central = std::vector<CentralRecord>{};
    for (const auto &entry : entries) {
        REQUIRE(entry.path.size() <= std::numeric_limits<std::uint16_t>::max());
        REQUIRE(entry.bytes.size() <= std::numeric_limits<std::uint32_t>::max());
        const auto compressed = use_deflate ? deflate_raw(entry.bytes) : entry.bytes;
        REQUIRE(compressed.size() <= std::numeric_limits<std::uint32_t>::max());
        const auto offset = static_cast<std::uint32_t>(bytes.size());
        const auto crc = crc32_bytes(entry.bytes);
        append_u32(bytes, 0x04034b50U);
        append_u16(bytes, 20U);
        append_u16(bytes, 0x0800U);
        append_u16(bytes, use_deflate ? 8U : 0U);
        append_u16(bytes, 0U);
        append_u16(bytes, 0U);
        append_u32(bytes, crc);
        append_u32(bytes, static_cast<std::uint32_t>(compressed.size()));
        append_u32(bytes, static_cast<std::uint32_t>(entry.bytes.size()));
        append_u16(bytes, static_cast<std::uint16_t>(entry.path.size()));
        append_u16(bytes, 0U);
        bytes.append(entry.path);
        bytes.append(compressed);
        central.push_back(CentralRecord{entry, compressed, crc, offset});
    }
    const auto central_offset = static_cast<std::uint32_t>(bytes.size());
    for (const auto &record : central) {
        append_u32(bytes, 0x02014b50U);
        append_u16(bytes, static_cast<std::uint16_t>((3U << 8U) | 20U));
        append_u16(bytes, 20U);
        append_u16(bytes, 0x0800U);
        append_u16(bytes, use_deflate ? 8U : 0U);
        append_u16(bytes, 0U);
        append_u16(bytes, 0U);
        append_u32(bytes, record.crc);
        append_u32(bytes, static_cast<std::uint32_t>(record.compressed.size()));
        append_u32(bytes, static_cast<std::uint32_t>(record.entry.bytes.size()));
        append_u16(bytes, static_cast<std::uint16_t>(record.entry.path.size()));
        append_u16(bytes, 0U);
        append_u16(bytes, 0U);
        append_u16(bytes, 0U);
        append_u16(bytes, 0U);
        append_u32(bytes, static_cast<std::uint32_t>(record.entry.unix_mode) << 16U);
        append_u32(bytes, record.local_offset);
        bytes.append(record.entry.path);
    }
    const auto central_size = static_cast<std::uint32_t>(bytes.size()) - central_offset;
    append_u32(bytes, 0x06054b50U);
    append_u16(bytes, 0U);
    append_u16(bytes, 0U);
    append_u16(bytes, static_cast<std::uint16_t>(central.size()));
    append_u16(bytes, static_cast<std::uint16_t>(central.size()));
    append_u32(bytes, central_size);
    append_u32(bytes, central_offset);
    append_u16(bytes, 0U);
    return bytes;
}

[[nodiscard]] std::filesystem::path write_fixture_zip(const TempDirectory &temporary,
                                                      std::vector<ZipEntry> entries,
                                                      bool use_deflate = false) {
    const auto archive =
        temporary.path() / (use_deflate ? "fixture-deflated.volt.zip" : "fixture.volt.zip");
    write_bytes(archive, encode_zip(entries, use_deflate));
    return archive;
}

[[nodiscard]] std::set<volt::io::ProjectBundleV2Meaning> unavailable_meanings() {
    using Meaning = volt::io::ProjectBundleV2Meaning;
    return {
        Meaning::ArtifactGraph,
        Meaning::ArtifactRoles,
        Meaning::ArtifactKinds,
        Meaning::DependencyEdges,
        Meaning::DependencyLock,
        Meaning::BuildId,
        Meaning::AuthoringInputsDigest,
        Meaning::BundleDigest,
        Meaning::CompleteArtifactDigests,
        Meaning::CompiledBoards,
        Meaning::BoardScenes,
        Meaning::SelectedClosure,
        Meaning::CapabilityRecords,
        Meaning::VerifiedProvenance,
    };
}

} // namespace

TEST_CASE("ProjectBundle reopens an existing v1 directory truthfully and without mutation") {
    const auto before = regular_file_snapshot(fixture_root());
    auto bundle = volt::io::ProjectBundle::open(fixture_root());

    CHECK(bundle.schema_version() == volt::io::ProjectBundleSchemaVersion::V1);
    CHECK(bundle.storage_kind() == volt::io::ProjectBundleStorageKind::Directory);
    CHECK(bundle.integrity_status() == volt::io::BundleIntegrityStatus::LegacyUnverified);
    CHECK(std::holds_alternative<volt::io::LegacyProjectBundleV1View>(bundle.view()));

    const auto legacy = bundle.legacy_v1();
    CHECK(legacy.project_name() == "pcb-led-board");
    CHECK_FALSE(legacy.project_version().has_value());
    CHECK(legacy.project_description() == "First-board LED PCB example");
    CHECK(legacy.integrity_status() == volt::io::BundleIntegrityStatus::LegacyUnverified);
    CHECK(legacy.circuits().size() == 1U);
    CHECK(legacy.schematics().size() == 1U);
    CHECK(legacy.boards().size() == 1U);
    CHECK(legacy.circuits().front().design() == "pcb-led-board");
    CHECK(legacy.schematics().front().design() == "pcb-led-board");
    CHECK(legacy.schematics().front().schematic() == "First Board LED");
    CHECK(legacy.schematics().front().circuit().design() == "pcb-led-board");
    CHECK(legacy.boards().front().design() == "pcb-led-board");
    CHECK(legacy.boards().front().board() == "First Board LED");
    CHECK(legacy.boards().front().circuit().design() == "pcb-led-board");
    CHECK(legacy.manifest_bytes() == read_bytes(fixture_root() / "manifest.volt.json"));

    const auto artifacts = legacy.artifacts();
    REQUIRE(artifacts.size() == 17U);
    auto paths = std::vector<std::string>{};
    paths.reserve(artifacts.size());
    std::ranges::transform(artifacts, std::back_inserter(paths),
                           [](const auto &artifact) { return artifact.path(); });
    CHECK(std::ranges::is_sorted(paths));
    const auto asset = std::ranges::find_if(
        artifacts, [](const auto &artifact) { return artifact.recorded_sha256().has_value(); });
    REQUIRE(asset != artifacts.end());
    CHECK(asset->recorded_sha256() ==
          "4b91321ef3df9390eb915bd133820f68e31cb54c88ce033e9445ceeec00eb8bf");
    CHECK_FALSE(asset->bytes().empty());
    CHECK(Json::parse(asset->manifest_record_json()).at("path") == asset->path());
    CHECK(regular_file_snapshot(fixture_root()) == before);
}

TEST_CASE("ProjectBundle reopens every committed supported v1 fixture") {
    for (const auto &root : existing_fixture_roots()) {
        INFO(root.string());
        const auto view = volt::io::ProjectBundle::open(root).legacy_v1();
        CHECK(view.integrity_status() == volt::io::BundleIntegrityStatus::LegacyUnverified);
        CHECK(view.circuits().size() == 1U);
        CHECK(view.schematics().size() == 1U);
        CHECK(view.boards().size() == 1U);
    }
}

TEST_CASE("ProjectBundle storage leases remain valid across owner moves and destruction") {
    auto retained_board = [&] {
        auto bundle = volt::io::ProjectBundle::open(fixture_root());
        auto moved = std::move(bundle);
        auto reassigned = volt::io::ProjectBundle::open(fixture_root());
        reassigned = std::move(moved);
        return reassigned.legacy_v1().boards().front();
    }();

    CHECK(retained_board.board() == "First Board LED");
    CHECK(retained_board.circuit().design() == "pcb-led-board");
    CHECK_FALSE(retained_board.artifact().bytes().empty());
}

TEST_CASE("ProjectBundle v1 exposes typed unavailable results for every absent v2 meaning") {
    const auto legacy = volt::io::ProjectBundle::open(fixture_root()).legacy_v1();
    for (const auto meaning : unavailable_meanings()) {
        CHECK(legacy.availability(meaning) ==
              volt::io::ProjectBundleMeaningAvailability::UnavailableInV1);
        try {
            legacy.require(meaning);
            FAIL("Legacy view must reject v2-only meaning");
        } catch (const volt::io::ProjectBundleUnavailableError &error) {
            CHECK(error.schema_version() == volt::io::ProjectBundleSchemaVersion::V1);
            CHECK(error.meaning() == meaning);
            const auto message = std::string_view{error.what()};
            const auto label = message.find("meaning ");
            REQUIRE(label != std::string_view::npos);
            REQUIRE(message.size() > label + std::string_view{"meaning "}.size());
            CHECK(message[label + std::string_view{"meaning "}.size()] >= 'a');
            CHECK(message[label + std::string_view{"meaning "}.size()] <= 'z');
        }
    }
    try {
        static_cast<void>(volt::io::ProjectBundle::open(fixture_root()).require_v2());
        FAIL("Legacy ProjectBundle must not synthesize a v2 graph");
    } catch (const volt::io::ProjectBundleUnavailableError &error) {
        CHECK(error.meaning() == volt::io::ProjectBundleV2Meaning::ArtifactGraph);
        CHECK(std::string_view{error.what()}.ends_with("meaning artifact_graph"));
    }
}

TEST_CASE("ProjectBundle reopens the same existing v1 fixture from a ZIP archive") {
    for (const auto use_deflate : {false, true}) {
        auto temporary = TempDirectory{};
        const auto archive =
            write_fixture_zip(temporary, fixture_zip_entries(fixture_root()), use_deflate);

        const auto bundle = volt::io::ProjectBundle::open(archive);
        CHECK(bundle.storage_kind() == volt::io::ProjectBundleStorageKind::ZipArchive);
        CHECK(bundle.integrity_status() == volt::io::BundleIntegrityStatus::LegacyUnverified);
        CHECK(bundle.legacy_v1().circuits().size() == 1U);
        CHECK(bundle.legacy_v1().schematics().size() == 1U);
        CHECK(bundle.legacy_v1().boards().size() == 1U);
        CHECK(bundle.legacy_v1().manifest_bytes() ==
              read_bytes(fixture_root() / "manifest.volt.json"));
    }
}

TEST_CASE("ProjectBundle open is offline data-only and ignores undeclared source") {
    auto temporary = TempDirectory{};
    const auto root = copy_fixture(temporary);
    const auto sentinel = temporary.path() / "source-executed";
    write_bytes(root / "project.py", "from pathlib import Path\nPath(" + sentinel.string() +
                                         ").write_text('executed')\n");

    const auto before = regular_file_snapshot(root);
    const auto bundle = volt::io::ProjectBundle::open(root);
    CHECK(bundle.legacy_v1().project_name() == "pcb-led-board");
    CHECK_FALSE(std::filesystem::exists(sentinel));
    CHECK(regular_file_snapshot(root) == before);
}

TEST_CASE("ProjectBundle fail-closed dispatch rejects incomplete v2 and unknown schemas") {
    {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        write_bytes(root / "manifest.volt.json",
                    R"({"format":"volt.project_result","schema_version":2,"v2_only":true})");
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::MalformedManifest);
    }
    for (const auto schema : {3U}) {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        auto manifest = read_json(root / "manifest.volt.json");
        manifest["schema_version"] = schema;
        write_json(root / "manifest.volt.json", manifest);
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::UnsupportedSchema);
    }

    auto temporary = TempDirectory{};
    const auto root = copy_fixture(temporary);
    auto manifest = read_json(root / "manifest.volt.json");
    manifest["format"] = "volt.not-a-project";
    write_json(root / "manifest.volt.json", manifest);
    CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::UnsupportedFormat);
}

TEST_CASE("ProjectBundle rejects unsafe duplicate colliding and aliased directory paths") {
    const auto logical_path = std::string{"logical/pcb-led-board.volt.json"};
    const auto unsafe_paths = std::vector<std::string>{
        "../logical.json",       "/absolute.json",
        "C:/drive.json",         R"(logical\board.json)",
        "logical//board.json",   "logical/./board.json",
        "logical/../board.json", std::string{"logical/control"} + static_cast<char>(1) + ".json",
    };
    for (const auto &unsafe : unsafe_paths) {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        auto manifest = read_json(root / "manifest.volt.json");
        manifest["artifacts"][0]["path"] = unsafe;
        write_json(root / "manifest.volt.json", manifest);
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::UnsafePath);
    }

    SECTION("duplicate byte path") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        auto manifest = read_json(root / "manifest.volt.json");
        manifest["artifacts"].push_back(manifest["artifacts"][0]);
        manifest["artifacts"].back()["kind"] = "opaque";
        manifest["artifacts"].back()["name"] = "other";
        write_json(root / "manifest.volt.json", manifest);
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::DuplicateIdentity);
    }

    SECTION("duplicate manifest identity") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        auto manifest = read_json(root / "manifest.volt.json");
        auto duplicate = manifest["artifacts"][0];
        duplicate["path"] = "logical/second.volt.json";
        write_bytes(root / "logical" / "second.volt.json", read_bytes(root / logical_path));
        manifest["artifacts"].push_back(std::move(duplicate));
        write_json(root / "manifest.volt.json", manifest);
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::DuplicateIdentity);
    }

    SECTION("file-parent collision") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        auto manifest = read_json(root / "manifest.volt.json");
        auto collision = manifest["artifacts"][0];
        collision["kind"] = "opaque";
        collision["name"] = "parent";
        collision["path"] = "logical";
        manifest["artifacts"].push_back(std::move(collision));
        write_json(root / "manifest.volt.json", manifest);
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::DuplicateIdentity);
    }

    SECTION("file-parent collision with an interposed lexical sibling") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        auto manifest = read_json(root / "manifest.volt.json");
        auto parent = manifest["artifacts"][0];
        parent["kind"] = "opaque";
        parent["name"] = "parent";
        parent["path"] = "logical";
        auto sibling = parent;
        sibling["name"] = "sibling";
        sibling["path"] = "logical.txt";
        manifest["artifacts"].push_back(std::move(parent));
        manifest["artifacts"].push_back(std::move(sibling));
        write_json(root / "manifest.volt.json", manifest);
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::DuplicateIdentity);
    }

    SECTION("symbolic link traversal") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        const auto logical = root / logical_path;
        const auto target = temporary.path() / "logical-target.json";
        std::filesystem::rename(logical, target);
        auto error = std::error_code{};
        std::filesystem::create_symlink(target, logical, error);
        if (error) {
            WARN("Host could not create the symlink fixture: " << error.message());
        } else {
            CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::UnsafePath);
        }
    }

    SECTION("hard-link alias") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        const auto alias_path = root / "logical" / "alias.volt.json";
        std::filesystem::create_hard_link(root / logical_path, alias_path);
        auto manifest = read_json(root / "manifest.volt.json");
        manifest["artifacts"].push_back({{"kind", "opaque"},
                                         {"name", "logical alias"},
                                         {"path", "logical/alias.volt.json"},
                                         {"media_type", "application/octet-stream"}});
        write_json(root / "manifest.volt.json", manifest);
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::DuplicateIdentity);
    }

    SECTION("manifest hard-link alias") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        const auto alias_path = root / "logical" / "manifest-alias.json";
        std::filesystem::create_hard_link(root / "manifest.volt.json", alias_path);
        auto manifest = read_json(root / "manifest.volt.json");
        manifest["artifacts"].push_back({{"kind", "opaque"},
                                         {"name", "manifest alias"},
                                         {"path", "logical/manifest-alias.json"},
                                         {"media_type", "application/octet-stream"}});
        write_json(root / "manifest.volt.json", manifest);
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::DuplicateIdentity);
    }

    SECTION("legacy UTF-8 path remains accepted") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        auto manifest = read_json(root / "manifest.volt.json");
        const auto record = std::ranges::find_if(
            manifest["artifacts"], [](const Json &value) { return value.at("kind") == "bom_csv"; });
        REQUIRE(record != manifest["artifacts"].end());
        const auto old_path = root / record->at("path").get<std::string>();
        const auto new_relative = std::string{"bom/données.csv"};
        std::filesystem::rename(old_path, root / std::filesystem::path{u8"bom/données.csv"});
        (*record)["path"] = new_relative;
        write_json(root / "manifest.volt.json", manifest);
        CHECK_FALSE(open_error(root).has_value());
    }
}

TEST_CASE("ProjectBundle rejects archive ambiguity unsafe entries and unsupported entry types") {
    auto temporary = TempDirectory{};
    const auto base = fixture_zip_entries(fixture_root());

    SECTION("duplicate archive path") {
        auto entries = base;
        entries.push_back(entries.front());
        CHECK(open_error(write_fixture_zip(temporary, std::move(entries))) ==
              volt::io::ProjectBundleOpenErrorCode::DuplicateIdentity);
    }

    SECTION("unsafe undeclared traversal entry") {
        auto entries = base;
        entries.push_back(ZipEntry{"../escape", "payload"});
        CHECK(open_error(write_fixture_zip(temporary, std::move(entries))) ==
              volt::io::ProjectBundleOpenErrorCode::UnsafePath);
    }

    SECTION("symlink-like entry") {
        auto entries = base;
        entries.push_back(ZipEntry{"link", "manifest.volt.json", 0120777U});
        CHECK(open_error(write_fixture_zip(temporary, std::move(entries))) ==
              volt::io::ProjectBundleOpenErrorCode::MalformedArchive);
    }

    SECTION("recorded CRC mismatch") {
        auto bytes = encode_zip(base);
        REQUIRE(bytes.size() > 20U);
        bytes[20] = static_cast<char>(bytes[20] ^ 0x01);
        const auto archive = temporary.path() / "bad-crc.zip";
        write_bytes(archive, bytes);
        CHECK(open_error(archive) == volt::io::ProjectBundleOpenErrorCode::MalformedArchive);
    }

    SECTION("valid empty archive") {
        CHECK(open_error(write_fixture_zip(temporary, {})) ==
              volt::io::ProjectBundleOpenErrorCode::MissingEntry);
    }

    SECTION("recorded expansion exceeds the bounded entry size") {
        const auto path = std::string{"manifest.volt.json"};
        auto bytes = encode_zip({ZipEntry{path, ""}});
        const auto central_offset = 30U + path.size();
        constexpr auto oversized = std::uint32_t{256U * 1024U * 1024U + 1U};
        overwrite_u32(bytes, 22U, oversized);
        overwrite_u32(bytes, central_offset + 24U, oversized);
        const auto archive = temporary.path() / "oversized-entry.zip";
        write_bytes(archive, bytes);
        CHECK(open_error(archive) == volt::io::ProjectBundleOpenErrorCode::InputTooLarge);
    }
}

TEST_CASE("ProjectBundle verifies every recorded v1 digest and rejects missing bytes") {
    SECTION("digest mismatch") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        const auto asset = root / "assets" / "models" /
                           "4b91321ef3df9390eb915bd133820f68e31cb54c88ce033e9445ceeec00eb8bf.step";
        write_bytes(asset, "tampered");
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::DigestMismatch);
    }

    SECTION("missing declared entry") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        std::filesystem::remove(root / "diagnostics" / "tests.json");
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::MissingEntry);
    }
}

TEST_CASE("ProjectBundle rejects malformed manifests and every supported-model decode failure") {
    SECTION("duplicate JSON key") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        write_bytes(
            root / "manifest.volt.json",
            R"({"format":"volt.project_result","format":"volt.project_result","schema_version":1})");
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::MalformedManifest);
    }

    SECTION("non-string diagnostics status remains a typed manifest error") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        auto manifest = read_json(root / "manifest.volt.json");
        manifest["diagnostics"]["status"] = 42;
        write_json(root / "manifest.volt.json", manifest);
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::MalformedManifest);
    }

    SECTION("logical decode") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        write_bytes(root / "logical" / "pcb-led-board.volt.json", "{}");
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::ModelDecodeFailure);
    }

    SECTION("duplicate JSON key in supported model") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        write_bytes(root / "logical" / "pcb-led-board.volt.json",
                    R"({"format":"volt.logical","format":"volt.logical"})");
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::ModelDecodeFailure);
    }

    SECTION("schematic decode") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        write_bytes(root / "schematic" / "First-Board-LED.volt.schematic.json", "{}");
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::ModelDecodeFailure);
    }

    SECTION("board decode") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        write_bytes(root / "pcb" / "First-Board-LED.volt.pcb.json", "{}");
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::ModelDecodeFailure);
    }

    SECTION("foreign logical owner") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        auto manifest = read_json(root / "manifest.volt.json");
        const auto schematic = std::ranges::find_if(manifest["artifacts"], [](const Json &record) {
            return record.at("kind") == "schematic";
        });
        REQUIRE(schematic != manifest["artifacts"].end());
        (*schematic)["group"]["design"] = "foreign";
        write_json(root / "manifest.volt.json", manifest);
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::OwnershipViolation);
    }

    SECTION("foreign projection owner for derived artifact") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        auto manifest = read_json(root / "manifest.volt.json");
        const auto schematic_svg =
            std::ranges::find_if(manifest["artifacts"], [](const Json &record) {
                return record.at("kind") == "schematic_svg";
            });
        REQUIRE(schematic_svg != manifest["artifacts"].end());
        (*schematic_svg)["group"]["schematic"] = "Foreign";
        write_json(root / "manifest.volt.json", manifest);
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::OwnershipViolation);
    }

    SECTION("board manifest and payload identity mismatch") {
        auto temporary = TempDirectory{};
        const auto root = copy_fixture(temporary);
        auto board = read_json(root / "pcb" / "First-Board-LED.volt.pcb.json");
        board["board"]["name"] = "Foreign";
        write_json(root / "pcb" / "First-Board-LED.volt.pcb.json", board);
        CHECK(open_error(root) == volt::io::ProjectBundleOpenErrorCode::OwnershipViolation);
    }
}

TEST_CASE("ProjectBundle enumeration is deterministic across manifest and archive order") {
    auto temporary = TempDirectory{};
    const auto root = copy_fixture(temporary);
    auto manifest = read_json(root / "manifest.volt.json");
    std::ranges::reverse(manifest["artifacts"]);
    write_json(root / "manifest.volt.json", manifest);

    const auto directory = volt::io::ProjectBundle::open(root).legacy_v1();
    auto entries = fixture_zip_entries(root);
    std::ranges::reverse(entries);
    const auto archive =
        volt::io::ProjectBundle::open(write_fixture_zip(temporary, std::move(entries))).legacy_v1();
    const auto paths = [](const auto &view) {
        const auto artifacts = view.artifacts();
        auto result = std::vector<std::string>{};
        result.reserve(artifacts.size());
        std::ranges::transform(artifacts, std::back_inserter(result),
                               [](const auto &artifact) { return artifact.path(); });
        return result;
    };
    CHECK(paths(directory) == paths(archive));
    CHECK(std::ranges::is_sorted(paths(directory)));
}
