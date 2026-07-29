#pragma once

#include <compare>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <volt/io/project_bundle.hpp>

namespace volt::io::detail {

inline constexpr auto project_bundle_manifest_path = std::string_view{"manifest.volt.json"};

struct FileIdentity {
    std::uint64_t first;
    std::uint64_t second;

    [[nodiscard]] auto operator<=>(const FileIdentity &) const = default;
};

struct CapturedEntry {
    std::string bytes;
    std::optional<FileIdentity> identity;
};

class BundleSource {
  public:
    virtual ~BundleSource() = default;
    [[nodiscard]] virtual ProjectBundleStorageKind kind() const noexcept = 0;
    [[nodiscard]] virtual CapturedEntry read(std::string_view path) const = 0;
    [[nodiscard]] virtual std::vector<std::string> paths() const = 0;
};

[[nodiscard]] std::unique_ptr<BundleSource>
open_project_bundle_source(const std::filesystem::path &path);

} // namespace volt::io::detail
