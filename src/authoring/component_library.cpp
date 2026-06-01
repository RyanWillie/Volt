#include <volt/authoring/component_library.hpp>

#include <volt/circuit/electrical_mutations.hpp>

namespace volt::authoring {

[[nodiscard]] PinSpec passive_pin(std::string name, std::string number) {
    auto pin = PinSpec{std::move(name), std::move(number)};
    pin.terminal_kind = ElectricalTerminalKind::Passive;
    pin.direction = ElectricalDirection::Passive;
    pin.drive_kind = ElectricalDriveKind::Passive;
    return pin;
}
[[nodiscard]] PinSpec bidirectional_pin(std::string name, std::string number) {
    auto pin = PinSpec{std::move(name), std::move(number), PinRole::Bidirectional};
    pin.terminal_kind = ElectricalTerminalKind::Signal;
    pin.direction = ElectricalDirection::Bidirectional;
    pin.signal_domain = ElectricalSignalDomain::Mixed;
    return pin;
}
[[nodiscard]] PinSpec analog_input_pin(std::string name, std::string number) {
    auto pin = PinSpec{std::move(name), std::move(number), PinRole::AnalogInput};
    pin.terminal_kind = ElectricalTerminalKind::Signal;
    pin.direction = ElectricalDirection::Input;
    pin.signal_domain = ElectricalSignalDomain::Analog;
    return pin;
}
[[nodiscard]] PinSpec analog_output_pin(std::string name, std::string number) {
    auto pin = PinSpec{std::move(name), std::move(number), PinRole::AnalogOutput};
    pin.terminal_kind = ElectricalTerminalKind::Signal;
    pin.direction = ElectricalDirection::Output;
    pin.signal_domain = ElectricalSignalDomain::Analog;
    return pin;
}
[[nodiscard]] PinSpec power_input_pin(std::string name, std::string number) {
    auto pin = PinSpec{std::move(name), std::move(number), PinRole::PowerInput};
    pin.terminal_kind = ElectricalTerminalKind::Power;
    pin.direction = ElectricalDirection::Input;
    return pin;
}
[[nodiscard]] PinSpec power_output_pin(std::string name, std::string number) {
    auto pin = PinSpec{std::move(name), std::move(number), PinRole::PowerOutput};
    pin.terminal_kind = ElectricalTerminalKind::Power;
    pin.direction = ElectricalDirection::Output;
    return pin;
}
[[nodiscard]] PinSpec ground_pin(std::string name, std::string number) {
    auto pin = PinSpec{std::move(name), std::move(number), PinRole::Ground};
    pin.terminal_kind = ElectricalTerminalKind::Ground;
    pin.direction = ElectricalDirection::Passive;
    return pin;
}
[[nodiscard]] ComponentDefId define_component(Circuit &circuit, const ComponentSpec &spec) {
    auto pin_definitions = std::vector<PinDefId>{};
    pin_definitions.reserve(spec.pins.size());
    auto electrical = CircuitElectrical{circuit};

    for (const auto &pin : spec.pins) {
        const auto pin_definition = circuit.add_pin_definition(
            PinDefinition{pin.name, pin.number, pin.role, pin.requirement, pin.terminal_kind,
                          pin.direction, pin.signal_domain, pin.drive_kind, pin.polarity});
        if (pin.voltage_range.has_value()) {
            electrical.set_pin_definition_electrical_attribute(
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
[[nodiscard]] ComponentSpec resistor() {
    return ComponentSpec{
        "Resistor",
        std::vector{passive_pin("1", "1"), passive_pin("2", "2")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"passive"}}},
        DefinitionSource{"volt.passives", "resistor_2pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.passives:resistor"}},
    };
}
[[nodiscard]] ComponentSpec capacitor() {
    return ComponentSpec{
        "Capacitor",
        std::vector{passive_pin("1", "1"), passive_pin("2", "2")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"passive"}}},
        DefinitionSource{"volt.passives", "capacitor_2pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.passives:capacitor"}},
    };
}
[[nodiscard]] ComponentSpec polarized_capacitor() {
    return ComponentSpec{
        "Polarized capacitor",
        std::vector{passive_pin("+", "1"), passive_pin("-", "2")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"passive"}},
                    {PropertyKey{"polarized"}, PropertyValue{true}}},
        DefinitionSource{"volt.passives", "capacitor_polarized_2pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.passives:capacitor_polarized"}},
    };
}
[[nodiscard]] ComponentSpec inductor() {
    return ComponentSpec{
        "Inductor",
        std::vector{passive_pin("1", "1"), passive_pin("2", "2")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"passive"}}},
        DefinitionSource{"volt.passives", "inductor_2pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.passives:inductor"}},
    };
}
[[nodiscard]] ComponentSpec diode() {
    return ComponentSpec{
        "Diode",
        std::vector{passive_pin("A", "2"), passive_pin("K", "1")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"diode"}}},
        DefinitionSource{"volt.discretes", "diode_2pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.discretes:diode"}},
    };
}
[[nodiscard]] ComponentSpec led() {
    return ComponentSpec{
        "LED",
        std::vector{passive_pin("A", "2"), passive_pin("K", "1")},
        PropertyMap{},
        DefinitionSource{"volt.optos", "led_2pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.optos:led"}},
    };
}
[[nodiscard]] ComponentSpec switch_spst() {
    return ComponentSpec{
        "Switch",
        std::vector{passive_pin("A", "1"), passive_pin("B", "2")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"switch"}}},
        DefinitionSource{"volt.switches", "switch_spst", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.switches:switch_spst"}},
    };
}
[[nodiscard]] ComponentSpec crystal_2pin() {
    return ComponentSpec{
        "Crystal",
        std::vector{passive_pin("1", "1"), passive_pin("2", "2")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"frequency_control"}}},
        DefinitionSource{"volt.frequency", "crystal_2pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.frequency:crystal_2pin"}},
    };
}
[[nodiscard]] ComponentSpec test_point() {
    return ComponentSpec{
        "Test point",
        std::vector{bidirectional_pin("TP", "1")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"test_point"}}},
        DefinitionSource{"volt.testpoints", "test_point_1pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.testpoints:test_point"}},
    };
}
[[nodiscard]] ComponentSpec connector_1x01() {
    return ComponentSpec{
        "One-pin connector",
        std::vector{PinSpec{"1", "1", PinRole::Bidirectional}},
        PropertyMap{},
        DefinitionSource{"volt.connectors", "connector_1x01", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.connectors:connector_1x01"}},
    };
}
[[nodiscard]] ComponentSpec connector_1x02() {
    return ComponentSpec{
        "Two-pin connector",
        std::vector{PinSpec{"+", "1", PinRole::Bidirectional},
                    PinSpec{"-", "2", PinRole::Bidirectional}},
        PropertyMap{},
        DefinitionSource{"volt.connectors", "connector_1x02", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.connectors:connector_1x02"}},
    };
}
[[nodiscard]] ComponentSpec connector_1x03() {
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
[[nodiscard]] ComponentSpec regulator_3pin() {
    return ComponentSpec{
        "Regulator",
        std::vector{ground_pin("GND", "1"), power_output_pin("OUT", "2"),
                    power_input_pin("IN", "3")},
        PropertyMap{{PropertyKey{"category"}, PropertyValue{"regulator"}}},
        DefinitionSource{"volt.power", "regulator_3pin", "1.0.0"},
        std::vector{SchematicSymbolReference{"volt.power:regulator_3pin"}},
    };
}
[[nodiscard]] ComponentSpec op_amp_5pin() {
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
