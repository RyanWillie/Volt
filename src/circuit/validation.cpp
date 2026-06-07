#include <volt/circuit/validation.hpp>

#include <string>
#include <string_view>
#include <utility>

#include <volt/circuit/queries.hpp>
#include <volt/core/rule_set.hpp>

namespace volt::detail {

[[nodiscard]] Diagnostic erc_error(std::string_view code, std::string message,
                                   std::vector<EntityRef> entities = {}) {
    return Diagnostic{Severity::Error, DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::Erc}, std::move(message),
                      std::move(entities)};
}

[[nodiscard]] Diagnostic erc_warning(std::string_view code, std::string message,
                                     std::vector<EntityRef> entities = {}) {
    return Diagnostic{Severity::Warning, DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::Erc}, std::move(message),
                      std::move(entities)};
}

[[nodiscard]] bool is_no_connect_pin(const PinDefinition &definition) {
    return definition.connection_requirement() == ConnectionRequirement::MustNotConnect ||
           definition.role() == PinRole::NoConnect;
}

[[nodiscard]] bool is_output_pin(const PinDefinition &definition) {
    if (definition.direction() == ElectricalDirection::Output) {
        return true;
    }
    return definition.role() == PinRole::PowerOutput ||
           definition.role() == PinRole::DigitalOutput ||
           definition.role() == PinRole::AnalogOutput;
}

[[nodiscard]] bool is_input_pin(const PinDefinition &definition) {
    if (definition.role() == PinRole::DigitalInput || definition.role() == PinRole::AnalogInput) {
        return true;
    }
    return definition.terminal_kind() == ElectricalTerminalKind::Signal &&
           definition.direction() == ElectricalDirection::Input;
}

[[nodiscard]] bool can_drive_signal_net(const PinDefinition &definition) {
    if (definition.terminal_kind() != ElectricalTerminalKind::Signal ||
        definition.signal_domain() == ElectricalSignalDomain::Unspecified) {
        return false;
    }

    return definition.direction() == ElectricalDirection::Output ||
           definition.direction() == ElectricalDirection::Bidirectional ||
           definition.role() == PinRole::DigitalOutput ||
           definition.role() == PinRole::AnalogOutput ||
           definition.role() == PinRole::Bidirectional;
}

[[nodiscard]] bool is_power_input(const PinDefinition &definition) {
    return definition.terminal_kind() == ElectricalTerminalKind::Power &&
           definition.direction() == ElectricalDirection::Input;
}

[[nodiscard]] bool is_power_source(const PinDefinition &definition) {
    return definition.terminal_kind() == ElectricalTerminalKind::Power &&
           (definition.direction() == ElectricalDirection::Output ||
            definition.direction() == ElectricalDirection::Bidirectional);
}

NetContinuityView::NetContinuityView(const Circuit &circuit) {
    parent_.reserve(circuit.net_count());
    for (std::size_t index = 0; index < circuit.net_count(); ++index) {
        parent_.push_back(index);
    }

    for (std::size_t index = 0; index < circuit.port_binding_count(); ++index) {
        const auto &binding = circuit.port_binding(PortBindingId{index});
        join(binding.internal_net(), binding.parent_net());
    }
}

[[nodiscard]] std::vector<PinId> NetContinuityView::pins_for_group(const Circuit &circuit,
                                                                   NetId net) const {
    auto pins = std::vector<PinId>{};
    const auto group = find(net.index());
    for (std::size_t index = 0; index < circuit.net_count(); ++index) {
        if (find(index) != group) {
            continue;
        }
        const auto &net_pins = circuit.net(NetId{index}).pins();
        pins.insert(pins.end(), net_pins.begin(), net_pins.end());
    }
    return pins;
}

[[nodiscard]] std::size_t NetContinuityView::find(std::size_t index) const {
    while (parent_.at(index) != index) {
        index = parent_.at(index);
    }
    return index;
}

void NetContinuityView::join(NetId first, NetId second) {
    const auto first_root = find(first.index());
    const auto second_root = find(second.index());
    if (first_root != second_root) {
        parent_.at(second_root) = first_root;
    }
}

