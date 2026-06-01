#include <volt/circuit/design_intent_mutations.hpp>

namespace volt {

bool CircuitDesignIntent::mark_intentional_stub_net(NetId net) {
    return circuit_->mark_intentional_stub_net(net);
}
bool CircuitDesignIntent::mark_intentional_no_connect_pin(PinId pin) {
    return circuit_->mark_intentional_no_connect_pin(pin);
}

} // namespace volt
