#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/instances.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>

namespace volt::authoring {

/** Return the first unused deterministic reference designator for a prefix, such as R1. */
[[nodiscard]] inline ReferenceDesignator allocate_reference(const Circuit &circuit,
                                                            std::string_view prefix) {
    if (prefix.empty()) {
        throw std::invalid_argument{"Reference designator prefix must not be empty"};
    }

    for (std::size_t index = 1;; ++index) {
        const auto candidate = ReferenceDesignator{std::string{prefix} + std::to_string(index)};
        if (!circuit.component_by_reference(candidate).has_value()) {
            return candidate;
        }
    }
}

/** Instantiate a component with an explicit reference designator through the circuit boundary. */
[[nodiscard]] inline ComponentId instantiate(Circuit &circuit, ComponentDefId definition,
                                             ReferenceDesignator reference,
                                             PropertyMap properties = {}) {
    return circuit.instantiate_component(definition, std::move(reference), std::move(properties));
}

/** Instantiate a component with the next available reference for a prefix, such as R or U. */
[[nodiscard]] inline ComponentId instantiate(Circuit &circuit, ComponentDefId definition,
                                             std::string_view prefix, PropertyMap properties = {}) {
    return instantiate(circuit, definition, allocate_reference(circuit, prefix),
                       std::move(properties));
}

} // namespace volt::authoring
