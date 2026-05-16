#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/ids.hpp>
#include <volt/core/properties.hpp>
#include <volt/core/quantities.hpp>

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
    /** Broad terminal behavior. */
    ElectricalTerminalKind terminal_kind = ElectricalTerminalKind::Unspecified;
    /** Direction of electrical behavior. */
    ElectricalDirection direction = ElectricalDirection::Unspecified;
    /** Signal domain, when the pin carries a signal. */
    ElectricalSignalDomain signal_domain = ElectricalSignalDomain::Unspecified;
    /** Output or terminal drive behavior. */
    ElectricalDriveKind drive_kind = ElectricalDriveKind::Unspecified;
    /** Logical polarity for control-oriented pins. */
    ElectricalPolarity polarity = ElectricalPolarity::None;
    /** Optional voltage constraint for this reusable pin definition. */
    std::optional<QuantityRange> voltage_range = std::nullopt;
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
    /** Optional schematic symbol choices owned by the component definition. */
    std::vector<SchematicSymbolReference> schematic_symbols = {};
};

/** Return a simple passive pin preset. */
[[nodiscard]] inline PinSpec passive_pin(std::string name, std::string number) {
    auto pin = PinSpec{std::move(name), std::move(number)};
    pin.terminal_kind = ElectricalTerminalKind::Passive;
    pin.direction = ElectricalDirection::Passive;
    pin.drive_kind = ElectricalDriveKind::Passive;
    return pin;
}

/** Return a bidirectional signal pin preset. */
[[nodiscard]] inline PinSpec bidirectional_pin(std::string name, std::string number) {
    auto pin = PinSpec{std::move(name), std::move(number), PinRole::Bidirectional};
    pin.terminal_kind = ElectricalTerminalKind::Signal;
    pin.direction = ElectricalDirection::Bidirectional;
    pin.signal_domain = ElectricalSignalDomain::Mixed;
    return pin;
}

/** Return an analog input pin preset. */
[[nodiscard]] inline PinSpec analog_input_pin(std::string name, std::string number) {
    auto pin = PinSpec{std::move(name), std::move(number), PinRole::AnalogInput};
    pin.terminal_kind = ElectricalTerminalKind::Signal;
    pin.direction = ElectricalDirection::Input;
    pin.signal_domain = ElectricalSignalDomain::Analog;
    return pin;
}

/** Return an analog output pin preset. */
[[nodiscard]] inline PinSpec analog_output_pin(std::string name, std::string number) {
    auto pin = PinSpec{std::move(name), std::move(number), PinRole::AnalogOutput};
    pin.terminal_kind = ElectricalTerminalKind::Signal;
    pin.direction = ElectricalDirection::Output;
    pin.signal_domain = ElectricalSignalDomain::Analog;
    return pin;
}

/** Return a power input pin preset. */
[[nodiscard]] inline PinSpec power_input_pin(std::string name, std::string number) {
    auto pin = PinSpec{std::move(name), std::move(number), PinRole::PowerInput};
    pin.terminal_kind = ElectricalTerminalKind::Power;
    pin.direction = ElectricalDirection::Input;
    return pin;
}

/** Return a power output pin preset. */
[[nodiscard]] inline PinSpec power_output_pin(std::string name, std::string number) {
    auto pin = PinSpec{std::move(name), std::move(number), PinRole::PowerOutput};
    pin.terminal_kind = ElectricalTerminalKind::Power;
    pin.direction = ElectricalDirection::Output;
    return pin;
}

/** Return a ground pin preset. */
[[nodiscard]] inline PinSpec ground_pin(std::string name, std::string number) {
    auto pin = PinSpec{std::move(name), std::move(number), PinRole::Ground};
    pin.terminal_kind = ElectricalTerminalKind::Ground;
    pin.direction = ElectricalDirection::Passive;
    return pin;
}

/** Define a reusable component in a circuit from a data-only component specification. */
[[nodiscard]] inline ComponentDefId define_component(Circuit &circuit, const ComponentSpec &spec) {
    auto pin_definitions = std::vector<PinDefId>{};
    pin_definitions.reserve(spec.pins.size());

    for (const auto &pin : spec.pins) {
        const auto pin_definition = circuit.add_pin_definition(
            PinDefinition{pin.name, pin.number, pin.role, pin.requirement, pin.terminal_kind,
                          pin.direction, pin.signal_domain, pin.drive_kind, pin.polarity});
        if (pin.voltage_range.has_value()) {
            circuit.set_pin_definition_electrical_attribute(
                pin_definition,
                ElectricalAttributeSpec{
                    ElectricalAttributeName{"voltage_range"}, ElectricalAttributeOwner::PinSpec,
                    ElectricalAttributeKind::Constraint, UnitDimension::Voltage},
                ElectricalAttributeValue{pin.voltage_range.value()});
        }
        pin_definitions.push_back(pin_definition);
    }

    return circuit.add_component_definition(
        ComponentDefinition{spec.name, std::move(pin_definitions), spec.properties, spec.source,
                            spec.schematic_symbols});
}

