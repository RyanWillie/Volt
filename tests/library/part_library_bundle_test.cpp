#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/circuit/circuit.hpp>
#include <volt/core/errors.hpp>
#include <volt/io/parts/part_library_bundle.hpp>

namespace {

constexpr auto exact_asset_bytes = std::string_view{"p6-exact-asset-bytes"};

struct ComponentFixture {
    volt::ComponentSpec spec;
    volt::ComponentDefinition definition;
};

[[nodiscard]] ComponentFixture component(std::string key = "test.component/led@1") {
    auto spec = volt::ComponentSpec{
        .name = "LED",
        .pins = {volt::PinSpec{.name = "A", .number = "2"},
                 volt::PinSpec{.name = "K", .number = "1"}},
        .source = volt::DefinitionSource{"test.components", "led", "1.0.0"},
        .schematic_symbols = {volt::SchematicSymbolReference{"test.symbols:led"}},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{std::move(key)},
                .pin_keys = {volt::PinKey{"A"}, volt::PinKey{"K"}},
            },
    };
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(spec);
    return ComponentFixture{std::move(spec), circuit.get(definition)};
}

[[nodiscard]] volt::PartDefinition part(const volt::ComponentDefinition &component, std::string key,
                                        std::string manufacturer, bool with_evidence = false) {
    const auto digest = volt::sha256_content_hash(exact_asset_bytes);
    auto records = volt::ElectricalRecordSet{2};
    if (with_evidence) {
        records = records.with_record(volt::voltage_record(
            volt::ElectricalSubject::directed_relation(volt::ElectricalPinIndex{0},
                                                       volt::ElectricalPinIndex{1}),
            volt::ElectricalMeaning::Characteristic,
            volt::ElectricalValue{volt::Quantity{volt::UnitDimension::Voltage, 2.0}}, {},
            {volt::sha256_content_hash("evidence-bytes")}));
    }
    return volt::PartDefinition{
        component,
        volt::PartIdentity{"test.parts", key, "1.0.0"},
        std::move(records),
        {
            volt::PinPackageTerminalMapping{volt::PinKey{"A"}, {volt::PackageTerminalKey{"2"}}},
            volt::PinPackageTerminalMapping{volt::PinKey{"K"}, {volt::PackageTerminalKey{"1"}}},
        },
        {},
        volt::PartProvenance{"fixture-datasheet", "volt.tests", "native P6 fixture"},
        {volt::PartSchematicAssetReference{"test.symbols:" + key, "default", digest}},
        volt::OrderablePart{
            volt::ManufacturerPart{std::move(manufacturer), "MPN-" + key},
            volt::PackageRef{"0603"},
            volt::HashedFootprintReference{volt::FootprintRef{"TestFootprints", key}, digest},
            {
                volt::PartFootprintPad{"1", -0.5, 0.0, 0.5, 0.5},
                volt::PartFootprintPad{"2", 0.5, 0.0, 0.5, 0.5},
            },
            {
                volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"1"},
                                                {volt::FootprintPadKey{"1"}}},
                volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"2"},
                                                {volt::FootprintPadKey{"2"}}},
            },
            {},
            volt::PartModel3DReference{"glb", "led.glb", digest, {0.0, 0.0, 0.0}, 0.0},
        },
    };
}

[[nodiscard]] std::string asset_map_key(const volt::PartAssetReference &reference) {
    return std::to_string(static_cast<int>(reference.kind())) + ":" + reference.key();
}

class MemoryAssetResolver final : public volt::PartAssetResolver {
  public:
    void add(const volt::PartAssetReference &reference, std::string bytes) {
        assets_.insert_or_assign(asset_map_key(reference), std::move(bytes));
    }

    void add(const volt::PartDefinition &definition) {
        for (const auto &reference : volt::part_asset_references(definition)) {
            add(reference, std::string{exact_asset_bytes});
        }
    }

    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &reference) const override {
        const auto match = assets_.find(asset_map_key(reference));
        return match == assets_.end() ? std::nullopt : std::optional{match->second};
    }

  private:
    std::map<std::string, std::string> assets_;
};

