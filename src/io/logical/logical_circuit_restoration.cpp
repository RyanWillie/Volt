#include "logical_circuit_restoration.hpp"

#include <algorithm>
#include <utility>

#include <volt/core/errors.hpp>

namespace volt::io::detail {

[[nodiscard]] Circuit restore_logical_circuit(LogicalCircuitRestorationPlan plan) {
    auto circuit = Circuit{};

    for (auto &pin : plan.connectivity.pin_definitions) {
        const auto id = circuit.connectivity_.add_pin_definition(std::move(pin.definition));
        circuit.electrical_.restore_pin_definition_attributes(id,
                                                              std::move(pin.electrical_attributes));
    }
    for (auto &definition : plan.connectivity.component_definitions) {
        [[maybe_unused]] const auto id =
            circuit.connectivity_.add_component_definition(std::move(definition.definition));
    }
    for (auto &component : plan.connectivity.components) {
        const auto id = circuit.connectivity_.add_component(std::move(component.instance));
        circuit.electrical_.restore_component_attributes(
            id, std::move(component.electrical_attributes));
    }
    for (auto &pin : plan.connectivity.pins) {
        [[maybe_unused]] const auto id = circuit.connectivity_.add_pin(pin);
    }

    for (auto &restored : plan.nets) {
        const auto id = circuit.connectivity_.add_net(std::move(restored.net));
        if (id != restored.id) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Net restoration order is not deterministic"};
        }
        for (const auto &[name, value] : restored.electrical_attributes.entries()) {
            circuit.electrical_.set_net_attribute(
                id,
                ElectricalAttributeSpec{name, ElectricalAttributeOwner::Net,
                                        ElectricalAttributeKind::DesignInput, value.dimension()},
                value);
        }
    }

    for (auto &net_class : plan.net_classes) {
        [[maybe_unused]] const auto id = circuit.net_classes_.add_net_class(std::move(net_class));
    }
    for (const auto &assignment : plan.net_class_assignments) {
        [[maybe_unused]] const auto changed =
            circuit.net_classes_.assign_net_class(assignment.net, assignment.net_class);
    }

    for (const auto net : plan.intentional_stub_nets) {
        [[maybe_unused]] const auto changed = circuit.intent_.mark_intentional_stub_net(net);
    }
    for (const auto pin : plan.intentional_no_connect_pins) {
        [[maybe_unused]] const auto changed = circuit.intent_.mark_intentional_no_connect_pin(pin);
    }
    for (const auto &intent : plan.assembly_intent) {
        if (intent.dnp.has_value()) {
            circuit.intent_.set_component_dnp(intent.component, intent.dnp.value());
        }
        circuit.intent_.set_component_selection_override(intent.component,
                                                         intent.selection_override);
    }

    for (auto &definition : plan.hierarchy.module_definitions) {
        const auto id = circuit.hierarchy_.add_module_definition(std::move(definition.definition));
        if (id != definition.id) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Module definition restoration order is not deterministic"};
        }
    }
    for (auto &net : plan.hierarchy.template_nets) {
        const auto id = circuit.hierarchy_.add_template_net(net.module, std::move(net.definition));
        if (id != net.id) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Template net restoration order is not deterministic"};
        }
    }
    for (auto &component : plan.hierarchy.components) {
        circuit.require_component_definition(component.component.definition());
        const auto id = circuit.hierarchy_.add_module_component(component.module,
                                                                std::move(component.component));
        if (id != component.id) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Module component restoration order is not deterministic"};
        }
    }
    for (const auto &connection : plan.hierarchy.connections) {
        circuit.require_pin_definition(connection.pin);
        const auto &component = circuit.hierarchy_.module_component_template(connection.component);
        const auto &pins = circuit.component_definition(component.definition()).pins();
        if (std::find(pins.begin(), pins.end(), connection.pin) == pins.end()) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Pin definition does not belong to module component definition"};
        }
        if (!circuit.hierarchy_.connect_module_pin(connection.module, connection.net,
                                                   connection.component, connection.pin)) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Module component pin is already connected"};
        }
    }
    for (auto &port : plan.hierarchy.ports) {
        const auto id =
            circuit.hierarchy_.add_port_definition(port.module, std::move(port.definition));
        if (id != port.id) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Port definition restoration order is not deterministic"};
        }
    }

    for (auto &restored : plan.module_instances) {
        auto &instance = restored.instance;
        const auto id =
            circuit.restore_root_module_instance(instance.definition, std::move(instance.name),
                                                 instance.net_origins, instance.component_origins);
        if (id != restored.id) {
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Module instance restoration order is not deterministic"};
        }
        for (const auto &binding : restored.bindings) {
            [[maybe_unused]] const auto binding_id =
                circuit.bind_port(id, binding.port, binding.parent_net);
        }
    }

    for (auto &selected : plan.selected_physical_parts) {
        const auto definition = circuit.component(selected.component).definition();
        circuit.electrical_.select_physical_part(selected.component,
                                                 std::move(selected.physical_part),
                                                 circuit.component_definition(definition).pins());
        for (const auto &[name, value] : selected.electrical_attributes.entries()) {
            circuit.electrical_.set_selected_part_attribute(
                selected.component,
                ElectricalAttributeSpec{name, ElectricalAttributeOwner::SelectedPart,
                                        ElectricalAttributeKind::DesignInput, value.dimension()},
                value);
        }
    }

    return circuit;
}

} // namespace volt::io::detail
