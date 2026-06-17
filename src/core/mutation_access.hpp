#pragma once

#include <volt/core/mutation_access.hpp>

namespace volt::detail {

[[nodiscard]] constexpr KernelMutationAccess kernel_mutation_access() noexcept {
    return KernelMutationAccess{};
}

} // namespace volt::detail
