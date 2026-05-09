#pragma once

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/core/ids.hpp>

namespace volt {

namespace detail {

[[nodiscard]] inline bool is_no_connect_pin(const PinDefinition &definition) {
    return definition.connection_requirement() == ConnectionRequirement::MustNotConnect ||
           definition.role() == PinRole::NoConnect;
}

[[nodiscard]] inline bool is_output_pin(const PinDefinition &definition) {
    if (definition.direction() == ElectricalDirection::Output) {
        return true;
    }
    return definition.role() == PinRole::PowerOutput ||
           definition.role() == PinRole::DigitalOutput ||
           definition.role() == PinRole::AnalogOutput;
}

[[nodiscard]] inline bool is_power_input(const PinDefinition &definition) {
    return definition.terminal_kind() == ElectricalTerminalKind::Power &&
           definition.direction() == ElectricalDirection::Input;
}

[[nodiscard]] inline bool is_power_source(const PinDefinition &definition) {
    return definition.terminal_kind() == ElectricalTerminalKind::Power &&
           (definition.direction() == ElectricalDirection::Output ||
            definition.direction() == ElectricalDirection::Bidirectional);
}

inline void validate_pin_connection_requirements(const Circuit &circuit, DiagnosticReport &report) {
    for (std::size_t index = 0; index < circuit.pin_count(); ++index) {
        const auto pin_id = PinId{index};
        const auto &pin = circuit.pin(pin_id);
        const auto &definition = circuit.pin_definition(pin.definition());
        const auto connected_net = circuit.net_of(pin_id);

        if (is_no_connect_pin(definition)) {
            if (connected_net.has_value()) {
                report.add(Diagnostic{
                    Severity::Error,
                    DiagnosticCode{"PIN_MUST_NOT_CONNECT"},
                    "Pin must not be connected",
                    std::vector{
                        EntityRef::pin(pin_id),
                        EntityRef::component(pin.component()),
                        EntityRef::pin_def(pin.definition()),
                        EntityRef::net(connected_net.value()),
                    },
                });
            }
            continue;
        }

        if (definition.connection_requirement() == ConnectionRequirement::Required &&
            !connected_net.has_value()) {
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"UNCONNECTED_REQUIRED_PIN"},
                "Required pin is not connected",
                std::vector{
                    EntityRef::pin(pin_id),
                    EntityRef::component(pin.component()),
                    EntityRef::pin_def(pin.definition()),
                },
            });
        }
    }
}

inline void validate_net_shape(NetId net_id, const Net &net, DiagnosticReport &report) {
    if (net.pins().empty()) {
        report.add(Diagnostic{
            Severity::Warning,
            DiagnosticCode{"EMPTY_NET"},
            "Net has no connected pins",
            std::vector{EntityRef::net(net_id)},
        });
    } else if (net.pins().size() == 1) {
        report.add(Diagnostic{
            Severity::Warning,
            DiagnosticCode{"SINGLE_PIN_NET"},
            "Net has only one connected pin",
            std::vector{EntityRef::net(net_id), EntityRef::pin(net.pins().front())},
        });
    }
}

inline void validate_power_and_ground_semantics(const Circuit &circuit, NetId net_id,
                                                const Net &net, DiagnosticReport &report) {
    auto power_input_pins = std::vector<PinId>{};
    auto has_power_source = false;
    for (const auto pin_id : net.pins()) {
        const auto &pin = circuit.pin(pin_id);
        const auto &definition = circuit.pin_definition(pin.definition());
        if (is_power_input(definition)) {
            power_input_pins.push_back(pin_id);
        }
        if (is_power_source(definition)) {
            has_power_source = true;
        }
        if (definition.terminal_kind() == ElectricalTerminalKind::Ground &&
            net.kind() != NetKind::Ground) {
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"PIN_GROUND_ON_NON_GROUND_NET"},
                "Ground pin is connected to a non-ground net",
                std::vector{EntityRef::net(net_id), EntityRef::pin(pin_id),
                            EntityRef::pin_def(pin.definition())},
            });
        }
        if (definition.terminal_kind() == ElectricalTerminalKind::Power &&
            net.kind() == NetKind::Ground) {
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"PIN_POWER_ON_GROUND_NET"},
                "Power pin is connected to a ground net",
                std::vector{EntityRef::net(net_id), EntityRef::pin(pin_id),
                            EntityRef::pin_def(pin.definition())},
            });
        }
    }

    if (!power_input_pins.empty() && !has_power_source && net.kind() != NetKind::Ground) {
        auto entities = std::vector<EntityRef>{EntityRef::net(net_id)};
        for (const auto pin_id : power_input_pins) {
            entities.push_back(EntityRef::pin(pin_id));
        }
        report.add(Diagnostic{
            Severity::Error,
            DiagnosticCode{"POWER_INPUT_WITHOUT_SOURCE"},
            "Power input is connected to a net with no typed supply source",
            std::move(entities),
        });
    }
}