struct BundleFixture {
    ComponentFixture component;
    volt::PartDefinition selected_part;
    volt::PartDefinition unselected_part;
    volt::PartLibraryBuilder builder;
    MemoryAssetResolver resolver;
    std::vector<volt::io::PartLibraryBundleAttachment> attachments;
};

[[nodiscard]] BundleFixture bundle_fixture(bool reverse_parts = false) {
    auto component_fixture = component();
    auto unselected_component = component("test.component/unselected@1");
    auto selected = part(component_fixture.definition, "vendor/A-LED", "Vendor A", true);
    auto unselected = part(unselected_component.definition, "vendor/Z-LED", "Vendor Z");
    auto builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"test.parts", "2026.1", volt::PartLibrarySchemaVersion::V1}};
    if (reverse_parts) {
        builder.add_component(unselected_component.spec);
        builder.add_component(component_fixture.spec);
        builder.add_part(unselected).add_part(selected);
    } else {
        builder.add_component(component_fixture.spec);
        builder.add_component(unselected_component.spec);
        builder.add_part(selected).add_part(unselected);
    }

    auto resolver = MemoryAssetResolver{};
    resolver.add(selected);
    resolver.add(unselected);

    auto attachments = std::vector<volt::io::PartLibraryBundleAttachment>{};
    auto add_attachment = [&](volt::PartAssetKind kind, std::string key, std::string bytes) {
        auto reference =
            volt::PartAssetReference{kind, std::move(key), volt::sha256_content_hash(bytes)};
        resolver.add(reference, std::move(bytes));
        attachments.emplace_back(volt::PartKey{"vendor/A-LED"}, std::move(reference));
    };
    add_attachment(volt::PartAssetKind::Simulation, "sim:vendor/A-LED", "simulation-bytes");
    add_attachment(volt::PartAssetKind::Evidence, "evidence:vendor/A-LED", "evidence-bytes");
    add_attachment(volt::PartAssetKind::Licence, "licence:vendor/A-LED", "licence-bytes");
    add_attachment(volt::PartAssetKind::Provenance, "provenance:vendor/A-LED", "provenance-bytes");

    return BundleFixture{std::move(component_fixture), std::move(selected), std::move(unselected),
                         std::move(builder),           std::move(resolver), std::move(attachments)};
}

template <typename Function>
void check_kernel_error(Function function, volt::ErrorCode expected_code) {
    try {
        function();
        FAIL("Expected a typed kernel error");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == expected_code);
    }
}

template <typename T>
concept CanBorrowBundleBytesFromTemporary = requires(T &&value) { std::forward<T>(value).bytes(); };

template <typename T>
concept CanBorrowBundleDigestFromTemporary =
    requires(T &&value) { std::forward<T>(value).digest(); };

template <typename T>
concept CanBorrowBundleLibraryFromTemporary =
    requires(T &&value) { std::forward<T>(value).library(); };

template <typename T>
concept CanBorrowBundleEntriesFromTemporary =
    requires(T &&value) { std::forward<T>(value).entries(); };

template <typename T>
concept CanResolveBundleFromTemporary = requires(T &&value, const volt::LibraryPartRef &reference) {
    std::forward<T>(value).resolve(reference);
};

template <typename T>
concept CanBorrowBundleAssetFromTemporary =
    requires(T &&value, const volt::PartAssetReference &reference) {
        std::forward<T>(value).asset(reference);
    };

static_assert(!CanBorrowBundleBytesFromTemporary<volt::io::PartLibraryBundle>);
static_assert(!CanBorrowBundleDigestFromTemporary<volt::io::PartLibraryBundle>);
static_assert(!CanBorrowBundleLibraryFromTemporary<volt::io::PartLibraryBundle>);
static_assert(!CanBorrowBundleEntriesFromTemporary<volt::io::PartLibraryBundle>);
static_assert(!CanResolveBundleFromTemporary<volt::io::PartLibraryBundle>);
static_assert(!CanBorrowBundleAssetFromTemporary<volt::io::PartLibraryBundle>);

struct TestArchive {
    nlohmann::json manifest;
    std::vector<std::pair<std::string, std::string>> entries;
};

void append_u64(std::string &out, std::uint64_t value) {
    for (auto shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<char>((value >> static_cast<unsigned>(shift)) & 0xffU));
    }
}

