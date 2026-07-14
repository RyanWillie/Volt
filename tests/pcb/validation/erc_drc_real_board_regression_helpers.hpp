#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/definitions.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/constraints/net_classes.hpp>
#include <volt/circuit/validation/validation.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints/footprints.hpp>

namespace {

struct ExpectedDiagnostic {
    std::string code;
    volt::Severity severity;
    volt::DiagnosticCategory category;
};

[[nodiscard, maybe_unused]] volt::ElectricalAttributeSpec net_voltage_spec() {
    return volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage"},
        volt::ElectricalAttributeOwner::Net,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };
}

[[nodiscard, maybe_unused]] volt::ElectricalAttributeSpec pin_voltage_range_spec() {
    return volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_range"},
        volt::ElectricalAttributeOwner::PinSpec,
        volt::ElectricalAttributeKind::Constraint,
        volt::UnitDimension::Voltage,
    };
}

[[nodiscard, maybe_unused]] volt::ElectricalAttributeSpec selected_part_voltage_rating_spec() {
    return volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_rating"},
        volt::ElectricalAttributeOwner::SelectedPart,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };
}

[[maybe_unused]] void set_net_voltage(volt::Circuit &circuit, volt::NetId net, double voltage) {
    circuit.update(net, volt::SetNetElectricalAttribute{
                            net_voltage_spec(), volt::ElectricalAttributeValue{volt::Quantity{
                                                    volt::UnitDimension::Voltage, voltage}}});
}

[[nodiscard]] volt::ElectricalAttributeAssignment pin_voltage_range(double low, double high) {
    return volt::ElectricalAttributeAssignment{
        pin_voltage_range_spec(), volt::ElectricalAttributeValue{volt::QuantityRange::bounded(
                                      volt::Quantity{volt::UnitDimension::Voltage, low},
                                      volt::Quantity{volt::UnitDimension::Voltage, high})}};
}

[[maybe_unused]] void set_selected_part_voltage_rating(volt::Circuit &circuit,
                                                       volt::ComponentId component,
                                                       double voltage) {
    circuit.update(component, volt::SetSelectedPartElectricalAttribute{
                                  selected_part_voltage_rating_spec(),
                                  volt::ElectricalAttributeValue{
                                      volt::Quantity{volt::UnitDimension::Voltage, voltage}}});
}

[[nodiscard, maybe_unused]] volt::FootprintPolygon rectangle_polygon(double half_width,
                                                                     double half_height) {
    return volt::FootprintPolygon{std::vector{
        volt::FootprintPoint{-half_width, -half_height},
        volt::FootprintPoint{half_width, -half_height},
        volt::FootprintPoint{half_width, half_height},
        volt::FootprintPoint{-half_width, half_height},
    }};
}

[[nodiscard, maybe_unused]] volt::FootprintDefinition real_header_footprint() {
    return volt::FootprintDefinition{
        volt::FootprintRef{"regression", "HDR_1x4_PWR"},
        std::vector{
            volt::FootprintPad::through_hole(
                "1", volt::FootprintPadShape::Circle, volt::FootprintPoint{0.0, -4.5},
                volt::FootprintSize{1.2, 1.2}, volt::FootprintLayerSet::through_hole(),
                volt::FootprintDrill{0.6, volt::FootprintPadPlating::Plated}),
            volt::FootprintPad::through_hole(
                "2", volt::FootprintPadShape::Circle, volt::FootprintPoint{0.0, -1.5},
                volt::FootprintSize{1.2, 1.2}, volt::FootprintLayerSet::through_hole(),
                volt::FootprintDrill{0.6, volt::FootprintPadPlating::Plated}),
            volt::FootprintPad::through_hole(
                "3", volt::FootprintPadShape::Circle, volt::FootprintPoint{0.0, 1.5},
                volt::FootprintSize{1.2, 1.2}, volt::FootprintLayerSet::through_hole(),
                volt::FootprintDrill{0.6, volt::FootprintPadPlating::Plated}),
            volt::FootprintPad::through_hole(
                "4", volt::FootprintPadShape::Circle, volt::FootprintPoint{0.0, 4.5},
                volt::FootprintSize{1.2, 1.2}, volt::FootprintLayerSet::through_hole(),
                volt::FootprintDrill{0.6, volt::FootprintPadPlating::Plated}),
        },
        rectangle_polygon(1.3, 5.8),
        rectangle_polygon(1.0, 5.3),
    };
}

