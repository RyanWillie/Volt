#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <volt/library/part_library.hpp>

namespace volt::io {

/** Supported deterministic PartLibraryBundle manifest and archive schema. */
enum class PartLibraryBundleSchemaVersion : std::uint32_t {
    V1 = 1,
};

/** Supported native producer contract for deterministic PartLibraryBundle bytes. */
enum class PartLibraryBundleProducerVersion : std::uint32_t {
    V1 = 1,
};

/** Closed stable roles admitted by a PartLibraryBundle manifest. */
enum class PartLibraryBundleEntryRole : std::uint32_t {
    ComponentDefinition = 1,
    PartDefinition = 2,
    LibraryPartReference = 3,
    Symbol = 4,
    Footprint = 5,
    Model3D = 6,
    Simulation = 7,
    Evidence = 8,
    Licence = 9,
    Provenance = 10,
};

/** One explicit optional asset attachment owned by an exact selected part. */
class PartLibraryBundleAttachment {
  public:
    /** Attach one simulation, evidence, licence, or provenance record to an exact part key. */
    PartLibraryBundleAttachment(PartKey part, PartAssetReference reference);

    /** Return the exact part that owns this attachment. */
    [[nodiscard]] const PartKey &part() const noexcept { return part_; }

    /** Return the typed content-addressed attachment request. */
    [[nodiscard]] const PartAssetReference &reference() const noexcept { return reference_; }

  private:
    PartKey part_;
    PartAssetReference reference_;
};

/** Immutable typed metadata for one deterministic archive entry. */
class PartLibraryBundleEntry {
  public:
    /** Construct one validated entry record used by build and fail-closed reopen. */
    PartLibraryBundleEntry(std::string id, PartLibraryBundleEntryRole role, std::string path,
                           ContentHash digest, std::optional<ContentHash> semantic_identity,
                           std::optional<std::string> source_key,
                           std::vector<std::string> dependencies);

    /** Return the globally unique manifest entry ID. */
    [[nodiscard]] const std::string &id() const noexcept { return id_; }

    /** Return the closed stable entry role. */
    [[nodiscard]] PartLibraryBundleEntryRole role() const noexcept { return role_; }

    /** Return the safe canonical relative archive path. */
    [[nodiscard]] const std::string &path() const noexcept { return path_; }

    /** Return the digest of the exact archived entry bytes. */
    [[nodiscard]] const ContentHash &digest() const noexcept { return digest_; }

    /** Return the semantic component or part identity, when the role carries one. */
    [[nodiscard]] const std::optional<ContentHash> &semantic_identity() const noexcept {
        return semantic_identity_;
    }

    /** Return the stable source key for an asset entry, when present. */
    [[nodiscard]] const std::optional<std::string> &source_key() const noexcept {
        return source_key_;
    }

    /** Return exact direct dependency entry IDs in stable order. */
    [[nodiscard]] std::span<const std::string> dependencies() const noexcept {
        return dependencies_;
    }

  private:
    std::string id_;
    PartLibraryBundleEntryRole role_;
    std::string path_;
    ContentHash digest_;
    std::optional<ContentHash> semantic_identity_;
    std::optional<std::string> source_key_;
    std::vector<std::string> dependencies_;
};

/** Owning immutable selected-library closure reopened entirely from native archive bytes. */
class PartLibraryBundle {
  public:
    /** Build exactly the requested part closure, or publish no bundle on any failure. */
    [[nodiscard]] static PartLibraryBundle
    build(const PartLibraryBuilder &builder, std::span<const PartKey> selected_parts,
          const PartAssetResolver &asset_resolver,
          std::span<const PartLibraryBundleAttachment> attachments = {});

    /** Reopen and completely validate one deterministic archive before exposing state. */
    [[nodiscard]] static PartLibraryBundle open(std::string_view bytes);

    /** Return the exact deterministic archive bytes. */
    [[nodiscard]] std::string_view bytes() const & noexcept { return bytes_; }

    /** Prevent borrowing archive bytes from a temporary bundle. */
    [[nodiscard]] std::string_view bytes() const && = delete;

    /** Return the SHA-256 digest of the complete archive bytes. */
    [[nodiscard]] const ContentHash &digest() const & noexcept { return digest_; }

    /** Prevent borrowing the whole-bundle digest from a temporary bundle. */
    [[nodiscard]] const ContentHash &digest() const && = delete;

    /** Return the deterministic digest pinned by LibraryPartRefs from this bundle closure. */
    [[nodiscard]] const ContentHash &library_digest() const & noexcept { return library_digest_; }

    /** Prevent borrowing the bundle-library digest from a temporary bundle. */
    [[nodiscard]] const ContentHash &library_digest() const && = delete;

    /** Return the owning immutable native part-library snapshot. */
    [[nodiscard]] const PartLibrary &library() const & noexcept { return library_; }

    /** Prevent borrowing the reopened snapshot from a temporary bundle. */
    [[nodiscard]] const PartLibrary &library() const && = delete;

    /** Pin one reopened exact part to this complete immutable bundle closure. */
    [[nodiscard]] LibraryPartRef require(const PartKey &key) const;

    /** Resolve one exact bundle reference without ambient catalogue fallback. */
    [[nodiscard]] const PartDefinition &resolve(const LibraryPartRef &reference) const &;

    /** Prevent resolving a borrowed part from a temporary bundle. */
    [[nodiscard]] const PartDefinition &resolve(const LibraryPartRef &reference) const && = delete;

    /** Return typed manifest entries in canonical path order. */
    [[nodiscard]] std::span<const PartLibraryBundleEntry> entries() const & noexcept {
        return entries_;
    }

    /** Prevent borrowing manifest entries from a temporary bundle. */
    [[nodiscard]] std::span<const PartLibraryBundleEntry> entries() const && = delete;

    /** Return exact archived bytes for one typed asset reference, if present. */
    [[nodiscard]] std::optional<std::string_view>
    asset(const PartAssetReference &reference) const & noexcept;

    /** Prevent borrowing asset bytes from a temporary bundle. */
    [[nodiscard]] std::optional<std::string_view>
    asset(const PartAssetReference &reference) const && = delete;

  private:
    PartLibraryBundle(std::string bytes, ContentHash library_digest, PartLibrary library,
                      std::vector<PartLibraryBundleEntry> entries,
                      std::map<std::string, std::string> payloads_by_id);

    std::string bytes_;
    ContentHash library_digest_;
    PartLibrary library_;
    std::vector<PartLibraryBundleEntry> entries_;
    std::map<std::string, std::string> payloads_by_id_;
    ContentHash digest_;
};

/** Return the canonical PartLibraryBundle manifest format name. */
[[nodiscard]] inline constexpr std::string_view part_library_bundle_format_name() noexcept {
    return "volt.part-library-bundle";
}

} // namespace volt::io
