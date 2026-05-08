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

/** Run the first logical electrical-rule checks over a circuit. */
[[nodiscard]] inline DiagnosticReport validate_circuit(const Circuit &circuit) {
    auto report = DiagnosticReport{};

    const auto is_no_connect_pin = [](const PinDefinition &definition) {
        return definition.connection_requirement() == ConnectionRequirement::MustNotConnect ||
               definition.role() == PinRole::NoConnect;
    };
    const auto is_output_pin = [](const PinDefinition &definition) {
        return definition.role() == PinRole::PowerOutput ||
               definition.role() == PinRole::DigitalOutput ||
               definition.role() == PinRole::AnalogOutput;
    };

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

    const auto voltage_attribute = ElectricalAttributeName{"voltage"};
    const auto voltage_rating_attribute = ElectricalAttributeName{"voltage_rating"};

    for (std::size_t index = 0; index < circuit.net_count(); ++index) {
        const auto net_id = NetId{index};
        const auto &net = circuit.net(net_id);

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

        auto output_pins = std::vector<PinId>{};
        for (const auto pin_id : net.pins()) {
            const auto &pin = circuit.pin(pin_id);
            const auto &definition = circuit.pin_definition(pin.definition());
            if (is_output_pin(definition)) {
                output_pins.push_back(pin_id);
            }
        }

        if (net.electrical_attributes().contains(voltage_attribute)) {
            const auto net_voltage =
                std::abs(net.electrical_attributes().get(voltage_attribute).as_quantity().value());
            for (const auto pin_id : net.pins()) {
                const auto &pin = circuit.pin(pin_id);
                const auto &selected_part = circuit.selected_physical_part(pin.component());
                if (!selected_part.has_value() ||
                    !selected_part->electrical_attributes().contains(voltage_rating_attribute)) {
                    continue;
                }

                const auto voltage_rating = selected_part->electrical_attributes()
                                                .get(voltage_rating_attribute)
                                                .as_quantity()
                                                .value();
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

    return report;
}

} // namespace volt