[[nodiscard, maybe_unused]] volt::FootprintDefinition regulator_footprint() {
    return volt::FootprintDefinition{
        volt::FootprintRef{"regression", "SOT89_REG"},
        std::vector{
            volt::FootprintPad::surface_mount(
                "1", volt::FootprintPadShape::RoundedRectangle, volt::FootprintPoint{0.0, 0.0},
                volt::FootprintSize{0.8, 0.8}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "2", volt::FootprintPadShape::RoundedRectangle, volt::FootprintPoint{0.0, -3.0},
                volt::FootprintSize{0.8, 0.8}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "3", volt::FootprintPadShape::RoundedRectangle, volt::FootprintPoint{0.0, 3.0},
                volt::FootprintSize{0.8, 0.8}, volt::FootprintLayerSet::front_smd()),
        },
        rectangle_polygon(1.4, 4.4),
        rectangle_polygon(1.0, 3.9),
    };
}

[[nodiscard, maybe_unused]] volt::FootprintDefinition mcu_footprint() {
    return volt::FootprintDefinition{
        volt::FootprintRef{"regression", "QFN5_MCU"},
        std::vector{
            volt::FootprintPad::surface_mount(
                "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{-2.0, -3.0},
                volt::FootprintSize{0.8, 0.8}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "2", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{-2.0, 3.0},
                volt::FootprintSize{0.8, 0.8}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "3", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{4.0, 3.0},
                volt::FootprintSize{0.8, 0.8}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "4", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{4.0, 6.0},
                volt::FootprintSize{0.8, 0.8}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "5", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{4.0, -3.0},
                volt::FootprintSize{0.8, 0.8}, volt::FootprintLayerSet::front_smd()),
        },
        volt::FootprintPolygon{std::vector{
            volt::FootprintPoint{-3.0, -4.0},
            volt::FootprintPoint{5.0, -4.0},
            volt::FootprintPoint{5.0, 7.0},
            volt::FootprintPoint{-3.0, 7.0},
        }},
        volt::FootprintPolygon{std::vector{
            volt::FootprintPoint{-2.5, -3.5},
            volt::FootprintPoint{4.5, -3.5},
            volt::FootprintPoint{4.5, 6.5},
            volt::FootprintPoint{-2.5, 6.5},
        }},
    };
}

[[nodiscard, maybe_unused]] volt::FootprintDefinition
two_pad_smd_footprint(volt::FootprintRef ref, double courtyard_half_width) {
    return volt::FootprintDefinition{
        std::move(ref),
        std::vector{
            volt::FootprintPad::surface_mount(
                "1", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{-1.0, 0.0},
                volt::FootprintSize{0.8, 0.8}, volt::FootprintLayerSet::front_smd()),
            volt::FootprintPad::surface_mount(
                "2", volt::FootprintPadShape::Rectangle, volt::FootprintPoint{1.0, 0.0},
                volt::FootprintSize{0.8, 0.8}, volt::FootprintLayerSet::front_smd()),
        },
        rectangle_polygon(courtyard_half_width, 0.9),
        rectangle_polygon(courtyard_half_width - 0.25, 0.6),
    };
}

[[nodiscard, maybe_unused]] volt::FootprintLibrary real_board_library() {
    auto library = volt::FootprintLibrary{};
    library.add(real_header_footprint());
    library.add(regulator_footprint());
    library.add(mcu_footprint());
    library.add(two_pad_smd_footprint(volt::FootprintRef{"regression", "R_0603_REAL"}, 1.55));
    library.add(two_pad_smd_footprint(volt::FootprintRef{"regression", "LED_0603_REAL"}, 1.55));
    return library;
}

struct RealBoardFixture {
    volt::Circuit circuit;

