#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/validation/validation.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/io/logical/logical_circuit_reader.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>

namespace {

[[nodiscard]] volt::PinSpec passive_pin(std::string name, std::string number) {
    return volt::PinSpec{
        .name = std::move(name),
        .number = std::move(number),
        .terminal_kind = volt::ElectricalTerminalKind::Passive,
        .direction = volt::ElectricalDirection::Passive,
        .drive_kind = volt::ElectricalDriveKind::Passive,
    };
}

template <typename Operation>
void check_failure_is_byte_atomic(volt::Circuit &circuit, Operation operation) {
    const auto before = volt::io::write_logical_circuit(circuit);
    CHECK_THROWS(operation());
    CHECK(volt::io::write_logical_circuit(circuit) == before);
}

[[nodiscard]] bool has_diagnostic(const volt::DiagnosticReport &report, std::string_view code) {
    return std::any_of(report.diagnostics().begin(), report.diagnostics().end(),
                       [code](const auto &diagnostic) {
                           return diagnostic.code() == volt::DiagnosticCode{std::string{code}};
                       });
}

} // namespace

TEST_CASE("Circuit defines complete components atomically from typed specs") {
    auto circuit = volt::Circuit{};
    auto supply = passive_pin("VDD", "1");
    supply.terminal_kind = volt::ElectricalTerminalKind::Power;
    supply.direction = volt::ElectricalDirection::Input;
    supply.electrical_attributes.push_back(volt::ElectricalAttributeAssignment{
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage_range"},
            volt::ElectricalAttributeOwner::PinSpec,
            volt::ElectricalAttributeKind::Constraint,
            volt::UnitDimension::Voltage,
        },
        volt::ElectricalAttributeValue{
            volt::QuantityRange::bounded(volt::Quantity{volt::UnitDimension::Voltage, 3.0},
                                         volt::Quantity{volt::UnitDimension::Voltage, 5.5})},
    });

    const auto definition = circuit.define_component(volt::ComponentSpec{
        .name = "Sensor",
        .pins = {std::move(supply), passive_pin("OUT", "2")},
        .properties =
            volt::PropertyMap{{volt::PropertyKey{"category"}, volt::PropertyValue{"sensor"}}},
        .source = volt::DefinitionSource{"volt.sensors", "sensor", "1.0.0"},
        .schematic_symbols = {volt::SchematicSymbolReference{"volt.sensors:sensor"}},
    });

    CHECK(definition == volt::ComponentDefId{0});
    REQUIRE(circuit.pin_definition_count() == 2);
    const auto &stored = circuit.component_definition(definition);
    REQUIRE(stored.pins() == std::vector{volt::PinDefId{0}, volt::PinDefId{1}});
    CHECK(circuit.pin_definition(stored.pins()[0]).name() == "VDD");
    CHECK(circuit.pin_definition(stored.pins()[1]).name() == "OUT");
    CHECK(circuit.pin_definition_electrical_attributes(stored.pins()[0])
              .get(volt::ElectricalAttributeName{"voltage_range"})
              .as_range()
              .maximum() == volt::Quantity{volt::UnitDimension::Voltage, 5.5});
    CHECK(stored.properties().get(volt::PropertyKey{"category"}) == volt::PropertyValue{"sensor"});
    REQUIRE(stored.source().has_value());
    CHECK(stored.source()->name() == "sensor");
    REQUIRE(stored.schematic_symbols().size() == 1);
    CHECK(stored.schematic_symbols()[0].name() == "volt.sensors:sensor");
}