/** Return a two-pin passive resistor component specification. */
[[nodiscard]] inline ComponentSpec resistor() {
    return ComponentSpec{
        "Resistor",
        std::vector{passive_pin("1", "1"), passive_pin("2", "2")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"passive"}}},
        DefinitionSource{"volt.passives", "resistor_2pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.passives:resistor"}},
    };
}

/** Return a two-pin passive capacitor component specification. */
[[nodiscard]] inline ComponentSpec capacitor() {
    return ComponentSpec{
        "Capacitor",
        std::vector{passive_pin("1", "1"), passive_pin("2", "2")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"passive"}}},
        DefinitionSource{"volt.passives", "capacitor_2pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.passives:capacitor"}},
    };
}

/** Return a two-pin polarized capacitor component specification. */
[[nodiscard]] inline ComponentSpec polarized_capacitor() {
    return ComponentSpec{
        "Polarized capacitor",
        std::vector{passive_pin("+", "1"), passive_pin("-", "2")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"passive"}},
                    {PropertyKey{"polarized"}, PropertyValue{true}}},
        DefinitionSource{"volt.passives", "capacitor_polarized_2pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.passives:capacitor_polarized"}},
    };
}

/** Return a two-pin passive inductor component specification. */
[[nodiscard]] inline ComponentSpec inductor() {
    return ComponentSpec{
        "Inductor",
        std::vector{passive_pin("1", "1"), passive_pin("2", "2")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"passive"}}},
        DefinitionSource{"volt.passives", "inductor_2pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.passives:inductor"}},
    };
}

/** Return a two-pin diode component specification using anode/cathode logical names. */
[[nodiscard]] inline ComponentSpec diode() {
    return ComponentSpec{
        "Diode",
        std::vector{passive_pin("A", "2"), passive_pin("K", "1")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"diode"}}},
        DefinitionSource{"volt.discretes", "diode_2pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.discretes:diode"}},
    };
}

/** Return a two-pin LED component specification using anode/cathode logical names. */
[[nodiscard]] inline ComponentSpec led() {
    return ComponentSpec{
        "LED",
        std::vector{passive_pin("A", "2"), passive_pin("K", "1")},
        PropertyMap{},
        DefinitionSource{"volt.optos", "led_2pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.optos:led"}},
    };
}

/** Return a two-pin SPST switch component specification. */
[[nodiscard]] inline ComponentSpec switch_spst() {
    return ComponentSpec{
        "Switch",
        std::vector{passive_pin("A", "1"), passive_pin("B", "2")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"switch"}}},
        DefinitionSource{"volt.switches", "switch_spst", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.switches:switch_spst"}},
    };
}

/** Return a two-pin crystal or resonator component specification. */
[[nodiscard]] inline ComponentSpec crystal_2pin() {
    return ComponentSpec{
        "Crystal",
        std::vector{passive_pin("1", "1"), passive_pin("2", "2")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"frequency_control"}}},
        DefinitionSource{"volt.frequency", "crystal_2pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.frequency:crystal_2pin"}},
    };
}

/** Return a one-pin test point component specification. */
[[nodiscard]] inline ComponentSpec test_point() {
    return ComponentSpec{
        "Test point",
        std::vector{bidirectional_pin("TP", "1")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"test_point"}}},
        DefinitionSource{"volt.testpoints", "test_point_1pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.testpoints:test_point"}},
    };
}

/** Return a one-pin bidirectional connector component specification. */
[[nodiscard]] inline ComponentSpec connector_1x01() {
    return ComponentSpec{
        "One-pin connector",
        std::vector{PinSpec{"1", "1", PinRole::Bidirectional}},
        PropertyMap{},
        DefinitionSource{"volt.connectors", "connector_1x01", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.connectors:connector_1x01"}},
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
        std::vector{SchematicSymbolReference{"volt.connectors:connector_1x02"}},
    };
}

/** Return a three-pin bidirectional connector component specification. */
[[nodiscard]] inline ComponentSpec connector_1x03() {
    return ComponentSpec{
        "Three-pin connector",
        std::vector{PinSpec{"1", "1", PinRole::Bidirectional},
                    PinSpec{"2", "2", PinRole::Bidirectional},
                    PinSpec{"3", "3", PinRole::Bidirectional}},
        PropertyMap{},
        DefinitionSource{"volt.connectors", "connector_1x03", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.connectors:connector_1x03"}},
    };
}

/** Return a generic three-pin regulator component specification. */
[[nodiscard]] inline ComponentSpec regulator_3pin() {
    return ComponentSpec{
        "Regulator",
        std::vector{ground_pin("GND", "1"), power_output_pin("OUT", "2"),
                    power_input_pin("IN", "3")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"regulator"}}},
        DefinitionSource{"volt.power", "regulator_3pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.power:regulator_3pin"}},
    };
}

/** Return a generic five-pin op-amp component specification. */
[[nodiscard]] inline ComponentSpec op_amp_5pin() {
    return ComponentSpec{
        "Op amp",
        std::vector{analog_output_pin("OUT", "1"), analog_input_pin("IN-", "2"),
                    analog_input_pin("IN+", "3"), power_input_pin("V-", "4"),
                    power_input_pin("V+", "5")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"analog"}}},
        DefinitionSource{"volt.analog", "op_amp_5pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.analog:op_amp_5pin"}},
    };
}

} // namespace volt::authoring
