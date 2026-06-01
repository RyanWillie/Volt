#pragma once

#include <initializer_list>
#include <span>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/core/ids.hpp>

namespace volt::authoring {

/** Connect a deterministic range of concrete pins to an existing net. */
void connect(Circuit &circuit, NetId net, std::span<const PinId> pins);

/** Connect an initializer list of concrete pins to an existing net. */
void connect(Circuit &circuit, NetId net, std::initializer_list<PinId> pins);

/** Connect a vector of concrete pins to an existing net. */
void connect(Circuit &circuit, NetId net, const std::vector<PinId> &pins);

} // namespace volt::authoring
