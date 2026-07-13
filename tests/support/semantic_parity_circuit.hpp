#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>

namespace volt::test {

[[nodiscard]] inline Circuit build_semantic_parity_circuit() {
    auto circuit = Circuit{};

    const auto pin_voltage_range = ElectricalAttributeSpec{
        ElectricalAttributeName{"voltage_range"}, ElectricalAttributeOwner::PinSpec,
        ElectricalAttributeKind::Constraint, UnitDimension::Voltage};
    const auto controller =
        circuit.define_component(
            ComponentSpec{
                .name = "Controller",
                .pins = {PinSpec{
                             .name = "IO",
                             .number = "1",
                             .requirement = ConnectionRequirement::Required,
                             .terminal_kind = ElectricalTerminalKind::Signal,
                             .direction = ElectricalDirection::Bidirectional,
                             .signal_domain = ElectricalSignalDomain::Digital,
                             .drive_kind = ElectricalDriveKind::HighImpedance,
                             .polarity = ElectricalPolarity::ActiveLow,
                             .electrical_attributes = {ElectricalAttributeAssignment{
                                 pin_voltage_range, ElectricalAttributeValue{QuantityRange::bounded(
                                                        Quantity{UnitDimension::Voltage, 0.0},
                                                        Quantity{UnitDimension::Voltage, 5.5})}}},
                         },
                         PinSpec{
                             .name = "NC",
                             .number = "2",
                             .requirement = ConnectionRequirement::MustNotConnect,
                             .terminal_kind = ElectricalTerminalKind::NoConnect,
                             .direction = ElectricalDirection::Passive,
                             .drive_kind = ElectricalDriveKind::Passive,
                         }},
                .properties =
                    PropertyMap{
                        {PropertyKey{"category"}, PropertyValue{"control"}},
                        {PropertyKey{"gain"}, PropertyValue{1.5}},
                        {PropertyKey{"production"}, PropertyValue{true}},
                        {PropertyKey{"revision"}, PropertyValue{std::int64_t{2}}},
                    },
                .source = DefinitionSource{"volt.logic", "controller_2pin", "1.0.0"},
                .schematic_symbols = {SchematicSymbolReference{"volt.logic:controller", "default"}},
            });
    const auto &definition_pins = circuit.get(controller).pins();

    const auto component = circuit.instantiate_component(
        controller, ComponentInstanceSpec{
                        .reference = ReferenceDesignator{"U1"},
                        .properties = PropertyMap{{PropertyKey{"value"}, PropertyValue{"CTRL-1"}}},
                    });
    circuit.update(
        component,
        SetComponentElectricalAttribute{
            ElectricalAttributeSpec{ElectricalAttributeName{"frequency"},
                                    ElectricalAttributeOwner::ComponentInstance,
                                    ElectricalAttributeKind::DesignInput, UnitDimension::Frequency},
            ElectricalAttributeValue{Quantity{UnitDimension::Frequency, 1'000'000.0}}});
    circuit.update(component, SetComponentElectricalAttribute{
                                  ElectricalAttributeSpec{
                                      ElectricalAttributeName{"tolerance"},
                                      ElectricalAttributeOwner::ComponentInstance,
                                      ElectricalAttributeKind::Constraint, UnitDimension::Ratio},
                                  ElectricalAttributeValue{Tolerance::percent(0.01, 0.02)}});
    circuit.update(component, SelectPhysicalPart{PhysicalPart{
                                  ManufacturerPart{"Acme", "CTRL-1"},
                                  PackageRef{"SOIC-2"},
                                  FootprintRef{"logic", "SOIC-2_3.9x4.9mm"},
                                  std::vector{PinPadMapping{definition_pins[0], "1"},
                                              PinPadMapping{definition_pins[1], "2"}},
                                  PropertyMap{{PropertyKey{"lifecycle"}, PropertyValue{"active"}}},
                                  PartModel3D{"step", "controller.step", {0.1, -0.2, 0.3}, 90.0},
                                  std::vector<std::string>{"CTRL-1A", "CTRL-1B"},
                              }});
    circuit.update(component, SetSelectedPartElectricalAttribute{
                                  ElectricalAttributeSpec{ElectricalAttributeName{"voltage_rating"},
                                                          ElectricalAttributeOwner::SelectedPart,
                                                          ElectricalAttributeKind::Constraint,
                                                          UnitDimension::Voltage},
                                  ElectricalAttributeValue{Quantity{UnitDimension::Voltage, 5.5}}});
    circuit.update(component, SetAssemblyIntent{.dnp = true, .selection_override = true});

    const auto bus = circuit.add_net(NetSpec{.name = NetName{"BUS"}, .kind = NetKind::Signal});
    const auto stub =
        circuit.add_net(NetSpec{.name = NetName{"TEST_STUB"}, .kind = NetKind::Signal});
    circuit.update(bus, SetNetElectricalAttribute{
                            ElectricalAttributeSpec{
                                ElectricalAttributeName{"voltage"}, ElectricalAttributeOwner::Net,
                                ElectricalAttributeKind::DesignInput, UnitDimension::Voltage},
                            ElectricalAttributeValue{Quantity{UnitDimension::Voltage, 3.3}}});
    circuit.update(stub, MarkIntentionalStub{});

    auto power_bus = NetClass{NetClassName{"PowerBus"}};
    power_bus.set_maximum_net_voltage(Quantity{UnitDimension::Voltage, 5.0});
    power_bus.set_copper_clearance_mm(0.25);
    power_bus.derive_copper_clearance(ipc2221_external_voltage_clearance_mm(5.0));
    power_bus.set_track_width_mm(0.4);
    power_bus.derive_track_width(
        ipc2221_trace_width_from_current_mm(1.0, 10.0, 1.0, NetClassTraceEnvironment::External));
    power_bus.set_via_size_mm(0.3, 0.6);
    power_bus.set_layer_scope(NetClassLayerScope::OuterOnly);
    power_bus.set_priority(20);
    power_bus.set_default_for_net_kind(NetKind::Power);
    const auto power_bus_id =
        circuit.define_net_class(NetClassSpec{.net_class = std::move(power_bus)});

    auto named_layers = NetClass{NetClassName{"NamedLayers"}};
    named_layers.set_allowed_layer_names({"In1.Cu", "In2.Cu"});
    [[maybe_unused]] const auto named_layers_id =
        circuit.define_net_class(NetClassSpec{.net_class = std::move(named_layers)});
    circuit.update(bus, AssignNetClass{power_bus_id});

    const auto module = circuit.define_module(ModuleSpec{
        .name = ModuleName{"Channel"},
        .template_nets = {TemplateNetDefinition{NetName{"IO"}, NetKind::Signal}},
        .components = {ModuleComponentTemplate{
            controller, ReferenceDesignator{"U1"},
            PropertyMap{{PropertyKey{"mode"}, PropertyValue{"module"}}}}},
        .connections = {ModulePinConnectionSpec{NetName{"IO"}, ReferenceDesignator{"U1"},
                                                definition_pins[0]}},
        .ports = {ModulePortSpec{PortName{"IO"}, NetName{"IO"}, PortRole::Bidirectional, true}},
    });
    const auto module_instance = circuit.instantiate_root_module(module, ModuleInstanceName{"CH1"});
    const auto port = circuit.get(module).ports().front();
    [[maybe_unused]] const auto binding = circuit.bind_port(module_instance, port, bus);

    const auto component_pins = queries::pins_for(circuit, component);
    [[maybe_unused]] const auto connected = circuit.connect(bus, component_pins[0]);
    circuit.mark_no_connect(component_pins[1]);

    return circuit;
}

} // namespace volt::test
