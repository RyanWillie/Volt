#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>

namespace volt::authoring {

/** Data-only reusable pin definition preset for authoring libraries. */
struct PinSpec {
    /** Human-readable logical pin name, such as VDD, A, K, or 1. */
    std::string name;
    /** Logical or package pin number, such as 1, 2, or 17. */
    std::string number;
    /** Electrical role to assign to the created pin definition. */
    PinRole role = PinRole::Passive;
    /** Expected connection requirement for normal use. */
    ConnectionRequirement requirement = ConnectionRequirement::Required;
};

/** Data-only reusable component definition preset for authoring libraries. */
struct ComponentSpec {
    /** Human-readable reusable component name, such as Resistor or LED. */
    std::string name;
    /** Ordered pin definitions to create before the component definition. */
    std::vector<PinSpec> pins;
    /** Metadata properties attached to the component definition. */
    PropertyMap properties = {};
    /** Optional provenance for built-in or imported library definitions. */
    std::optional<DefinitionSource> source = std::nullopt;
};

/** Define a reusable component in a circuit from a data-only component specification. */
[[nodiscard]] inline ComponentDefId define_component(Circuit &circuit, const ComponentSpec &spec) {
    auto pin_definitions = std::vector<PinDefId>{};
    pin_definitions.reserve(spec.pins.size());

    for (const auto &pin : spec.pins) {
        pin_definitions.push_back(circuit.add_pin_definition(
            PinDefinition{pin.name, pin.number, pin.role, pin.requirement}));
    }

    return circuit.add_component_definition(
        ComponentDefinition{spec.name, std::move(pin_definitions), spec.properties, spec.source});
}

/** Return a two-pin passive resistor component specification. */
[[nodiscard]] inline ComponentSpec resistor() {
    return ComponentSpec{
        "Resistor",
        std::vector{PinSpec{"1", "1"}, PinSpec{"2", "2"}},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"passive"}}},
        DefinitionSource{"volt.passives", "resistor_2pin", "1.0.0"},
    };
}

/** Return a two-pin passive capacitor component specification. */
[[nodiscard]] inline ComponentSpec capacitor() {
    return ComponentSpec{
        "Capacitor",
        std::vector{PinSpec{"1", "1"}, PinSpec{"2", "2"}},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"passive"}}},
        DefinitionSource{"volt.passives", "capacitor_2pin", "1.0.0"},
    };
}

/** Return a two-pin LED component specification using anode/cathode logical names. */
[[nodiscard]] inline ComponentSpec led() {
    return ComponentSpec{
        "LED",
        std::vector{PinSpec{"A", "2"}, PinSpec{"K", "1"}},
        PropertyMap{},
        DefinitionSource{"volt.optos", "led_2pin", "1.0.0"},
    };
}

/** Return a two-pin bidirectional connector component specification. */
[[nodiscard]] inline ComponentSpec connector_1x02() {
    return ComponentSpec{
        "Two-pin connector",
        std::vector{PinSpec{"+", "1", PinRole::Bidirectional},
                    PinSpec{"-", "2", PinRole::Bidirectional}},
        PropertyMap{},
        DefinitionSource{"volt.connectors", "connector_1x02", "1.0.0"},
    };
}

} // namespace volt::authoring
