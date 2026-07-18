#pragma once

#include <compare>
#include <string>

#include <volt/core/content_hash.hpp>

namespace volt {

/** Stable non-placeholder key for one exact part inside a library snapshot. */
class PartKey {
  public:
    /** Construct a part key from one non-empty portable label. */
    explicit PartKey(std::string value);

    /** Return the portable part-key label. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Compare exact part keys. */
    [[nodiscard]] bool operator==(const PartKey &) const noexcept = default;

    /** Order exact part keys lexicographically. */
    [[nodiscard]] std::strong_ordering operator<=>(const PartKey &) const noexcept = default;

  private:
    std::string value_;
};

/** Circuit-owned exact reference to one part in one immutable library snapshot. */
class LibraryPartRef {
  public:
    /** Construct one complete exact library-part reference. */
    LibraryPartRef(std::string library_namespace, std::string library_version, PartKey part_key,
                   ContentHash library_digest, ContentHash part_digest);

    /** Return the exact library namespace. */
    [[nodiscard]] const std::string &library_namespace() const noexcept {
        return library_namespace_;
    }

    /** Return the exact human library version. */
    [[nodiscard]] const std::string &library_version() const noexcept { return library_version_; }

    /** Return the exact stable part key. */
    [[nodiscard]] const PartKey &part_key() const noexcept { return part_key_; }

    /** Return the immutable library snapshot digest. */
    [[nodiscard]] const ContentHash &library_digest() const noexcept { return library_digest_; }

    /** Return the immutable exact-part digest. */
    [[nodiscard]] const ContentHash &part_digest() const noexcept { return part_digest_; }

    /** Compare complete exact references. */
    [[nodiscard]] bool operator==(const LibraryPartRef &) const noexcept = default;

  private:
    std::string library_namespace_;
    std::string library_version_;
    PartKey part_key_;
    ContentHash library_digest_;
    ContentHash part_digest_;
};

} // namespace volt