void append_sized(std::string &out, std::string_view value) {
    append_u64(out, static_cast<std::uint64_t>(value.size()));
    out.append(value);
}

[[nodiscard]] std::uint64_t read_u64(std::string_view bytes, std::size_t &cursor) {
    auto value = std::uint64_t{0};
    for (auto index = std::size_t{0}; index < 8U; ++index) {
        value = (value << 8U) | static_cast<unsigned char>(bytes[cursor + index]);
    }
    cursor += 8U;
    return value;
}

[[nodiscard]] std::string read_sized(std::string_view bytes, std::size_t &cursor) {
    const auto size = static_cast<std::size_t>(read_u64(bytes, cursor));
    auto result = std::string{bytes.substr(cursor, size)};
    cursor += size;
    return result;
}

[[nodiscard]] TestArchive decode_test_archive(std::string_view bytes) {
    auto cursor = std::size_t{16U};
    auto manifest = nlohmann::json::parse(read_sized(bytes, cursor));
    const auto entry_count = read_u64(bytes, cursor);
    auto entries = std::vector<std::pair<std::string, std::string>>{};
    for (auto index = std::uint64_t{0}; index < entry_count; ++index) {
        auto path = read_sized(bytes, cursor);
        auto payload = read_sized(bytes, cursor);
        entries.emplace_back(std::move(path), std::move(payload));
    }
    return TestArchive{std::move(manifest), std::move(entries)};
}

[[nodiscard]] std::string encode_test_archive_with_manifest(const TestArchive &archive,
                                                            std::string_view manifest_bytes) {
    auto result = std::string{"VOLT-LIB-BUNDLE\0", 16U};
    append_sized(result, manifest_bytes);
    append_u64(result, static_cast<std::uint64_t>(archive.entries.size()));
    for (const auto &[path, payload] : archive.entries) {
        append_sized(result, path);
        append_sized(result, payload);
    }
    return result;
}

[[nodiscard]] std::string encode_test_archive(const TestArchive &archive) {
    return encode_test_archive_with_manifest(archive, archive.manifest.dump());
}

void refresh_content_digest(TestArchive &archive) {
    auto core = archive.manifest;
    core.erase("content_digest");
    auto canonical = std::string{};
    append_sized(canonical, core.dump());
    append_u64(canonical, static_cast<std::uint64_t>(archive.entries.size()));
    for (const auto &entry : core.at("entries")) {
        const auto path = entry.at("path").get<std::string>();
        const auto payload =
            std::ranges::find(archive.entries, path, &decltype(archive.entries)::value_type::first);
        REQUIRE(payload != archive.entries.end());
        append_sized(canonical, path);
        append_sized(canonical, payload->second);
    }
    archive.manifest["content_digest"] = volt::sha256_content_hash(canonical).value();
}

[[nodiscard]] nlohmann::json &entry_with_role(TestArchive &archive, std::string_view role) {
    const auto match =
        std::ranges::find(archive.manifest.at("entries"), role, [](const auto &entry) {
            return entry.at("role").template get<std::string>();
        });
    REQUIRE(match != archive.manifest.at("entries").end());
    return *match;
}

[[nodiscard]] std::string &payload_for(TestArchive &archive, const nlohmann::json &entry) {
    const auto path = entry.at("path").get<std::string>();
    const auto match =
        std::ranges::find(archive.entries, path, &decltype(archive.entries)::value_type::first);
    REQUIRE(match != archive.entries.end());
    return match->second;
}

void refresh_entry_digest(nlohmann::json &entry, std::string_view payload) {
    entry["digest"] = volt::sha256_content_hash(payload).value();
}

[[nodiscard]] std::string hash_suffix(std::string_view bytes) {
    return volt::sha256_content_hash(bytes).value().substr(7U);
}

void check_reopen_rejected(std::string_view bytes) {
    auto exposed = false;
    try {
        const auto reopened = volt::io::PartLibraryBundle::open(bytes);
        exposed = !reopened.entries().empty();
        FAIL("Expected native bundle reopen to fail closed");
    } catch (const volt::KernelError &) {
    }
    CHECK_FALSE(exposed);
}

} // namespace