TEST_CASE("Failed complete component definitions leave canonical bytes unchanged") {
    auto circuit = volt::Circuit{};
    static_cast<void>(circuit.define_component(volt::ComponentSpec{
        .name = "Existing",
        .pins = {passive_pin("1", "1")},
    }));

    check_failure_is_byte_atomic(circuit, [&] {
        static_cast<void>(circuit.define_component(volt::ComponentSpec{
            .name = "Broken pin",
            .pins = {passive_pin("1", "1"), passive_pin("", "2")},
        }));
    });

    check_failure_is_byte_atomic(circuit, [&] {
        static_cast<void>(circuit.define_component(volt::ComponentSpec{
            .name = "Broken symbols",
            .pins = {passive_pin("1", "1")},
            .schematic_symbols =
                {
                    volt::SchematicSymbolReference{"first", "default"},
                    volt::SchematicSymbolReference{"second", "default"},
                },
        }));
    });

    auto bad_attribute_pin = passive_pin("1", "1");
    bad_attribute_pin.electrical_attributes.push_back(volt::ElectricalAttributeAssignment{
        volt::ElectricalAttributeSpec{
            volt::ElectricalAttributeName{"voltage_range"},
            volt::ElectricalAttributeOwner::PinSpec,
            volt::ElectricalAttributeKind::Constraint,
            volt::UnitDimension::Voltage,
        },
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Current, 1.0}},
    });
    check_failure_is_byte_atomic(circuit, [&] {
        static_cast<void>(circuit.define_component(volt::ComponentSpec{
            .name = "Broken attribute",
            .pins = {bad_attribute_pin},
        }));
    });
}

TEST_CASE("Legacy electrical facade rejects committed pin-definition mutation") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(volt::ComponentSpec{
        .name = "Input",
        .pins = {passive_pin("IN", "1")},
    });
    const auto pin = circuit.component_definition(definition).pins().front();
    const auto before = volt::io::write_logical_circuit(circuit);

    // Raw committed PinDef mutation exists only on the transitional facade until #266.
    try {
        circuit.electrical().set_pin_definition_electrical_attribute(
            pin,
            volt::ElectricalAttributeSpec{
                volt::ElectricalAttributeName{"voltage_range"},
                volt::ElectricalAttributeOwner::PinSpec,
                volt::ElectricalAttributeKind::Constraint,
                volt::UnitDimension::Voltage,
            },
            volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, 5.0}});
        FAIL("Committed pin electrical semantics must not be mutable");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::InvalidState);
        CHECK(std::string{error.what()} ==
              "Committed pin definition electrical attributes are immutable");
    }
    CHECK(volt::io::write_logical_circuit(circuit) == before);
}

