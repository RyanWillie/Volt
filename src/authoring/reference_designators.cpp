#include <volt/authoring/reference_designators.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

#include <volt/circuit/connectivity/queries.hpp>

namespace volt::authoring {

[[nodiscard]] ReferenceDesignator allocate_reference(const Circuit &circuit,
                                                     std::string_view prefix) {
    if (prefix.empty()) {
        throw std::invalid_argument{"Reference designator prefix must not be empty"};
    }

    for (std::size_t index = 1;; ++index) {
        const auto candidate = ReferenceDesignator{std::string{prefix} + std::to_string(index)};
        if (!queries::component_by_reference(circuit, candidate).has_value()) {
            return candidate;
        }
    }
}

[[nodiscard]] ComponentId instantiate(Circuit &circuit, ComponentDefId definition,
                                      ReferenceDesignator reference, PropertyMap properties) {
    return circuit.instantiate_component(definition, std::move(reference), std::move(properties));
}

[[nodiscard]] ComponentId instantiate(Circuit &circuit, ComponentDefId definition,
                                      std::string_view prefix, PropertyMap properties) {
    return instantiate(circuit, definition, allocate_reference(circuit, prefix),
                       std::move(properties));
}

} // namespace volt::authoring