void validate_pin_connection_requirements(const Circuit &circuit, DiagnosticReport &report) {
    for (std::size_t index = 0; index < circuit.pin_count(); ++index) {
        const auto pin_id = PinId{index};
        const auto &pin = circuit.pin(pin_id);
        const auto &definition = circuit.pin_definition(pin.definition());
        const auto connected_net = queries::net_of(circuit, pin_id);

        if (is_no_connect_pin(definition)) {
            if (connected_net.has_value()) {
                report.add(erc_error(erc_diagnostic_codes::PinMustNotConnect,
                                     "Pin must not be connected",
                                     std::vector{
                                         EntityRef::pin(pin_id),
                                         EntityRef::component(pin.component()),
                                         EntityRef::pin_def(pin.definition()),
                                         EntityRef::net(connected_net.value()),
                                     }));
            }
            continue;
        }

        if (circuit.is_intentional_no_connect_pin(pin_id)) {
            if (connected_net.has_value()) {
                report.add(erc_error(erc_diagnostic_codes::PinIntentionalNoConnectIsConnected,
                                     "Intentional no-connect pin is connected",
                                     std::vector{
                                         EntityRef::pin(pin_id),
                                         EntityRef::component(pin.component()),
                                         EntityRef::pin_def(pin.definition()),
                                         EntityRef::net(connected_net.value()),
                                     }));
            }
            continue;
        }

        if (definition.connection_requirement() == ConnectionRequirement::Required &&
            !connected_net.has_value()) {
            report.add(erc_error(erc_diagnostic_codes::UnconnectedRequiredPin,
                                 "Required pin is not connected",
                                 std::vector{
                                     EntityRef::pin(pin_id),
                                     EntityRef::component(pin.component()),
                                     EntityRef::pin_def(pin.definition()),
                                 }));
        }
    }
}

void validate_net_shape(NetId net_id, const Net &net, const std::vector<PinId> &group_pins,
                        DiagnosticReport &report) {
    if (group_pins.empty()) {
        report.add(erc_warning(erc_diagnostic_codes::EmptyNet, "Net has no connected pins",
                               std::vector{EntityRef::net(net_id)}));
    } else if (group_pins.size() == 1 && net.contains(group_pins.front())) {
        report.add(
            erc_warning(erc_diagnostic_codes::SinglePinNet, "Net has only one connected pin",
                        std::vector{EntityRef::net(net_id), EntityRef::pin(group_pins.front())}));
    }
}

void validate_power_and_ground_semantics(const Circuit &circuit, NetId net_id, const Net &net,
                                         const std::vector<PinId> &group_pins,
                                         DiagnosticReport &report) {
    auto power_input_pins = std::vector<PinId>{};
    auto has_power_source = false;
    for (const auto pin_id : group_pins) {
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
            report.add(erc_error(erc_diagnostic_codes::PinGroundOnNonGroundNet,
                                 "Ground pin is connected to a non-ground net",
                                 std::vector{EntityRef::net(net_id), EntityRef::pin(pin_id),
                                             EntityRef::pin_def(pin.definition())}));
        }
        if (definition.terminal_kind() == ElectricalTerminalKind::Power &&
            net.kind() == NetKind::Ground) {
            report.add(erc_error(erc_diagnostic_codes::PinPowerOnGroundNet,
                                 "Power pin is connected to a ground net",
                                 std::vector{EntityRef::net(net_id), EntityRef::pin(pin_id),
                                             EntityRef::pin_def(pin.definition())}));
        }
    }

    if (!power_input_pins.empty() && !has_power_source && net.kind() != NetKind::Ground) {
        auto entities = std::vector<EntityRef>{EntityRef::net(net_id)};
        for (const auto pin_id : power_input_pins) {
            entities.push_back(EntityRef::pin(pin_id));
        }
        report.add(erc_error(erc_diagnostic_codes::PowerInputWithoutSource,
                             "Power input is connected to a net with no typed supply source",
                             std::move(entities)));
    }
}

