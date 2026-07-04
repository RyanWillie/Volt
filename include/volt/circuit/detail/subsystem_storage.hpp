#pragma once

#include <memory>

namespace volt::detail {

/**
 * Owner-side storage plumbing shared by Circuit's per-subsystem storage types.
 *
 * Responsibility: pairs a read-only Facade base with the mutable owner view of the same
 *   State object, and centralizes the deep-copy and move rules every subsystem storage
 *   would otherwise repeat by hand.
 * Invariants: the Facade base and this class always reference the same State object;
 *   copies deep-copy the State; moved-from storages are reset to a fresh empty State so
 *   they remain safely queryable instead of holding null state.
 * Collaborators: subclassed by Circuit's *Storage structs, which add only their mutation
 *   methods. Member definitions live in src/circuit/subsystem_storage_impl.hpp with
 *   explicit instantiations in src/circuit/subsystem_storage.cpp, so State stays an
 *   incomplete type for public-header consumers.
 */
template <typename Facade, typename State> class SubsystemStorage : public Facade {
  public:
    /** Construct storage over a fresh empty State. */
    SubsystemStorage();
    /** Deep-copy storage state. */
    SubsystemStorage(const SubsystemStorage &other);
    /** Move storage state, resetting the source to a fresh empty State. */
    SubsystemStorage(SubsystemStorage &&other) noexcept;
    /** Deep-copy storage state. */
    SubsystemStorage &operator=(const SubsystemStorage &other);
    /** Move storage state, resetting the source to a fresh empty State. */
    SubsystemStorage &operator=(SubsystemStorage &&other) noexcept;
    /** Destroy storage state. */
    ~SubsystemStorage();

  protected:
    /** Return the mutable owner view of the shared subsystem state. */
    [[nodiscard]] State &mutable_state() noexcept;

    /** Return the read-only owner view of the shared subsystem state. */
    [[nodiscard]] const State &state() const noexcept;

  private:
    explicit SubsystemStorage(std::shared_ptr<State> state);

    void swap_with(SubsystemStorage &other) noexcept;

    std::shared_ptr<State> state_;
};

} // namespace volt::detail
