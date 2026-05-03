#pragma once

#include <cstddef>

namespace volt {

template <typename Tag> class EntityId {
  public:
    using index_type = std::size_t;

    explicit constexpr EntityId(index_type index) noexcept : index_{index} {}

    [[nodiscard]] constexpr index_type index() const noexcept { return index_; }

    [[nodiscard]] friend constexpr bool operator==(EntityId lhs, EntityId rhs) noexcept = default;

  private:
    index_type index_;
};

namespace detail {

struct ComponentDefIdTag;
struct ComponentIdTag;
struct PinDefIdTag;
struct PinIdTag;
struct NetIdTag;

} // namespace detail

using ComponentDefId = EntityId<detail::ComponentDefIdTag>;
using ComponentId = EntityId<detail::ComponentIdTag>;
using PinDefId = EntityId<detail::PinDefIdTag>;
using PinId = EntityId<detail::PinIdTag>;
using NetId = EntityId<detail::NetIdTag>;

} // namespace volt
