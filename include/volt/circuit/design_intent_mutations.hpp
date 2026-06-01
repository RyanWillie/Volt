#pragma once

#include <volt/circuit/circuit.hpp>

namespace volt {

/** Kernel-owned design-intent mutation surface over a Circuit invariant core. */
class CircuitDesignIntent {
  public:
    /** Construct design-intent mutations over an existing logical circuit. */
    explicit CircuitDesignIntent(Circuit &circuit) noexcept : circuit_{&circuit} {}

    /** Record that an otherwise empty or single-ended named net is intentionally exported. */
    bool mark_intentional_stub_net(NetId net);

    /** Record that an otherwise connectable concrete pin is intentionally left open. */
    bool mark_intentional_no_connect_pin(PinId pin);

  private:
    Circuit *circuit_;
};

} // namespace volt
