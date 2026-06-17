#pragma once

namespace volt::detail {

/**
 * Passkey for kernel-owned mutation paths whose public signatures must not be raw handles.
 *
 * The type is visible so subsystem APIs can require it, but only kernel implementation files
 * include the private factory that constructs values.
 */
class KernelMutationAccess {
  public:
    constexpr KernelMutationAccess(const KernelMutationAccess &) noexcept = default;
    constexpr KernelMutationAccess &operator=(const KernelMutationAccess &) noexcept = default;

  private:
    constexpr KernelMutationAccess() noexcept = default;

    friend constexpr KernelMutationAccess kernel_mutation_access() noexcept;
};

} // namespace volt::detail