TEST_CASE("Selected PartLibraryBundle builds byte-identically and reopens fully offline") {
    auto first_fixture = bundle_fixture();
    auto second_fixture = bundle_fixture(true);
    std::ranges::reverse(second_fixture.attachments);
    const auto first_selected =
        std::vector{volt::PartKey{"vendor/A-LED"}, volt::PartKey{"vendor/Z-LED"}};
    const auto second_selected =
        std::vector{volt::PartKey{"vendor/Z-LED"}, volt::PartKey{"vendor/A-LED"}};
    const auto admitted = first_fixture.builder.build(first_fixture.resolver);
    const auto admitted_reference = admitted.require(volt::PartKey{"vendor/A-LED"});

    const auto first = volt::io::PartLibraryBundle::build(
        first_fixture.builder, first_selected, first_fixture.resolver, first_fixture.attachments);
    const auto second =
        volt::io::PartLibraryBundle::build(second_fixture.builder, second_selected,
                                           second_fixture.resolver, second_fixture.attachments);

    CHECK(std::string{first.bytes()} == std::string{second.bytes()});
    CHECK(first.digest() == second.digest());
    CHECK(first.digest().value() ==
          "sha256:a1e7ea48eefa0f8950843e908e944db23347e54ffe31fb6699c47a832b48a5aa");
    CHECK(std::ranges::is_sorted(first.entries(), {}, &volt::io::PartLibraryBundleEntry::path));
    CHECK(first.library().components().size() == 2U);
    CHECK(first.library().parts().size() == 2U);
    CHECK(first.resolve(admitted_reference).content_identity() ==
          first_fixture.selected_part.content_identity());

    const auto reopened = volt::io::PartLibraryBundle::open(first.bytes());
    CHECK(std::string{reopened.bytes()} == std::string{first.bytes()});
    CHECK(reopened.digest() == first.digest());
    REQUIRE(reopened.library().components().size() == 2U);
    REQUIRE(reopened.library().parts().size() == 2U);
    CHECK(reopened.library().components().front().content_identity() ==
          first_fixture.component.definition.content_identity());
    CHECK(reopened.library().parts().front().content_identity() ==
          first_fixture.selected_part.content_identity());

    const auto reference = reopened.require(volt::PartKey{"vendor/A-LED"});
    CHECK(reference.library_digest() == reopened.library_digest());
    CHECK(reopened.resolve(reference).content_identity() ==
          first_fixture.selected_part.content_identity());
    CHECK(reopened.resolve(admitted_reference).content_identity() ==
          first_fixture.selected_part.content_identity());
    CHECK(reopened.resolve(reopened.library().require(volt::PartKey{"vendor/A-LED"}))
              .content_identity() == first_fixture.selected_part.content_identity());

    for (const auto &asset : volt::part_asset_references(first_fixture.selected_part)) {
        REQUIRE(reopened.asset(asset).has_value());
        CHECK(std::string{*reopened.asset(asset)} == std::string{exact_asset_bytes});
    }
    for (const auto &attachment : first_fixture.attachments) {
        REQUIRE(reopened.asset(attachment.reference()).has_value());
    }
    for (const auto role : {volt::io::PartLibraryBundleEntryRole::Symbol,
                            volt::io::PartLibraryBundleEntryRole::Footprint,
                            volt::io::PartLibraryBundleEntryRole::Model3D,
                            volt::io::PartLibraryBundleEntryRole::Simulation,
                            volt::io::PartLibraryBundleEntryRole::Evidence,
                            volt::io::PartLibraryBundleEntryRole::Licence,
                            volt::io::PartLibraryBundleEntryRole::Provenance}) {
        CHECK(
            std::ranges::find(reopened.entries(), role, &volt::io::PartLibraryBundleEntry::role) !=
            reopened.entries().end());
    }
}

