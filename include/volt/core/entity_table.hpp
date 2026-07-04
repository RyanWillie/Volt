#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include <volt/core/errors.hpp>

namespace volt {

/**
 * Deterministic vector-backed storage for kernel entities.
 *
 * EntityTable assigns typed IDs in insertion order. It intentionally does not support
 * deletion or generational handles yet; those features require concrete mutation and
 * undo/redo requirements before they are worth the complexity.
 */
template <typename T, typename Id> class EntityTable {
  public:
    /** Insert a new entity and return its typed table ID. */
    [[nodiscard]] Id insert(T value) {
        const auto index = items_.size();
        items_.push_back(std::move(value));
        return Id{index};
    }

    /** Return a mutable entity reference for an existing ID. */
    [[nodiscard]] T &get(Id id) {
        if (!contains(id)) {
            throw KernelRangeError{ErrorCode::UnknownEntity, "Volt entity id is out of range"};
        }

        return items_[id.index()];
    }

    /** Return a const entity reference for an existing ID. */
    [[nodiscard]] const T &get(Id id) const {
        if (!contains(id)) {
            throw KernelRangeError{ErrorCode::UnknownEntity, "Volt entity id is out of range"};
        }

        return items_[id.index()];
    }

    /** Return whether the ID indexes an entity currently stored in this table. */
    [[nodiscard]] bool contains(Id id) const noexcept { return id.index() < items_.size(); }

    /** Return the number of stored entities. */
    [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }

    /** Return whether the table stores no entities. */
    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }

  private:
    std::vector<T> items_;
};

} // namespace volt
