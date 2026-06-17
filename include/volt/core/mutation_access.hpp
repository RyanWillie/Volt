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
    /** Copy a mutation access passkey through an already-authorized kernel call chain. */
    constexpr KernelMutationAccess(const KernelMutationAccess &) noexcept = default;

    /** Replace a mutation access passkey through an already-authorized kernel call chain. */
    constexpr KernelMutationAccess &operator=(const KernelMutationAccess &) noexcept = default;

  private:
    /** Construct a mutation access passkey from the source-private factory. */
    constexpr KernelMutationAccess() noexcept = default;

    /** Return a mutation access passkey for kernel implementation files. */
    friend constexpr KernelMutationAccess kernel_mutation_access() noexcept;
};

} // namespace volt::detail