TEST_CASE("Circuit component instances materialize every definition pin atomically") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(volt::ComponentSpec{
        .name = "Resistor",
        .pins = {passive_pin("1", "1"), passive_pin("2", "2")},
    });

    const auto component = circuit.instantiate_component(
        definition, volt::ComponentInstanceSpec{
                        .reference = volt::ReferenceDesignator{"R1"},
                        .properties = volt::PropertyMap{{volt::PropertyKey{"value"},
                                                         volt::PropertyValue{"1 kohm"}}},
                    });

    CHECK(component == volt::ComponentId{0});
    const auto pins = volt::queries::pins_for(circuit, component);
    REQUIRE(pins == std::vector{volt::PinId{0}, volt::PinId{1}});
    CHECK(circuit.pin(pins[0]).definition() == volt::PinDefId{0});
    CHECK(circuit.pin(pins[1]).definition() == volt::PinDefId{1});

    check_failure_is_byte_atomic(circuit, [&] {
        static_cast<void>(circuit.instantiate_component(
            definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}}));
    });
    check_failure_is_byte_atomic(circuit, [&] {
        static_cast<void>(circuit.instantiate_component(
            volt::ComponentDefId{99},
            volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R2"}}));
    });
}

TEST_CASE("Circuit adds canonical nets from typed specs atomically") {
    auto circuit = volt::Circuit{};

    const auto ground =
        circuit.add_net(volt::NetSpec{.name = volt::NetName{"GND"}, .kind = volt::NetKind::Ground});

    CHECK(ground == volt::NetId{0});
    CHECK(circuit.net(ground).name() == volt::NetName{"GND"});
    CHECK(circuit.net(ground).kind() == volt::NetKind::Ground);
    check_failure_is_byte_atomic(circuit, [&] {
        static_cast<void>(circuit.add_net(
            volt::NetSpec{.name = volt::NetName{"GND"}, .kind = volt::NetKind::Signal}));
    });
}

TEST_CASE("Circuit defines complete modules atomically from typed specs") {
    auto circuit = volt::Circuit{};
    const auto resistor = circuit.define_component(volt::ComponentSpec{
        .name = "Resistor",
        .pins = {passive_pin("1", "1"), passive_pin("2", "2")},
    });

    const auto module = circuit.define_module(volt::ModuleSpec{
        .name = volt::ModuleName{"Divider"},
        .template_nets =
            {
                volt::TemplateNetDefinition{volt::NetName{"IN"}, volt::NetKind::Signal},
                volt::TemplateNetDefinition{volt::NetName{"OUT"}, volt::NetKind::Signal},
            },
        .components =
            {
                volt::ModuleComponentTemplate{resistor, volt::ReferenceDesignator{"R1"}},
            },
        .connections =
            {
                volt::ModulePinConnectionSpec{volt::NetName{"IN"}, volt::ReferenceDesignator{"R1"},
                                              volt::PinDefId{0}},
                volt::ModulePinConnectionSpec{volt::NetName{"OUT"}, volt::ReferenceDesignator{"R1"},
                                              volt::PinDefId{1}},
            },
        .ports =
            {
                volt::ModulePortSpec{volt::PortName{"IN"}, volt::NetName{"IN"},
                                     volt::PortRole::Input, true},
            },
    });

    CHECK(module == volt::ModuleDefId{0});
    REQUIRE(circuit.module_definition(module).template_nets() ==
            std::vector{volt::TemplateNetDefId{0}, volt::TemplateNetDefId{1}});
    REQUIRE(circuit.module_definition(module).components() ==
            std::vector{volt::ModuleComponentId{0}});
    REQUIRE(circuit.module_definition(module).ports() == std::vector{volt::PortDefId{0}});
    CHECK(volt::queries::template_net_for(circuit, module, volt::ModuleComponentId{0},
                                          volt::PinDefId{0}) == volt::TemplateNetDefId{0});
    CHECK(volt::queries::template_net_for(circuit, module, volt::ModuleComponentId{0},
                                          volt::PinDefId{1}) == volt::TemplateNetDefId{1});

    const auto instance =
        circuit.instantiate_root_module(module, volt::ModuleInstanceName{"DIV_A"});
    const auto concrete =
        volt::queries::concrete_component_for(circuit, instance, volt::ModuleComponentId{0});
    REQUIRE(concrete.has_value());
    CHECK(volt::queries::pins_for(circuit, concrete.value()).size() == 2);

    const auto serialized = volt::io::write_logical_circuit(circuit);
    CHECK(volt::io::write_logical_circuit(volt::io::read_logical_circuit_text(serialized)) ==
          serialized);
    check_failure_is_byte_atomic(circuit, [&] {
        static_cast<void>(
            circuit.instantiate_root_module(module, volt::ModuleInstanceName{"DIV_A"}));
    });
}

TEST_CASE("Failed complete module definitions leave canonical bytes unchanged") {
    auto circuit = volt::Circuit{};
    const auto resistor = circuit.define_component(volt::ComponentSpec{
        .name = "Resistor",
        .pins = {passive_pin("1", "1"), passive_pin("2", "2")},
    });
    const auto connector = circuit.define_component(volt::ComponentSpec{
        .name = "Connector",
        .pins = {passive_pin("1", "1")},
    });

    check_failure_is_byte_atomic(circuit, [&] {
        static_cast<void>(circuit.define_module(volt::ModuleSpec{
            .name = volt::ModuleName{"Duplicate nets"},
            .template_nets =
                {volt::TemplateNetDefinition{volt::NetName{"A"}, volt::NetKind::Signal},
                 volt::TemplateNetDefinition{volt::NetName{"A"}, volt::NetKind::Power}},
        }));
    });

    check_failure_is_byte_atomic(circuit, [&] {
        static_cast<void>(circuit.define_module(volt::ModuleSpec{
            .name = volt::ModuleName{"Missing definition"},
            .components = {volt::ModuleComponentTemplate{volt::ComponentDefId{99},
                                                         volt::ReferenceDesignator{"U1"}}},
        }));
    });

    check_failure_is_byte_atomic(circuit, [&] {
        static_cast<void>(circuit.define_module(volt::ModuleSpec{
            .name = volt::ModuleName{"Duplicate references"},
            .components = {volt::ModuleComponentTemplate{resistor, volt::ReferenceDesignator{"R1"}},
                           volt::ModuleComponentTemplate{connector,
                                                         volt::ReferenceDesignator{"R1"}}},
        }));
    });

    check_failure_is_byte_atomic(circuit, [&] {
        static_cast<void>(circuit.define_module(volt::ModuleSpec{
            .name = volt::ModuleName{"Foreign pin"},
            .template_nets = {volt::TemplateNetDefinition{volt::NetName{"A"},
                                                          volt::NetKind::Signal}},
            .components = {volt::ModuleComponentTemplate{resistor,
                                                         volt::ReferenceDesignator{"R1"}}},
            .connections = {volt::ModulePinConnectionSpec{
                volt::NetName{"A"}, volt::ReferenceDesignator{"R1"}, volt::PinDefId{2}}},
        }));
    });

    check_failure_is_byte_atomic(circuit, [&] {
        static_cast<void>(circuit.define_module(volt::ModuleSpec{
            .name = volt::ModuleName{"Broken port"},
            .template_nets = {volt::TemplateNetDefinition{volt::NetName{"IN"},
                                                          volt::NetKind::Signal}},
            .components = {volt::ModuleComponentTemplate{resistor,
                                                         volt::ReferenceDesignator{"R1"}}},
            .ports = {volt::ModulePortSpec{volt::PortName{"IN"}, volt::NetName{"MISSING"},
                                           volt::PortRole::Input, true}},
        }));
    });

    check_failure_is_byte_atomic(circuit, [&] {
        static_cast<void>(circuit.define_module(volt::ModuleSpec{
            .name = volt::ModuleName{"Broken connection"},
            .template_nets =
                {
                    volt::TemplateNetDefinition{volt::NetName{"A"}, volt::NetKind::Signal},
                    volt::TemplateNetDefinition{volt::NetName{"B"}, volt::NetKind::Signal},
                },
            .components = {volt::ModuleComponentTemplate{resistor,
                                                         volt::ReferenceDesignator{"R1"}}},
            .connections =
                {
                    volt::ModulePinConnectionSpec{
                        volt::NetName{"A"}, volt::ReferenceDesignator{"R1"}, volt::PinDefId{0}},
                    volt::ModulePinConnectionSpec{
                        volt::NetName{"B"}, volt::ReferenceDesignator{"R1"}, volt::PinDefId{0}},
                },
        }));
    });
}

TEST_CASE("Complete construction preserves the structural and diagnostic boundary") {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(volt::ComponentSpec{
        .name = "Required input",
        .pins = {volt::PinSpec{
            .name = "IN",
            .number = "1",
            .requirement = volt::ConnectionRequirement::Required,
            .terminal_kind = volt::ElectricalTerminalKind::Signal,
            .direction = volt::ElectricalDirection::Input,
        }},
    });
    static_cast<void>(circuit.instantiate_component(
        definition, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}}));
    static_cast<void>(circuit.add_net(
        volt::NetSpec{.name = volt::NetName{"EMPTY"}, .kind = volt::NetKind::Signal}));

    const auto report = volt::validate_circuit(circuit);

    CHECK(has_diagnostic(report, volt::erc_diagnostic_codes::UnconnectedRequiredPin));
    CHECK(has_diagnostic(report, volt::erc_diagnostic_codes::EmptyNet));
}
