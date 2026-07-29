#include "project_bundle_source.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <zlib.h>

#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace volt::io::detail {
namespace {

constexpr auto max_bundle_entry_size = std::uint64_t{256U * 1024U * 1024U};
constexpr auto max_bundle_total_size = std::uint64_t{1024U * 1024U * 1024U};

[[noreturn]] void fail(ProjectBundleOpenErrorCode code, std::string message) {
    throw ProjectBundleOpenError{code, std::move(message)};
}

[[nodiscard]] bool is_control(unsigned char value) noexcept {
    return value < 0x20U || value == 0x7fU;
}

[[nodiscard]] bool valid_utf8(std::string_view text) noexcept {
    auto index = std::size_t{0};
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7fU) {
            ++index;
            continue;
        }
        auto continuation_count = std::size_t{0};
        auto code_point = std::uint32_t{0};
        if (first >= 0xc2U && first <= 0xdfU) {
            continuation_count = 1U;
            code_point = first & 0x1fU;
        } else if (first >= 0xe0U && first <= 0xefU) {
            continuation_count = 2U;
            code_point = first & 0x0fU;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            continuation_count = 3U;
            code_point = first & 0x07U;
        } else {
            return false;
        }
        if (text.size() - index <= continuation_count) {
            return false;
        }
        for (auto offset = std::size_t{1}; offset <= continuation_count; ++offset) {
            const auto next = static_cast<unsigned char>(text[index + offset]);
            if ((next & 0xc0U) != 0x80U) {
                return false;
            }
            code_point = (code_point << 6U) | (next & 0x3fU);
        }
        const auto overlong = (continuation_count == 1U && code_point < 0x80U) ||
                              (continuation_count == 2U && code_point < 0x800U) ||
                              (continuation_count == 3U && code_point < 0x10000U);
        if (overlong || (code_point >= 0xd800U && code_point <= 0xdfffU) ||
            code_point > 0x10ffffU) {
            return false;
        }
        index += continuation_count + 1U;
    }
    return true;
}

[[nodiscard]] std::vector<std::string> source_path_segments(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string_view::npos ||
        path.find('\0') != std::string_view::npos || (path.size() >= 2U && path[1] == ':') ||
        !valid_utf8(path)) {
        fail(ProjectBundleOpenErrorCode::UnsafePath,
             "ProjectBundle source path is not a safe UTF-8 relative path: " + std::string{path});
    }
    auto result = std::vector<std::string>{};
    auto cursor = std::size_t{0};
    while (cursor <= path.size()) {
        const auto separator = path.find('/', cursor);
        const auto end = separator == std::string_view::npos ? path.size() : separator;
        const auto segment = path.substr(cursor, end - cursor);
        if (segment.empty() || segment == "." || segment == "..") {
            fail(ProjectBundleOpenErrorCode::UnsafePath,
                 "ProjectBundle source path contains an empty or relative segment: " +
                     std::string{path});
        }
        for (const auto value : segment) {
            if (is_control(static_cast<unsigned char>(value))) {
                fail(ProjectBundleOpenErrorCode::UnsafePath,
                     "ProjectBundle source path contains a control byte: " + std::string{path});
            }
        }
        result.emplace_back(segment);
        if (separator == std::string_view::npos) {
            break;
        }
        cursor = separator + 1U;
    }
    return result;
}

#ifdef _WIN32
[[nodiscard]] std::vector<std::string> inventory_directory(const std::filesystem::path &root) {
    auto result = std::vector<std::string>{};
    auto error = std::error_code{};
    auto iterator = std::filesystem::recursive_directory_iterator{
        root, std::filesystem::directory_options::none, error};
    if (error) {
        fail(ProjectBundleOpenErrorCode::UnsafePath,
             "ProjectBundle directory cannot be enumerated safely");
    }
    const auto end = std::filesystem::recursive_directory_iterator{};
    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            fail(ProjectBundleOpenErrorCode::UnsafePath,
                 "ProjectBundle directory changed or cannot be enumerated safely");
        }
        const auto status = iterator->symlink_status(error);
        if (error || std::filesystem::is_symlink(status) ||
            (!std::filesystem::is_directory(status) && !std::filesystem::is_regular_file(status))) {
            fail(ProjectBundleOpenErrorCode::UnsafePath,
                 "ProjectBundle directory contains a symlink-like or unsupported entry");
        }
        const auto relative = iterator->path().lexically_relative(root).generic_string();
        static_cast<void>(source_path_segments(relative));
        if (std::filesystem::is_directory(status)) {
            result.push_back(relative + "/");
            continue;
        }
        result.push_back(relative);
    }
    std::ranges::sort(result);
    return result;
}
#endif

#ifndef _WIN32

class FileDescriptor final {
  public:
    explicit FileDescriptor(int value = -1) noexcept : value_{value} {}

    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;

    FileDescriptor(FileDescriptor &&other) noexcept : value_{std::exchange(other.value_, -1)} {}

