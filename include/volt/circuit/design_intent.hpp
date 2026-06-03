#pragma once

#include <vector>

#include <volt/core/ids.hpp>

namespace volt {

class Circuit;

/**
 * Owns explicit design-intent assertions for otherwise suspicious connectivity shapes.
 *
 * Responsibility: records deliberate intent (intentional stub nets, intentional no-connect
 *   pins) so validation can distinguish a choice from a mistake.
 * Invariants: assertions reference existing nets/pins (cross-reference confirmed by the root).
 * Collaborators: composed by Circuit; read by ERC rules to suppress would-be diagnostics;
 *   never references Circuit back (acyclic).
 */
class DesignIntent {
  public:
    /** Return whether a net is intentionally left as a stub. */
    [[nodiscard]] bool is_intentional_stub_net(NetId net) const;

    /** Return whether a concrete pin is intentionally left unconnected. */
    [[nodiscard]] bool is_intentional_no_connect_pin(PinId pin) const;

    /** Return intentional stub nets in deterministic order. */
    [[nodiscard]] const std::vector<NetId> &intentional_stub_nets() const noexcept;

    /** Return intentional no-connect pins in deterministic order. */
    [[nodiscard]] const std::vector<PinId> &intentional_no_connect_pins() const noexcept;

  private:
    friend class Circuit;

    /** Mark a single-pin net as intentional design intent. */
    bool mark_intentional_stub_net(NetId net);

    /** Mark an unconnected concrete pin as intentional design intent. */
    bool mark_intentional_no_connect_pin(PinId pin);

    std::vector<NetId> intentional_stub_nets_;
    std::vector<PinId> intentional_no_connect_pins_;
};

} // namespace volt
