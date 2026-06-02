#pragma once

#include <vector>

#include <volt/core/ids.hpp>

namespace volt {

/** Owns explicit design-intent assertions for otherwise suspicious connectivity shapes. */
class DesignIntent {
  public:
    bool mark_intentional_stub_net(NetId net);

    bool mark_intentional_no_connect_pin(PinId pin);

    [[nodiscard]] bool is_intentional_stub_net(NetId net) const;

    [[nodiscard]] bool is_intentional_no_connect_pin(PinId pin) const;

    [[nodiscard]] const std::vector<NetId> &intentional_stub_nets() const noexcept;

    [[nodiscard]] const std::vector<PinId> &intentional_no_connect_pins() const noexcept;

  private:
    std::vector<NetId> intentional_stub_nets_;
    std::vector<PinId> intentional_no_connect_pins_;
};

} // namespace volt
