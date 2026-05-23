#pragma once

#include "binding_common.hpp"

namespace volt::python {

namespace {

[[nodiscard]] inline volt::PinRole parse_pin_role(const std::string &value) {
    if (value == "passive" || value == "Passive") {
        return volt::PinRole::Passive;
    }
    if (value == "input" || value == "digital_input" || value == "DigitalInput") {
        return volt::PinRole::DigitalInput;
    }
    if (value == "output" || value == "digital_output" || value == "DigitalOutput") {
        return volt::PinRole::DigitalOutput;
    }
    if (value == "analog_input" || value == "AnalogInput") {
        return volt::PinRole::AnalogInput;
    }
    if (value == "analog_output" || value == "AnalogOutput") {
        return volt::PinRole::AnalogOutput;
    }
    if (value == "bidirectional" || value == "Bidirectional") {
        return volt::PinRole::Bidirectional;
    }
    if (value == "power" || value == "power_input" || value == "PowerInput") {
        return volt::PinRole::PowerInput;
    }
    if (value == "power_output" || value == "PowerOutput") {
        return volt::PinRole::PowerOutput;
    }
    if (value == "ground" || value == "Ground") {
        return volt::PinRole::Ground;
    }
    if (value == "no_connect" || value == "no-connect" || value == "NoConnect") {
        return volt::PinRole::NoConnect;
    }

    throw std::invalid_argument{"Unknown pin role"};
}

[[nodiscard]] inline volt::ConnectionRequirement
parse_connection_requirement(const std::string &value) {
    if (value == "required" || value == "Required") {
        return volt::ConnectionRequirement::Required;
    }
    if (value == "optional" || value == "Optional") {
        return volt::ConnectionRequirement::Optional;
    }
    if (value == "must_not_connect" || value == "must-not-connect" || value == "MustNotConnect") {
        return volt::ConnectionRequirement::MustNotConnect;
    }

    throw std::invalid_argument{"Unknown connection requirement"};
}

[[nodiscard]] inline volt::ElectricalTerminalKind parse_terminal_kind(const std::string &value) {
    if (value == "unspecified" || value == "Unspecified") {
        return volt::ElectricalTerminalKind::Unspecified;
    }
    if (value == "passive" || value == "Passive") {
        return volt::ElectricalTerminalKind::Passive;
    }
    if (value == "signal" || value == "Signal") {
        return volt::ElectricalTerminalKind::Signal;
    }
    if (value == "power" || value == "Power") {
        return volt::ElectricalTerminalKind::Power;
    }
    if (value == "ground" || value == "Ground") {
        return volt::ElectricalTerminalKind::Ground;
    }
    if (value == "no_connect" || value == "no-connect" || value == "NoConnect") {
        return volt::ElectricalTerminalKind::NoConnect;
    }

    throw std::invalid_argument{"Unknown electrical terminal kind"};
}

[[nodiscard]] inline volt::ElectricalDirection parse_direction(const std::string &value) {
    if (value == "unspecified" || value == "Unspecified") {
        return volt::ElectricalDirection::Unspecified;
    }
    if (value == "input" || value == "Input") {
        return volt::ElectricalDirection::Input;
    }
    if (value == "output" || value == "Output") {
        return volt::ElectricalDirection::Output;
    }
    if (value == "bidirectional" || value == "Bidirectional") {
        return volt::ElectricalDirection::Bidirectional;
    }
    if (value == "passive" || value == "Passive") {
        return volt::ElectricalDirection::Passive;
    }

    throw std::invalid_argument{"Unknown electrical direction"};
}

[[nodiscard]] inline volt::ElectricalSignalDomain parse_signal_domain(const std::string &value) {
    if (value == "unspecified" || value == "Unspecified") {
        return volt::ElectricalSignalDomain::Unspecified;
    }
    if (value == "digital" || value == "Digital") {
        return volt::ElectricalSignalDomain::Digital;
    }
    if (value == "analog" || value == "Analog") {
        return volt::ElectricalSignalDomain::Analog;
    }
    if (value == "mixed" || value == "Mixed") {
        return volt::ElectricalSignalDomain::Mixed;
    }

    throw std::invalid_argument{"Unknown electrical signal domain"};
}

[[nodiscard]] inline volt::ElectricalDriveKind parse_drive_kind(const std::string &value) {
    if (value == "unspecified" || value == "Unspecified") {
        return volt::ElectricalDriveKind::Unspecified;
    }
    if (value == "push_pull" || value == "push-pull" || value == "PushPull") {
        return volt::ElectricalDriveKind::PushPull;
    }
    if (value == "open_collector" || value == "open-collector" || value == "OpenCollector") {
        return volt::ElectricalDriveKind::OpenCollector;
    }
    if (value == "open_drain" || value == "open-drain" || value == "OpenDrain") {
        return volt::ElectricalDriveKind::OpenDrain;
    }
    if (value == "high_impedance" || value == "high-impedance" || value == "HighImpedance") {
        return volt::ElectricalDriveKind::HighImpedance;
    }
    if (value == "passive" || value == "Passive") {
        return volt::ElectricalDriveKind::Passive;
    }

    throw std::invalid_argument{"Unknown electrical drive kind"};
}

[[nodiscard]] inline volt::ElectricalPolarity parse_polarity(const std::string &value) {
    if (value == "none" || value == "None") {
        return volt::ElectricalPolarity::None;
    }
    if (value == "active_high" || value == "active-high" || value == "ActiveHigh") {
        return volt::ElectricalPolarity::ActiveHigh;
    }
    if (value == "active_low" || value == "active-low" || value == "ActiveLow") {
        return volt::ElectricalPolarity::ActiveLow;
    }

    throw std::invalid_argument{"Unknown electrical polarity"};
}

[[nodiscard]] inline volt::NetKind parse_net_kind(const std::string &value) {
    if (value == "signal" || value == "Signal") {
        return volt::NetKind::Signal;
    }
    if (value == "power" || value == "Power") {
        return volt::NetKind::Power;
    }
    if (value == "ground" || value == "Ground") {
        return volt::NetKind::Ground;
    }
    if (value == "clock" || value == "Clock") {
        return volt::NetKind::Clock;
    }
    if (value == "analog" || value == "Analog") {
        return volt::NetKind::Analog;
    }
    if (value == "high_current" || value == "high-current" || value == "HighCurrent") {
        return volt::NetKind::HighCurrent;
    }

    throw std::invalid_argument{"Unknown net kind"};
}

[[nodiscard]] inline volt::PortRole parse_port_role(const std::string &value) {
    if (value == "passive" || value == "Passive") {
        return volt::PortRole::Passive;
    }
    if (value == "input" || value == "Input") {
        return volt::PortRole::Input;
    }
    if (value == "output" || value == "Output") {
        return volt::PortRole::Output;
    }
    if (value == "bidirectional" || value == "Bidirectional") {
        return volt::PortRole::Bidirectional;
    }
    if (value == "power" || value == "power_input" || value == "PowerInput") {
        return volt::PortRole::PowerInput;
    }
    if (value == "power_output" || value == "PowerOutput") {
        return volt::PortRole::PowerOutput;
    }
    if (value == "ground" || value == "Ground") {
        return volt::PortRole::Ground;
    }

    throw std::invalid_argument{"Unknown port role"};
}

[[nodiscard]] inline std::string net_kind_name(volt::NetKind kind) {
    switch (kind) {
    case volt::NetKind::Signal:
        return "Signal";
    case volt::NetKind::Power:
        return "Power";
    case volt::NetKind::Ground:
        return "Ground";
    case volt::NetKind::Clock:
        return "Clock";
    case volt::NetKind::Analog:
        return "Analog";
    case volt::NetKind::HighCurrent:
        return "HighCurrent";
    }

    throw std::logic_error{"Unhandled net kind"};
}

[[nodiscard]] inline std::string port_role_name(volt::PortRole role) {
    switch (role) {
    case volt::PortRole::Passive:
        return "Passive";
    case volt::PortRole::Input:
        return "Input";
    case volt::PortRole::Output:
        return "Output";
    case volt::PortRole::Bidirectional:
        return "Bidirectional";
    case volt::PortRole::PowerInput:
        return "PowerInput";
    case volt::PortRole::PowerOutput:
        return "PowerOutput";
    case volt::PortRole::Ground:
        return "Ground";
    }

    throw std::logic_error{"Unhandled port role"};
}

[[nodiscard]] inline std::string severity_name(volt::Severity severity) {
    switch (severity) {
    case volt::Severity::Info:
        return "info";
    case volt::Severity::Warning:
        return "warning";
    case volt::Severity::Error:
        return "error";
    }

    throw std::logic_error{"Unhandled diagnostic severity"};
}

inline void require_finite(double value, const char *message) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument{message};
    }
}

