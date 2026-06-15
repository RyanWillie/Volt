#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/circuit/definitions.hpp>
#include <volt/circuit/net_classes.hpp>
#include <volt/circuit/queries.hpp>
#include <volt/circuit/validation.hpp>
#include <volt/core/diagnostics.hpp>
#include <volt/core/electrical_attributes.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints.hpp>

namespace {

struct ExpectedDiagnostic {
    std::string code;
    volt::Severity severity;
    volt::DiagnosticCategory category;
};

[[nodiscard]] volt::ElectricalAttributeSpec net_voltage_spec() {
    return volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage"},
        volt::ElectricalAttributeOwner::Net,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };
}

[[nodiscard]] volt::ElectricalAttributeSpec pin_voltage_range_spec() {
    return volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_range"},
        volt::ElectricalAttributeOwner::PinSpec,
        volt::ElectricalAttributeKind::Constraint,
        volt::UnitDimension::Voltage,
    };
}

[[nodiscard]] volt::ElectricalAttributeSpec selected_part_voltage_rating_spec() {
    return volt::ElectricalAttributeSpec{
        volt::ElectricalAttributeName{"voltage_rating"},
        volt::ElectricalAttributeOwner::SelectedPart,
        volt::ElectricalAttributeKind::DesignInput,
        volt::UnitDimension::Voltage,
    };
}

void set_net_voltage(volt::Circuit &circuit, volt::NetId net, double voltage) {
    circuit.set_net_electrical_attribute(
        net, net_voltage_spec(),
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, voltage}});
}

void set_pin_voltage_range(volt::Circuit &circuit, volt::PinDefId pin, double low, double high) {
    circuit.set_pin_definition_electrical_attribute(
        pin, pin_voltage_range_spec(),
        volt::ElectricalAttributeValue{
            volt::QuantityRange::bounded(volt::Quantity{volt::UnitDimension::Voltage, low},
                                         volt::Quantity{volt::UnitDimension::Voltage, high})});
}

void set_selected_part_voltage_rating(volt::Circuit &circuit, volt::ComponentId component,
                                      double voltage) {
    circuit.set_selected_part_electrical_attribute(
        component, selected_part_voltage_rating_spec(),
        volt::ElectricalAttributeValue{volt::Quantity{volt::UnitDimension::Voltage, voltage}});
}

[[nodiscard]] volt::FootprintPolygon rectangle_polygon(double half_width, double half_height) {
    return volt::FootprintPolygon{std::vector{
        volt::FootprintPoint{-half_width, -half_height},
        volt::FootprintPoint{half_width, -half_height},
        volt::FootprintPoint{half_width, half_height},
        volt::FootprintPoint{-half_width, half_height},
    }};
}

