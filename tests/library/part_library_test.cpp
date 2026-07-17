#include <catch2/catch_test_macros.hpp>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/circuit/parts/part_definition.hpp>
#include <volt/core/errors.hpp>
#include <volt/library/part_library.hpp>

namespace {

constexpr std::string_view asset_bytes = "native-part-library-asset";

[[nodiscard]] volt::ComponentDefinition component(std::string key = "test.component/led@1") {
    const auto pins = std::vector{volt::PinDefinition{"A", "2"}, volt::PinDefinition{"K", "1"}};
    return volt::ComponentDefinition::make("LED", pins, {volt::PinDefId{0}, volt::PinDefId{1}}, {},
                                           std::nullopt,
                                           {volt::SchematicSymbolReference{"test.symbols:led"}},
                                           volt::ComponentContractSpec{
                                               .key = volt::ComponentKey{std::move(key)},
                                               .pin_keys = {volt::PinKey{"A"}, volt::PinKey{"K"}},
                                           });
}

[[nodiscard]] volt::PartDefinition part(const volt::ComponentDefinition &definition,
                                        std::string key, std::string manufacturer,
                                        std::string package = "0603",
                                        std::string namespace_name = "test.parts") {
    const auto asset_digest = volt::sha256_content_hash(asset_bytes);
    return volt::PartDefinition{
        definition,
        volt::PartIdentity{std::move(namespace_name), key, "1.0.0"},
        volt::ElectricalRecordSet{2},
        {
            volt::PinPackageTerminalMapping{volt::PinKey{"A"}, {volt::PackageTerminalKey{"2"}}},
            volt::PinPackageTerminalMapping{volt::PinKey{"K"}, {volt::PackageTerminalKey{"1"}}},
        },
        {},
        volt::PartProvenance{"test datasheet", "volt.tests", "native fixture"},
        {volt::PartSchematicAssetReference{"test.symbols:" + key, "default", asset_digest}},
        volt::OrderablePart{
            volt::ManufacturerPart{std::move(manufacturer), "MPN-" + key},
            volt::PackageRef{std::move(package)},
            volt::HashedFootprintReference{volt::FootprintRef{"TestFootprints", key}, asset_digest},
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
            volt::PartModel3DReference{"glb", "led.glb", asset_digest, {0.0, 0.0, 0.0}, 0.0},
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

    void add(const volt::PartDefinition &definition, std::string bytes) {
        for (const auto &reference : volt::part_asset_references(definition)) {
            add(reference, bytes);
        }
    }

    [[nodiscard]] std::optional<std::string>
    resolve(const volt::PartAssetReference &reference) const override {
        const auto match = assets_.find(asset_map_key(reference));
        if (match == assets_.end()) {
            return std::nullopt;
        }
        return match->second;
    }

  private:
    std::map<std::string, std::string> assets_;
};

template <typename T>
concept CanBorrowIdentityFromTemporary = requires(T &&value) { std::forward<T>(value).identity(); };

template <typename T>
concept CanBorrowDigestFromTemporary = requires(T &&value) { std::forward<T>(value).digest(); };

template <typename T>
concept CanBorrowComponentsFromTemporary =
    requires(T &&value) { std::forward<T>(value).components(); };

template <typename T>
concept CanBorrowPartsFromTemporary = requires(T &&value) { std::forward<T>(value).parts(); };

template <typename T>
concept CanBorrowComponentFromTemporary =
    requires(T &&value, const volt::ComponentKey &key) { std::forward<T>(value).component(key); };

template <typename T>
concept CanBorrowPartFromTemporary =
    requires(T &&value, const volt::PartKey &key) { std::forward<T>(value).part(key); };

template <typename T>
concept CanBorrowCandidatesFromTemporary =
    requires(T &&value, const volt::PartQuery &query) { std::forward<T>(value).find(query); };

template <typename T>
concept CanResolveFromTemporary = requires(T &&value, const volt::LibraryPartRef &reference) {
    std::forward<T>(value).resolve(reference);
};

static_assert(!CanBorrowIdentityFromTemporary<volt::PartLibraryBuilder>);
static_assert(!CanBorrowComponentsFromTemporary<volt::PartLibraryBuilder>);
static_assert(!CanBorrowPartsFromTemporary<volt::PartLibraryBuilder>);
static_assert(!CanBorrowIdentityFromTemporary<volt::PartLibrary>);
static_assert(!CanBorrowDigestFromTemporary<volt::PartLibrary>);
static_assert(!CanBorrowComponentsFromTemporary<volt::PartLibrary>);
static_assert(!CanBorrowPartsFromTemporary<volt::PartLibrary>);
static_assert(!CanBorrowComponentFromTemporary<volt::PartLibrary>);
static_assert(!CanBorrowPartFromTemporary<volt::PartLibrary>);
static_assert(!CanBorrowCandidatesFromTemporary<volt::PartLibrary>);
static_assert(!CanResolveFromTemporary<volt::PartLibrary>);

[[nodiscard]] volt::PartLibrary build_library(const volt::ComponentDefinition &definition,
                                              const std::vector<volt::PartDefinition> &parts,
                                              const MemoryAssetResolver &resolver) {
    auto builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"test.parts", "2026.1", volt::PartLibrarySchemaVersion::V1}};
    builder.add_component(definition);
    for (const auto &candidate : parts) {
        builder.add_part(candidate);
    }
    return builder.build(resolver);
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

} // namespace

TEST_CASE("Native part catalogue construction and typed queries are deterministic") {
    const auto definition = component();
    const auto z_part = part(definition, "vendor/Z-LED", "Vendor Z");
    const auto a_part = part(definition, "vendor/A-LED", "Vendor A");
    auto resolver = MemoryAssetResolver{};
    resolver.add(z_part, std::string{asset_bytes});
    resolver.add(a_part, std::string{asset_bytes});

    const auto first = build_library(definition, {z_part, a_part}, resolver);
    const auto second = build_library(definition, {a_part, z_part}, resolver);

    CHECK(first.digest() == second.digest());
    CHECK(first.identity().namespace_name() == "test.parts");
    CHECK(first.identity().version() == "2026.1");
    CHECK(first.identity().schema_version() == volt::PartLibrarySchemaVersion::V1);
    REQUIRE(first.components().size() == 1U);
    REQUIRE(first.parts().size() == 2U);
    CHECK(first.parts()[0].identity().name() == "vendor/A-LED");
    CHECK(first.parts()[1].identity().name() == "vendor/Z-LED");

    const auto found_component = first.component(volt::ComponentKey{"test.component/led@1"});
    REQUIRE(found_component.has_value());
    CHECK(found_component->get().content_identity() == definition.content_identity());

    const auto found_part = first.part(volt::PartKey{"vendor/Z-LED"});
    REQUIRE(found_part.has_value());
    CHECK(found_part->get().content_identity() == z_part.content_identity());

    const auto all_candidates =
        first.find(volt::PartQuery{volt::ComponentKey{"test.component/led@1"}});
    REQUIRE(all_candidates.size() == 2U);
    CHECK(all_candidates[0].get().identity().name() == "vendor/A-LED");
    CHECK(all_candidates[1].get().identity().name() == "vendor/Z-LED");

    const auto vendor_candidate = first.find(
        volt::PartQuery{volt::ComponentKey{"test.component/led@1"},
                        volt::ManufacturerPart{"Vendor Z", "MPN-vendor/Z-LED"}, std::nullopt});
    REQUIRE(vendor_candidate.size() == 1U);
    CHECK(vendor_candidate[0].get().identity().name() == "vendor/Z-LED");

    const auto package_candidates = first.find(volt::PartQuery{
        volt::ComponentKey{"test.component/led@1"}, std::nullopt, volt::PackageRef{"0603"}});
    CHECK(package_candidates.size() == 2U);
    CHECK(first.find(volt::PartQuery{volt::ComponentKey{"missing/component@1"}}).empty());
}

TEST_CASE("Exact LibraryPartRef resolution never falls back from integrity fields") {
    const auto definition = component();
    const auto a_part = part(definition, "vendor/A-LED", "Vendor A");
    const auto z_part = part(definition, "vendor/Z-LED", "Vendor Z");
    auto resolver = MemoryAssetResolver{};
    resolver.add(a_part, std::string{asset_bytes});
    resolver.add(z_part, std::string{asset_bytes});
    const auto library = build_library(definition, {a_part, z_part}, resolver);

    const auto reference = library.require(volt::PartKey{"vendor/A-LED"});
    CHECK(reference.library_namespace() == "test.parts");
    CHECK(reference.library_version() == "2026.1");
    CHECK(reference.part_key() == volt::PartKey{"vendor/A-LED"});
    CHECK(reference.library_digest() == library.digest());
    CHECK(reference.part_digest() == a_part.content_identity());
    CHECK(library.resolve(reference).content_identity() == a_part.content_identity());

    const auto forged_digest = volt::sha256_content_hash("forged");
    check_kernel_error(
        [&] {
            static_cast<void>(
                library.resolve(volt::LibraryPartRef{"other.parts", "2026.1", reference.part_key(),
                                                     library.digest(), reference.part_digest()}));
        },
        volt::ErrorCode::CrossReferenceViolation);
    check_kernel_error(
        [&] {
            static_cast<void>(
                library.resolve(volt::LibraryPartRef{"test.parts", "older", reference.part_key(),
                                                     library.digest(), reference.part_digest()}));
        },
        volt::ErrorCode::CrossReferenceViolation);
    check_kernel_error(
        [&] {
            static_cast<void>(
                library.resolve(volt::LibraryPartRef{"test.parts", "2026.1", reference.part_key(),
                                                     forged_digest, reference.part_digest()}));
        },
        volt::ErrorCode::CrossReferenceViolation);
    check_kernel_error(
        [&] {
            static_cast<void>(library.resolve(
                volt::LibraryPartRef{"test.parts", "2026.1", volt::PartKey{"missing"},
                                     library.digest(), reference.part_digest()}));
        },
        volt::ErrorCode::UnknownEntity);
    check_kernel_error(
        [&] {
            static_cast<void>(library.resolve(volt::LibraryPartRef{
                "test.parts", "2026.1", reference.part_key(), library.digest(), forged_digest}));
        },
        volt::ErrorCode::CrossReferenceViolation);
    check_kernel_error(
        [&] {
            static_cast<void>(library.resolve(
                volt::LibraryPartRef{"test.parts", "2026.1", volt::PartKey{"vendor/Z-LED"},
                                     library.digest(), reference.part_digest()}));
        },
        volt::ErrorCode::CrossReferenceViolation);
    check_kernel_error([&] { static_cast<void>(library.require(volt::PartKey{"missing"})); },
                       volt::ErrorCode::UnknownEntity);
}

TEST_CASE("Part library builder rejects duplicate keys and foreign component contracts") {
    const auto definition = component();
    const auto exact = part(definition, "vendor/A-LED", "Vendor A");
    auto builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"test.parts", "2026.1", volt::PartLibrarySchemaVersion::V1}};
    builder.add_component(definition);

    check_kernel_error([&] { builder.add_component(definition); }, volt::ErrorCode::DuplicateName);
    builder.add_part(exact);
    check_kernel_error([&] { builder.add_part(exact); }, volt::ErrorCode::DuplicateName);

    const auto foreign_component = component("other.component/led@1");
    const auto foreign_part = part(foreign_component, "vendor/FOREIGN", "Vendor F");
    check_kernel_error([&] { builder.add_part(foreign_part); },
                       volt::ErrorCode::CrossReferenceViolation);

    auto namespace_builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"test.parts", "2026.1", volt::PartLibrarySchemaVersion::V1}};
    namespace_builder.add_component(definition);
    check_kernel_error(
        [&] {
            namespace_builder.add_part(
                part(definition, "vendor/OTHER", "Vendor O", "0603", "other.parts"));
        },
        volt::ErrorCode::CrossReferenceViolation);

    check_kernel_error([] { static_cast<void>(volt::PartKey{""}); },
                       volt::ErrorCode::InvalidArgument);
    check_kernel_error(
        [] {
            static_cast<void>(volt::PartLibraryIdentity{
                "test.parts", "2026.1", static_cast<volt::PartLibrarySchemaVersion>(99)});
        },
        volt::ErrorCode::InvalidArgument);
    check_kernel_error(
        [] {
            static_cast<void>(volt::PartAssetReference{static_cast<volt::PartAssetKind>(99),
                                                       "invalid",
                                                       volt::sha256_content_hash(asset_bytes)});
        },
        volt::ErrorCode::InvalidArgument);
}

TEST_CASE("Part library build requires explicit complete asset resolution") {
    const auto definition = component();
    const auto exact = part(definition, "vendor/A-LED", "Vendor A");
    auto builder = volt::PartLibraryBuilder{
        volt::PartLibraryIdentity{"test.parts", "2026.1", volt::PartLibrarySchemaVersion::V1}};
    builder.add_component(definition);
    builder.add_part(exact);

    const auto missing = MemoryAssetResolver{};
    check_kernel_error([&] { static_cast<void>(builder.build(missing)); },
                       volt::ErrorCode::UnknownEntity);

    auto mismatched = MemoryAssetResolver{};
    mismatched.add(exact, "wrong bytes");
    check_kernel_error([&] { static_cast<void>(builder.build(mismatched)); },
                       volt::ErrorCode::CrossReferenceViolation);

    auto complete = MemoryAssetResolver{};
    complete.add(exact, std::string{asset_bytes});
    const auto library = builder.build(complete);
    CHECK(library.parts().size() == 1U);
}