[[nodiscard]] inline volt::UnitDimension parse_dimension(const std::string &value) {
    if (value == "resistance") {
        return volt::UnitDimension::Resistance;
    }
    if (value == "capacitance") {
        return volt::UnitDimension::Capacitance;
    }
    if (value == "inductance") {
        return volt::UnitDimension::Inductance;
    }
    if (value == "voltage") {
        return volt::UnitDimension::Voltage;
    }
    if (value == "power") {
        return volt::UnitDimension::Power;
    }
    if (value == "ratio") {
        return volt::UnitDimension::Ratio;
    }

    throw std::invalid_argument{"Unknown electrical attribute dimension"};
}

[[nodiscard]] inline volt::ElectricalAttributeSpec
component_quantity_spec(const std::string &name, volt::UnitDimension dimension) {
    return volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{name},
                                         volt::ElectricalAttributeOwner::ComponentInstance,
                                         volt::ElectricalAttributeKind::DesignInput, dimension};
}

[[nodiscard]] inline volt::ElectricalAttributeSpec
selected_part_quantity_spec(const std::string &name, volt::UnitDimension dimension) {
    return volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{name},
                                         volt::ElectricalAttributeOwner::SelectedPart,
                                         volt::ElectricalAttributeKind::DesignInput, dimension};
}