    volt::PinDefId header_gnd_pin;
    volt::PinDefId header_vbus_pin;
    volt::PinDefId header_vdd_pin;
    volt::PinDefId header_reset_pin;
    volt::PinDefId regulator_vin_pin;
    volt::PinDefId regulator_gnd_pin;
    volt::PinDefId regulator_vout_pin;
    volt::PinDefId mcu_gnd_pin;
    volt::PinDefId mcu_vdd_pin;
    volt::PinDefId mcu_gpio_pin;
    volt::PinDefId mcu_reset_pin;
    volt::PinDefId mcu_boot_pin;
    volt::PinDefId resistor_a_pin;
    volt::PinDefId resistor_b_pin;
    volt::PinDefId led_a_pin;
    volt::PinDefId led_k_pin;

    volt::ComponentId header;
    volt::ComponentId regulator;
    volt::ComponentId mcu;
    volt::ComponentId resistor;
    volt::ComponentId led;

    volt::PinId header_gnd;
    volt::PinId header_vbus;
    volt::PinId header_vdd;
    volt::PinId header_reset;
    volt::PinId regulator_vin;
    volt::PinId regulator_gnd;
    volt::PinId regulator_vout;
    volt::PinId mcu_gnd;
    volt::PinId mcu_vdd;
    volt::PinId mcu_gpio;
    volt::PinId mcu_reset;
    volt::PinId mcu_boot;
    volt::PinId resistor_a;
    volt::PinId resistor_b;
    volt::PinId led_a;
    volt::PinId led_k;

    volt::NetId ground;
    volt::NetId vbus;
    volt::NetId vdd;
    volt::NetId led_drive;
    volt::NetId led_anode;
    volt::NetId reset;
};

[[maybe_unused]] void select_part(volt::Circuit &circuit, volt::ComponentId component,
                                  std::string_view mpn, volt::PackageRef package,
                                  volt::FootprintRef footprint,
                                  std::vector<volt::PinPadMapping> mappings) {
    circuit.update(component, volt::SelectPhysicalPart{volt::PhysicalPart{
                                  volt::ManufacturerPart{"Volt Regression", std::string{mpn}},
                                  std::move(package), std::move(footprint), std::move(mappings)}});
}

struct AddedPin {
    volt::PinDefId definition;
    volt::ComponentId component;
    volt::PinId pin;
};

[[nodiscard, maybe_unused]] AddedPin add_single_pin_component(
    volt::Circuit &circuit, std::string reference, std::string pin_name,
    volt::ConnectionRequirement requirement, volt::ElectricalTerminalKind terminal,
    volt::ElectricalDirection direction,
    volt::ElectricalSignalDomain domain = volt::ElectricalSignalDomain::Unspecified,
    volt::ElectricalDriveKind drive = volt::ElectricalDriveKind::Unspecified) {
    const auto definition = circuit.define_component(volt::ComponentSpec{
        .name = "RealBoardEdgeCaseOnePin",
        .pins = {volt::PinSpec{.name = pin_name,
                               .number = "1",
                               .requirement = requirement,
                               .terminal_kind = terminal,
                               .direction = direction,
                               .signal_domain = domain,
                               .drive_kind = drive}},
    });
    const auto pin_definition = circuit.get(definition).pins()[0];
    const auto component = circuit.instantiate_component(
        definition,
        volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{std::move(reference)}});
    return AddedPin{pin_definition, component,
                    volt::queries::pin_by_name(circuit, component, pin_name).value()};
}

[[maybe_unused]] void assign_net_class(volt::Circuit &circuit, volt::NetId net,
                                       volt::NetClass net_class) {
    const auto id = circuit.define_net_class(volt::NetClassSpec{.net_class = std::move(net_class)});
    circuit.update(net, volt::AssignNetClass{id});
}