    FileDescriptor &operator=(FileDescriptor &&other) noexcept {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, -1);
        }
        return *this;
    }

    ~FileDescriptor() { reset(); }

    [[nodiscard]] int get() const noexcept { return value_; }

    [[nodiscard]] int release() noexcept { return std::exchange(value_, -1); }

  private:
    void reset() noexcept {
        if (value_ >= 0) {
            static_cast<void>(::close(value_));
            value_ = -1;
        }
    }

    int value_;
};

class DirectoryStream final {
  public:
    explicit DirectoryStream(DIR *value) noexcept : value_{value} {}

    DirectoryStream(const DirectoryStream &) = delete;
    DirectoryStream &operator=(const DirectoryStream &) = delete;

    ~DirectoryStream() {
        if (value_ != nullptr) {
            static_cast<void>(::closedir(value_));
        }
    }

    [[nodiscard]] DIR *get() const noexcept { return value_; }

  private:
    DIR *value_;
};

void inventory_directory(int directory, std::string_view prefix, std::vector<std::string> &result) {
    auto opened =
        FileDescriptor{::openat(directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
    if (opened.get() < 0) {
        fail(ProjectBundleOpenErrorCode::UnsafePath,
             "ProjectBundle pinned directory cannot be enumerated safely");
    }
    auto stream = DirectoryStream{::fdopendir(opened.get())};
    if (stream.get() == nullptr) {
        fail(ProjectBundleOpenErrorCode::UnsafePath,
             "ProjectBundle pinned directory cannot be enumerated safely");
    }
    static_cast<void>(opened.release());

    errno = 0;
    while (const auto *entry = ::readdir(stream.get())) {
        const auto name = std::string_view{entry->d_name};
        if (name == "." || name == "..") {
            errno = 0;
            continue;
        }
        const auto relative =
            prefix.empty() ? std::string{name} : std::string{prefix} + "/" + std::string{name};
        static_cast<void>(source_path_segments(relative));
        auto child = FileDescriptor{::openat(directory, std::string{name}.c_str(),
                                             O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
        if (child.get() < 0) {
            fail(ProjectBundleOpenErrorCode::UnsafePath,
                 "ProjectBundle directory changed or contains an unsafe entry");
        }

        struct stat status = {};

        if (::fstat(child.get(), &status) != 0) {
            fail(ProjectBundleOpenErrorCode::UnsafePath,
                 "ProjectBundle directory entry cannot be inspected safely");
        }
        if (S_ISDIR(status.st_mode)) {
            result.push_back(relative + "/");
            inventory_directory(child.get(), relative, result);
        } else if (S_ISREG(status.st_mode)) {
            result.push_back(relative);
        } else {
            fail(ProjectBundleOpenErrorCode::UnsafePath,
                 "ProjectBundle directory contains a symlink-like or unsupported entry");
        }
        errno = 0;
    }
    if (errno != 0) {
        fail(ProjectBundleOpenErrorCode::UnsafePath,
             "ProjectBundle pinned directory changed or cannot be enumerated safely");
    }
}

[[nodiscard]] std::vector<std::string> inventory_directory(int root) {
    auto result = std::vector<std::string>{};
    inventory_directory(root, {}, result);
    std::ranges::sort(result);
    return result;
}

[[nodiscard]] std::string read_fd(int descriptor, std::string_view label,
                                  std::uint64_t maximum_size) {
    auto result = std::string{};
    auto buffer = std::array<char, 64U * 1024U>{};
    while (true) {
        const auto count = ::read(descriptor, buffer.data(), buffer.size());
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            fail(ProjectBundleOpenErrorCode::MissingEntry,
                 "Failed to read ProjectBundle entry " + std::string{label});
        }
        if (static_cast<std::uint64_t>(count) > maximum_size - result.size()) {
            fail(ProjectBundleOpenErrorCode::InputTooLarge,
                 "ProjectBundle input exceeds the supported bounded size: " + std::string{label});
        }
        result.append(buffer.data(), static_cast<std::size_t>(count));
    }
    return result;
}

[[nodiscard]] std::uint64_t checked_file_size(const struct stat &status, std::uint64_t maximum_size,
                                              std::string_view label) {
    if (status.st_size < 0 || static_cast<std::uint64_t>(status.st_size) > maximum_size) {
        fail(ProjectBundleOpenErrorCode::InputTooLarge,
             "ProjectBundle input exceeds the supported bounded size: " + std::string{label});
    }
    return static_cast<std::uint64_t>(status.st_size);
}

class DirectorySource final : public BundleSource {
  public:
    explicit DirectorySource(const std::filesystem::path &root)
        : root_{::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)} {
        if (root_.get() < 0) {
            fail(ProjectBundleOpenErrorCode::UnsupportedStorage,
                 "ProjectBundle directory root is missing, unsafe, or not a directory");
        }
    }

    [[nodiscard]] ProjectBundleStorageKind kind() const noexcept override {
        return ProjectBundleStorageKind::Directory;
    }

    [[nodiscard]] CapturedEntry read(std::string_view path) const override {
        const auto segments = source_path_segments(path);
        auto current = FileDescriptor{::dup(root_.get())};
        if (current.get() < 0) {
            fail(ProjectBundleOpenErrorCode::MissingEntry,
                 "Failed to retain the ProjectBundle directory root");
        }
        for (auto index = std::size_t{0}; index < segments.size(); ++index) {
            const auto final = index + 1U == segments.size();
            const auto flags =
                O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | (final ? 0 : O_DIRECTORY);
            auto next = FileDescriptor{::openat(current.get(), segments[index].c_str(), flags)};
            if (next.get() < 0) {
                fail(errno == ELOOP ? ProjectBundleOpenErrorCode::UnsafePath
                                    : ProjectBundleOpenErrorCode::MissingEntry,
                     "ProjectBundle entry is missing or traverses an unsafe path: " +
                         std::string{path});
            }

            struct stat status = {};
            if (::fstat(next.get(), &status) != 0) {
                fail(ProjectBundleOpenErrorCode::MissingEntry,
                     "Failed to inspect ProjectBundle entry: " + std::string{path});
            }
            if ((!final && !S_ISDIR(status.st_mode)) || (final && !S_ISREG(status.st_mode))) {
                fail(ProjectBundleOpenErrorCode::UnsafePath,
                     "ProjectBundle entry is not a regular file beneath ordinary directories: " +
                         std::string{path});
            }
            current = std::move(next);
            if (final) {
                const auto size = checked_file_size(status, max_bundle_entry_size, path);
                if (size > max_bundle_total_size - captured_size_) {
                    fail(ProjectBundleOpenErrorCode::InputTooLarge,
                         "ProjectBundle directory exceeds the supported bounded size");
                }
                auto bytes = read_fd(current.get(), path, max_bundle_entry_size);
                if (bytes.size() > max_bundle_total_size - captured_size_) {
                    fail(ProjectBundleOpenErrorCode::InputTooLarge,
                         "ProjectBundle directory exceeds the supported bounded size");
                }
                captured_size_ += bytes.size();
                return CapturedEntry{std::move(bytes),
                                     FileIdentity{static_cast<std::uint64_t>(status.st_dev),
                                                  static_cast<std::uint64_t>(status.st_ino)}};
            }
        }
        fail(ProjectBundleOpenErrorCode::MissingEntry,
             "ProjectBundle entry path did not identify a file");
    }

    [[nodiscard]] std::vector<std::string> paths() const override {
        return inventory_directory(root_.get());
    }

  private:
    FileDescriptor root_;
    mutable std::uint64_t captured_size_{};
};

[[nodiscard]] CapturedEntry read_regular_file(const std::filesystem::path &path) {
    auto descriptor = FileDescriptor{::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
    if (descriptor.get() < 0) {
        fail(ProjectBundleOpenErrorCode::UnsupportedStorage,
             "ProjectBundle archive is missing, unsafe, or not a regular file");
    }

    struct stat status = {};
    if (::fstat(descriptor.get(), &status) != 0 || !S_ISREG(status.st_mode)) {
        fail(ProjectBundleOpenErrorCode::UnsupportedStorage,
             "ProjectBundle archive must be a regular file");
    }
    static_cast<void>(checked_file_size(status, max_bundle_total_size, path.string()));
    return CapturedEntry{read_fd(descriptor.get(), path.string(), max_bundle_total_size),
                         FileIdentity{static_cast<std::uint64_t>(status.st_dev),
                                      static_cast<std::uint64_t>(status.st_ino)}};
}

#else

class WinHandle final {
  public:
    explicit WinHandle(HANDLE value = INVALID_HANDLE_VALUE) noexcept : value_{value} {}

    WinHandle(const WinHandle &) = delete;
    WinHandle &operator=(const WinHandle &) = delete;

    WinHandle(WinHandle &&other) noexcept
        : value_{std::exchange(other.value_, INVALID_HANDLE_VALUE)} {}

    WinHandle &operator=(WinHandle &&other) noexcept {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }

    ~WinHandle() { reset(); }

    [[nodiscard]] HANDLE get() const noexcept { return value_; }

    [[nodiscard]] explicit operator bool() const noexcept { return value_ != INVALID_HANDLE_VALUE; }

  private:
    void reset() noexcept {
        if (*this) {
            static_cast<void>(::CloseHandle(value_));
            value_ = INVALID_HANDLE_VALUE;
        }
    }

    HANDLE value_;
};

[[nodiscard]] WinHandle open_windows_path(const std::filesystem::path &path, bool directory,
                                          bool read_content) {
    const auto access = read_content ? GENERIC_READ : FILE_READ_ATTRIBUTES;
    const auto flags = FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0U);
    return WinHandle{::CreateFileW(path.c_str(), access, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                   flags, nullptr)};
}

[[nodiscard]] BY_HANDLE_FILE_INFORMATION inspect_windows_handle(HANDLE handle,
                                                                std::string_view label) {
    auto information = BY_HANDLE_FILE_INFORMATION{};
    if (::GetFileInformationByHandle(handle, &information) == 0) {
        fail(ProjectBundleOpenErrorCode::MissingEntry,
             "Failed to inspect ProjectBundle entry " + std::string{label});
    }
    return information;
}

[[nodiscard]] std::wstring final_windows_path(HANDLE handle, std::string_view label) {
    const auto size =
        ::GetFinalPathNameByHandleW(handle, nullptr, 0U, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (size == 0U) {
        fail(ProjectBundleOpenErrorCode::MissingEntry,
             "Failed to resolve ProjectBundle entry " + std::string{label});
    }
    auto result = std::wstring(size, L'\0');
    const auto written = ::GetFinalPathNameByHandleW(handle, result.data(), size,
                                                     FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (written == 0U || written >= size) {
        fail(ProjectBundleOpenErrorCode::MissingEntry,
             "Failed to resolve ProjectBundle entry " + std::string{label});
    }
    result.resize(written);
    return result;
}

[[nodiscard]] bool windows_path_is_beneath(std::wstring_view child,
                                           std::wstring_view root) noexcept {
    if (child.size() <= root.size() ||
        ::CompareStringOrdinal(child.data(), static_cast<int>(root.size()), root.data(),
                               static_cast<int>(root.size()), TRUE) != CSTR_EQUAL) {
        return false;
    }
    return child[root.size()] == L'\\' || child[root.size()] == L'/';
}

[[nodiscard]] bool windows_paths_equal(std::wstring_view left, std::wstring_view right) noexcept {
    return left.size() == right.size() &&
           ::CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                                  static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

[[nodiscard]] std::filesystem::path windows_path_from_utf8(std::string_view value) {
    auto utf8 = std::u8string{};
    utf8.reserve(value.size());
    std::ranges::transform(value, std::back_inserter(utf8),
                           [](char character) { return static_cast<char8_t>(character); });
    return std::filesystem::path{utf8};
}

[[nodiscard]] FileIdentity windows_file_identity(const BY_HANDLE_FILE_INFORMATION &information) {
    return FileIdentity{information.dwVolumeSerialNumber,
                        (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32U) |
                            information.nFileIndexLow};
}

[[nodiscard]] std::uint64_t windows_file_size(const BY_HANDLE_FILE_INFORMATION &information) {
    return (static_cast<std::uint64_t>(information.nFileSizeHigh) << 32U) |
           information.nFileSizeLow;
}

[[nodiscard]] std::string read_windows_handle(HANDLE handle, std::string_view label,
                                              std::uint64_t maximum_size) {
    auto result = std::string{};
    auto buffer = std::array<char, 64U * 1024U>{};
    while (true) {
        auto count = DWORD{0};
        if (::ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr) ==
            0) {
            fail(ProjectBundleOpenErrorCode::MissingEntry,
                 "Failed to read ProjectBundle entry " + std::string{label});
        }
        if (count == 0U) {
            break;
        }
        if (count > maximum_size - result.size()) {
            fail(ProjectBundleOpenErrorCode::InputTooLarge,
                 "ProjectBundle input exceeds the supported bounded size: " + std::string{label});
        }
        result.append(buffer.data(), count);
    }
    return result;
}

class DirectorySource final : public BundleSource {
  public:
    explicit DirectorySource(std::filesystem::path root)
        : root_path_{std::move(root)}, root_{open_windows_path(root_path_, true, false)} {
        if (!root_) {
            fail(ProjectBundleOpenErrorCode::UnsupportedStorage,
                 "ProjectBundle directory root is missing, unsafe, or not a directory");
        }
        const auto information = inspect_windows_handle(root_.get(), "root");
        if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
            (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
            fail(ProjectBundleOpenErrorCode::UnsupportedStorage,
                 "ProjectBundle directory root is unsafe or not a directory");
        }
        root_final_path_ = final_windows_path(root_.get(), "root");
        root_identity_ = windows_file_identity(information);
    }

    [[nodiscard]] ProjectBundleStorageKind kind() const noexcept override {
        return ProjectBundleStorageKind::Directory;
    }

    [[nodiscard]] CapturedEntry read(std::string_view path) const override {
        require_current_root();
        auto current_path = root_path_;
        const auto segments = source_path_segments(path);
        auto opened_chain = std::vector<WinHandle>{};
        opened_chain.reserve(segments.size());
        for (auto index = std::size_t{0}; index < segments.size(); ++index) {
            const auto final = index + 1U == segments.size();
            current_path /= windows_path_from_utf8(segments[index]);
            auto current = open_windows_path(current_path, !final, final);
            if (!current) {
                fail(ProjectBundleOpenErrorCode::MissingEntry,
                     "ProjectBundle entry is missing: " + std::string{path});
            }
            const auto information = inspect_windows_handle(current.get(), path);
            const auto directory = (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
            const auto reparse =
                (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
            if (reparse || (!final && !directory) || (final && directory)) {
                fail(ProjectBundleOpenErrorCode::UnsafePath,
                     "ProjectBundle entry traverses a reparse point or non-regular path: " +
                         std::string{path});
            }
            const auto resolved = final_windows_path(current.get(), path);
            if (!windows_path_is_beneath(resolved, root_final_path_)) {
                fail(ProjectBundleOpenErrorCode::UnsafePath,
                     "ProjectBundle entry resolves outside its opened root: " + std::string{path});
            }
            opened_chain.push_back(std::move(current));
            if (final) {
                const auto size = windows_file_size(information);
                if (size > max_bundle_entry_size || size > max_bundle_total_size - captured_size_) {
                    fail(ProjectBundleOpenErrorCode::InputTooLarge,
                         "ProjectBundle directory exceeds the supported bounded size");
                }
                auto bytes =
                    read_windows_handle(opened_chain.back().get(), path, max_bundle_entry_size);
                if (bytes.size() > max_bundle_total_size - captured_size_) {
                    fail(ProjectBundleOpenErrorCode::InputTooLarge,
                         "ProjectBundle directory exceeds the supported bounded size");
                }
                captured_size_ += bytes.size();
                return CapturedEntry{std::move(bytes), windows_file_identity(information)};
            }
        }
        fail(ProjectBundleOpenErrorCode::MissingEntry,
             "ProjectBundle entry path did not identify a file");
    }

    [[nodiscard]] std::vector<std::string> paths() const override {
        require_current_root();
        auto result = inventory_directory(root_path_);
        require_current_root();
        return result;
    }

  private:
    void require_current_root() const {
        auto current_root = open_windows_path(root_path_, true, false);
        if (!current_root) {
            fail(ProjectBundleOpenErrorCode::UnsafePath,
                 "ProjectBundle directory root no longer names the opened bundle");
        }
        const auto current_root_information = inspect_windows_handle(current_root.get(), "root");
        if ((current_root_information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
            (current_root_information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
            windows_file_identity(current_root_information) != root_identity_ ||
            !windows_paths_equal(final_windows_path(current_root.get(), "root"),
                                 root_final_path_)) {
            fail(ProjectBundleOpenErrorCode::UnsafePath,
                 "ProjectBundle directory root identity changed during open");
        }
    }

    std::filesystem::path root_path_;
    WinHandle root_;
    std::wstring root_final_path_;
    FileIdentity root_identity_{};
    mutable std::uint64_t captured_size_{};
};

[[nodiscard]] CapturedEntry read_regular_file(const std::filesystem::path &path) {
    auto handle = open_windows_path(path, false, true);
    if (!handle) {
        fail(ProjectBundleOpenErrorCode::UnsupportedStorage,
             "ProjectBundle archive is missing, unsafe, or not a regular file");
    }
    const auto information = inspect_windows_handle(handle.get(), path.string());
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        fail(ProjectBundleOpenErrorCode::UnsupportedStorage,
             "ProjectBundle archive is unsafe or not a regular file");
    }
    if (windows_file_size(information) > max_bundle_total_size) {
        fail(ProjectBundleOpenErrorCode::InputTooLarge,
             "ProjectBundle archive exceeds the supported bounded size");
    }
    return CapturedEntry{read_windows_handle(handle.get(), path.string(), max_bundle_total_size),
                         windows_file_identity(information)};
}

#endif

[[nodiscard]] std::uint16_t little_u16(std::string_view bytes, std::size_t offset,
                                       std::string_view label) {
    if (bytes.size() - std::min(bytes.size(), offset) < 2U) {
        fail(ProjectBundleOpenErrorCode::MalformedArchive,
             "ProjectBundle ZIP is truncated while reading " + std::string{label});
    }
    const auto first = static_cast<unsigned char>(bytes[offset]);
    const auto second = static_cast<unsigned char>(bytes[offset + 1U]);
    return static_cast<std::uint16_t>(first | (static_cast<std::uint16_t>(second) << 8U));
}

[[nodiscard]] std::uint32_t little_u32(std::string_view bytes, std::size_t offset,
                                       std::string_view label) {
    if (bytes.size() - std::min(bytes.size(), offset) < 4U) {
        fail(ProjectBundleOpenErrorCode::MalformedArchive,
             "ProjectBundle ZIP is truncated while reading " + std::string{label});
    }
    auto result = std::uint32_t{0};
    for (auto index = std::size_t{0}; index < 4U; ++index) {
        result |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + index]))
                  << (8U * index);
    }
    return result;
}

[[nodiscard]] std::string inflate_entry(std::string_view compressed, std::size_t output_size,
                                        std::string_view path) {
    if (compressed.size() > std::numeric_limits<uInt>::max() ||
        output_size > std::numeric_limits<uInt>::max()) {
        fail(ProjectBundleOpenErrorCode::MalformedArchive,
             "ProjectBundle ZIP entry exceeds the supported deflate size: " + std::string{path});
    }
    auto output = std::string(output_size == 0U ? 1U : output_size, '\0');
    auto stream = z_stream{};
    stream.next_in =
        reinterpret_cast<Bytef *>(const_cast<char *>(compressed.data())); // zlib does not mutate.
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        fail(ProjectBundleOpenErrorCode::MalformedArchive,
             "Failed to initialize ProjectBundle ZIP deflate decoding");
    }
    const auto result = inflate(&stream, Z_FINISH);
    const auto finished = result == Z_STREAM_END && stream.total_out == output_size &&
                          stream.total_in == compressed.size();
    static_cast<void>(inflateEnd(&stream));
    if (!finished) {
        fail(ProjectBundleOpenErrorCode::MalformedArchive,
             "ProjectBundle ZIP entry has invalid deflate bytes: " + std::string{path});
    }
    output.resize(output_size);
    return output;
}

struct ZipDirectoryRecord {
    std::string path;
    std::uint16_t version_needed;
    std::uint16_t flags;
    std::uint16_t method;
    std::uint32_t crc;
    std::uint32_t compressed_size;
    std::uint32_t uncompressed_size;
    std::uint32_t local_offset;
    bool directory;
};

[[nodiscard]] std::map<std::string, std::string> decode_zip(std::string_view bytes) {
    constexpr auto eocd_signature = std::uint32_t{0x06054b50U};
    constexpr auto central_signature = std::uint32_t{0x02014b50U};
    constexpr auto local_signature = std::uint32_t{0x04034b50U};
    if (bytes.size() < 22U) {
        fail(ProjectBundleOpenErrorCode::MalformedArchive, "ProjectBundle ZIP is too short");
    }
    const auto search_begin = bytes.size() > 65557U ? bytes.size() - 65557U : 0U;
    auto eocd = std::optional<std::size_t>{};
    for (auto offset = bytes.size() - 22U;; --offset) {
        if (little_u32(bytes, offset, "end record signature") == eocd_signature) {
            eocd = offset;
            break;
        }
        if (offset == search_begin) {
            break;
        }
    }
    if (!eocd.has_value()) {
        fail(ProjectBundleOpenErrorCode::MalformedArchive,
             "ProjectBundle ZIP end record is missing");
    }
    const auto disk = little_u16(bytes, *eocd + 4U, "disk number");
    const auto central_disk = little_u16(bytes, *eocd + 6U, "central directory disk");
    const auto disk_entries = little_u16(bytes, *eocd + 8U, "disk entry count");
    const auto entry_count = little_u16(bytes, *eocd + 10U, "entry count");
    const auto central_size = little_u32(bytes, *eocd + 12U, "central directory size");
    const auto central_offset = little_u32(bytes, *eocd + 16U, "central directory offset");
    const auto comment_size = little_u16(bytes, *eocd + 20U, "comment size");
    if (disk != 0U || central_disk != 0U || disk_entries != entry_count ||
        entry_count == std::numeric_limits<std::uint16_t>::max() ||
        central_size == std::numeric_limits<std::uint32_t>::max() ||
        central_offset == std::numeric_limits<std::uint32_t>::max()) {
        fail(ProjectBundleOpenErrorCode::MalformedArchive,
             "ProjectBundle ZIP uses unsupported multi-disk or ZIP64 structure");
    }
    if (*eocd + 22U + comment_size != bytes.size() ||
        static_cast<std::uint64_t>(central_offset) + central_size != *eocd) {
        fail(ProjectBundleOpenErrorCode::MalformedArchive,
             "ProjectBundle ZIP central directory boundaries are ambiguous");
    }

    auto records = std::vector<ZipDirectoryRecord>{};
    records.reserve(entry_count);
    auto cursor = static_cast<std::size_t>(central_offset);
    auto names = std::set<std::string>{};
    auto file_names = std::set<std::string>{};
    auto directory_names = std::set<std::string>{};
    for (auto index = std::uint32_t{0}; index < entry_count; ++index) {
        if (little_u32(bytes, cursor, "central entry signature") != central_signature) {
            fail(ProjectBundleOpenErrorCode::MalformedArchive,
                 "ProjectBundle ZIP central entry signature is invalid");
        }
        const auto made_by = little_u16(bytes, cursor + 4U, "creator version");
        const auto version_needed = little_u16(bytes, cursor + 6U, "required version");
        const auto flags = little_u16(bytes, cursor + 8U, "entry flags");
        const auto method = little_u16(bytes, cursor + 10U, "compression method");
        const auto crc = little_u32(bytes, cursor + 16U, "entry CRC");
        const auto compressed_size = little_u32(bytes, cursor + 20U, "compressed size");
        const auto uncompressed_size = little_u32(bytes, cursor + 24U, "uncompressed size");
        const auto name_size = little_u16(bytes, cursor + 28U, "entry name size");
        const auto extra_size = little_u16(bytes, cursor + 30U, "entry extra size");
        const auto entry_comment_size = little_u16(bytes, cursor + 32U, "entry comment size");
        const auto start_disk = little_u16(bytes, cursor + 34U, "entry disk");
        const auto external_attributes = little_u32(bytes, cursor + 38U, "external attributes");
        const auto local_offset = little_u32(bytes, cursor + 42U, "local entry offset");
        const auto header_size = std::uint64_t{46U} + name_size + extra_size + entry_comment_size;
        if (header_size > bytes.size() - std::min(bytes.size(), cursor) || start_disk != 0U ||
            version_needed < 10U || version_needed > 20U || extra_size != 0U ||
            entry_comment_size != 0U ||
            compressed_size == std::numeric_limits<std::uint32_t>::max() ||
            uncompressed_size == std::numeric_limits<std::uint32_t>::max() ||
            local_offset == std::numeric_limits<std::uint32_t>::max()) {
            fail(ProjectBundleOpenErrorCode::MalformedArchive,
                 "ProjectBundle ZIP central entry is truncated or requires ZIP64");
        }
        auto path = std::string{bytes.substr(cursor + 46U, name_size)};
        const auto utf8_flag = (flags & 0x0800U) != 0U;
        const auto ascii_only = std::ranges::all_of(
            path, [](char value) { return static_cast<unsigned char>(value) <= 0x7fU; });
        const auto unsupported_flags = static_cast<std::uint16_t>(flags & ~0x0806U);
        if ((!utf8_flag && !ascii_only) || !valid_utf8(path) || unsupported_flags != 0U ||
            (flags & 0x0001U) != 0U || (method == 0U && (flags & 0x0006U) != 0U) ||
            (method != 0U && method != 8U)) {
            fail(ProjectBundleOpenErrorCode::MalformedArchive,
                 "ProjectBundle ZIP entry uses unsupported encoding, encryption, or compression");
        }
        const auto dos_directory = (external_attributes & 0x10U) != 0U;
        const auto creator_os = static_cast<std::uint16_t>(made_by >> 8U);
        const auto unix_mode =
            creator_os == 3U ? static_cast<std::uint16_t>(external_attributes >> 16U) : 0U;
        const auto unix_type = unix_mode & 0170000U;
        const auto unix_directory = unix_type == 0040000U;
        const auto unix_regular = unix_type == 0100000U || unix_type == 0U;
        const auto trailing_slash = path.ends_with('/');
        const auto directory = dos_directory || unix_directory || trailing_slash;
        if ((directory && !trailing_slash) || (!directory && trailing_slash) ||
            (!directory && !unix_regular) || (directory && unix_type != 0U && !unix_directory)) {
            fail(ProjectBundleOpenErrorCode::MalformedArchive,
                 "ProjectBundle ZIP contains a symlink-like or unsupported entry");
        }
        auto safety_path = path;
        if (directory) {
            safety_path.pop_back();
        }
        static_cast<void>(source_path_segments(safety_path));
        if (!names.insert(path).second) {
            fail(ProjectBundleOpenErrorCode::DuplicateIdentity,
                 "ProjectBundle ZIP contains a duplicate path: " + path);
        }
        if (directory) {
            directory_names.insert(std::move(safety_path));
        } else {
            file_names.insert(path);
        }
        records.push_back(ZipDirectoryRecord{std::move(path), version_needed, flags, method, crc,
                                             compressed_size, uncompressed_size, local_offset,
                                             directory});
        cursor += static_cast<std::size_t>(header_size);
    }
    if (cursor != *eocd) {
        fail(ProjectBundleOpenErrorCode::MalformedArchive,
             "ProjectBundle ZIP central directory contains extraneous bytes");
    }
    for (const auto &file : file_names) {
        if (directory_names.contains(file)) {
            fail(ProjectBundleOpenErrorCode::DuplicateIdentity,
                 "ProjectBundle ZIP path is both a file and directory: " + file);
        }
        auto slash = file.find('/');
        while (slash != std::string::npos) {
            if (file_names.contains(file.substr(0U, slash))) {
                fail(ProjectBundleOpenErrorCode::DuplicateIdentity,
                     "ProjectBundle ZIP file path is also used as a parent: " + file);
            }
            slash = file.find('/', slash + 1U);
        }
    }

    auto result = std::map<std::string, std::string>{};
    auto ranges = std::vector<std::pair<std::size_t, std::size_t>>{};
    auto total_size = std::uint64_t{0};
    for (const auto &record : records) {
        const auto local = static_cast<std::size_t>(record.local_offset);
        if (local >= central_offset ||
            little_u32(bytes, local, "local entry signature") != local_signature) {
            fail(ProjectBundleOpenErrorCode::MalformedArchive,
                 "ProjectBundle ZIP local entry is missing or overlaps the central directory");
        }
        const auto local_version = little_u16(bytes, local + 4U, "local required version");
        const auto local_flags = little_u16(bytes, local + 6U, "local entry flags");
        const auto local_method = little_u16(bytes, local + 8U, "local compression method");
        const auto local_crc = little_u32(bytes, local + 14U, "local entry CRC");
        const auto local_compressed = little_u32(bytes, local + 18U, "local compressed size");
        const auto local_uncompressed = little_u32(bytes, local + 22U, "local uncompressed size");
        const auto local_name_size = little_u16(bytes, local + 26U, "local name size");
        const auto local_extra_size = little_u16(bytes, local + 28U, "local extra size");
        const auto payload_offset = std::uint64_t{local} + 30U + local_name_size + local_extra_size;
        const auto payload_end = payload_offset + record.compressed_size;
        if (payload_end > central_offset || local_version != record.version_needed ||
            local_extra_size != 0U || bytes.substr(local + 30U, local_name_size) != record.path ||
            local_flags != record.flags || local_method != record.method) {
            fail(ProjectBundleOpenErrorCode::MalformedArchive,
                 "ProjectBundle ZIP local and central records disagree");
        }
        if (local_crc != record.crc || local_compressed != record.compressed_size ||
            local_uncompressed != record.uncompressed_size) {
            fail(ProjectBundleOpenErrorCode::MalformedArchive,
                 "ProjectBundle ZIP local size or CRC facts disagree");
        }
        const auto range = std::pair{local, static_cast<std::size_t>(payload_end)};
        if (std::ranges::any_of(ranges, [&](const auto &existing) {
                return range.first < existing.second && existing.first < range.second;
            })) {
            fail(ProjectBundleOpenErrorCode::MalformedArchive,
                 "ProjectBundle ZIP local entries overlap");
        }
        ranges.push_back(range);
        if (record.directory) {
            if (record.compressed_size != 0U || record.uncompressed_size != 0U) {
                fail(ProjectBundleOpenErrorCode::MalformedArchive,
                     "ProjectBundle ZIP directory entry contains payload bytes");
            }
            result.emplace(record.path, std::string{});
            continue;
        }
        total_size += record.uncompressed_size;
        if (record.uncompressed_size > max_bundle_entry_size ||
            total_size > max_bundle_total_size) {
            fail(ProjectBundleOpenErrorCode::InputTooLarge,
                 "ProjectBundle ZIP expands beyond the supported bounded size");
        }
        const auto compressed =
            bytes.substr(static_cast<std::size_t>(payload_offset), record.compressed_size);
        auto payload = record.method == 0U
                           ? std::string{compressed}
                           : inflate_entry(compressed, record.uncompressed_size, record.path);
        if (payload.size() != record.uncompressed_size ||
            crc32(0U, reinterpret_cast<const Bytef *>(payload.data()),
                  static_cast<uInt>(payload.size())) != record.crc) {
            fail(ProjectBundleOpenErrorCode::MalformedArchive,
                 "ProjectBundle ZIP entry size or CRC does not match recorded structure: " +
                     record.path);
        }
        result.emplace(record.path, std::move(payload));
    }
    std::ranges::sort(ranges);
    if ((!ranges.empty() && ranges.front().first != 0U) ||
        (!ranges.empty() && ranges.back().second != central_offset)) {
        fail(ProjectBundleOpenErrorCode::MalformedArchive,
             "ProjectBundle ZIP contains bytes outside its local entries");
    }
    for (auto index = std::size_t{1}; index < ranges.size(); ++index) {
        if (ranges[index - 1U].second != ranges[index].first) {
            fail(ProjectBundleOpenErrorCode::MalformedArchive,
                 "ProjectBundle ZIP contains ambiguous bytes between local entries");
        }
    }
    return result;
}

class ZipSource final : public BundleSource {
  public:
    explicit ZipSource(std::string archive_bytes)
        : archive_bytes_{std::move(archive_bytes)}, entries_{decode_zip(archive_bytes_)} {}

    [[nodiscard]] ProjectBundleStorageKind kind() const noexcept override {
        return ProjectBundleStorageKind::ZipArchive;
    }

    [[nodiscard]] CapturedEntry read(std::string_view path) const override {
        static_cast<void>(source_path_segments(path));
        const auto match = entries_.find(std::string{path});
        if (match == entries_.end()) {
            fail(ProjectBundleOpenErrorCode::MissingEntry,
                 "ProjectBundle ZIP is missing declared entry: " + std::string{path});
        }
        return CapturedEntry{match->second, std::nullopt};
    }

    [[nodiscard]] std::vector<std::string> paths() const override {
        auto result = std::vector<std::string>{};
        result.reserve(entries_.size());
        for (const auto &[path, unused] : entries_) {
            static_cast<void>(unused);
            result.push_back(path);
        }
        return result;
    }

  private:
    std::string archive_bytes_;
    std::map<std::string, std::string> entries_;
};

} // namespace

[[nodiscard]] std::unique_ptr<BundleSource>
open_project_bundle_source(const std::filesystem::path &path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || status.type() == std::filesystem::file_type::not_found) {
        fail(ProjectBundleOpenErrorCode::MissingBundle, "ProjectBundle path does not exist");
    }
    if (std::filesystem::is_symlink(status)) {
        fail(ProjectBundleOpenErrorCode::UnsupportedStorage,
             "ProjectBundle root must not be a symbolic link");
    }
    if (std::filesystem::is_directory(status)) {
        return std::make_unique<DirectorySource>(path);
    }
    if (std::filesystem::is_regular_file(status)) {
        return std::make_unique<ZipSource>(read_regular_file(path).bytes);
    }
    fail(ProjectBundleOpenErrorCode::UnsupportedStorage,
         "ProjectBundle path must be a directory or regular ZIP archive");
}

} // namespace volt::io::detail