[[nodiscard]] inline volt::ElectricalAttributeSpec
net_quantity_spec(const std::string &name, volt::UnitDimension dimension) {
    return volt::ElectricalAttributeSpec{volt::ElectricalAttributeName{name},
                                         volt::ElectricalAttributeOwner::Net,
                                         volt::ElectricalAttributeKind::DesignInput, dimension};
}

[[nodiscard]] inline volt::SheetOrientation
sheet_orientation_from_string(const std::string &value) {
    if (value == "Portrait") {
        return volt::SheetOrientation::Portrait;
    }
    if (value == "Landscape") {
        return volt::SheetOrientation::Landscape;
    }
    throw std::invalid_argument{"Unknown schematic sheet orientation"};
}

[[nodiscard]] inline volt::SchematicOrientation
schematic_orientation_from_string(const std::string &value) {
    if (value == "Right") {
        return volt::SchematicOrientation::Right;
    }
    if (value == "Down") {
        return volt::SchematicOrientation::Down;
    }
    if (value == "Left") {
        return volt::SchematicOrientation::Left;
    }
    if (value == "Up") {
        return volt::SchematicOrientation::Up;
    }
    throw std::invalid_argument{"Unknown schematic orientation"};
}

[[nodiscard]] inline std::string schematic_orientation_name(volt::SchematicOrientation value) {
    switch (value) {
    case volt::SchematicOrientation::Right:
        return "Right";
    case volt::SchematicOrientation::Down:
        return "Down";
    case volt::SchematicOrientation::Left:
        return "Left";
    case volt::SchematicOrientation::Up:
        return "Up";
    }
    throw std::logic_error{"Unhandled schematic orientation"};
}

