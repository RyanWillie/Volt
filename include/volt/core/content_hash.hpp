#pragma once

#include <string>
#include <string_view>

namespace volt {

/** Canonical content-address label, currently limited to lowercase `sha256:<hex>`. */
class ContentHash {
  public:
    /** Construct a canonical content hash label such as `sha256:<64 lowercase hex digits>`. */
    explicit ContentHash(std::string value);

    /** Return the canonical hash label. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Return whether two content hashes carry the same canonical label. */
    [[nodiscard]] friend bool operator==(const ContentHash &lhs, const ContentHash &rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

  private:
    std::string value_;
};

/** Hash bytes with SHA-256 and return the canonical `sha256:<hex>` content label. */
[[nodiscard]] ContentHash sha256_content_hash(std::string_view bytes);

} // namespace volt