[[nodiscard, maybe_unused]] RealBoardFixture make_real_board_fixture(bool select_led_part = true) {
    auto circuit = volt::Circuit{};

    const auto pin = [](std::string name, std::string number, volt::ElectricalTerminalKind terminal,
                        volt::ElectricalDirection direction,
                        volt::ElectricalSignalDomain domain =
                            volt::ElectricalSignalDomain::Unspecified,
                        volt::ElectricalDriveKind drive = volt::ElectricalDriveKind::Unspecified) {
        return volt::PinSpec{.name = std::move(name),
                             .number = std::move(number),
                             .terminal_kind = terminal,
                             .direction = direction,
                             .signal_domain = domain,
                             .drive_kind = drive};
    };

    auto regulator_vin_spec =
        pin("VIN", "1", volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input);
    regulator_vin_spec.electrical_attributes.push_back(pin_voltage_range(4.5, 5.5));
    auto mcu_vdd_spec =
        pin("VDD", "2", volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input);
    mcu_vdd_spec.electrical_attributes.push_back(pin_voltage_range(1.8, 3.6));

    const auto header_def = circuit.define_component(volt::ComponentSpec{
        .name = "PowerAndDebugHeader",
        .pins =
            {
                pin("GND", "1", volt::ElectricalTerminalKind::Ground,
                    volt::ElectricalDirection::Passive),
                pin("VBUS", "2", volt::ElectricalTerminalKind::Power,
                    volt::ElectricalDirection::Output),
                pin("3V3", "3", volt::ElectricalTerminalKind::Passive,
                    volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
                    volt::ElectricalDriveKind::Passive),
                pin("RESET", "4", volt::ElectricalTerminalKind::Signal,
                    volt::ElectricalDirection::Input, volt::ElectricalSignalDomain::Digital),
            },
    });
    const auto regulator_def = circuit.define_component(volt::ComponentSpec{
        .name = "LDO",
        .pins = {std::move(regulator_vin_spec),
                 pin("GND", "2", volt::ElectricalTerminalKind::Ground,
                     volt::ElectricalDirection::Passive),
                 pin("VOUT", "3", volt::ElectricalTerminalKind::Power,
                     volt::ElectricalDirection::Output)},
    });
    const auto mcu_def = circuit.define_component(volt::ComponentSpec{
        .name = "MCU",
        .pins =
            {
                pin("VSS", "1", volt::ElectricalTerminalKind::Ground,
                    volt::ElectricalDirection::Passive),
                std::move(mcu_vdd_spec),
                pin("PA5", "3", volt::ElectricalTerminalKind::Signal,
                    volt::ElectricalDirection::Output, volt::ElectricalSignalDomain::Digital),
                pin("NRST", "4", volt::ElectricalTerminalKind::Signal,
                    volt::ElectricalDirection::Input, volt::ElectricalSignalDomain::Digital),
                pin("BOOT0", "5", volt::ElectricalTerminalKind::Signal,
                    volt::ElectricalDirection::Input, volt::ElectricalSignalDomain::Digital),
            },
    });
    const auto resistor_def = circuit.define_component(volt::ComponentSpec{
        .name = "LedResistor",
        .pins = {pin("A", "1", volt::ElectricalTerminalKind::Passive,
                     volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
                     volt::ElectricalDriveKind::Passive),
                 pin("B", "2", volt::ElectricalTerminalKind::Passive,
                     volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
                     volt::ElectricalDriveKind::Passive)},
    });
    const auto led_def = circuit.define_component(volt::ComponentSpec{
        .name = "StatusLed",
        .pins = {pin("A", "1", volt::ElectricalTerminalKind::Passive,
                     volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
                     volt::ElectricalDriveKind::Passive),
                 pin("K", "2", volt::ElectricalTerminalKind::Passive,
                     volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
                     volt::ElectricalDriveKind::Passive)},
    });
    const auto &header_pins = circuit.get(header_def).pins();
    const auto header_gnd_pin = header_pins[0];
    const auto header_vbus_pin = header_pins[1];
    const auto header_vdd_pin = header_pins[2];
    const auto header_reset_pin = header_pins[3];
    const auto &regulator_pins = circuit.get(regulator_def).pins();
    const auto regulator_vin_pin = regulator_pins[0];
    const auto regulator_gnd_pin = regulator_pins[1];
    const auto regulator_vout_pin = regulator_pins[2];
    const auto &mcu_pins = circuit.get(mcu_def).pins();
    const auto mcu_gnd_pin = mcu_pins[0];
    const auto mcu_vdd_pin = mcu_pins[1];
    const auto mcu_gpio_pin = mcu_pins[2];
    const auto mcu_reset_pin = mcu_pins[3];
    const auto mcu_boot_pin = mcu_pins[4];
    const auto &resistor_pins = circuit.get(resistor_def).pins();
    const auto resistor_a_pin = resistor_pins[0];
    const auto resistor_b_pin = resistor_pins[1];
    const auto &led_pins = circuit.get(led_def).pins();
    const auto led_a_pin = led_pins[0];
    const auto led_k_pin = led_pins[1];

    const auto header = circuit.instantiate_component(
        header_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"J1"}});
    const auto regulator = circuit.instantiate_component(
        regulator_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U1"}});
    const auto mcu = circuit.instantiate_component(
        mcu_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"U2"}});
    const auto resistor = circuit.instantiate_component(
        resistor_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"R1"}});
    const auto led = circuit.instantiate_component(
        led_def, volt::ComponentInstanceSpec{.reference = volt::ReferenceDesignator{"D1"}});

    const auto ground = circuit.add_net(volt::NetSpec{volt::NetName{"GND"}, volt::NetKind::Ground});
    const auto vbus = circuit.add_net(volt::NetSpec{volt::NetName{"VBUS"}, volt::NetKind::Power});
    const auto vdd = circuit.add_net(volt::NetSpec{volt::NetName{"+3V3"}, volt::NetKind::Power});
    const auto led_drive =
        circuit.add_net(volt::NetSpec{volt::NetName{"LED_DRV"}, volt::NetKind::Signal});
    const auto led_anode =
        circuit.add_net(volt::NetSpec{volt::NetName{"LED_A"}, volt::NetKind::Signal});
    const auto reset =
        circuit.add_net(volt::NetSpec{volt::NetName{"RESET"}, volt::NetKind::Signal});

    const auto header_gnd = volt::queries::pin_by_name(circuit, header, "GND").value();
    const auto header_vbus = volt::queries::pin_by_name(circuit, header, "VBUS").value();
    const auto header_vdd = volt::queries::pin_by_name(circuit, header, "3V3").value();
    const auto header_reset = volt::queries::pin_by_name(circuit, header, "RESET").value();
    const auto regulator_vin = volt::queries::pin_by_name(circuit, regulator, "VIN").value();
    const auto regulator_gnd = volt::queries::pin_by_name(circuit, regulator, "GND").value();
    const auto regulator_vout = volt::queries::pin_by_name(circuit, regulator, "VOUT").value();
    const auto mcu_gnd = volt::queries::pin_by_name(circuit, mcu, "VSS").value();
    const auto mcu_vdd = volt::queries::pin_by_name(circuit, mcu, "VDD").value();
    const auto mcu_gpio = volt::queries::pin_by_name(circuit, mcu, "PA5").value();
    const auto mcu_reset = volt::queries::pin_by_name(circuit, mcu, "NRST").value();
    const auto mcu_boot = volt::queries::pin_by_name(circuit, mcu, "BOOT0").value();
    const auto resistor_a = volt::queries::pin_by_name(circuit, resistor, "A").value();
    const auto resistor_b = volt::queries::pin_by_name(circuit, resistor, "B").value();
    const auto led_a = volt::queries::pin_by_name(circuit, led, "A").value();
    const auto led_k = volt::queries::pin_by_name(circuit, led, "K").value();

    circuit.connect(ground, header_gnd);
    circuit.connect(ground, regulator_gnd);
    circuit.connect(ground, mcu_gnd);
    circuit.connect(ground, led_k);
    circuit.connect(vbus, header_vbus);
    circuit.connect(vbus, regulator_vin);
    circuit.connect(vdd, header_vdd);
    circuit.connect(vdd, regulator_vout);
    circuit.connect(vdd, mcu_vdd);
    circuit.connect(led_drive, mcu_gpio);
    circuit.connect(led_drive, resistor_a);
    circuit.connect(led_anode, resistor_b);
    circuit.connect(led_anode, led_a);
    circuit.connect(reset, header_reset);
    circuit.connect(reset, mcu_reset);
    circuit.mark_no_connect(mcu_boot);

    set_net_voltage(circuit, vbus, 5.0);
    set_net_voltage(circuit, vdd, 3.3);
    set_net_voltage(circuit, ground, 0.0);

    select_part(circuit, header, "HDR-1x4", volt::PackageRef{"1x4"},
                volt::FootprintRef{"regression", "HDR_1x4_PWR"},
                std::vector{volt::PinPadMapping{header_gnd_pin, "1"},
                            volt::PinPadMapping{header_vbus_pin, "2"},
                            volt::PinPadMapping{header_vdd_pin, "3"},
                            volt::PinPadMapping{header_reset_pin, "4"}});
    select_part(circuit, regulator, "AP2112K", volt::PackageRef{"SOT-89"},
                volt::FootprintRef{"regression", "SOT89_REG"},
                std::vector{volt::PinPadMapping{regulator_vin_pin, "1"},
                            volt::PinPadMapping{regulator_gnd_pin, "2"},
                            volt::PinPadMapping{regulator_vout_pin, "3"}});
    select_part(
        circuit, mcu, "MCU-QFN5", volt::PackageRef{"QFN-5"},
        volt::FootprintRef{"regression", "QFN5_MCU"},
        std::vector{volt::PinPadMapping{mcu_gnd_pin, "1"}, volt::PinPadMapping{mcu_vdd_pin, "2"},
                    volt::PinPadMapping{mcu_gpio_pin, "3"}, volt::PinPadMapping{mcu_reset_pin, "4"},
                    volt::PinPadMapping{mcu_boot_pin, "5"}});
    select_part(circuit, resistor, "RC0603-1K", volt::PackageRef{"0603"},
                volt::FootprintRef{"regression", "R_0603_REAL"},
                std::vector{volt::PinPadMapping{resistor_a_pin, "1"},
                            volt::PinPadMapping{resistor_b_pin, "2"}});
    if (select_led_part) {
        select_part(
            circuit, led, "LTST-C190", volt::PackageRef{"0603"},
            volt::FootprintRef{"regression", "LED_0603_REAL"},
            std::vector{volt::PinPadMapping{led_a_pin, "1"}, volt::PinPadMapping{led_k_pin, "2"}});
    }

    set_selected_part_voltage_rating(circuit, header, 30.0);
    set_selected_part_voltage_rating(circuit, regulator, 16.0);
    set_selected_part_voltage_rating(circuit, mcu, 5.5);
    set_selected_part_voltage_rating(circuit, resistor, 50.0);
    if (select_led_part) {
        set_selected_part_voltage_rating(circuit, led, 5.0);
    }

    return RealBoardFixture{std::move(circuit),
                            header_gnd_pin,
                            header_vbus_pin,
                            header_vdd_pin,
                            header_reset_pin,
                            regulator_vin_pin,
                            regulator_gnd_pin,
                            regulator_vout_pin,
                            mcu_gnd_pin,
                            mcu_vdd_pin,
                            mcu_gpio_pin,
                            mcu_reset_pin,
                            mcu_boot_pin,
                            resistor_a_pin,
                            resistor_b_pin,
                            led_a_pin,
                            led_k_pin,
                            header,
                            regulator,
                            mcu,
                            resistor,
                            led,
                            header_gnd,
                            header_vbus,
                            header_vdd,
                            header_reset,
                            regulator_vin,
                            regulator_gnd,
                            regulator_vout,
                            mcu_gnd,
                            mcu_vdd,
                            mcu_gpio,
                            mcu_reset,
                            mcu_boot,
                            resistor_a,
                            resistor_b,
                            led_a,
                            led_k,
                            ground,
                            vbus,
                            vdd,
                            led_drive,
                            led_anode,
                            reset};
}

