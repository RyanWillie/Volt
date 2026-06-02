#include <volt/circuit/design_intent.hpp>

#include <algorithm>

namespace volt {

bool DesignIntent::mark_intentional_stub_net(NetId net) {
    if (is_intentional_stub_net(net)) {
        return false;
    }

    intentional_stub_nets_.push_back(net);
    return true;
}

bool DesignIntent::mark_intentional_no_connect_pin(PinId pin) {
    if (is_intentional_no_connect_pin(pin)) {
        return false;
    }

    intentional_no_connect_pins_.push_back(pin);
    return true;
}

[[nodiscard]] bool DesignIntent::is_intentional_stub_net(NetId net) const {
    return std::find(intentional_stub_nets_.begin(), intentional_stub_nets_.end(), net) !=
           intentional_stub_nets_.end();
}

[[nodiscard]] bool DesignIntent::is_intentional_no_connect_pin(PinId pin) const {
    return std::find(intentional_no_connect_pins_.begin(), intentional_no_connect_pins_.end(),
                     pin) != intentional_no_connect_pins_.end();
}

[[nodiscard]] const std::vector<NetId> &DesignIntent::intentional_stub_nets() const noexcept {
    return intentional_stub_nets_;
}

[[nodiscard]] const std::vector<PinId> &DesignIntent::intentional_no_connect_pins() const noexcept {
    return intentional_no_connect_pins_;
}

} // namespace volt