TEST_CASE("PartLibraryBundle closure is explicit and empty closure is valid") {
    auto fixture = bundle_fixture();

    const auto empty = volt::io::PartLibraryBundle::build(
        fixture.builder, std::vector<volt::PartKey>{}, fixture.resolver, fixture.attachments);
    CHECK(empty.library().components().empty());
    CHECK(empty.library().parts().empty());
    CHECK(empty.entries().empty());
    const auto reopened_empty = volt::io::PartLibraryBundle::open(empty.bytes());
    CHECK(reopened_empty.entries().empty());

    auto selected_only = MemoryAssetResolver{};
    selected_only.add(fixture.selected_part);
    for (const auto &attachment : fixture.attachments) {
        selected_only.add(attachment.reference(),
                          fixture.resolver.resolve(attachment.reference()).value());
    }
    const auto selected = std::vector{volt::PartKey{"vendor/A-LED"}};
    const auto bundle = volt::io::PartLibraryBundle::build(fixture.builder, selected, selected_only,
                                                           fixture.attachments);
    CHECK(bundle.library().components().size() == 1U);
    CHECK(bundle.library().parts().size() == 1U);
    const auto admitted = fixture.builder.build(fixture.resolver);
    const auto admitted_reference = admitted.require(selected.front());
    CHECK(bundle.library_digest() == admitted.digest());
    CHECK(bundle.resolve(admitted_reference).content_identity() ==
          fixture.selected_part.content_identity());

    const auto reopened = volt::io::PartLibraryBundle::open(bundle.bytes());
    CHECK(reopened.library_digest() == admitted.digest());
    CHECK(reopened.resolve(admitted_reference).content_identity() ==
          fixture.selected_part.content_identity());
    check_kernel_error(
        [&] { static_cast<void>(reopened.resolve(reopened.library().require(selected.front()))); },
        volt::ErrorCode::CrossReferenceViolation);
}

TEST_CASE("PartLibraryBundle build is all-or-nothing over selected assets") {
    auto fixture = bundle_fixture();
    const auto selected = std::vector{volt::PartKey{"vendor/A-LED"}};
    const auto missing = MemoryAssetResolver{};
    check_kernel_error(
        [&] {
            static_cast<void>(volt::io::PartLibraryBundle::build(fixture.builder, selected, missing,
                                                                 fixture.attachments));
        },
        volt::ErrorCode::UnknownEntity);

    auto mismatched = MemoryAssetResolver{};
    for (const auto &reference : volt::part_asset_references(fixture.selected_part)) {
        mismatched.add(reference, "wrong-bytes");
    }
    check_kernel_error(
        [&] {
            static_cast<void>(volt::io::PartLibraryBundle::build(fixture.builder, selected,
                                                                 mismatched, fixture.attachments));
        },
        volt::ErrorCode::CrossReferenceViolation);

    check_kernel_error(
        [&] {
            static_cast<void>(volt::io::PartLibraryBundle::build(
                fixture.builder,
                std::vector{volt::PartKey{"vendor/A-LED"}, volt::PartKey{"vendor/A-LED"}},
                fixture.resolver, fixture.attachments));
        },
        volt::ErrorCode::DuplicateName);

    auto without_evidence = fixture.attachments;
    std::erase_if(without_evidence, [](const auto &attachment) {
        return attachment.reference().kind() == volt::PartAssetKind::Evidence;
    });
    check_kernel_error(
        [&] {
            static_cast<void>(volt::io::PartLibraryBundle::build(
                fixture.builder, selected, fixture.resolver, without_evidence));
        },
        volt::ErrorCode::CrossReferenceViolation);

    auto incomplete_builder = volt::PartLibraryBuilder{fixture.builder.identity()};
    incomplete_builder.add_component(fixture.component.definition);
    incomplete_builder.add_part(fixture.selected_part);
    check_kernel_error(
        [&] {
            static_cast<void>(volt::io::PartLibraryBundle::build(
                incomplete_builder, selected, fixture.resolver, fixture.attachments));
        },
        volt::ErrorCode::InvalidState);
}

