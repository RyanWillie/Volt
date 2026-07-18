#include <volt/circuit/parts/selected_part.hpp>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

#include <volt/core/errors.hpp>

namespace volt {

PartKey::PartKey(std::string value) : value_{std::move(value)} {
    if (value_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, "Part key must not be empty"};
    }
    auto normalized = value_;
    std::ranges::transform(normalized, normalized.begin(), [](char character) {
        if (character >= 'A' && character <= 'Z') {
            return static_cast<char>(character + ('a' - 'A'));
        }
        return character;
    });
    constexpr auto reserved =
        std::array<std::string_view, 3>{"placeholder", "synthetic", "unspecified"};
    if (std::ranges::find(reserved, normalized) != reserved.end()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Part key must identify an exact non-placeholder part"};
    }
}

LibraryPartRef::LibraryPartRef(std::string library_namespace, std::string library_version,
                               PartKey part_key, ContentHash library_digest,
                               ContentHash part_digest)
    : library_namespace_{std::move(library_namespace)},
      library_version_{std::move(library_version)}, part_key_{std::move(part_key)},
      library_digest_{std::move(library_digest)}, part_digest_{std::move(part_digest)} {
    if (library_namespace_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "LibraryPartRef namespace must not be empty"};
    }
    if (library_version_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "LibraryPartRef version must not be empty"};
    }
}

} // namespace volt
