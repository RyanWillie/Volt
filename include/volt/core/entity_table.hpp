#pragma once

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace volt {

template <typename T, typename Id> class EntityTable {
  public:
    [[nodiscard]] Id insert(T value) {
        const auto index = items_.size();
        items_.push_back(std::move(value));
        return Id{index};
    }

    [[nodiscard]] T &get(Id id) {
        if (!contains(id)) {
            throw std::out_of_range{"Volt entity id is out of range"};
        }

        return items_[id.index()];
    }

    [[nodiscard]] const T &get(Id id) const {
        if (!contains(id)) {
            throw std::out_of_range{"Volt entity id is out of range"};
        }

        return items_[id.index()];
    }

    [[nodiscard]] bool contains(Id id) const noexcept { return id.index() < items_.size(); }

    [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }

    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }

  private:
    std::vector<T> items_;
};

} // namespace volt