TEST_CASE("Existing PartLibraryBundle bytes are immutable after later catalogue changes") {
    auto fixture = bundle_fixture();
    const auto selected = std::vector{volt::PartKey{"vendor/A-LED"}};
    const auto historical = volt::io::PartLibraryBundle::build(
        fixture.builder, selected, fixture.resolver, fixture.attachments);
    const auto historical_bytes = std::string{historical.bytes()};
    const auto historical_digest = historical.digest();

    auto later_builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"test.parts", "2026.2", volt::PartLibrarySchemaVersion::V1}};
    for (const auto &spec : fixture.builder.component_specs()) {
        later_builder.add_component(spec);
    }
    later_builder.add_part(fixture.selected_part).add_part(fixture.unselected_part);
    const auto later = volt::io::PartLibraryBundle::build(later_builder, selected, fixture.resolver,
                                                          fixture.attachments);
    CHECK(later.digest() != historical_digest);

    auto attachment_changed = fixture.attachments;
    const auto changed_reference =
        volt::PartAssetReference{volt::PartAssetKind::Licence, "licence:vendor/A-LED",
                                 volt::sha256_content_hash("changed-licence-bytes")};
    fixture.resolver.add(changed_reference, "changed-licence-bytes");
    const auto licence = std::ranges::find_if(attachment_changed, [](const auto &attachment) {
        return attachment.reference().kind() == volt::PartAssetKind::Licence;
    });
    REQUIRE(licence != attachment_changed.end());
    *licence =
        volt::io::PartLibraryBundleAttachment{volt::PartKey{"vendor/A-LED"}, changed_reference};
    const auto changed = volt::io::PartLibraryBundle::build(fixture.builder, selected,
                                                            fixture.resolver, attachment_changed);
    CHECK(changed.library_digest() == historical.library_digest());
    CHECK(changed.digest() != historical.digest());

    const auto reopened = volt::io::PartLibraryBundle::open(historical_bytes);
    CHECK(reopened.digest() == historical_digest);
    CHECK(reopened.library().identity().version() == "2026.1");
    CHECK(reopened.resolve(reopened.require(volt::PartKey{"vendor/A-LED"})).content_identity() ==
          fixture.selected_part.content_identity());
}

TEST_CASE("PartLibraryBundle open rejects truncated extraneous and corrupted archive bytes") {
    auto fixture = bundle_fixture();
    const auto bundle = volt::io::PartLibraryBundle::build(
        fixture.builder, std::vector{volt::PartKey{"vendor/A-LED"}}, fixture.resolver,
        fixture.attachments);

    auto truncated = std::string{bundle.bytes()};
    truncated.pop_back();
    check_kernel_error([&] { static_cast<void>(volt::io::PartLibraryBundle::open(truncated)); },
                       volt::ErrorCode::InvalidArgument);

    auto extraneous = std::string{bundle.bytes()};
    extraneous.push_back('x');
    check_kernel_error([&] { static_cast<void>(volt::io::PartLibraryBundle::open(extraneous)); },
                       volt::ErrorCode::InvalidArgument);

    auto corrupt = std::string{bundle.bytes()};
    corrupt.front() = 'X';
    check_kernel_error([&] { static_cast<void>(volt::io::PartLibraryBundle::open(corrupt)); },
                       volt::ErrorCode::InvalidArgument);

    auto reordered = decode_test_archive(bundle.bytes());
    std::ranges::reverse(reordered.entries);
    check_kernel_error(
        [&] {
            static_cast<void>(volt::io::PartLibraryBundle::open(encode_test_archive(reordered)));
        },
        volt::ErrorCode::InvalidArgument);

    const auto noncanonical_manifest = decode_test_archive(bundle.bytes());
    check_kernel_error(
        [&] {
            static_cast<void>(volt::io::PartLibraryBundle::open(encode_test_archive_with_manifest(
                noncanonical_manifest, noncanonical_manifest.manifest.dump(2))));
        },
        volt::ErrorCode::InvalidArgument);
}

TEST_CASE("PartLibraryBundle rejects unsafe absolute escaping and aliased manifest paths") {
    auto fixture = bundle_fixture();
    const auto bundle = volt::io::PartLibraryBundle::build(
        fixture.builder, std::vector{volt::PartKey{"vendor/A-LED"}}, fixture.resolver,
        fixture.attachments);

    for (const auto &path :
         {std::string{"/absolute/payload"}, std::string{"../escape"},
          std::string{"definitions//aliased"}, std::string{"C:/absolute/payload"},
          std::string{"assets\\aliased"}, std::string{"assets/nul\0path", 12U}}) {
        auto archive = decode_test_archive(bundle.bytes());
        archive.manifest.at("entries").front()["path"] = path;
        check_reopen_rejected(encode_test_archive(archive));
    }
}