[[nodiscard]] inline volt::SymbolLineRole symbol_line_role_from_string(const std::string &value) {
    if (value == "Normal") {
        return volt::SymbolLineRole::Normal;
    }
    if (value == "TerminalLeadStart") {
        return volt::SymbolLineRole::TerminalLeadStart;
    }
    if (value == "TerminalLeadEnd") {
        return volt::SymbolLineRole::TerminalLeadEnd;
    }
    throw std::invalid_argument{"Unknown symbol line role"};
}

[[nodiscard]] inline int schematic_orientation_quarter_turns(volt::SchematicOrientation value) {
    switch (value) {
    case volt::SchematicOrientation::Right:
        return 0;
    case volt::SchematicOrientation::Down:
        return 1;
    case volt::SchematicOrientation::Left:
        return 2;
    case volt::SchematicOrientation::Up:
        return 3;
    }
    throw std::logic_error{"Unhandled schematic orientation"};
}

[[nodiscard]] inline volt::SchematicOrientation
schematic_orientation_from_quarter_turns(int value) {
    switch (value % 4) {
    case 0:
        return volt::SchematicOrientation::Right;
    case 1:
        return volt::SchematicOrientation::Down;
    case 2:
        return volt::SchematicOrientation::Left;
    case 3:
        return volt::SchematicOrientation::Up;
    }
    throw std::logic_error{"Unhandled schematic orientation turn count"};
}

[[nodiscard]] inline volt::SchematicOrientation
rotated_schematic_orientation(volt::SchematicOrientation local,
                              volt::SchematicOrientation instance) {
    return schematic_orientation_from_quarter_turns(schematic_orientation_quarter_turns(local) +
                                                    schematic_orientation_quarter_turns(instance));
}

[[nodiscard]] inline volt::RouteIntent route_intent_from_string(const std::string &value) {
    if (value == "Direct") {
        return volt::RouteIntent::Direct;
    }
    if (value == "Orthogonal") {
        return volt::RouteIntent::Orthogonal;
    }
    throw std::invalid_argument{"Unknown schematic route intent"};
}

[[nodiscard]] inline volt::PowerPortKind power_port_kind_from_string(const std::string &value) {
    if (value == "Power") {
        return volt::PowerPortKind::Power;
    }
    if (value == "Ground") {
        return volt::PowerPortKind::Ground;
    }
    throw std::invalid_argument{"Unknown schematic power port kind"};
}

[[nodiscard]] inline volt::SheetPortKind sheet_port_kind_from_string(const std::string &value) {
    if (value == "Input") {
        return volt::SheetPortKind::Input;
    }
    if (value == "Output") {
        return volt::SheetPortKind::Output;
    }
    if (value == "Bidirectional") {
        return volt::SheetPortKind::Bidirectional;
    }
    if (value == "OffPage") {
        return volt::SheetPortKind::OffPage;
    }
    throw std::invalid_argument{"Unknown schematic sheet port kind"};
}

[[nodiscard]] inline volt::TextHorizontalAlignment
text_horizontal_alignment_from_string(const std::string &value) {
    if (value == "Start") {
        return volt::TextHorizontalAlignment::Start;
    }
    if (value == "Middle") {
        return volt::TextHorizontalAlignment::Middle;
    }
    if (value == "End") {
        return volt::TextHorizontalAlignment::End;
    }
    throw std::invalid_argument{"Unknown schematic text horizontal alignment"};
}

[[nodiscard]] inline volt::TextVerticalAlignment
text_vertical_alignment_from_string(const std::string &value) {
    if (value == "Top") {
        return volt::TextVerticalAlignment::Top;
    }
    if (value == "Middle") {
        return volt::TextVerticalAlignment::Middle;
    }
    if (value == "Bottom") {
        return volt::TextVerticalAlignment::Bottom;
    }
    if (value == "Baseline") {
        return volt::TextVerticalAlignment::Baseline;
    }
    throw std::invalid_argument{"Unknown schematic text vertical alignment"};
}

} // namespace

} // namespace volt::python
