#pragma once

#include <vector>

#include <volt/core/ids.hpp>

namespace volt {

/** Owns explicit design-intent assertions for otherwise suspicious connectivity shapes. */
class DesignIntent {
  public:
    /** Mark a single-pin net as intentional design intent. */
    bool mark_intentional_stub_net(NetId net);

    /** Mark an unconnected concrete pin as intentional design intent. */
    bool mark_intentional_no_connect_pin(PinId pin);

    /** Return whether a net is intentionally left as a stub. */
    [[nodiscard]] bool is_intentional_stub_net(NetId net) const;

    /** Return whether a concrete pin is intentionally left unconnected. */
    [[nodiscard]] bool is_intentional_no_connect_pin(PinId pin) const;

    /** Return intentional stub nets in deterministic order. */
    [[nodiscard]] const std::vector<NetId> &intentional_stub_nets() const noexcept;

    /** Return intentional no-connect pins in deterministic order. */
    [[nodiscard]] const std::vector<PinId> &intentional_no_connect_pins() const noexcept;

  private:
    std::vector<NetId> intentional_stub_nets_;
    std::vector<PinId> intentional_no_connect_pins_;
};

} // namespace volt