TEST_CASE("PartLibraryBundle rejects duplicate IDs paths and unsupported version states") {
    auto fixture = bundle_fixture();
    const auto bundle = volt::io::PartLibraryBundle::build(
        fixture.builder, std::vector{volt::PartKey{"vendor/A-LED"}}, fixture.resolver,
        fixture.attachments);

    SECTION("duplicate ID") {
        auto archive = decode_test_archive(bundle.bytes());
        archive.manifest.at("entries").push_back(archive.manifest.at("entries").front());
        check_reopen_rejected(encode_test_archive(archive));
    }
    SECTION("duplicate path alias") {
        auto archive = decode_test_archive(bundle.bytes());
        auto duplicate = archive.manifest.at("entries").front();
        duplicate["id"] = "duplicate-id";
        archive.manifest.at("entries").push_back(std::move(duplicate));
        check_reopen_rejected(encode_test_archive(archive));
    }
    SECTION("duplicate role and source key") {
        auto archive = decode_test_archive(bundle.bytes());
        auto duplicate = entry_with_role(archive, "evidence");
        const auto payload = std::string{"different-evidence"};
        const auto digest = volt::sha256_content_hash(payload);
        duplicate["id"] = "duplicate-evidence-id";
        duplicate["digest"] = digest.value();
        duplicate["path"] = "assets/evidence/" +
                            hash_suffix(duplicate.at("source_key").get<std::string>()) + "-" +
                            digest.value().substr(7U) + ".bin";
        archive.manifest.at("entries").push_back(duplicate);
        archive.entries.emplace_back(duplicate.at("path").get<std::string>(), payload);
        check_reopen_rejected(encode_test_archive(archive));
    }
    SECTION("unsupported manifest schema") {
        auto archive = decode_test_archive(bundle.bytes());
        archive.manifest["schema_version"] = 99;
        check_reopen_rejected(encode_test_archive(archive));
    }
    SECTION("unsupported producer") {
        auto archive = decode_test_archive(bundle.bytes());
        archive.manifest["producer"]["version"] = 99;
        check_reopen_rejected(encode_test_archive(archive));
    }
    SECTION("unsupported producer identity") {
        auto archive = decode_test_archive(bundle.bytes());
        archive.manifest["producer"]["name"] = "ambient-python-producer";
        check_reopen_rejected(encode_test_archive(archive));
    }
    SECTION("unsupported library schema") {
        auto archive = decode_test_archive(bundle.bytes());
        archive.manifest["library"]["schema_version"] = 99;
        refresh_content_digest(archive);
        check_reopen_rejected(encode_test_archive(archive));
    }
}

TEST_CASE("PartLibraryBundle rejects recorded key digest contract and mapping mismatches") {
    auto fixture = bundle_fixture();
    const auto bundle = volt::io::PartLibraryBundle::build(
        fixture.builder, std::vector{volt::PartKey{"vendor/A-LED"}}, fixture.resolver,
        fixture.attachments);

    SECTION("recorded asset digest") {
        auto archive = decode_test_archive(bundle.bytes());
        auto &entry = entry_with_role(archive, "symbol");
        payload_for(archive, entry).push_back('x');
        check_reopen_rejected(encode_test_archive(archive));
    }
    SECTION("LibraryPartRef key") {
        auto archive = decode_test_archive(bundle.bytes());
        auto &entry = entry_with_role(archive, "library_part_reference");
        auto document = nlohmann::json::parse(payload_for(archive, entry));
        document["part_key"] = "vendor/forged";
        payload_for(archive, entry) = document.dump();
        refresh_entry_digest(entry, payload_for(archive, entry));
        refresh_content_digest(archive);
        check_reopen_rejected(encode_test_archive(archive));
    }
    SECTION("non-canonical LibraryPartRef bytes") {
        auto archive = decode_test_archive(bundle.bytes());
        auto &entry = entry_with_role(archive, "library_part_reference");
        payload_for(archive, entry) = nlohmann::json::parse(payload_for(archive, entry)).dump(2);
        refresh_entry_digest(entry, payload_for(archive, entry));
        refresh_content_digest(archive);
        check_reopen_rejected(encode_test_archive(archive));
    }
    SECTION("component contract identity") {
        auto archive = decode_test_archive(bundle.bytes());
        auto &entry = entry_with_role(archive, "part_definition");
        auto document = nlohmann::json::parse(payload_for(archive, entry));
        document["implements"] = volt::sha256_content_hash("forged-component").value();
        payload_for(archive, entry) = document.dump(2) + "\n";
        refresh_entry_digest(entry, payload_for(archive, entry));
        refresh_content_digest(archive);
        check_reopen_rejected(encode_test_archive(archive));
    }
    SECTION("physical mapping") {
        auto archive = decode_test_archive(bundle.bytes());
        auto &entry = entry_with_role(archive, "part_definition");
        auto document = nlohmann::json::parse(payload_for(archive, entry));
        document["pin_terminal_mappings"] = nlohmann::json::array();
        payload_for(archive, entry) = document.dump(2) + "\n";
        refresh_entry_digest(entry, payload_for(archive, entry));
        refresh_content_digest(archive);
        check_reopen_rejected(encode_test_archive(archive));
    }
}