[[nodiscard]] volt::FootprintDefinition real_header_footprint() {
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

[[nodiscard]] volt::FootprintDefinition regulator_footprint() {
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

[[nodiscard]] volt::FootprintDefinition mcu_footprint() {
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

[[nodiscard]] volt::FootprintDefinition two_pad_smd_footprint(volt::FootprintRef ref,
                                                              double courtyard_half_width) {
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

[[nodiscard]] volt::FootprintLibrary real_board_library() {
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

void select_part(volt::Circuit &circuit, volt::ComponentId component, std::string_view mpn,
                 volt::PackageRef package, volt::FootprintRef footprint,
                 std::vector<volt::PinPadMapping> mappings) {
    circuit.select_physical_part(
        component,
        volt::PhysicalPart{volt::ManufacturerPart{"Volt Regression", std::string{mpn}},
                           std::move(package), std::move(footprint), std::move(mappings)});
}

[[nodiscard]] RealBoardFixture make_real_board_fixture(bool select_led_part = true) {
    auto circuit = volt::Circuit{};

    const auto header_gnd_pin = circuit.add_pin_definition(volt::PinDefinition{
        "GND", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Ground,
        volt::ElectricalDirection::Passive});
    const auto header_vbus_pin = circuit.add_pin_definition(volt::PinDefinition{
        "VBUS", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Power,
        volt::ElectricalDirection::Output});
    const auto header_vdd_pin = circuit.add_pin_definition(volt::PinDefinition{
        "3V3", "3", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto header_reset_pin = circuit.add_pin_definition(volt::PinDefinition{
        "RESET", "4", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Signal,
        volt::ElectricalDirection::Input, volt::ElectricalSignalDomain::Digital});
    const auto regulator_vin_pin = circuit.add_pin_definition(
        volt::PinDefinition{"VIN", "1", volt::ConnectionRequirement::Required,
                            volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto regulator_gnd_pin = circuit.add_pin_definition(volt::PinDefinition{
        "GND", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Ground,
        volt::ElectricalDirection::Passive});
    const auto regulator_vout_pin = circuit.add_pin_definition(volt::PinDefinition{
        "VOUT", "3", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Power,
        volt::ElectricalDirection::Output});
    const auto mcu_gnd_pin = circuit.add_pin_definition(volt::PinDefinition{
        "VSS", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Ground,
        volt::ElectricalDirection::Passive});
    const auto mcu_vdd_pin = circuit.add_pin_definition(
        volt::PinDefinition{"VDD", "2", volt::ConnectionRequirement::Required,
                            volt::ElectricalTerminalKind::Power, volt::ElectricalDirection::Input});
    const auto mcu_gpio_pin = circuit.add_pin_definition(volt::PinDefinition{
        "PA5", "3", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Signal,
        volt::ElectricalDirection::Output, volt::ElectricalSignalDomain::Digital});
    const auto mcu_reset_pin = circuit.add_pin_definition(volt::PinDefinition{
        "NRST", "4", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Signal,
        volt::ElectricalDirection::Input, volt::ElectricalSignalDomain::Digital});
    const auto mcu_boot_pin = circuit.add_pin_definition(volt::PinDefinition{
        "BOOT0", "5", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Signal,
        volt::ElectricalDirection::Input, volt::ElectricalSignalDomain::Digital});
    const auto resistor_a_pin = circuit.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto resistor_b_pin = circuit.add_pin_definition(volt::PinDefinition{
        "B", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto led_a_pin = circuit.add_pin_definition(volt::PinDefinition{
        "A", "1", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});
    const auto led_k_pin = circuit.add_pin_definition(volt::PinDefinition{
        "K", "2", volt::ConnectionRequirement::Required, volt::ElectricalTerminalKind::Passive,
        volt::ElectricalDirection::Passive, volt::ElectricalSignalDomain::Unspecified,
        volt::ElectricalDriveKind::Passive});

    const auto header_def = circuit.add_component_definition(volt::ComponentDefinition{
        "PowerAndDebugHeader",
        std::vector{header_gnd_pin, header_vbus_pin, header_vdd_pin, header_reset_pin}});
    const auto regulator_def = circuit.add_component_definition(volt::ComponentDefinition{
        "LDO", std::vector{regulator_vin_pin, regulator_gnd_pin, regulator_vout_pin}});
    const auto mcu_def = circuit.add_component_definition(volt::ComponentDefinition{
        "MCU", std::vector{mcu_gnd_pin, mcu_vdd_pin, mcu_gpio_pin, mcu_reset_pin, mcu_boot_pin}});
    const auto resistor_def = circuit.add_component_definition(
        volt::ComponentDefinition{"LedResistor", std::vector{resistor_a_pin, resistor_b_pin}});
    const auto led_def = circuit.add_component_definition(
        volt::ComponentDefinition{"StatusLed", std::vector{led_a_pin, led_k_pin}});

    const auto header = circuit.instantiate_component(header_def, volt::ReferenceDesignator{"J1"});
    const auto regulator =
        circuit.instantiate_component(regulator_def, volt::ReferenceDesignator{"U1"});
    const auto mcu = circuit.instantiate_component(mcu_def, volt::ReferenceDesignator{"U2"});
    const auto resistor =
        circuit.instantiate_component(resistor_def, volt::ReferenceDesignator{"R1"});
    const auto led = circuit.instantiate_component(led_def, volt::ReferenceDesignator{"D1"});

    const auto ground = circuit.add_net(volt::Net{volt::NetName{"GND"}, volt::NetKind::Ground});
    const auto vbus = circuit.add_net(volt::Net{volt::NetName{"VBUS"}, volt::NetKind::Power});
    const auto vdd = circuit.add_net(volt::Net{volt::NetName{"+3V3"}, volt::NetKind::Power});
    const auto led_drive =
        circuit.add_net(volt::Net{volt::NetName{"LED_DRV"}, volt::NetKind::Signal});
    const auto led_anode =
        circuit.add_net(volt::Net{volt::NetName{"LED_A"}, volt::NetKind::Signal});
    const auto reset = circuit.add_net(volt::Net{volt::NetName{"RESET"}, volt::NetKind::Signal});

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
    circuit.mark_intentional_no_connect_pin(mcu_boot);

    set_net_voltage(circuit, vbus, 5.0);
    set_net_voltage(circuit, vdd, 3.3);
    set_net_voltage(circuit, ground, 0.0);
    set_pin_voltage_range(circuit, regulator_vin_pin, 4.5, 5.5);
    set_pin_voltage_range(circuit, mcu_vdd_pin, 1.8, 3.6);

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

[[nodiscard]] RealBoardLayout make_real_board_layout(const RealBoardFixture &fixture,
                                                     BoardOptions options = {},
                                                     bool place_led = true) {
    auto board = volt::Board{fixture.circuit, volt::BoardName{"Status Controller"}};
    const auto front = board.add_layer(
        volt::BoardLayer{"F.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Top});
    const auto back = board.add_layer(
        volt::BoardLayer{"B.Cu", volt::BoardLayerRole::Copper, volt::BoardLayerSide::Bottom});
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
        const auto led_position = options.overlap_led_with_resistor ? volt::BoardPoint{32.0, 13.0}
                                                                    : volt::BoardPoint{37.0, 13.0};
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

[[nodiscard]] std::string diagnostic_code_list(const volt::DiagnosticReport &report) {
    auto result = std::string{};
    for (const auto &diagnostic : report.diagnostics()) {
        if (!result.empty()) {
            result += ", ";
        }
        result += diagnostic.code().value();
    }
    return result;
}

void check_diagnostic_summaries(const volt::DiagnosticReport &report,
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

[[nodiscard]] const volt::Diagnostic *find_diagnostic(const volt::DiagnosticReport &report,
                                                      std::string_view code) {
    for (const auto &diagnostic : report.diagnostics()) {
        if (diagnostic.code().value() == code) {
            return &diagnostic;
        }
    }
    return nullptr;
}

[[nodiscard]] volt::BoardCapabilityProfile make_manufacturing_prereq_profile() {
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

TEST_CASE("Real-board ERC and DRC regression accepts a fully routed status-controller board") {
    const auto fixture = make_real_board_fixture();
    const auto layout = make_real_board_layout(fixture);
    const auto library = real_board_library();

    const auto erc = volt::validate_circuit(fixture.circuit);
    const auto pcb_readiness = volt::validate_for_pcb(fixture.circuit);
    const auto board = volt::validate_board(layout.board, library);

    CHECK(erc.empty());
    CHECK(pcb_readiness.empty());
    INFO("actual board diagnostic codes: " << diagnostic_code_list(board));
    CHECK(board.empty());
}

TEST_CASE("Real-board regression keeps ERC, PCB readiness, DRC, visual, and fab boundaries clear") {
    auto fixture = make_real_board_fixture(false);
    auto layout = make_real_board_layout(fixture, {}, false);
    const auto library = real_board_library();

    const auto erc = volt::validate_circuit(fixture.circuit);
    const auto pcb_readiness = volt::validate_for_pcb(fixture.circuit);
    auto board = volt::validate_board(layout.board, library);

    CHECK(erc.empty());
    check_diagnostic_summaries(
        pcb_readiness,
        {ExpectedDiagnostic{"PHYSICAL_PART_REQUIRED", volt::Severity::Error,
                            volt::DiagnosticCategory{volt::diagnostic_categories::General}}});
    check_diagnostic_summaries(
        board,
        {ExpectedDiagnostic{"PCB_COMPONENT_NOT_PLACED", volt::Severity::Error,
                            volt::DiagnosticCategory{volt::diagnostic_categories::PcbBoard}},
         ExpectedDiagnostic{"PCB_COMPONENT_MISSING_SELECTED_PART", volt::Severity::Error,
                            volt::DiagnosticCategory{volt::diagnostic_categories::PcbBoard}}});
    CHECK(board.diagnostics()[0].entities() ==
          std::vector{volt::EntityRef::component(fixture.led)});
    CHECK(board.diagnostics()[1].entities() ==
          std::vector{volt::EntityRef::component(fixture.led)});

    [[maybe_unused]] const auto text = layout.board.add_text(
        volt::BoardText{"REV A", volt::BoardPoint{42.0, 1.0}, volt::BoardRotation::degrees(0.0),
                        layout.front, 2.0});
    board = volt::validate_board(layout.board, library);
    const auto *visual =
        find_diagnostic(board, volt::pcb_visual_diagnostic_codes::LabelOutsideBoard);
    REQUIRE(visual != nullptr);
    CHECK(visual->severity() == volt::Severity::Warning);
    CHECK(visual->category() == volt::DiagnosticCategory{volt::diagnostic_categories::PcbVisual});

    layout.board.set_capability_profile(make_manufacturing_prereq_profile());
    board = volt::validate_board(layout.board, library);
    const auto *capability =
        find_diagnostic(board, volt::drc_diagnostic_codes::RuleBelowCapability);
    REQUIRE(capability != nullptr);
    CHECK(capability->severity() == volt::Severity::Error);
    CHECK(capability->category() == volt::DiagnosticCategory{volt::diagnostic_categories::Drc});
    CHECK(find_diagnostic(board, volt::pcb_fabrication_diagnostic_codes::KiCadFabExportLoss) ==
          nullptr);
}

TEST_CASE("Real-board ERC regression locks intentionally broken logical variants") {
    SECTION("required MCU power pin left unconnected") {
        auto fixture = make_real_board_fixture();
        REQUIRE(fixture.circuit.disconnect(fixture.mcu_vdd));

        const auto report = volt::validate_circuit(fixture.circuit);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"UNCONNECTED_REQUIRED_PIN", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Erc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::pin(fixture.mcu_vdd),
                          volt::EntityRef::component(fixture.mcu),
                          volt::EntityRef::pin_def(fixture.mcu_vdd_pin)});
    }

    SECTION("authored no-connect MCU boot pin is accidentally tied to reset") {
        auto fixture = make_real_board_fixture();
        fixture.circuit.connect(fixture.reset, fixture.mcu_boot);

        const auto report = volt::validate_circuit(fixture.circuit);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PIN_INTENTIONAL_NO_CONNECT_IS_CONNECTED", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Erc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::pin(fixture.mcu_boot),
                          volt::EntityRef::component(fixture.mcu),
                          volt::EntityRef::pin_def(fixture.mcu_boot_pin),
                          volt::EntityRef::net(fixture.reset)});
    }

    SECTION("3V3 rail overdrives the MCU pin voltage range") {
        auto fixture = make_real_board_fixture();
        set_net_voltage(fixture.circuit, fixture.vdd, 5.0);

        const auto report = volt::validate_circuit(fixture.circuit);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PIN_VOLTAGE_RANGE_VIOLATION", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Erc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::net(fixture.vdd), volt::EntityRef::pin(fixture.mcu_vdd),
                          volt::EntityRef::pin_def(fixture.mcu_vdd_pin)});
    }

    SECTION("selected MCU part voltage rating is below the authored 3V3 rail") {
        auto fixture = make_real_board_fixture();
        set_selected_part_voltage_rating(fixture.circuit, fixture.mcu, 2.5);

        const auto report = volt::validate_circuit(fixture.circuit);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"SELECTED_PART_VOLTAGE_RATING_EXCEEDED", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Erc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::net(fixture.vdd), volt::EntityRef::pin(fixture.mcu_vdd),
                          volt::EntityRef::component(fixture.mcu)});
    }

    SECTION("two physical supply outputs are shorted onto VBUS") {
        auto fixture = make_real_board_fixture();
        REQUIRE(fixture.circuit.disconnect(fixture.regulator_vout));
        fixture.circuit.connect(fixture.vbus, fixture.regulator_vout);

        const auto report = volt::validate_circuit(fixture.circuit);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"MULTIPLE_OUTPUTS_ON_NET", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Erc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::net(fixture.vbus),
                          volt::EntityRef::pin(fixture.header_vbus),
                          volt::EntityRef::pin(fixture.regulator_vout)});
    }
}

TEST_CASE("Real-board DRC regression locks broken copper and placement variants") {
    const auto library = real_board_library();

    SECTION("narrow routed LED drive track fails the board minimum") {
        const auto fixture = make_real_board_fixture();
        const auto layout =
            make_real_board_layout(fixture, BoardOptions{.narrow_led_drive_route = true});

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_TRACK_WIDTH_BELOW_MINIMUM", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::board_track(layout.led_drive_route),
                          volt::EntityRef::net(fixture.led_drive),
                          volt::EntityRef::board_layer(layout.front)});
        REQUIRE(report.diagnostics()[0].measurement().has_value());
        CHECK(report.diagnostics()[0].measurement()->actual_mm == 0.10);
        CHECK(report.diagnostics()[0].measurement()->required_mm == 0.25);
    }

    SECTION("undersized via locks drill and annular diagnostics before later DRC rules") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        const auto via = layout.board.add_via(volt::BoardVia{
            fixture.vdd, volt::BoardPoint{18.0, 13.0}, layout.front, layout.back, 0.20, 0.50});

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_VIA_DRILL_BELOW_MINIMUM", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_VIA_ANNULAR_BELOW_MINIMUM", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::board_via(via), volt::EntityRef::net(fixture.vdd)});
        CHECK(report.diagnostics()[1].entities() ==
              std::vector{volt::EntityRef::board_via(via), volt::EntityRef::net(fixture.vdd)});
    }

    SECTION("clearance failure between two extra routed nets preserves copper entity ordering") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        const auto vbus_island = layout.board.add_track(volt::BoardTrack{
            fixture.vbus, layout.front,
            std::vector{volt::BoardPoint{10.0, 20.0}, volt::BoardPoint{20.0, 20.0}}, 0.30});
        const auto reset_island = layout.board.add_track(volt::BoardTrack{
            fixture.reset, layout.front,
            std::vector{volt::BoardPoint{10.0, 20.25}, volt::BoardPoint{20.0, 20.25}}, 0.30});

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_COPPER_CLEARANCE_VIOLATION", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::board_track(vbus_island),
                          volt::EntityRef::board_track(reset_island),
                          volt::EntityRef::net(fixture.vbus), volt::EntityRef::net(fixture.reset),
                          volt::EntityRef::board_layer(layout.front)});
        REQUIRE(report.diagnostics()[0].measurement().has_value());
        CHECK(report.diagnostics()[0].measurement()->actual_mm <
              report.diagnostics()[0].measurement()->required_mm);
    }

    SECTION("copper outside the outline stays a DRC error") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        const auto outside = layout.board.add_track(volt::BoardTrack{
            fixture.reset, layout.front,
            std::vector{volt::BoardPoint{43.9, 18.0}, volt::BoardPoint{46.0, 18.0}}, 0.30});

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_COPPER_OUTSIDE_OUTLINE", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::board_track(outside),
                          volt::EntityRef::net(fixture.reset),
                          volt::EntityRef::board_layer(layout.front)});
    }

    SECTION("keepouts catch copper, vias, and placements on realistic board geometry") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        const auto copper_keepout = layout.board.add_keepout(volt::BoardKeepout{
            std::vector{volt::BoardPoint{7.0, 9.75}, volt::BoardPoint{11.0, 9.75},
                        volt::BoardPoint{11.0, 10.25}, volt::BoardPoint{7.0, 10.25}},
            std::vector{layout.front}, std::vector{volt::BoardKeepoutRestriction::Copper}});
        const auto via_keepout = layout.board.add_keepout(volt::BoardKeepout{
            std::vector{volt::BoardPoint{17.0, 12.0}, volt::BoardPoint{19.0, 12.0},
                        volt::BoardPoint{19.0, 14.0}, volt::BoardPoint{17.0, 14.0}},
            std::vector{layout.front, layout.back},
            std::vector{volt::BoardKeepoutRestriction::Via}});
        const auto placement_keepout = layout.board.add_keepout(volt::BoardKeepout{
            std::vector{volt::BoardPoint{10.5, 8.5}, volt::BoardPoint{13.5, 8.5},
                        volt::BoardPoint{13.5, 11.5}, volt::BoardPoint{10.5, 11.5}},
            std::vector{layout.front}, std::vector{volt::BoardKeepoutRestriction::Placement}});
        const auto via = layout.board.add_via(volt::BoardVia{
            fixture.vdd, volt::BoardPoint{18.0, 13.0}, layout.front, layout.back, 0.30, 0.70});

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_KEEPOUT_COPPER_VIOLATION", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_KEEPOUT_VIA_VIOLATION", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_KEEPOUT_PLACEMENT_VIOLATION", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::board_keepout(copper_keepout),
                          volt::EntityRef::board_track(layout.vbus_route),
                          volt::EntityRef::net(fixture.vbus),
                          volt::EntityRef::board_layer(layout.front)});
        CHECK(report.diagnostics()[1].entities() ==
              std::vector{volt::EntityRef::board_keepout(via_keepout),
                          volt::EntityRef::board_via(via), volt::EntityRef::net(fixture.vdd),
                          volt::EntityRef::board_layer(layout.front)});
        CHECK(report.diagnostics()[2].entities() ==
              std::vector{volt::EntityRef::board_keepout(placement_keepout),
                          volt::EntityRef::component_placement(layout.regulator_placement),
                          volt::EntityRef::component(fixture.regulator)});
    }

    SECTION("overlapping placed components report visual and DRC footprint-geometry diagnostics") {
        const auto fixture = make_real_board_fixture();
        const auto layout =
            make_real_board_layout(fixture, BoardOptions{.overlap_led_with_resistor = true});

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_VISUAL_PLACEMENT_OVERLAP", volt::Severity::Warning,
                                volt::DiagnosticCategory{volt::diagnostic_categories::PcbVisual}},
             ExpectedDiagnostic{"PCB_COPPER_CLEARANCE_VIOLATION", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_COPPER_CLEARANCE_VIOLATION", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_COPPER_CLEARANCE_VIOLATION", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_COPPER_CLEARANCE_VIOLATION", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_COMPONENT_BODY_OVERLAP", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_COMPONENT_COURTYARD_OVERLAP", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_NET_UNROUTED", volt::Severity::Warning,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_NET_UNROUTED", volt::Severity::Warning,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        REQUIRE(layout.led_placement.has_value());
        CHECK(report.diagnostics()[5].entities() ==
              std::vector{volt::EntityRef::component_placement(layout.resistor_placement),
                          volt::EntityRef::component_placement(layout.led_placement.value()),
                          volt::EntityRef::component(fixture.resistor),
                          volt::EntityRef::component(fixture.led)});
    }

    SECTION("omitted LED anode route reports the stable unrouted ratsnest endpoint") {
        const auto fixture = make_real_board_fixture();
        const auto layout =
            make_real_board_layout(fixture, BoardOptions{.omit_led_anode_route = true});

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_NET_UNROUTED", volt::Severity::Warning,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        REQUIRE(layout.led_placement.has_value());
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::net(fixture.led_anode),
                          volt::EntityRef::component_placement(layout.resistor_placement),
                          volt::EntityRef::footprint_pad(volt::FootprintPadId{1}),
                          volt::EntityRef::component_placement(layout.led_placement.value()),
                          volt::EntityRef::footprint_pad(volt::FootprintPadId{0})});
    }
}

