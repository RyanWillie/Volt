#pragma once

#include <initializer_list>
#include <span>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/core/ids.hpp>

namespace volt::authoring {

/** Connect a deterministic range of concrete pins to an existing net. */
inline void connect(Circuit &circuit, NetId net, std::span<const PinId> pins) {
    for (const auto pin : pins) {
        [[maybe_unused]] const auto changed = circuit.connect(net, pin);
    }
}

/** Connect an initializer list of concrete pins to an existing net. */
inline void connect(Circuit &circuit, NetId net, std::initializer_list<PinId> pins) {
    connect(circuit, net, std::span<const PinId>{pins.begin(), pins.size()});
}

/** Connect a vector of concrete pins to an existing net. */
inline void connect(Circuit &circuit, NetId net, const std::vector<PinId> &pins) {
    connect(circuit, net, std::span<const PinId>{pins.data(), pins.size()});
}

} // namespace volt::authoring