TEST_CASE("PartLibraryBundle rejects incomplete extraneous and unreachable dependency closure") {
    auto fixture = bundle_fixture();
    const auto bundle = volt::io::PartLibraryBundle::build(
        fixture.builder, std::vector{volt::PartKey{"vendor/A-LED"}}, fixture.resolver,
        fixture.attachments);

    SECTION("incomplete direct edge") {
        auto archive = decode_test_archive(bundle.bytes());
        auto &entry = entry_with_role(archive, "part_definition");
        auto &dependencies = entry.at("dependencies");
        const auto asset = std::ranges::find_if(dependencies, [](const auto &dependency) {
            return dependency.template get<std::string>().starts_with("asset:");
        });
        REQUIRE(asset != dependencies.end());
        dependencies.erase(asset);
        refresh_content_digest(archive);
        check_reopen_rejected(encode_test_archive(archive));
    }
    SECTION("extraneous direct edge") {
        auto archive = decode_test_archive(bundle.bytes());
        auto &entry = entry_with_role(archive, "part_definition");
        entry.at("dependencies").push_back("reference:vendor/A-LED");
        auto dependencies = entry.at("dependencies").get<std::vector<std::string>>();
        std::ranges::sort(dependencies);
        entry["dependencies"] = std::move(dependencies);
        refresh_content_digest(archive);
        check_reopen_rejected(encode_test_archive(archive));
    }
    SECTION("missing archive entry") {
        auto archive = decode_test_archive(bundle.bytes());
        archive.entries.pop_back();
        check_reopen_rejected(encode_test_archive(archive));
    }
    SECTION("extraneous archive entry") {
        auto archive = decode_test_archive(bundle.bytes());
        archive.entries.emplace_back("extraneous/payload.bin", "bytes");
        check_reopen_rejected(encode_test_archive(archive));
    }
    SECTION("manifest entry outside selected closure") {
        auto archive = decode_test_archive(bundle.bytes());
        const auto source_key = std::string{"evidence:orphan"};
        const auto payload = std::string{"orphan-evidence"};
        const auto digest = volt::sha256_content_hash(payload);
        const auto path =
            "assets/evidence/" + hash_suffix(source_key) + "-" + digest.value().substr(7U) + ".bin";
        archive.manifest.at("entries").push_back({{"dependencies", nlohmann::json::array()},
                                                  {"digest", digest.value()},
                                                  {"id", "asset:evidence:" + source_key},
                                                  {"path", path},
                                                  {"role", "evidence"},
                                                  {"semantic_identity", nullptr},
                                                  {"source_key", source_key}});
        archive.entries.emplace_back(path, payload);
        auto manifest_entries = archive.manifest.at("entries").get<std::vector<nlohmann::json>>();
        std::ranges::sort(manifest_entries, {}, [](const auto &entry) {
            return entry.at("path").template get<std::string>();
        });
        archive.manifest["entries"] = std::move(manifest_entries);
        std::ranges::sort(archive.entries, {}, &decltype(archive.entries)::value_type::first);
        refresh_content_digest(archive);
        check_reopen_rejected(encode_test_archive(archive));
    }
    SECTION("manifest cardinality") {
        auto archive = decode_test_archive(bundle.bytes());
        archive.manifest["library"]["part_count"] = 2;
        refresh_content_digest(archive);
        check_reopen_rejected(encode_test_archive(archive));
    }
}