void validate_selected_part_voltage_ratings(const Circuit &circuit, NetId net_id, const Net &,
                                            const std::vector<PinId> &group_pins,
                                            DiagnosticReport &report) {
    const auto voltage_attribute_name = ElectricalAttributeName{"voltage"};
    const auto voltage_rating_attribute_name = ElectricalAttributeName{"voltage_rating"};
    const auto &net_attributes = circuit.net_electrical_attributes(net_id);
    if (net_attributes.contains(voltage_attribute_name)) {
        const auto &net_voltage_attribute = net_attributes.get(voltage_attribute_name);
        if (net_voltage_attribute.kind() == ElectricalAttributeValueKind::Quantity) {
            const auto net_voltage = std::abs(net_voltage_attribute.as_quantity().value());
            for (const auto pin_id : group_pins) {
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
                    report.add(erc_error(erc_diagnostic_codes::SelectedPartVoltageRatingExceeded,
                                         "Net voltage exceeds selected part voltage rating",
                                         std::vector{EntityRef::net(net_id), EntityRef::pin(pin_id),
                                                     EntityRef::component(pin.component())}));
                }
            }
        }
    }
}

void validate_pin_voltage_ranges(const Circuit &circuit, NetId net_id, const Net &,
                                 const std::vector<PinId> &group_pins, DiagnosticReport &report) {
    const auto voltage_attribute_name = ElectricalAttributeName{"voltage"};
    const auto voltage_range_attribute_name = ElectricalAttributeName{"voltage_range"};
    const auto &net_attributes = circuit.net_electrical_attributes(net_id);
    if (!net_attributes.contains(voltage_attribute_name)) {
        return;
    }

    const auto &net_voltage_attribute = net_attributes.get(voltage_attribute_name);
    if (net_voltage_attribute.kind() != ElectricalAttributeValueKind::Quantity) {
        return;
    }

    const auto &net_voltage_quantity = net_voltage_attribute.as_quantity();
    if (net_voltage_quantity.dimension() != UnitDimension::Voltage) {
        return;
    }

    const auto net_voltage = net_voltage_quantity.value();
    for (const auto pin_id : group_pins) {
        const auto &pin = circuit.pin(pin_id);
        const auto &definition_attributes =
            circuit.pin_definition_electrical_attributes(pin.definition());
        if (!definition_attributes.contains(voltage_range_attribute_name)) {
            continue;
        }

        const auto &range_attribute = definition_attributes.get(voltage_range_attribute_name);
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
            report.add(erc_error(erc_diagnostic_codes::PinVoltageRangeViolation,
                                 "Net voltage is outside pin voltage range",
                                 std::vector{EntityRef::net(net_id), EntityRef::pin(pin_id),
                                             EntityRef::pin_def(pin.definition())}));
        }
    }
}

void validate_rule_class_voltage_limit(const Circuit &circuit, NetId net_id,
                                       DiagnosticReport &report) {
    const auto rule_class_id = circuit.rule_class_for_net(net_id);
    if (!rule_class_id.has_value()) {
        return;
    }

    const auto &rule_class = circuit.rule_class(rule_class_id.value());
    if (!rule_class.maximum_net_voltage().has_value()) {
        return;
    }

    const auto voltage_attribute_name = ElectricalAttributeName{"voltage"};
    const auto &net_attributes = circuit.net_electrical_attributes(net_id);
    if (!net_attributes.contains(voltage_attribute_name)) {
        return;
    }

    const auto &net_voltage_attribute = net_attributes.get(voltage_attribute_name);
    if (net_voltage_attribute.kind() != ElectricalAttributeValueKind::Quantity ||
        net_voltage_attribute.as_quantity().dimension() != UnitDimension::Voltage) {
        return;
    }

    const auto net_voltage = std::abs(net_voltage_attribute.as_quantity().value());
    if (net_voltage <= rule_class.maximum_net_voltage()->value()) {
        return;
    }

    report.add(erc_error(erc_diagnostic_codes::NetRuleClassVoltageExceeded,
                         "Net voltage exceeds assigned rule class limit",
                         std::vector{EntityRef::net(net_id)}));
}

