#include <volt/io/parts/part_library_bundle.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/circuit/circuit.hpp>
#include <volt/core/errors.hpp>
#include <volt/io/logical/logical_circuit_reader.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/io/parts/part_definition_reader.hpp>
#include <volt/io/parts/part_definition_writer.hpp>

namespace volt::io {
namespace {

using Json = nlohmann::json;

constexpr auto archive_magic = std::string_view{"VOLT-LIB-BUNDLE\0", 16U};
constexpr auto producer_name = std::string_view{"volt.native.part-library-bundle"};
constexpr std::uint64_t max_manifest_bytes = 16U * 1024U * 1024U;
constexpr std::uint64_t max_entry_count = 100'000U;
constexpr std::uint64_t max_path_bytes = 4096U;

[[noreturn]] void fail(std::string message, ErrorCode code = ErrorCode::InvalidArgument) {
    throw KernelLogicError{code, message};
}

void require_bundle(bool condition, std::string message,
                    ErrorCode code = ErrorCode::InvalidArgument) {
    if (!condition) {
        fail(std::move(message), code);
    }
}

void require_canonical_relative_path(std::string_view path) {
    require_bundle(!path.empty(), "PartLibraryBundle path must not be empty");
    require_bundle(path.front() != '/', "PartLibraryBundle path must be relative");
    require_bundle(path.find('\\') == std::string_view::npos,
                   "PartLibraryBundle path must use canonical forward slashes");
    require_bundle(path.find('\0') == std::string_view::npos,
                   "PartLibraryBundle path must not contain NUL bytes");
    require_bundle(!(path.size() >= 2U && path[1] == ':'),
                   "PartLibraryBundle path must not be absolute");
    auto begin = std::size_t{0};
    while (begin <= path.size()) {
        const auto end = path.find('/', begin);
        const auto segment =
            path.substr(begin, end == std::string_view::npos ? path.size() - begin : end - begin);
        require_bundle(!segment.empty(), "PartLibraryBundle path contains an empty segment");
        require_bundle(segment != "." && segment != "..",
                       "PartLibraryBundle path contains a non-canonical segment");
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
}

[[nodiscard]] std::string hash_suffix(const ContentHash &digest) {
    return digest.value().substr(std::string_view{"sha256:"}.size());
}

[[nodiscard]] std::string role_name(PartLibraryBundleEntryRole role) {
    switch (role) {
    case PartLibraryBundleEntryRole::ComponentDefinition:
        return "component_definition";
    case PartLibraryBundleEntryRole::PartDefinition:
        return "part_definition";
    case PartLibraryBundleEntryRole::LibraryPartReference:
        return "library_part_reference";
    case PartLibraryBundleEntryRole::Symbol:
        return "symbol";
    case PartLibraryBundleEntryRole::Footprint:
        return "footprint";
    case PartLibraryBundleEntryRole::Model3D:
        return "model_3d";
    case PartLibraryBundleEntryRole::Simulation:
        return "simulation";
    case PartLibraryBundleEntryRole::Evidence:
        return "evidence";
    case PartLibraryBundleEntryRole::Licence:
        return "licence";
    case PartLibraryBundleEntryRole::Provenance:
        return "provenance";
    }
    fail("PartLibraryBundle entry role is unsupported");
}

[[nodiscard]] PartLibraryBundleEntryRole role_from_name(const std::string &name) {
    static const auto roles = std::map<std::string, PartLibraryBundleEntryRole>{
        {"component_definition", PartLibraryBundleEntryRole::ComponentDefinition},
        {"part_definition", PartLibraryBundleEntryRole::PartDefinition},
        {"library_part_reference", PartLibraryBundleEntryRole::LibraryPartReference},
        {"symbol", PartLibraryBundleEntryRole::Symbol},
        {"footprint", PartLibraryBundleEntryRole::Footprint},
        {"model_3d", PartLibraryBundleEntryRole::Model3D},
        {"simulation", PartLibraryBundleEntryRole::Simulation},
        {"evidence", PartLibraryBundleEntryRole::Evidence},
        {"licence", PartLibraryBundleEntryRole::Licence},
        {"provenance", PartLibraryBundleEntryRole::Provenance},
    };
    const auto match = roles.find(name);
    require_bundle(match != roles.end(), "PartLibraryBundle manifest contains an unsupported role");
    return match->second;
}

[[nodiscard]] PartLibraryBundleEntryRole role_for_asset(PartAssetKind kind) {
    switch (kind) {
    case PartAssetKind::Schematic:
        return PartLibraryBundleEntryRole::Symbol;
    case PartAssetKind::Footprint:
        return PartLibraryBundleEntryRole::Footprint;
    case PartAssetKind::Model3D:
        return PartLibraryBundleEntryRole::Model3D;
    case PartAssetKind::Simulation:
        return PartLibraryBundleEntryRole::Simulation;
    case PartAssetKind::Evidence:
        return PartLibraryBundleEntryRole::Evidence;
    case PartAssetKind::Licence:
        return PartLibraryBundleEntryRole::Licence;
    case PartAssetKind::Provenance:
        return PartLibraryBundleEntryRole::Provenance;
    }
    fail("PartLibraryBundle asset role is unsupported");
}

[[nodiscard]] PartAssetKind asset_kind_for_role(PartLibraryBundleEntryRole role) {
    switch (role) {
    case PartLibraryBundleEntryRole::Symbol:
        return PartAssetKind::Schematic;
    case PartLibraryBundleEntryRole::Footprint:
        return PartAssetKind::Footprint;
    case PartLibraryBundleEntryRole::Model3D:
        return PartAssetKind::Model3D;
    case PartLibraryBundleEntryRole::Simulation:
        return PartAssetKind::Simulation;
    case PartLibraryBundleEntryRole::Evidence:
        return PartAssetKind::Evidence;
    case PartLibraryBundleEntryRole::Licence:
        return PartAssetKind::Licence;
    case PartLibraryBundleEntryRole::Provenance:
        return PartAssetKind::Provenance;
    case PartLibraryBundleEntryRole::ComponentDefinition:
    case PartLibraryBundleEntryRole::PartDefinition:
    case PartLibraryBundleEntryRole::LibraryPartReference:
        break;
    }
    fail("PartLibraryBundle entry is not an asset");
}

[[nodiscard]] bool is_asset_role(PartLibraryBundleEntryRole role) {
    return role >= PartLibraryBundleEntryRole::Symbol &&
           role <= PartLibraryBundleEntryRole::Provenance;
}

[[nodiscard]] bool is_optional_attachment_role(PartLibraryBundleEntryRole role) {
    return role >= PartLibraryBundleEntryRole::Simulation &&
           role <= PartLibraryBundleEntryRole::Provenance;
}

[[nodiscard]] std::string component_id(const ComponentDefinition &component) {
    return "component:" + component.contract().key().value();
}

[[nodiscard]] std::string part_id(const PartDefinition &part) {
    return "part:" + part.identity().name();
}

[[nodiscard]] std::string reference_id(const PartKey &key) { return "reference:" + key.value(); }

[[nodiscard]] std::string asset_id(const PartAssetReference &reference) {
    return "asset:" + role_name(role_for_asset(reference.kind())) + ":" + reference.key();
}

[[nodiscard]] std::string component_path(const ContentHash &identity) {
    return "definitions/components/" + hash_suffix(identity) + ".volt.json";
}

[[nodiscard]] std::string part_path(const ContentHash &identity) {
    return "definitions/parts/" + hash_suffix(identity) + ".volt.json";
}

[[nodiscard]] std::string reference_path(const PartKey &key) {
    return "references/" + hash_suffix(sha256_content_hash(key.value())) + ".volt.json";
}

[[nodiscard]] std::string asset_path(const PartAssetReference &reference) {
    return "assets/" + role_name(role_for_asset(reference.kind())) + "/" +
           hash_suffix(sha256_content_hash(reference.key())) + "-" +
           hash_suffix(reference.digest()) + ".bin";
}

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
    require_bundle(bytes.size() - cursor >= 8U, "PartLibraryBundle archive is truncated");
    auto value = std::uint64_t{0};
    for (auto index = std::size_t{0}; index < 8U; ++index) {
        value = (value << 8U) | static_cast<unsigned char>(bytes[cursor + index]);
    }
    cursor += 8U;
    return value;
}

[[nodiscard]] std::string read_sized(std::string_view bytes, std::size_t &cursor,
                                     std::uint64_t limit, std::string_view label) {
    const auto size = read_u64(bytes, cursor);
    require_bundle(size <= limit, "PartLibraryBundle " + std::string{label} + " exceeds its limit");
    require_bundle(size <= static_cast<std::uint64_t>(bytes.size() - cursor),
                   "PartLibraryBundle archive is truncated");
    const auto result = std::string{bytes.substr(cursor, static_cast<std::size_t>(size))};
    cursor += static_cast<std::size_t>(size);
    return result;
}

[[nodiscard]] Json entry_json(const PartLibraryBundleEntry &entry) {
    auto document = Json{{"dependencies", entry.dependencies()},
                         {"digest", entry.digest().value()},
                         {"id", entry.id()},
                         {"path", entry.path()},
                         {"role", role_name(entry.role())},
                         {"semantic_identity", nullptr},
                         {"source_key", nullptr}};
    if (entry.semantic_identity().has_value()) {
        document["semantic_identity"] = entry.semantic_identity()->value();
    }
    if (entry.source_key().has_value()) {
        document["source_key"] = *entry.source_key();
    }
    return document;
}

[[nodiscard]] Json manifest_core(const PartLibrary &library,
                                 const ContentHash &bundle_library_digest,
                                 std::span<const PartLibraryBundleEntry> entries,
                                 std::span<const std::string> selected_roots) {
    auto entry_documents = Json::array();
    for (const auto &entry : entries) {
        entry_documents.push_back(entry_json(entry));
    }
    return Json{
        {"entries", std::move(entry_documents)},
        {"format", part_library_bundle_format_name()},
        {"library",
         {{"component_count", library.components().size()},
          {"digest", bundle_library_digest.value()},
          {"namespace", library.identity().namespace_name()},
          {"part_count", library.parts().size()},
          {"schema_version", static_cast<std::uint32_t>(library.identity().schema_version())},
          {"snapshot_digest", library.digest().value()},
          {"version", library.identity().version()}}},
        {"producer",
         {{"name", producer_name},
          {"version", static_cast<std::uint32_t>(PartLibraryBundleProducerVersion::V1)}}},
        {"schema_version", static_cast<std::uint32_t>(PartLibraryBundleSchemaVersion::V1)},
        {"selected_roots", selected_roots},
    };
}

[[nodiscard]] ContentHash content_digest(const Json &core,
                                         std::span<const PartLibraryBundleEntry> entries,
                                         const std::map<std::string, std::string> &payloads_by_id) {
    auto canonical = std::string{};
    append_sized(canonical, core.dump());
    append_u64(canonical, static_cast<std::uint64_t>(entries.size()));
    for (const auto &entry : entries) {
        const auto payload = payloads_by_id.find(entry.id());
        require_bundle(payload != payloads_by_id.end(), "PartLibraryBundle payload is missing",
                       ErrorCode::UnknownEntity);
        append_sized(canonical, entry.path());
        append_sized(canonical, payload->second);
    }
    return sha256_content_hash(canonical);
}

[[nodiscard]] ContentHash
bundle_library_digest(const PartLibrary &library, std::span<const PartLibraryBundleEntry> entries,
                      const std::map<std::string, std::string> &payloads_by_id) {
    auto entry_documents = Json::array();
    for (const auto &entry : entries) {
        entry_documents.push_back(entry_json(entry));
    }
    const auto identity = Json{
        {"entries", std::move(entry_documents)},
        {"format", "volt.part-library-bundle-library"},
        {"library",
         {{"namespace", library.identity().namespace_name()},
          {"schema_version", static_cast<std::uint32_t>(library.identity().schema_version())},
          {"version", library.identity().version()}}},
        {"schema_version", static_cast<std::uint32_t>(PartLibraryBundleSchemaVersion::V1)},
    };
    return content_digest(identity, entries, payloads_by_id);
}

[[nodiscard]] std::string encode_archive(const Json &manifest,
                                         std::span<const PartLibraryBundleEntry> entries,
                                         const std::map<std::string, std::string> &payloads_by_id) {
    auto result = std::string{archive_magic};
    append_sized(result, manifest.dump());
    append_u64(result, static_cast<std::uint64_t>(entries.size()));
    for (const auto &entry : entries) {
        const auto payload = payloads_by_id.find(entry.id());
        require_bundle(payload != payloads_by_id.end(), "PartLibraryBundle payload is missing",
                       ErrorCode::UnknownEntity);
        append_sized(result, entry.path());
        append_sized(result, payload->second);
    }
    return result;
}

[[nodiscard]] std::string library_part_reference_document(const LibraryPartRef &reference) {
    return Json{{"format", "volt.library-part-ref"},
                {"library_digest", reference.library_digest().value()},
                {"library_namespace", reference.library_namespace()},
                {"library_version", reference.library_version()},
                {"part_digest", reference.part_digest().value()},
                {"part_key", reference.part_key().value()},
                {"schema_version", 1}}
        .dump();
}

[[nodiscard]] bool same_reference(const LibraryPartRef &lhs, const LibraryPartRef &rhs) {
    return lhs.library_namespace() == rhs.library_namespace() &&
           lhs.library_version() == rhs.library_version() && lhs.part_key() == rhs.part_key() &&
           lhs.library_digest() == rhs.library_digest() && lhs.part_digest() == rhs.part_digest();
}

class ArchivedAssetResolver final : public PartAssetResolver {
  public:
    void add(const PartAssetReference &reference, std::string bytes) {
        const auto key = asset_id(reference);
        const auto [unused, inserted] = bytes_.try_emplace(key, std::move(bytes));
        static_cast<void>(unused);
        require_bundle(inserted, "PartLibraryBundle asset key is duplicated",
                       ErrorCode::DuplicateName);
    }

