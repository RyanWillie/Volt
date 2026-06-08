#pragma once

#include "binding_enum_conversions.hpp"
#include "binding_property_conversions.hpp"
#include "binding_schematic_conversions.hpp"

namespace volt::python {

namespace {

struct PinPresetSemantics {
    volt::ConnectionRequirement connection_requirement = volt::ConnectionRequirement::Required;
    volt::ElectricalTerminalKind terminal_kind = volt::ElectricalTerminalKind::Unspecified;
    volt::ElectricalDirection direction = volt::ElectricalDirection::Unspecified;
    volt::ElectricalSignalDomain signal_domain = volt::ElectricalSignalDomain::Unspecified;
    volt::ElectricalDriveKind drive_kind = volt::ElectricalDriveKind::Unspecified;
    volt::ElectricalPolarity polarity = volt::ElectricalPolarity::None;
};

[[nodiscard]] inline PinPresetSemantics pin_preset_semantics(const std::string &value) {
    if (value == "passive" || value == "Passive") {
        return {volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
                volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
                volt::ElectricalDriveKind::Passive};
    }
    if (value == "input" || value == "digital_input" || value == "DigitalInput") {
        return {volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Signal,
                volt::ElectricalDirection::Input, volt::ElectricalSignalDomain::Digital};
    }
    if (value == "output" || value == "digital_output" || value == "DigitalOutput") {
        return {volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Signal,
                volt::ElectricalDirection::Output, volt::ElectricalSignalDomain::Digital};
    }
    if (value == "analog_input" || value == "AnalogInput") {
        return {volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Signal,
                volt::ElectricalDirection::Input, volt::ElectricalSignalDomain::Analog};
    }
    if (value == "analog_output" || value == "AnalogOutput") {
        return {volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Signal,
                volt::ElectricalDirection::Output, volt::ElectricalSignalDomain::Analog};
    }
    if (value == "bidirectional" || value == "Bidirectional") {
        return {volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Signal,
                volt::ElectricalDirection::Bidirectional};
    }
    if (value == "power" || value == "power_input" || value == "PowerInput") {
        return {volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Power,
                volt::ElectricalDirection::Input};
    }
    if (value == "power_output" || value == "PowerOutput") {
        return {volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Power,
                volt::ElectricalDirection::Output};
    }
    if (value == "ground" || value == "Ground") {
        return {volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Ground,
                volt::ElectricalDirection::Passive};
    }
    if (value == "no_connect" || value == "no-connect" || value == "NoConnect") {
        return {volt::ConnectionRequirement::MustNotConnect,
                volt::ElectricalTerminalKind::NoConnect};
    }

    throw std::invalid_argument{"Unknown PinSpec role preset"};
}

template <typename T>
[[nodiscard]] inline T lower_or_check(T explicit_value, T unspecified, T preset_value,
                                      const char *message) {
    if (explicit_value == unspecified) {
        return preset_value;
    }
    if (preset_value != unspecified && explicit_value != preset_value) {
        throw std::invalid_argument{message};
    }
    return explicit_value;
}

[[nodiscard]] inline volt::ConnectionRequirement
lower_connection_requirement(volt::ConnectionRequirement explicit_value,
                             volt::ConnectionRequirement preset_value) {
    if (preset_value == volt::ConnectionRequirement::MustNotConnect &&
        explicit_value == volt::ConnectionRequirement::Optional) {
        throw std::invalid_argument{
            "PinSpec role preset contradicts explicit connection requirement"};
    }
    if (preset_value == volt::ConnectionRequirement::MustNotConnect &&
        explicit_value == volt::ConnectionRequirement::Required) {
        return volt::ConnectionRequirement::MustNotConnect;
    }
    return explicit_value;
}

inline void
apply_pin_preset(const PinPresetSemantics &preset, volt::ConnectionRequirement &requirement,
                 volt::ElectricalTerminalKind &terminal_kind, volt::ElectricalDirection &direction,
                 volt::ElectricalSignalDomain &signal_domain, volt::ElectricalDriveKind &drive_kind,
                 volt::ElectricalPolarity &polarity) {
    requirement = lower_connection_requirement(requirement, preset.connection_requirement);
    terminal_kind = lower_or_check(terminal_kind, volt::ElectricalTerminalKind::Unspecified,
                                   preset.terminal_kind,
                                   "PinSpec role preset contradicts explicit terminal kind");
    direction = lower_or_check(direction, volt::ElectricalDirection::Unspecified, preset.direction,
                               "PinSpec role preset contradicts explicit direction");
    signal_domain = lower_or_check(signal_domain, volt::ElectricalSignalDomain::Unspecified,
                                   preset.signal_domain,
                                   "PinSpec role preset contradicts explicit signal domain");
    drive_kind =
        lower_or_check(drive_kind, volt::ElectricalDriveKind::Unspecified, preset.drive_kind,
                       "PinSpec role preset contradicts explicit drive kind");
    polarity = lower_or_check(polarity, volt::ElectricalPolarity::None, preset.polarity,
                              "PinSpec role preset contradicts explicit polarity");
}

[[nodiscard]] inline volt::authoring::PinSpec pin_spec_from_dict(const py::dict &dict) {
    if (!dict.contains("name")) {
        throw std::invalid_argument{"Pin specs must include a name"};
    }
    if (!dict.contains("number")) {
        throw std::invalid_argument{"Pin specs must include a number"};
    }

    auto requirement = volt::ConnectionRequirement::Required;
    if (dict.contains("requirement")) {
        requirement = parse_connection_requirement(py::cast<std::string>(dict["requirement"]));
    }

    auto terminal_kind = volt::ElectricalTerminalKind::Unspecified;
    if (dict.contains("terminal")) {
        terminal_kind = parse_terminal_kind(py::cast<std::string>(dict["terminal"]));
    }

    auto direction = volt::ElectricalDirection::Unspecified;
    if (dict.contains("direction")) {
        direction = parse_direction(py::cast<std::string>(dict["direction"]));
    }

    auto signal_domain = volt::ElectricalSignalDomain::Unspecified;
    if (dict.contains("signal")) {
        signal_domain = parse_signal_domain(py::cast<std::string>(dict["signal"]));
    }

    auto drive_kind = volt::ElectricalDriveKind::Unspecified;
    if (dict.contains("drive")) {
        drive_kind = parse_drive_kind(py::cast<std::string>(dict["drive"]));
    }

    auto polarity = volt::ElectricalPolarity::None;
    if (dict.contains("polarity")) {
        polarity = parse_polarity(py::cast<std::string>(dict["polarity"]));
    }

    if (dict.contains("role")) {
        apply_pin_preset(pin_preset_semantics(py::cast<std::string>(dict["role"])), requirement,
                         terminal_kind, direction, signal_domain, drive_kind, polarity);
    }

    auto voltage_range = std::optional<volt::QuantityRange>{};
    if (dict.contains("voltage_range")) {
        const auto range = py::cast<std::pair<std::optional<double>, std::optional<double>>>(
            dict["voltage_range"]);
        if (!range.first.has_value() && !range.second.has_value()) {
            throw std::invalid_argument{"voltage_range must include at least one bound"};
        }
        if (range.first.has_value()) {
            require_finite(range.first.value(), "Voltage range bounds must be finite");
        }
        if (range.second.has_value()) {
            require_finite(range.second.value(), "Voltage range bounds must be finite");
        }
        if (range.first.has_value() && range.second.has_value()) {
            voltage_range = volt::QuantityRange::bounded(
                volt::Quantity{volt::UnitDimension::Voltage, range.first.value()},
                volt::Quantity{volt::UnitDimension::Voltage, range.second.value()});
        } else if (range.first.has_value()) {
            voltage_range = volt::QuantityRange::minimum(
                volt::Quantity{volt::UnitDimension::Voltage, range.first.value()});
        } else {
            voltage_range = volt::QuantityRange::maximum(
                volt::Quantity{volt::UnitDimension::Voltage, range.second.value()});
        }
    }

    return volt::authoring::PinSpec{py::cast<std::string>(dict["name"]),
                                    py::cast<std::string>(dict["number"]),
                                    requirement,
                                    terminal_kind,
                                    direction,
                                    signal_domain,
                                    drive_kind,
                                    polarity,
                                    voltage_range};
}

[[nodiscard]] inline std::vector<volt::authoring::PinSpec>
pin_specs_from_list(const py::list &pins) {
    auto specs = std::vector<volt::authoring::PinSpec>{};
    specs.reserve(static_cast<std::size_t>(py::len(pins)));
    for (const auto item : pins) {
        specs.push_back(pin_spec_from_dict(py::cast<py::dict>(item)));
    }
    return specs;
}

[[nodiscard]] inline std::string string_from_pin_key(const py::handle &key) {
    if (py::isinstance<py::int_>(key) || py::isinstance<py::str>(key)) {
        return py::cast<std::string>(py::str(key));
    }

    throw std::invalid_argument{"Pin-pad mapping keys must be pin numbers or names"};
}

} // namespace

} // namespace volt::python