TEST_CASE("Real-board DRC regression covers net-class and manufacturability prerequisites") {
    const auto library = real_board_library();

    SECTION("bottom-layer copper violates a top-only signal net class") {
        auto fixture = make_real_board_fixture();
        auto signal_class = volt::NetClass{volt::NetClassName{"TOP_ONLY_LED"}};
        signal_class.set_layer_scope(volt::NetClassLayerScope::TopOnly);
        const auto class_id = fixture.circuit.add_net_class(std::move(signal_class));
        REQUIRE(fixture.circuit.assign_net_class(fixture.led_drive, class_id));
        auto layout = make_real_board_layout(fixture);
        const auto bottom_track = layout.board.add_track(volt::BoardTrack{
            fixture.led_drive, layout.back,
            std::vector{volt::BoardPoint{28.0, 18.0}, volt::BoardPoint{31.0, 18.0}}, 0.30});

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_COPPER_ON_DISALLOWED_LAYER", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        CHECK(report.diagnostics()[0].entities() ==
              std::vector{volt::EntityRef::board_track(bottom_track),
                          volt::EntityRef::net(fixture.led_drive),
                          volt::EntityRef::board_layer(layout.back)});
    }

    SECTION("fab capability profile rejects board rules below manufacturing prerequisites") {
        const auto fixture = make_real_board_fixture();
        auto layout = make_real_board_layout(fixture);
        layout.board.set_capability_profile(make_manufacturing_prereq_profile());

        const auto report = volt::validate_board(layout.board, library);

        check_diagnostic_summaries(
            report,
            {ExpectedDiagnostic{"PCB_RULE_BELOW_CAPABILITY", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_RULE_BELOW_CAPABILITY", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}},
             ExpectedDiagnostic{"PCB_RULE_BELOW_CAPABILITY", volt::Severity::Error,
                                volt::DiagnosticCategory{volt::diagnostic_categories::Drc}}});
        CHECK(report.diagnostics()[0].entities() == std::vector{volt::EntityRef::board()});
        CHECK(report.diagnostics()[1].entities() == std::vector{volt::EntityRef::board()});
        CHECK(report.diagnostics()[2].entities() == std::vector{volt::EntityRef::board()});
    }
}
