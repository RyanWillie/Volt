#pragma once

#include "binding_enum_conversions.hpp"
#include "binding_property_conversions.hpp"
#include "binding_schematic_conversions.hpp"

namespace volt::python {

namespace {

[[nodiscard]] inline volt::authoring::PinSpec pin_spec_from_dict(const py::dict &dict) {
    if (!dict.contains("name")) {
        throw std::invalid_argument{"Pin specs must include a name"};
    }
    if (!dict.contains("number")) {
        throw std::invalid_argument{"Pin specs must include a number"};
    }

    auto role = volt::PinRole::Passive;
    if (dict.contains("role")) {
        role = parse_pin_role(py::cast<std::string>(dict["role"]));
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
                                    role,
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