inline void validate_selected_part_voltage_ratings(const Circuit &circuit, NetId net_id,
                                                   const Net &net, DiagnosticReport &report) {
    const auto voltage_attribute_name = ElectricalAttributeName{"voltage"};
    const auto voltage_rating_attribute_name = ElectricalAttributeName{"voltage_rating"};
    if (net.electrical_attributes().contains(voltage_attribute_name)) {
        const auto &net_voltage_attribute = net.electrical_attributes().get(voltage_attribute_name);
        if (net_voltage_attribute.kind() == ElectricalAttributeValueKind::Quantity) {
            const auto net_voltage = std::abs(net_voltage_attribute.as_quantity().value());
            for (const auto pin_id : net.pins()) {
                const auto &pin = circuit.pin(pin_id);
                const auto &selected_part = circuit.selected_physical_part(pin.component());
                if (!selected_part.has_value() || !selected_part->electrical_attributes().contains(
                                                      voltage_rating_attribute_name)) {
                    continue;
                }

                const auto &voltage_rating_attribute =
                    selected_part->electrical_attributes().get(voltage_rating_attribute_name);
                if (voltage_rating_attribute.kind() != ElectricalAttributeValueKind::Quantity) {
                    continue;
                }

                const auto voltage_rating = voltage_rating_attribute.as_quantity().value();
                if (net_voltage > voltage_rating) {
                    report.add(Diagnostic{
                        Severity::Error,
                        DiagnosticCode{"SELECTED_PART_VOLTAGE_RATING_EXCEEDED"},
                        "Net voltage exceeds selected part voltage rating",
                        std::vector{EntityRef::net(net_id), EntityRef::pin(pin_id),
                                    EntityRef::component(pin.component())},
                    });
                }
            }
        }
    }
}

inline void validate_pin_voltage_ranges(const Circuit &circuit, NetId net_id, const Net &net,
                                        DiagnosticReport &report) {
    const auto voltage_attribute_name = ElectricalAttributeName{"voltage"};
    const auto voltage_range_attribute_name = ElectricalAttributeName{"voltage_range"};
    if (!net.electrical_attributes().contains(voltage_attribute_name)) {
        return;
    }

    const auto &net_voltage_attribute = net.electrical_attributes().get(voltage_attribute_name);
    if (net_voltage_attribute.kind() != ElectricalAttributeValueKind::Quantity) {
        return;
    }

    const auto &net_voltage_quantity = net_voltage_attribute.as_quantity();
    if (net_voltage_quantity.dimension() != UnitDimension::Voltage) {
        return;
    }

    const auto net_voltage = net_voltage_quantity.value();
    for (const auto pin_id : net.pins()) {
        const auto &pin = circuit.pin(pin_id);
        const auto &definition = circuit.pin_definition(pin.definition());
        if (!definition.electrical_attributes().contains(voltage_range_attribute_name)) {
            continue;
        }

        const auto &range_attribute =
            definition.electrical_attributes().get(voltage_range_attribute_name);
        if (range_attribute.kind() != ElectricalAttributeValueKind::Range) {
            continue;
        }

        const auto &range = range_attribute.as_range();
        if (range.dimension() != UnitDimension::Voltage) {
            continue;
        }

        const auto below_minimum =
            range.minimum().has_value() && net_voltage < range.minimum()->value();
        const auto above_maximum =
            range.maximum().has_value() && net_voltage > range.maximum()->value();
        if (below_minimum || above_maximum) {
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"PIN_VOLTAGE_RANGE_VIOLATION"},
                "Net voltage is outside pin voltage range",
                std::vector{EntityRef::net(net_id), EntityRef::pin(pin_id),
                            EntityRef::pin_def(pin.definition())},
            });
        }
    }
}

