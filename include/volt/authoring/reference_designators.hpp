#pragma once

#include <string_view>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/circuit_view.hpp>
#include <volt/circuit/instances.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>

namespace volt::authoring {

/** Return the first unused deterministic reference designator for a prefix, such as R1. */
[[nodiscard]] ReferenceDesignator allocate_reference(CircuitView circuit, std::string_view prefix);

/** Instantiate a component with an explicit reference designator through the circuit boundary. */
[[nodiscard]] ComponentId instantiate(Circuit &circuit, ComponentDefId definition,
                                      ReferenceDesignator reference, PropertyMap properties = {});

/** Instantiate a component with the next available reference for a prefix, such as R or U. */
[[nodiscard]] ComponentId instantiate(Circuit &circuit, ComponentDefId definition,
                                      std::string_view prefix, PropertyMap properties = {});

} // namespace volt::authoring