void validate_output_driver_conflicts(const Circuit &circuit, NetId net_id,
                                      const std::vector<PinId> &group_pins,
                                      DiagnosticReport &report) {
    auto output_pins = std::vector<PinId>{};
    for (const auto pin_id : group_pins) {
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

        report.add(erc_error(erc_diagnostic_codes::MultipleOutputsOnNet,
                             "Net has multiple output drivers", std::move(entities)));
    }
}

void validate_input_signal_domains(const Circuit &circuit, NetId net_id,
                                   const std::vector<PinId> &group_pins, DiagnosticReport &report) {
    if (group_pins.size() <= 1) {
        return;
    }

    auto input_pins = std::vector<PinId>{};
    auto first_domain = ElectricalSignalDomain::Unspecified;
    auto has_domain = false;
    auto has_mismatched_domain = false;
    auto has_driver = false;
    for (const auto pin_id : group_pins) {
        const auto &pin = circuit.pin(pin_id);
        const auto &definition = circuit.pin_definition(pin.definition());
        if (is_input_pin(definition)) {
            input_pins.push_back(pin_id);
            if (definition.signal_domain() != ElectricalSignalDomain::Unspecified) {
                if (!has_domain) {
                    first_domain = definition.signal_domain();
                    has_domain = true;
                } else if (definition.signal_domain() != first_domain) {
                    has_mismatched_domain = true;
                }
            }
        }
        if (can_drive_signal_net(definition)) {
            has_driver = true;
        }
    }

    if (input_pins.size() > 1 && has_mismatched_domain && !has_driver) {
        auto entities = std::vector<EntityRef>{EntityRef::net(net_id)};
        for (const auto pin_id : input_pins) {
            entities.push_back(EntityRef::pin(pin_id));
        }

        report.add(erc_error(erc_diagnostic_codes::InputSignalDomainMismatch,
                             "Input pins with incompatible signal domains share a net with no "
                             "typed driver",
                             std::move(entities)));
    }
}

void validate_net_shapes(const Circuit &circuit, const NetContinuityView &continuity,
                         DiagnosticReport &report) {
    for (std::size_t index = 0; index < circuit.net_count(); ++index) {
        const auto net_id = NetId{index};
        const auto &net = circuit.net(net_id);
        if (circuit.is_intentional_stub_net(net_id)) {
            continue;
        }

        validate_net_shape(net_id, net, continuity.pins_for_group(circuit, net_id), report);
    }
}

void validate_net_electrical_rules(const Circuit &circuit, const NetContinuityView &continuity,
                                   DiagnosticReport &report) {
    for (std::size_t index = 0; index < circuit.net_count(); ++index) {
        const auto net_id = NetId{index};
        const auto &net = circuit.net(net_id);
        const auto group_pins = continuity.pins_for_group(circuit, net_id);

        validate_power_and_ground_semantics(circuit, net_id, net, group_pins, report);
        validate_selected_part_voltage_ratings(circuit, net_id, net, group_pins, report);
        validate_pin_voltage_ranges(circuit, net_id, net, group_pins, report);
        validate_rule_class_voltage_limit(circuit, net_id, report);
        validate_output_driver_conflicts(circuit, net_id, group_pins, report);
        validate_input_signal_domains(circuit, net_id, group_pins, report);
    }
}

void validate_net_semantics(const Circuit &circuit, const NetContinuityView &continuity,
                            DiagnosticReport &report) {
    for (std::size_t index = 0; index < circuit.net_count(); ++index) {
        const auto net_id = NetId{index};
        const auto &net = circuit.net(net_id);
        const auto group_pins = continuity.pins_for_group(circuit, net_id);

        if (!circuit.is_intentional_stub_net(net_id)) {
            validate_net_shape(net_id, net, group_pins, report);
        }
        validate_power_and_ground_semantics(circuit, net_id, net, group_pins, report);
        validate_selected_part_voltage_ratings(circuit, net_id, net, group_pins, report);
        validate_pin_voltage_ranges(circuit, net_id, net, group_pins, report);
        validate_rule_class_voltage_limit(circuit, net_id, report);
        validate_output_driver_conflicts(circuit, net_id, group_pins, report);
        validate_input_signal_domains(circuit, net_id, group_pins, report);
    }
}