inline void validate_output_driver_conflicts(const Circuit &circuit, NetId net_id, const Net &net,
                                             DiagnosticReport &report) {
    auto output_pins = std::vector<PinId>{};
    for (const auto pin_id : net.pins()) {
        const auto &pin = circuit.pin(pin_id);
        const auto &definition = circuit.pin_definition(pin.definition());
        if (is_output_pin(definition)) {
            output_pins.push_back(pin_id);
        }
    }

    if (output_pins.size() > 1) {
        auto entities = std::vector<EntityRef>{EntityRef::net(net_id)};
        for (const auto pin_id : output_pins) {
            entities.push_back(EntityRef::pin(pin_id));
        }

        report.add(Diagnostic{
            Severity::Error,
            DiagnosticCode{"MULTIPLE_OUTPUTS_ON_NET"},
            "Net has multiple output drivers",
            std::move(entities),
        });
    }
}

inline void validate_net_shapes(const Circuit &circuit, DiagnosticReport &report) {
    for (std::size_t index = 0; index < circuit.net_count(); ++index) {
        const auto net_id = NetId{index};
        const auto &net = circuit.net(net_id);

        validate_net_shape(net_id, net, report);
    }
}

inline void validate_net_electrical_rules(const Circuit &circuit, DiagnosticReport &report) {
    for (std::size_t index = 0; index < circuit.net_count(); ++index) {
        const auto net_id = NetId{index};
        const auto &net = circuit.net(net_id);

        validate_power_and_ground_semantics(circuit, net_id, net, report);
        validate_selected_part_voltage_ratings(circuit, net_id, net, report);
        validate_pin_voltage_ranges(circuit, net_id, net, report);
        validate_output_driver_conflicts(circuit, net_id, net, report);
    }
}

inline void validate_net_semantics(const Circuit &circuit, DiagnosticReport &report) {
    for (std::size_t index = 0; index < circuit.net_count(); ++index) {
        const auto net_id = NetId{index};
        const auto &net = circuit.net(net_id);

        validate_net_shape(net_id, net, report);
        validate_power_and_ground_semantics(circuit, net_id, net, report);
        validate_selected_part_voltage_ratings(circuit, net_id, net, report);
        validate_pin_voltage_ranges(circuit, net_id, net, report);
        validate_output_driver_conflicts(circuit, net_id, net, report);
    }
}

inline void validate_physical_part_selection(const Circuit &circuit, DiagnosticReport &report) {
    for (std::size_t index = 0; index < circuit.component_count(); ++index) {
        const auto component_id = ComponentId{index};
        const auto &component = circuit.component(component_id);
        if (!component.selected_physical_part().has_value()) {
            report.add(Diagnostic{
                Severity::Error,
                DiagnosticCode{"PHYSICAL_PART_REQUIRED"},
                "Component requires a selected physical part for PCB readiness",
                std::vector{EntityRef::component(component_id),
                            EntityRef::component_def(component.definition())},
            });
        }
    }
}

} // namespace detail

/** Validate logical connectivity shape and pin connection requirements. */
[[nodiscard]] inline DiagnosticReport validate_connectivity(const Circuit &circuit) {
    auto report = DiagnosticReport{};

    detail::validate_pin_connection_requirements(circuit, report);
    detail::validate_net_shapes(circuit, report);

    return report;
}

/** Validate electrical rules over the existing logical circuit connectivity. */
[[nodiscard]] inline DiagnosticReport validate_electrical_rules(const Circuit &circuit) {
    auto report = DiagnosticReport{};

    detail::validate_net_electrical_rules(circuit, report);

    return report;
}

/** Run the default logical circuit validation suite. */
[[nodiscard]] inline DiagnosticReport validate_circuit(const Circuit &circuit) {
    auto report = DiagnosticReport{};

    detail::validate_pin_connection_requirements(circuit, report);
    detail::validate_net_semantics(circuit, report);

    return report;
}

/** Validate whether a circuit is ready for PCB/layout work. */
[[nodiscard]] inline DiagnosticReport validate_for_pcb(const Circuit &circuit) {
    auto report = validate_circuit(circuit);

    detail::validate_physical_part_selection(circuit, report);

    return report;
}

} // namespace volt
