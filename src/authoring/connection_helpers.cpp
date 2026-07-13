#include <volt/authoring/connection_helpers.hpp>

namespace volt::authoring {

void connect(Circuit &circuit, NetId net, std::span<const PinId> pins) {
    static_cast<void>(circuit.get(net));
    for (const auto pin : pins) {
        const auto existing_net = circuit.net_of(pin);
        if (existing_net.has_value() && existing_net.value() != net) {
            // Re-enter the canonical boundary to preserve its typed conflict contract.
            static_cast<void>(circuit.connect(net, pin));
        }
    }

    for (const auto pin : pins) {
        [[maybe_unused]] const auto changed = circuit.connect(net, pin);
    }
}

void connect(Circuit &circuit, NetId net, std::initializer_list<PinId> pins) {
    connect(circuit, net, std::span<const PinId>{pins.begin(), pins.size()});
}

void connect(Circuit &circuit, NetId net, const std::vector<PinId> &pins) {
    connect(circuit, net, std::span<const PinId>{pins.data(), pins.size()});
}

} // namespace volt::authoring
