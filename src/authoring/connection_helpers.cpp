#include <volt/authoring/connection_helpers.hpp>

namespace volt::authoring {

void connect(Circuit &circuit, NetId net, std::span<const PinId> pins) {
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