struct BoardOptions {
    bool omit_led_anode_route = false;
    bool narrow_led_drive_route = false;
    bool overlap_led_with_resistor = false;
    std::optional<volt::BoardPoint> led_position = std::nullopt;
};

struct RealBoardLayout {
    volt::Board board;
    volt::BoardLayerId front;
    volt::BoardLayerId back;
    volt::ComponentPlacementId header_placement;
    volt::ComponentPlacementId regulator_placement;
    volt::ComponentPlacementId mcu_placement;
    volt::ComponentPlacementId resistor_placement;
    std::optional<volt::ComponentPlacementId> led_placement;
    volt::BoardTrackId ground_route;
    volt::BoardTrackId vbus_route;
    volt::BoardTrackId vdd_route;
    volt::BoardTrackId led_drive_route;
    std::optional<volt::BoardTrackId> led_anode_route;
    volt::BoardTrackId reset_route;
};

[[nodiscard, maybe_unused]] RealBoardLayout make_real_board_layout(const RealBoardFixture &fixture,
                                                                   BoardOptions options = {},
                                                                   bool place_led = true) {
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Status Controller"}};
    auto front_layer =
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top};
    front_layer.set_copper_weight_oz(1.0);
    const auto front = board.add_layer(std::move(front_layer));
    auto back_layer =
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom};
    back_layer.set_copper_weight_oz(1.0);
    const auto back = board.add_layer(std::move(back_layer));
    board.set_layer_stack(volt::LayerStack{{front, back}, 1.6});
    board.set_outline(
        volt::BoardOutline::rectangle(volt::BoardPoint{0.0, 0.0}, volt::BoardSize{44.0, 22.0}));
    board.set_design_rules(volt::BoardDesignRules{0.20, 0.25, 0.30, 0.70, 0.30});

    const auto header_placement = board.place_component(volt::ComponentPlacement{
        fixture.header, volt::BoardPoint{5.0, 11.5}, volt::BoardRotation::degrees(0.0)});
    const auto regulator_placement = board.place_component(volt::ComponentPlacement{
        fixture.regulator, volt::BoardPoint{12.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    const auto mcu_placement = board.place_component(volt::ComponentPlacement{
        fixture.mcu, volt::BoardPoint{24.0, 10.0}, volt::BoardRotation::degrees(0.0)});
    const auto resistor_placement = board.place_component(volt::ComponentPlacement{
        fixture.resistor, volt::BoardPoint{32.0, 13.0}, volt::BoardRotation::degrees(0.0)});
    auto led_placement = std::optional<volt::ComponentPlacementId>{};
    if (place_led) {
        const auto led_position = options.led_position.value_or(options.overlap_led_with_resistor
                                                                    ? volt::BoardPoint{32.0, 13.0}
                                                                    : volt::BoardPoint{37.0, 13.0});
        led_placement = board.place_component(
            volt::ComponentPlacement{fixture.led, led_position, volt::BoardRotation::degrees(0.0)});
    }

    const auto ground_route = board.add_track(
        volt::BoardTrack{fixture.ground, front,
                         std::vector{volt::BoardPoint{5.0, 7.0}, volt::BoardPoint{12.0, 7.0},
                                     volt::BoardPoint{22.0, 7.0}, volt::BoardPoint{38.0, 7.0},
                                     volt::BoardPoint{38.0, 13.0}},
                         0.30});
    const auto vbus_route = board.add_track(volt::BoardTrack{
        fixture.vbus, front, std::vector{volt::BoardPoint{5.0, 10.0}, volt::BoardPoint{12.0, 10.0}},
        0.30});
    const auto vdd_route = board.add_track(
        volt::BoardTrack{fixture.vdd, front,
                         std::vector{volt::BoardPoint{5.0, 13.0}, volt::BoardPoint{12.0, 13.0},
                                     volt::BoardPoint{22.0, 13.0}},
                         0.30});
    const auto led_drive_route = board.add_track(
        volt::BoardTrack{fixture.led_drive, front,
                         std::vector{volt::BoardPoint{28.0, 13.0}, volt::BoardPoint{31.0, 13.0}},
                         options.narrow_led_drive_route ? 0.10 : 0.30});
    auto led_anode_route = std::optional<volt::BoardTrackId>{};
    if (!options.omit_led_anode_route) {
        led_anode_route = board.add_track(volt::BoardTrack{
            fixture.led_anode, front,
            std::vector{volt::BoardPoint{33.0, 13.0}, volt::BoardPoint{36.0, 13.0}}, 0.30});
    }
    const auto reset_route = board.add_track(volt::BoardTrack{
        fixture.reset, front,
        std::vector{volt::BoardPoint{5.0, 16.0}, volt::BoardPoint{28.0, 16.0}}, 0.30});

    return RealBoardLayout{std::move(board),
                           front,
                           back,
                           header_placement,
                           regulator_placement,
                           mcu_placement,
                           resistor_placement,
                           led_placement,
                           ground_route,
                           vbus_route,
                           vdd_route,
                           led_drive_route,
                           led_anode_route,
                           reset_route};
}

[[nodiscard, maybe_unused]] std::string diagnostic_code_list(const volt::DiagnosticReport &report) {
    auto result = std::string{};
    for (const auto &diagnostic : report.diagnostics()) {
        if (!result.empty()) {
            result += ", ";
        }
        result += diagnostic.code().value();
    }
    return result;
}

[[maybe_unused]] void check_diagnostic_summaries(const volt::DiagnosticReport &report,
                                                 const std::vector<ExpectedDiagnostic> &expected) {
    INFO("actual diagnostic codes: " << diagnostic_code_list(report));
    REQUIRE(report.count() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const auto &diagnostic = report.diagnostics()[index];
        CHECK(diagnostic.code() == volt::DiagnosticCode{expected[index].code});
        CHECK(diagnostic.severity() == expected[index].severity);
        CHECK(diagnostic.category() == expected[index].category);
    }
}

[[nodiscard, maybe_unused]] const volt::Diagnostic *
find_diagnostic(const volt::DiagnosticReport &report, std::string_view code) {
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code().value() == code) {
            return &diagnostic;
        }
    }
    return nullptr;
}

