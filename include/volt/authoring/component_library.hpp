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
[[nodiscard]] PinSpec passive_pin(std::string name, std::string number);

/** Return a bidirectional signal pin preset. */
[[nodiscard]] PinSpec bidirectional_pin(std::string name, std::string number);

/** Return an analog input pin preset. */
[[nodiscard]] PinSpec analog_input_pin(std::string name, std::string number);

/** Return an analog output pin preset. */
[[nodiscard]] PinSpec analog_output_pin(std::string name, std::string number);

/** Return a power input pin preset. */
[[nodiscard]] PinSpec power_input_pin(std::string name, std::string number);

/** Return a power output pin preset. */
[[nodiscard]] PinSpec power_output_pin(std::string name, std::string number);

/** Return a ground pin preset. */
[[nodiscard]] PinSpec ground_pin(std::string name, std::string number);

/** Define a reusable component in a circuit from a data-only component specification. */
[[nodiscard]] ComponentDefId define_component(Circuit &circuit, const ComponentSpec &spec);

/** Return a two-pin passive resistor component specification. */
[[nodiscard]] ComponentSpec resistor();

/** Return a two-pin passive capacitor component specification. */
[[nodiscard]] ComponentSpec capacitor();

/** Return a two-pin polarized capacitor component specification. */
[[nodiscard]] ComponentSpec polarized_capacitor();

/** Return a two-pin passive inductor component specification. */
[[nodiscard]] ComponentSpec inductor();

/** Return a two-pin diode component specification using anode/cathode logical names. */
[[nodiscard]] ComponentSpec diode();

/** Return a two-pin LED component specification using anode/cathode logical names. */
[[nodiscard]] ComponentSpec led();

/** Return a two-pin SPST switch component specification. */
[[nodiscard]] ComponentSpec switch_spst();

/** Return a two-pin crystal or resonator component specification. */
[[nodiscard]] ComponentSpec crystal_2pin();

/** Return a one-pin test point component specification. */
[[nodiscard]] ComponentSpec test_point();

/** Return a one-pin bidirectional connector component specification. */
[[nodiscard]] ComponentSpec connector_1x01();

/** Return a two-pin bidirectional connector component specification. */
[[nodiscard]] ComponentSpec connector_1x02();

/** Return a three-pin bidirectional connector component specification. */
[[nodiscard]] ComponentSpec connector_1x03();

/** Return a generic three-pin regulator component specification. */
[[nodiscard]] ComponentSpec regulator_3pin();

/** Return a generic five-pin op-amp component specification. */
[[nodiscard]] ComponentSpec op_amp_5pin();

} // namespace volt::authoring