    [[nodiscard]] std::optional<std::string>
    resolve(const PartAssetReference &reference) const override {
        const auto match = bytes_.find(asset_id(reference));
        return match == bytes_.end() ? std::nullopt : std::optional{match->second};
    }

  private:
    std::map<std::string, std::string> bytes_;
};

struct DecodedArchive {
    Json manifest;
    std::map<std::string, std::string> payloads_by_path;
};

[[nodiscard]] DecodedArchive decode_archive(std::string_view bytes) {
    require_bundle(bytes.size() >= archive_magic.size(), "PartLibraryBundle archive is truncated");
    require_bundle(bytes.substr(0U, archive_magic.size()) == archive_magic,
                   "PartLibraryBundle archive magic is invalid");
    auto cursor = archive_magic.size();
    const auto manifest_bytes = read_sized(bytes, cursor, max_manifest_bytes, "manifest");
    auto manifest = Json{};
    try {
        manifest = Json::parse(manifest_bytes);
    } catch (const Json::exception &error) {
        fail("PartLibraryBundle manifest JSON is invalid: " + std::string{error.what()});
    }
    require_bundle(manifest.dump() == manifest_bytes,
                   "PartLibraryBundle manifest bytes are not canonical JSON");
    const auto entry_count = read_u64(bytes, cursor);
    require_bundle(entry_count <= max_entry_count,
                   "PartLibraryBundle entry count exceeds its limit");
    auto payloads = std::map<std::string, std::string>{};
    auto previous_path = std::optional<std::string>{};
    for (auto index = std::uint64_t{0}; index < entry_count; ++index) {
        auto path = read_sized(bytes, cursor, max_path_bytes, "path");
        require_canonical_relative_path(path);
        require_bundle(!previous_path.has_value() || *previous_path < path,
                       "PartLibraryBundle archive entries must use canonical path order");
        previous_path = path;
        auto payload =
            read_sized(bytes, cursor, std::numeric_limits<std::uint64_t>::max(), "payload");
        const auto [unused, inserted] = payloads.emplace(std::move(path), std::move(payload));
        static_cast<void>(unused);
        require_bundle(inserted, "PartLibraryBundle archive contains a duplicate path",
                       ErrorCode::DuplicateName);
    }
    require_bundle(cursor == bytes.size(), "PartLibraryBundle archive contains extraneous bytes");
    return DecodedArchive{std::move(manifest), std::move(payloads)};
}

void require_object_fields(const Json &object, std::initializer_list<std::string_view> fields,
                           std::string_view label) {
    require_bundle(object.is_object(),
                   "PartLibraryBundle " + std::string{label} + " must be an object");
    auto expected = std::set<std::string>{};
    for (const auto field : fields) {
        expected.emplace(field);
    }
    auto actual = std::set<std::string>{};
    for (auto item = object.begin(); item != object.end(); ++item) {
        actual.insert(item.key());
    }
    require_bundle(actual == expected,
                   "PartLibraryBundle " + std::string{label} + " fields do not match the schema");
}

[[nodiscard]] std::string required_string(const Json &object, std::string_view field) {
    const auto match = object.find(field);
    require_bundle(match != object.end() && match->is_string(),
                   "PartLibraryBundle field must be a string: " + std::string{field});
    return match->get<std::string>();
}

[[nodiscard]] std::uint32_t required_u32(const Json &object, std::string_view field) {
    const auto match = object.find(field);
    require_bundle(match != object.end() && match->is_number_unsigned(),
                   "PartLibraryBundle field must be an unsigned integer: " + std::string{field});
    const auto value = match->get<std::uint64_t>();
    require_bundle(value <= std::numeric_limits<std::uint32_t>::max(),
                   "PartLibraryBundle integer field is out of range");
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::size_t required_size(const Json &object, std::string_view field) {
    const auto value = required_u32(object, field);
    return static_cast<std::size_t>(value);
}

[[nodiscard]] std::vector<std::string> required_string_array(const Json &object,
                                                             std::string_view field) {
    const auto match = object.find(field);
    require_bundle(match != object.end() && match->is_array(),
                   "PartLibraryBundle field must be an array: " + std::string{field});
    auto result = std::vector<std::string>{};
    result.reserve(match->size());
    for (const auto &value : *match) {
        require_bundle(value.is_string(),
                       "PartLibraryBundle string array contains another value kind");
        result.push_back(value.get<std::string>());
    }
    require_bundle(std::ranges::is_sorted(result),
                   "PartLibraryBundle string array must use canonical order");
    require_bundle(std::ranges::adjacent_find(result) == result.end(),
                   "PartLibraryBundle string array contains duplicates", ErrorCode::DuplicateName);
    return result;
}

[[nodiscard]] std::string expected_path(const PartLibraryBundleEntry &entry) {
    switch (entry.role()) {
    case PartLibraryBundleEntryRole::ComponentDefinition:
        require_bundle(entry.semantic_identity().has_value(),
                       "Component bundle entry requires a semantic identity");
        return component_path(*entry.semantic_identity());
    case PartLibraryBundleEntryRole::PartDefinition:
        require_bundle(entry.semantic_identity().has_value(),
                       "Part bundle entry requires a semantic identity");
        return part_path(*entry.semantic_identity());
    case PartLibraryBundleEntryRole::LibraryPartReference: {
        constexpr auto prefix = std::string_view{"reference:"};
        require_bundle(entry.id().starts_with(prefix), "LibraryPartRef entry ID is invalid");
        return reference_path(PartKey{entry.id().substr(prefix.size())});
    }
    case PartLibraryBundleEntryRole::Symbol:
    case PartLibraryBundleEntryRole::Footprint:
    case PartLibraryBundleEntryRole::Model3D:
    case PartLibraryBundleEntryRole::Simulation:
    case PartLibraryBundleEntryRole::Evidence:
    case PartLibraryBundleEntryRole::Licence:
    case PartLibraryBundleEntryRole::Provenance:
        require_bundle(entry.source_key().has_value(), "Asset bundle entry requires a source key");
        {
            const auto reference = PartAssetReference{asset_kind_for_role(entry.role()),
                                                      *entry.source_key(), entry.digest()};
            require_bundle(entry.id() == asset_id(reference),
                           "PartLibraryBundle asset entry ID is not canonical");
            return asset_path(reference);
        }
    }
    fail("PartLibraryBundle entry role is unsupported");
}

[[nodiscard]] std::vector<PartLibraryBundleEntry>
parse_entries(const Json &manifest, const std::map<std::string, std::string> &payloads_by_path) {
    const auto documents = manifest.find("entries");
    require_bundle(documents != manifest.end() && documents->is_array(),
                   "PartLibraryBundle entries must be an array");
    require_bundle(documents->size() <= max_entry_count,
                   "PartLibraryBundle manifest entry count exceeds its limit");
    auto result = std::vector<PartLibraryBundleEntry>{};
    auto ids = std::set<std::string>{};
    auto paths = std::set<std::string>{};
    auto asset_keys = std::set<std::pair<PartLibraryBundleEntryRole, std::string>>{};
    for (const auto &document : *documents) {
        require_object_fields(
            document,
            {"dependencies", "digest", "id", "path", "role", "semantic_identity", "source_key"},
            "entry");
        const auto id = required_string(document, "id");
        const auto path = required_string(document, "path");
        require_canonical_relative_path(path);
        require_bundle(ids.insert(id).second, "PartLibraryBundle manifest contains a duplicate ID",
                       ErrorCode::DuplicateName);
        require_bundle(paths.insert(path).second,
                       "PartLibraryBundle manifest contains a duplicate or aliased path",
                       ErrorCode::DuplicateName);
        const auto role = role_from_name(required_string(document, "role"));
        auto semantic_identity = std::optional<ContentHash>{};
        if (!document.at("semantic_identity").is_null()) {
            require_bundle(document.at("semantic_identity").is_string(),
                           "PartLibraryBundle semantic identity must be a string or null");
            semantic_identity.emplace(document.at("semantic_identity").get<std::string>());
        }
        auto source_key = std::optional<std::string>{};
        if (!document.at("source_key").is_null()) {
            require_bundle(document.at("source_key").is_string(),
                           "PartLibraryBundle source key must be a string or null");
            source_key = document.at("source_key").get<std::string>();
            require_bundle(!source_key->empty(), "PartLibraryBundle source key must not be empty");
        }
        if (is_asset_role(role)) {
            require_bundle(source_key.has_value() && !semantic_identity.has_value(),
                           "PartLibraryBundle asset entry fields do not match its role");
            require_bundle(asset_keys.emplace(role, *source_key).second,
                           "PartLibraryBundle manifest contains a duplicate role and asset key",
                           ErrorCode::DuplicateName);
        } else {
            require_bundle(
                !source_key.has_value(),
                "PartLibraryBundle definition/reference entry must not have a source key");
            require_bundle((role == PartLibraryBundleEntryRole::LibraryPartReference) ==
                               !semantic_identity.has_value(),
                           "PartLibraryBundle semantic identity cardinality is invalid");
        }
        const auto payload = payloads_by_path.find(path);
        require_bundle(payload != payloads_by_path.end(),
                       "PartLibraryBundle archive entry is missing", ErrorCode::UnknownEntity);
        auto entry = PartLibraryBundleEntry{id,
                                            role,
                                            path,
                                            ContentHash{required_string(document, "digest")},
                                            std::move(semantic_identity),
                                            std::move(source_key),
                                            required_string_array(document, "dependencies")};
        require_bundle(entry.digest() == sha256_content_hash(payload->second),
                       "PartLibraryBundle entry digest does not match archived bytes",
                       ErrorCode::CrossReferenceViolation);
        require_bundle(entry.path() == expected_path(entry),
                       "PartLibraryBundle entry path is not canonical for its identity");
        result.push_back(std::move(entry));
    }
    require_bundle(result.size() == payloads_by_path.size(),
                   "PartLibraryBundle archive contains an extraneous entry");
    require_bundle(std::ranges::is_sorted(result, {}, &PartLibraryBundleEntry::path),
                   "PartLibraryBundle entries must use canonical path order");
    return result;
}

[[nodiscard]] LibraryPartRef parse_reference(std::string_view bytes) {
    auto document = Json{};
    try {
        document = Json::parse(bytes);
    } catch (const Json::exception &error) {
        fail("PartLibraryBundle LibraryPartRef JSON is invalid: " + std::string{error.what()});
    }
    require_object_fields(document,
                          {"format", "library_digest", "library_namespace", "library_version",
                           "part_digest", "part_key", "schema_version"},
                          "LibraryPartRef");
    require_bundle(required_string(document, "format") == "volt.library-part-ref",
                   "PartLibraryBundle LibraryPartRef format is unsupported");
    require_bundle(required_u32(document, "schema_version") == 1U,
                   "PartLibraryBundle LibraryPartRef schema is unsupported");
    return LibraryPartRef{required_string(document, "library_namespace"),
                          required_string(document, "library_version"),
                          PartKey{required_string(document, "part_key")},
                          ContentHash{required_string(document, "library_digest")},
                          ContentHash{required_string(document, "part_digest")}};
}

void require_component_document_shape(const Circuit &owner) {
    require_bundle(
        owner.all<ComponentDefId>().size() == 1U,
        "PartLibraryBundle component entry must contain exactly one component definition");
    require_bundle(owner.all<PinDefId>().size() >= 1U,
                   "PartLibraryBundle component entry must contain its pin definitions");
    require_bundle(
        owner.all<ComponentId>().size() == 0U && owner.all<PinId>().size() == 0U &&
            owner.all<NetId>().size() == 0U && owner.all<ModuleDefId>().size() == 0U &&
            owner.all<TemplateNetDefId>().size() == 0U && owner.all<PortDefId>().size() == 0U &&
            owner.all<ModuleComponentId>().size() == 0U &&
            owner.all<ModuleInstanceId>().size() == 0U && owner.all<PortBindingId>().size() == 0U &&
            owner.all<NetClassId>().size() == 0U,
        "PartLibraryBundle component entry contains extraneous circuit state");
}

[[nodiscard]] const PartLibraryBundleEntry &
entry_by_id(const std::map<std::string, const PartLibraryBundleEntry *> &entries,
            const std::string &id) {
    const auto match = entries.find(id);
    require_bundle(match != entries.end(), "PartLibraryBundle dependency ID is missing",
                   ErrorCode::UnknownEntity);
    return *match->second;
}

} // namespace

PartLibraryBundleAttachment::PartLibraryBundleAttachment(PartKey part, PartAssetReference reference)
    : part_{std::move(part)}, reference_{std::move(reference)} {
    require_bundle(is_optional_attachment_role(role_for_asset(reference_.kind())),
                   "PartLibraryBundle explicit attachment role is not optional attachment data");
}

PartLibraryBundleEntry::PartLibraryBundleEntry(std::string id, PartLibraryBundleEntryRole role,
                                               std::string path, ContentHash digest,
                                               std::optional<ContentHash> semantic_identity,
                                               std::optional<std::string> source_key,
                                               std::vector<std::string> dependencies)
    : id_{std::move(id)}, role_{role}, path_{std::move(path)}, digest_{std::move(digest)},
      semantic_identity_{std::move(semantic_identity)}, source_key_{std::move(source_key)},
      dependencies_{std::move(dependencies)} {
    require_bundle(!id_.empty(), "PartLibraryBundle entry ID must not be empty");
    static_cast<void>(role_name(role_));
    require_canonical_relative_path(path_);
    require_bundle(std::ranges::is_sorted(dependencies_),
                   "PartLibraryBundle dependencies must use canonical order");
    require_bundle(std::ranges::adjacent_find(dependencies_) == dependencies_.end(),
                   "PartLibraryBundle dependencies must not contain duplicates",
                   ErrorCode::DuplicateName);
}

PartLibraryBundle::PartLibraryBundle(std::string bytes, ContentHash library_digest,
                                     PartLibrary library,
                                     std::vector<PartLibraryBundleEntry> entries,
                                     std::map<std::string, std::string> payloads_by_id)
    : bytes_{std::move(bytes)}, library_digest_{std::move(library_digest)},
      library_{std::move(library)}, entries_{std::move(entries)},
      payloads_by_id_{std::move(payloads_by_id)}, digest_{sha256_content_hash(bytes_)} {}

PartLibraryBundle
PartLibraryBundle::build(const PartLibraryBuilder &builder, std::span<const PartKey> selected_parts,
                         const PartAssetResolver &asset_resolver,
                         std::span<const PartLibraryBundleAttachment> attachments) {
    auto selected_keys = std::vector<PartKey>{selected_parts.begin(), selected_parts.end()};
    std::ranges::sort(selected_keys);
    require_bundle(std::ranges::adjacent_find(selected_keys) == selected_keys.end(),
                   "PartLibraryBundle selected part keys contain duplicates",
                   ErrorCode::DuplicateName);

    auto selected_definitions = std::vector<PartDefinition>{};
    selected_definitions.reserve(selected_keys.size());
    for (const auto &key : selected_keys) {
        const auto match =
            std::ranges::find(builder.parts(), key.value(), [](const PartDefinition &candidate) {
                return candidate.identity().name();
            });
        require_bundle(match != builder.parts().end(),
                       "PartLibraryBundle selected part key does not exist",
                       ErrorCode::UnknownEntity);
        selected_definitions.push_back(*match);
    }

    auto component_specs = std::map<std::string, ComponentSpec>{};
    auto component_documents = std::map<std::string, std::string>{};
    for (const auto &spec : builder.component_specs()) {
        auto owner = Circuit{};
        const auto id = owner.define_component(spec);
        const auto &definition = owner.get(id);
        const auto digest = definition.content_identity().value();
        const auto [unused, inserted] = component_specs.emplace(digest, spec);
        static_cast<void>(unused);
        require_bundle(inserted, "PartLibraryBundle component specifications contain duplicates",
                       ErrorCode::DuplicateName);
        component_documents.emplace(digest, write_logical_circuit(owner));
    }

    auto selected_components = std::vector<ComponentDefinition>{};
    for (const auto &part : selected_definitions) {
        const auto existing = std::ranges::find(selected_components, part.implemented_component(),
                                                &ComponentDefinition::content_identity);
        if (existing != selected_components.end()) {
            continue;
        }
        const auto component = std::ranges::find(builder.components(), part.implemented_component(),
                                                 &ComponentDefinition::content_identity);
        require_bundle(component != builder.components().end(),
                       "PartLibraryBundle selected part has no component definition",
                       ErrorCode::CrossReferenceViolation);
        require_bundle(component_specs.contains(component->content_identity().value()),
                       "PartLibraryBundle selected component has no complete ComponentSpec",
                       ErrorCode::InvalidState);
        selected_components.push_back(*component);
    }
    std::ranges::sort(selected_components, {}, [](const ComponentDefinition &component) {
        return component.contract().key();
    });

    auto closure_builder = PartLibraryBuilder{builder.identity()};
    for (const auto &component : selected_components) {
        closure_builder.add_component(component);
    }
    for (const auto &part : selected_definitions) {
        closure_builder.add_part(part);
    }
    auto library = closure_builder.build(asset_resolver);

    auto entries = std::vector<PartLibraryBundleEntry>{};
    auto payloads = std::map<std::string, std::string>{};
    auto add_entry = [&](PartLibraryBundleEntry entry, std::string payload) {
        const auto [unused, inserted] = payloads.emplace(entry.id(), std::move(payload));
        static_cast<void>(unused);
        require_bundle(inserted, "PartLibraryBundle build produced a duplicate entry ID",
                       ErrorCode::DuplicateName);
        entries.push_back(std::move(entry));
    };

    for (const auto &component : selected_components) {
        const auto document = component_documents.find(component.content_identity().value());
        require_bundle(document != component_documents.end(),
                       "PartLibraryBundle component document is missing", ErrorCode::UnknownEntity);
        add_entry(PartLibraryBundleEntry{component_id(component),
                                         PartLibraryBundleEntryRole::ComponentDefinition,
                                         component_path(component.content_identity()),
                                         sha256_content_hash(document->second),
                                         component.content_identity(),
                                         std::nullopt,
                                         {}},
                  document->second);
    }

    struct ResolvedAsset {
        PartAssetReference reference;
        std::string bytes;
    };

    auto resolved_assets = std::map<std::string, ResolvedAsset>{};
    auto resolve_asset = [&](const PartAssetReference &reference) {
        const auto id = asset_id(reference);
        const auto existing = resolved_assets.find(id);
        if (existing != resolved_assets.end()) {
            require_bundle(existing->second.reference.digest() == reference.digest(),
                           "PartLibraryBundle asset role and key resolve to conflicting digests",
                           ErrorCode::CrossReferenceViolation);
            return id;
        }
        const auto bytes = asset_resolver.resolve(reference);
        require_bundle(bytes.has_value(), "PartLibraryBundle selected asset is unavailable",
                       ErrorCode::UnknownEntity);
        require_bundle(sha256_content_hash(*bytes) == reference.digest(),
                       "PartLibraryBundle selected asset digest does not match its bytes",
                       ErrorCode::CrossReferenceViolation);
        resolved_assets.emplace(id, ResolvedAsset{reference, *bytes});
        return id;
    };

    auto part_dependencies = std::map<std::string, std::vector<std::string>>{};
    for (const auto &part : selected_definitions) {
        auto dependencies = std::vector{
            component_id(*std::ranges::find(selected_components, part.implemented_component(),
                                            &ComponentDefinition::content_identity))};
        for (const auto &reference : part_asset_references(part)) {
            dependencies.push_back(resolve_asset(reference));
        }
        for (const auto &attachment : attachments) {
            if (attachment.part() == PartKey{part.identity().name()}) {
                dependencies.push_back(resolve_asset(attachment.reference()));
            }
        }
        std::ranges::sort(dependencies);
        require_bundle(std::ranges::adjacent_find(dependencies) == dependencies.end(),
                       "PartLibraryBundle part dependency edge is duplicated",
                       ErrorCode::DuplicateName);
        for (const auto &record : part.electrical_records().records()) {
            for (const auto &evidence : record.evidence()) {
                const auto present = std::ranges::any_of(dependencies, [&](const auto &id) {
                    const auto asset = resolved_assets.find(id);
                    return asset != resolved_assets.end() &&
                           asset->second.reference.kind() == PartAssetKind::Evidence &&
                           asset->second.reference.digest() == evidence;
                });
                require_bundle(
                    present,
                    "PartLibraryBundle electrical evidence is missing from the exact closure",
                    ErrorCode::CrossReferenceViolation);
            }
        }
        part_dependencies.emplace(part.identity().name(), std::move(dependencies));
    }

    for (const auto &[id, asset] : resolved_assets) {
        add_entry(PartLibraryBundleEntry{id,
                                         role_for_asset(asset.reference.kind()),
                                         asset_path(asset.reference),
                                         asset.reference.digest(),
                                         std::nullopt,
                                         asset.reference.key(),
                                         {}},
                  asset.bytes);
    }

    for (const auto &part : selected_definitions) {
        const auto artifact = write_part_definition(part);
        const auto part_entry_id = part_id(part);
        add_entry(PartLibraryBundleEntry{part_entry_id, PartLibraryBundleEntryRole::PartDefinition,
                                         part_path(part.content_identity()),
                                         sha256_content_hash(artifact), part.content_identity(),
                                         std::nullopt,
                                         part_dependencies.at(part.identity().name())},
                  artifact);
    }

    std::ranges::sort(entries, {}, &PartLibraryBundleEntry::path);
    const auto closure_digest = bundle_library_digest(library, entries, payloads);
    auto selected_roots = std::vector<std::string>{};
    for (const auto &part : selected_definitions) {
        const auto part_entry_id = part_id(part);
        const auto key = PartKey{part.identity().name()};
        const auto reference =
            LibraryPartRef{library.identity().namespace_name(), library.identity().version(), key,
                           closure_digest, part.content_identity()};
        const auto reference_entry_id = reference_id(key);
        const auto reference_document = library_part_reference_document(reference);
        add_entry(PartLibraryBundleEntry{reference_entry_id,
                                         PartLibraryBundleEntryRole::LibraryPartReference,
                                         reference_path(key),
                                         sha256_content_hash(reference_document),
                                         std::nullopt,
                                         std::nullopt,
                                         {part_entry_id}},
                  reference_document);
        selected_roots.push_back(reference_entry_id);
    }

    std::ranges::sort(entries, {}, &PartLibraryBundleEntry::path);
    std::ranges::sort(selected_roots);
    auto core = manifest_core(library, closure_digest, entries, selected_roots);
    auto manifest = core;
    manifest["content_digest"] = content_digest(core, entries, payloads).value();
    auto bytes = encode_archive(manifest, entries, payloads);
    return PartLibraryBundle{std::move(bytes), closure_digest, std::move(library),
                             std::move(entries), std::move(payloads)};
}

PartLibraryBundle PartLibraryBundle::open(std::string_view bytes) {
    try {
        auto decoded = decode_archive(bytes);
        require_object_fields(decoded.manifest,
                              {"content_digest", "entries", "format", "library", "producer",
                               "schema_version", "selected_roots"},
                              "manifest");
        require_bundle(required_string(decoded.manifest, "format") ==
                           part_library_bundle_format_name(),
                       "PartLibraryBundle manifest format is unsupported");
        require_bundle(required_u32(decoded.manifest, "schema_version") ==
                           static_cast<std::uint32_t>(PartLibraryBundleSchemaVersion::V1),
                       "PartLibraryBundle manifest schema is unsupported");
        const auto &producer = decoded.manifest.at("producer");
        require_object_fields(producer, {"name", "version"}, "producer");
        require_bundle(required_string(producer, "name") == producer_name,
                       "PartLibraryBundle producer is unsupported");
        require_bundle(required_u32(producer, "version") ==
                           static_cast<std::uint32_t>(PartLibraryBundleProducerVersion::V1),
                       "PartLibraryBundle producer version is unsupported");

        auto entries = parse_entries(decoded.manifest, decoded.payloads_by_path);
        auto payloads_by_id = std::map<std::string, std::string>{};
        auto entries_by_id = std::map<std::string, const PartLibraryBundleEntry *>{};
        for (const auto &entry : entries) {
            payloads_by_id.emplace(entry.id(), decoded.payloads_by_path.at(entry.path()));
            entries_by_id.emplace(entry.id(), &entry);
        }

        auto core = decoded.manifest;
        core.erase("content_digest");
        require_bundle(ContentHash{required_string(decoded.manifest, "content_digest")} ==
                           content_digest(core, entries, payloads_by_id),
                       "PartLibraryBundle content digest does not match the manifest and archive",
                       ErrorCode::CrossReferenceViolation);

        const auto &library_document = decoded.manifest.at("library");
        require_object_fields(library_document,
                              {"component_count", "digest", "namespace", "part_count",
                               "schema_version", "snapshot_digest", "version"},
                              "library identity");
        require_bundle(required_u32(library_document, "schema_version") ==
                           static_cast<std::uint32_t>(PartLibrarySchemaVersion::V1),
                       "PartLibraryBundle library schema is unsupported");
        auto builder = PartLibraryBuilder{PartLibraryIdentity{
            required_string(library_document, "namespace"),
            required_string(library_document, "version"), PartLibrarySchemaVersion::V1}};

        auto components_by_id = std::map<std::string, ComponentDefinition>{};
        for (const auto &entry : entries) {
            if (entry.role() != PartLibraryBundleEntryRole::ComponentDefinition) {
                continue;
            }
            auto owner = read_logical_circuit_text(payloads_by_id.at(entry.id()));
            require_component_document_shape(owner);
            require_bundle(write_logical_circuit(owner) == payloads_by_id.at(entry.id()),
                           "PartLibraryBundle component bytes are not canonical");
            const auto &component = owner.get(ComponentDefId{0});
            require_bundle(entry.id() == component_id(component),
                           "PartLibraryBundle component key does not match its entry ID",
                           ErrorCode::CrossReferenceViolation);
            require_bundle(entry.semantic_identity().has_value() &&
                               *entry.semantic_identity() == component.content_identity(),
                           "PartLibraryBundle component semantic identity does not match its bytes",
                           ErrorCode::CrossReferenceViolation);
            components_by_id.emplace(entry.id(), component);
            builder.add_component(component);
        }
        require_bundle(components_by_id.size() ==
                           required_size(library_document, "component_count"),
                       "PartLibraryBundle component cardinality does not match its manifest");

        auto parts_by_id = std::map<std::string, PartDefinition>{};
        for (const auto &entry : entries) {
            if (entry.role() != PartLibraryBundleEntryRole::PartDefinition) {
                continue;
            }
            auto component_dependencies = std::vector<std::string>{};
            for (const auto &dependency : entry.dependencies()) {
                if (entry_by_id(entries_by_id, dependency).role() ==
                    PartLibraryBundleEntryRole::ComponentDefinition) {
                    component_dependencies.push_back(dependency);
                }
            }
            require_bundle(
                component_dependencies.size() == 1U,
                "PartLibraryBundle part must depend on exactly one component definition");
            const auto component = components_by_id.find(component_dependencies.front());
            require_bundle(component != components_by_id.end(),
                           "PartLibraryBundle part component dependency is missing",
                           ErrorCode::UnknownEntity);
            auto part = read_part_definition_text(payloads_by_id.at(entry.id()), component->second);
            require_bundle(write_part_definition(part) == payloads_by_id.at(entry.id()),
                           "PartLibraryBundle part bytes are not canonical");
            require_bundle(entry.id() == part_id(part),
                           "PartLibraryBundle part key does not match its entry ID",
                           ErrorCode::CrossReferenceViolation);
            require_bundle(entry.semantic_identity().has_value() &&
                               *entry.semantic_identity() == part.content_identity(),
                           "PartLibraryBundle part semantic identity does not match its bytes",
                           ErrorCode::CrossReferenceViolation);

            auto expected_dependencies = std::vector{component_dependencies.front()};
            for (const auto &reference : part_asset_references(part)) {
                const auto id = asset_id(reference);
                const auto &asset_entry = entry_by_id(entries_by_id, id);
                require_bundle(asset_entry.role() == role_for_asset(reference.kind()) &&
                                   asset_entry.source_key() == std::optional{reference.key()} &&
                                   asset_entry.digest() == reference.digest(),
                               "PartLibraryBundle recorded asset does not match the exact part",
                               ErrorCode::CrossReferenceViolation);
                expected_dependencies.push_back(id);
            }
            for (const auto &dependency : entry.dependencies()) {
                const auto &dependency_entry = entry_by_id(entries_by_id, dependency);
                if (is_optional_attachment_role(dependency_entry.role())) {
                    expected_dependencies.push_back(dependency);
                }
            }
            for (const auto &record : part.electrical_records().records()) {
                for (const auto &evidence : record.evidence()) {
                    const auto present =
                        std::ranges::any_of(entry.dependencies(), [&](const auto &dependency) {
                            const auto &candidate = entry_by_id(entries_by_id, dependency);
                            return candidate.role() == PartLibraryBundleEntryRole::Evidence &&
                                   candidate.digest() == evidence;
                        });
                    require_bundle(
                        present,
                        "PartLibraryBundle electrical evidence is missing from the exact closure",
                        ErrorCode::CrossReferenceViolation);
                }
            }
            std::ranges::sort(expected_dependencies);
            require_bundle(
                std::ranges::adjacent_find(expected_dependencies) == expected_dependencies.end(),
                "PartLibraryBundle part has duplicate dependency roles", ErrorCode::DuplicateName);
            require_bundle(std::ranges::equal(entry.dependencies(), expected_dependencies),
                           "PartLibraryBundle part dependency closure is incomplete or extraneous",
                           ErrorCode::CrossReferenceViolation);
            parts_by_id.emplace(entry.id(), part);
            builder.add_part(std::move(part));
        }
        require_bundle(parts_by_id.size() == required_size(library_document, "part_count"),
                       "PartLibraryBundle part cardinality does not match its manifest");

        auto resolver = ArchivedAssetResolver{};
        for (const auto &entry : entries) {
            if (!is_asset_role(entry.role())) {
                continue;
            }
            resolver.add(PartAssetReference{asset_kind_for_role(entry.role()), *entry.source_key(),
                                            entry.digest()},
                         payloads_by_id.at(entry.id()));
        }
        auto library = builder.build(resolver);
        require_bundle(library.digest() ==
                           ContentHash{required_string(library_document, "snapshot_digest")},
                       "PartLibraryBundle snapshot digest does not match reopened meaning",
                       ErrorCode::CrossReferenceViolation);

        auto closure_entries = std::vector<PartLibraryBundleEntry>{};
        for (const auto &entry : entries) {
            if (entry.role() != PartLibraryBundleEntryRole::LibraryPartReference) {
                closure_entries.push_back(entry);
            }
        }
        const auto closure_digest = bundle_library_digest(library, closure_entries, payloads_by_id);
        require_bundle(closure_digest == ContentHash{required_string(library_document, "digest")},
                       "PartLibraryBundle library digest does not match its exact closure",
                       ErrorCode::CrossReferenceViolation);

        auto reference_ids = std::vector<std::string>{};
        for (const auto &entry : entries) {
            if (entry.role() != PartLibraryBundleEntryRole::LibraryPartReference) {
                continue;
            }
            require_bundle(entry.dependencies().size() == 1U,
                           "PartLibraryBundle LibraryPartRef must depend on exactly one part");
            const auto &part_entry = entry_by_id(entries_by_id, entry.dependencies().front());
            require_bundle(part_entry.role() == PartLibraryBundleEntryRole::PartDefinition,
                           "PartLibraryBundle LibraryPartRef dependency is not a part definition");
            const auto recorded = parse_reference(payloads_by_id.at(entry.id()));
            require_bundle(library_part_reference_document(recorded) ==
                               payloads_by_id.at(entry.id()),
                           "PartLibraryBundle LibraryPartRef bytes are not canonical");
            const auto part = library.part(recorded.part_key());
            require_bundle(part.has_value(), "PartLibraryBundle LibraryPartRef part key is missing",
                           ErrorCode::UnknownEntity);
            const auto expected =
                LibraryPartRef{library.identity().namespace_name(), library.identity().version(),
                               recorded.part_key(), closure_digest, part->get().content_identity()};
            require_bundle(same_reference(recorded, expected),
                           "PartLibraryBundle LibraryPartRef does not resolve exactly",
                           ErrorCode::CrossReferenceViolation);
            require_bundle(entry.id() == reference_id(recorded.part_key()) &&
                               part_entry.id() == "part:" + recorded.part_key().value(),
                           "PartLibraryBundle LibraryPartRef key mapping is inconsistent",
                           ErrorCode::CrossReferenceViolation);
            reference_ids.push_back(entry.id());
        }
        std::ranges::sort(reference_ids);
        const auto selected_roots = required_string_array(decoded.manifest, "selected_roots");
        require_bundle(reference_ids == selected_roots,
                       "PartLibraryBundle selected roots do not match its exact references",
                       ErrorCode::CrossReferenceViolation);

        auto reachable = std::set<std::string>{};
        auto pending = selected_roots;
        while (!pending.empty()) {
            auto id = std::move(pending.back());
            pending.pop_back();
            if (!reachable.insert(id).second) {
                continue;
            }
            const auto &entry = entry_by_id(entries_by_id, id);
            pending.insert(pending.end(), entry.dependencies().begin(), entry.dependencies().end());
        }
        require_bundle(
            reachable.size() == entries.size(),
            "PartLibraryBundle contains entries outside the exact selected dependency closure",
            ErrorCode::CrossReferenceViolation);

        return PartLibraryBundle{std::string{bytes}, closure_digest, std::move(library),
                                 std::move(entries), std::move(payloads_by_id)};
    } catch (const KernelError &) {
        throw;
    } catch (const std::exception &error) {
        throw KernelLogicError{ErrorCode::InvalidArgument,
                               "PartLibraryBundle reopen failed: " + std::string{error.what()}};
    }
}

LibraryPartRef PartLibraryBundle::require(const PartKey &key) const {
    const auto part = library_.part(key);
    if (!part.has_value()) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Part key does not exist in this library bundle"};
    }
    return LibraryPartRef{library_.identity().namespace_name(), library_.identity().version(), key,
                          library_digest_, part->get().content_identity()};
}

const PartDefinition &PartLibraryBundle::resolve(const LibraryPartRef &reference) const & {
    if (reference.library_namespace() != library_.identity().namespace_name() ||
        reference.library_version() != library_.identity().version()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "LibraryPartRef identifies another library bundle release"};
    }
    if (reference.library_digest() != library_digest_) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "LibraryPartRef bundle digest does not match this closure"};
    }
    const auto part = library_.part(reference.part_key());
    if (!part.has_value()) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "LibraryPartRef part key does not exist in this bundle"};
    }
    if (reference.part_digest() != part->get().content_identity()) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "LibraryPartRef part digest does not match the exact part"};
    }
    return part->get();
}

std::optional<std::string_view>
PartLibraryBundle::asset(const PartAssetReference &reference) const & noexcept {
    const auto id = asset_id(reference);
    const auto entry = std::ranges::find(entries_, id, &PartLibraryBundleEntry::id);
    if (entry == entries_.end() || entry->digest() != reference.digest()) {
        return std::nullopt;
    }
    const auto payload = payloads_by_id_.find(id);
    if (payload == payloads_by_id_.end()) {
        return std::nullopt;
    }
    return std::string_view{payload->second};
}

} // namespace volt::io