[[nodiscard, maybe_unused]] std::vector<const volt::Diagnostic *>
find_diagnostics(const volt::DiagnosticReport &report, std::string_view code) {
    auto matches = std::vector<const volt::Diagnostic *>{};
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code().value() == code) {
            matches.push_back(&diagnostic);
        }
    }
    return matches;
}

[[nodiscard, maybe_unused]] std::reference_wrapper<const volt::Diagnostic>
require_diagnostic(const volt::DiagnosticReport &report, const ExpectedDiagnostic &expected) {
    INFO("actual diagnostic codes: " << diagnostic_code_list(report));
    const auto *match = static_cast<const volt::Diagnostic *>(nullptr);
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code() == volt::DiagnosticCode{expected.code} &&
            diagnostic.severity() == expected.severity &&
            diagnostic.category() == expected.category) {
            match = &diagnostic;
            break;
        }
    }
    REQUIRE(match != nullptr);
    return std::cref(*match);
}

[[nodiscard, maybe_unused]] std::reference_wrapper<const volt::Diagnostic>
require_diagnostic_with_entities(const volt::DiagnosticReport &report,
                                 const ExpectedDiagnostic &expected,
                                 const std::vector<volt::EntityRef> &entities) {
    INFO("actual diagnostic codes: " << diagnostic_code_list(report));
    const auto *match = static_cast<const volt::Diagnostic *>(nullptr);
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code() == volt::DiagnosticCode{expected.code} &&
            diagnostic.severity() == expected.severity &&
            diagnostic.category() == expected.category && diagnostic.entities() == entities) {
            match = &diagnostic;
            break;
        }
    }
    REQUIRE(match != nullptr);
    return std::cref(*match);
}