void validate_required_module_ports(const Circuit &circuit, DiagnosticReport &report) {
    for (std::size_t instance_index = 0; instance_index < circuit.module_instance_count();
         ++instance_index) {
        const auto instance_id = ModuleInstanceId{instance_index};
        const auto &instance = circuit.module_instance(instance_id);
        const auto &definition = circuit.module_definition(instance.definition());
        for (const auto port_id : definition.ports()) {
            const auto &port = circuit.port_definition(port_id);
            if (port.required() &&
                !queries::port_binding_for(circuit, instance_id, port_id).has_value()) {
                report.add(erc_error(erc_diagnostic_codes::UnboundRequiredPort,
                                     "Required module port is not bound",
                                     std::vector{EntityRef::module_instance(instance_id),
                                                 EntityRef::module_def(instance.definition()),
                                                 EntityRef::port_def(port_id)}));
            }
        }
    }
}

void validate_physical_part_selection(const Circuit &circuit, DiagnosticReport &report) {
    for (std::size_t index = 0; index < circuit.component_count(); ++index) {
        const auto component_id = ComponentId{index};
        const auto &component = circuit.component(component_id);
        if (!circuit.selected_physical_part(component_id).has_value()) {
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

} // namespace volt::detail

namespace volt {

[[nodiscard]] DiagnosticReport validate_connectivity(const Circuit &circuit) {
    auto report = DiagnosticReport{};
    auto rules = RuleSet<Circuit>{};
    rules
        .add([](const Circuit &rule_circuit, DiagnosticReport &rule_report) {
            detail::validate_pin_connection_requirements(rule_circuit, rule_report);
        })
        .add([](const Circuit &rule_circuit, DiagnosticReport &rule_report) {
            detail::validate_required_module_ports(rule_circuit, rule_report);
        })
        .add([](const Circuit &rule_circuit, DiagnosticReport &rule_report) {
            const auto continuity = detail::NetContinuityView{rule_circuit};
            detail::validate_net_shapes(rule_circuit, continuity, rule_report);
        });
    rules.run(circuit, report);

    return report;
}

[[nodiscard]] DiagnosticReport validate_electrical_rules(const Circuit &circuit) {
    auto report = DiagnosticReport{};
    auto rules = RuleSet<Circuit>{};
    rules.add([](const Circuit &rule_circuit, DiagnosticReport &rule_report) {
        const auto continuity = detail::NetContinuityView{rule_circuit};
        detail::validate_net_electrical_rules(rule_circuit, continuity, rule_report);
    });
    rules.run(circuit, report);

    return report;
}

[[nodiscard]] DiagnosticReport validate_circuit(const Circuit &circuit) {
    auto report = DiagnosticReport{};
    auto rules = RuleSet<Circuit>{};
    rules
        .add([](const Circuit &rule_circuit, DiagnosticReport &rule_report) {
            detail::validate_pin_connection_requirements(rule_circuit, rule_report);
        })
        .add([](const Circuit &rule_circuit, DiagnosticReport &rule_report) {
            detail::validate_required_module_ports(rule_circuit, rule_report);
        })
        .add([](const Circuit &rule_circuit, DiagnosticReport &rule_report) {
            const auto continuity = detail::NetContinuityView{rule_circuit};
            detail::validate_net_semantics(rule_circuit, continuity, rule_report);
        });
    rules.run(circuit, report);

    return report;
}

[[nodiscard]] DiagnosticReport validate_for_pcb(const Circuit &circuit) {
    auto report = validate_circuit(circuit);

    auto rules = RuleSet<Circuit>{};
    rules.add([](const Circuit &rule_circuit, DiagnosticReport &rule_report) {
        detail::validate_physical_part_selection(rule_circuit, rule_report);
    });
    rules.run(circuit, report);

    return report;
}

} // namespace volt