[[maybe_unused]] void check_diagnostic_count(const volt::DiagnosticReport &report,
                                             std::string_view code, std::size_t expected_count) {
    INFO("actual diagnostic codes: " << diagnostic_code_list(report));
    CHECK(find_diagnostics(report, code).size() == expected_count);
}

[[maybe_unused]] void check_diagnostic_entities(const volt::Diagnostic &diagnostic,
                                                const std::vector<volt::EntityRef> &entities) {
    CHECK(diagnostic.entities() == entities);
}

[[nodiscard, maybe_unused]] volt::BoardCapabilityProfile make_manufacturing_prereq_profile() {
    return volt::BoardCapabilityProfile{
        "Real-board regression fab",
        volt::BoardCapabilityProvenance{"Regression fixture capability table", "2026-06-15"},
        0.30,
        0.35,
        0.75,
        std::vector{
            volt::BoardClearancePair{volt::BoardClearanceKind::Track,
                                     volt::BoardClearanceKind::Track, 0.10},
            volt::BoardClearancePair{volt::BoardClearanceKind::Track, volt::BoardClearanceKind::Pad,
                                     0.10},
            volt::BoardClearancePair{volt::BoardClearanceKind::Pad,
                                     volt::BoardClearanceKind::BoardEdge, 0.10},
        },
    };
}

} // namespace
