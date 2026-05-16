import json
from pathlib import Path
from tempfile import TemporaryDirectory

import volt
from volt.libraries import stm32_usb_buck


def _two_pin_test_symbol(name: str, *, variant: str = "default", label: str = "SYM"):
    return volt.SchematicSymbolSpec(
        name,
        variant=variant,
        pins=(
            volt.SchematicSymbolSpec.pin("1", 1, (0, 0), "Left"),
            volt.SchematicSymbolSpec.pin("2", 2, (20, 0), "Right"),
        ),
        primitives=(
            volt.SchematicSymbolSpec.line((0, 0), (4, 0)),
            volt.SchematicSymbolSpec.rectangle((4, -3), (16, 3)),
            volt.SchematicSymbolSpec.line((16, 0), (20, 0)),
            volt.SchematicSymbolSpec.text(label, (10, -8)),
        ),
    )


def test_led_circuit_validates():
    design = volt.Design("led")

    vcc = design.net("VCC", kind="power")
    led_a = design.net("LED_A")
    gnd = design.net("GND", kind="ground")

    j1 = design.connector_1x02(ref="J1")
    r1 = design.R("330 ohm", ref="R1")
    d1 = design.LED(ref="D1")

    vcc += j1[1], r1[1]
    led_a += r1[2], d1["A"]
    gnd += d1["K"], j1[2]

    report = design.validate()
    assert len(report) == 0
    assert not report.has_errors

    json_text = design.to_json()
    assert '"name": "VCC"' in json_text
    assert '"reference": "R1"' in json_text


def test_natural_electrical_values_serialize_as_kernel_attributes():
    design = volt.Design("typed")

    design.R(resistance=330, tolerance=0.01, ref="R1")
    design.C(capacitance=100e-9, voltage_rating=16, ref="C1")
    design.net("VDD", kind="power", voltage=3.3)

    circuit = json.loads(design.to_json())
    resistor = next(
        component for component in circuit["components"] if component["reference"] == "R1"
    )
    capacitor = next(
        component for component in circuit["components"] if component["reference"] == "C1"
    )
    vdd = next(net for net in circuit["nets"] if net["name"] == "VDD")

    assert resistor["electrical_attributes"]["resistance"] == {
        "type": "quantity",
        "dimension": "resistance",
        "value": 330.0,
    }
    assert resistor["electrical_attributes"]["tolerance"] == {
        "type": "tolerance",
        "mode": "percent",
        "dimension": "ratio",
        "minus": 0.01,
        "plus": 0.01,
    }
    assert capacitor["electrical_attributes"]["capacitance"] == {
        "type": "quantity",
        "dimension": "capacitance",
        "value": 100e-9,
    }
    assert capacitor["selected_physical_part"]["electrical_attributes"]["voltage_rating"] == {
        "type": "quantity",
        "dimension": "voltage",
        "value": 16.0,
    }
    assert vdd["electrical_attributes"]["voltage"] == {
        "type": "quantity",
        "dimension": "voltage",
        "value": 3.3,
    }


def test_custom_component_definitions_are_kernel_owned():
    design = volt.Design("custom")

    opamp = design.define_component(
        "OpAmp",
        pins=[
            volt.PinSpec("OUT", 1, role="output"),
            volt.PinSpec("IN-", 2, role="input"),
            volt.PinSpec("IN+", 3, role="input"),
            volt.PinSpec("V-", 4, role="power"),
            volt.PinSpec("V+", 8, role="power"),
        ],
        properties={"category": "analog"},
    )
    u1 = design.instantiate(opamp, ref="U1")

    vout = design.net("VOUT")
    vout += u1["OUT"]

    circuit = json.loads(design.to_json())
    definition = circuit["component_definitions"][0]
    component = circuit["components"][0]
    pin_definitions = {pin["name"]: pin for pin in circuit["pin_definitions"]}

    assert definition["name"] == "OpAmp"
    assert definition["properties"]["category"] == {"type": "string", "value": "analog"}
    assert component["reference"] == "U1"
    assert pin_definitions["OUT"]["role"] == "DigitalOutput"
    assert pin_definitions["V+"]["role"] == "PowerInput"
    assert len(circuit["pins"]) == 5
    assert circuit["nets"][0]["pins"] == ["pin:0"]

    report = design.validate()
    assert report.has_errors
    assert {diagnostic.code for diagnostic in report} == {"UNCONNECTED_REQUIRED_PIN", "SINGLE_PIN_NET"}


def test_python_design_intent_serializes_as_kernel_design_intent():
    design = volt.Design("intent")
    mcu = design.define_component(
        "MCU",
        pins=[volt.PinSpec("PB2", 1, role="input")],
    )
    u1 = design.instantiate(mcu, ref="U1")

    boot = design.net("BOOT_TRACE").mark_stub()
    u1["PB2"].mark_no_connect()

    circuit = json.loads(design.to_json())

    assert boot.index == 0
    assert u1["PB2"].index == 0
    assert circuit["design_intent"] == {
        "stub_nets": ["net:0"],
        "no_connect_pins": ["pin:0"],
    }


def test_python_stub_net_intent_suppresses_only_intended_net_shape_diagnostics():
    design = volt.Design("stub-validation")
    probe_definition = design.define_component(
        "Probe",
        pins=[volt.PinSpec("SWDIO", 1, requirement="optional")],
    )
    tp1 = design.instantiate(probe_definition, ref="TP1")

    design.net("BOOT_TRACE").mark_stub()
    swdio = design.net("SWDIO").mark_stub()
    swdio += tp1["SWDIO"]
    design.net("REAL_FLOATING_NET")

    report = design.validate()

    assert [diagnostic.code for diagnostic in report] == ["EMPTY_NET"]
    assert report[0].entities[0].kind == "net"
    assert report[0].entities[0].index == 2


def test_python_no_connect_intent_suppresses_only_intended_missing_pin_diagnostics():
    design = volt.Design("no-connect-validation")
    mcu = design.define_component(
        "MCU",
        pins=[
            volt.PinSpec("PB2", 1, role="input"),
            volt.PinSpec("PB3", 2, role="input"),
        ],
    )
    u1 = design.instantiate(mcu, ref="U1")

    u1["PB2"].mark_no_connect()

    report = design.validate()

    assert [diagnostic.code for diagnostic in report] == ["UNCONNECTED_REQUIRED_PIN"]
    assert report[0].entities[0].kind == "pin"
    assert report[0].entities[0].index == u1["PB3"].index


def test_python_schematic_no_connect_marker_does_not_mutate_logical_intent():
    design = volt.Design("schematic-no-connect-boundary")
    test_point = design.define_component(
        "TestPoint",
        pins=[volt.PinSpec("NC", 1)],
        schematic_symbol=volt.SchematicSymbolSpec(
            "test:point",
            pins=(volt.SchematicSymbolSpec.pin("NC", 1, (0, 0), "Right"),),
            primitives=(volt.SchematicSymbolSpec.circle((0, 0), 1.5),),
        ),
    )
    tp1 = design.instantiate(test_point, ref="TP1")
    schematic = design.schematic("Main")
    pin = schematic.place(tp1, at=(10, 20), symbol="test:point").pin("NC")

    schematic.no_connect(pin, reason="open test pad")

    logical = json.loads(design.to_json())
    projection = json.loads(schematic.to_json())
    report = design.validate()

    assert logical.get("design_intent", {}).get("no_connect_pins", []) == []
    assert projection["no_connect_markers"][0]["pin"] == f"pin:{pin.pin.index}"
    assert [diagnostic.code for diagnostic in report] == ["UNCONNECTED_REQUIRED_PIN"]
    assert report[0].entities[0].index == pin.pin.index


def test_library_component_instantiates_kernel_owned_definition_once():
    design = volt.Design("library")
    library = volt.Library("volt.test")
    sensor = library.component(
        "Sensor",
        pins=[
            volt.PinSpec("VDD", 1, role="power", terminal="power", direction="input"),
            volt.PinSpec("OUT", 2, role="output", terminal="signal", direction="output"),
            volt.PinSpec("GND", 3, role="ground", terminal="ground", direction="passive"),
        ],
        properties={"category": "sensor"},
        physical_part=volt.PhysicalPartSpec.same_numbered(
            manufacturer="Example",
            part_number="SENSOR-3",
            package="SOT-23-3",
            footprint=("Package_TO_SOT_SMD", "SOT-23"),
        ),
    )

    u1 = design.instantiate(sensor, ref="U1")
    u2 = design.instantiate(sensor, ref="U2")

    circuit = json.loads(design.to_json())

    assert len(circuit["component_definitions"]) == 1
    definition = circuit["component_definitions"][0]
    assert definition["name"] == "Sensor"
    assert definition["source"] == {
        "namespace": "volt.test",
        "name": "Sensor",
        "version": "1.0.0",
    }
    assert definition["properties"]["category"] == {"type": "string", "value": "sensor"}
    assert [component["reference"] for component in circuit["components"]] == ["U1", "U2"]
    assert u1["OUT"].index == 1
    assert u2["OUT"].index == 4
    assert circuit["components"][0]["selected_physical_part"]["pin_pad_mappings"] == [
        {"pin": "pin_def:0", "pad": "1"},
        {"pin": "pin_def:1", "pad": "2"},
        {"pin": "pin_def:2", "pad": "3"},
    ]


def test_library_component_schematic_symbol_default_is_definition_owned():
    design = volt.Design("library-symbol")
    library = volt.Library("volt.test")
    symbol = _two_pin_test_symbol("volt.test:Sensor")
    sensor = library.component(
        "Sensor",
        pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        schematic_symbol=symbol,
    )

    u1 = design.instantiate(sensor, ref="U1")
    schematic = design.schematic("Main")
    schematic.place(u1, at=(10, 20))

    circuit = json.loads(design.to_json())
    projection = json.loads(schematic.to_json())

    assert circuit["component_definitions"][0]["schematic_symbols"] == [
        {"name": "volt.test:Sensor", "variant": "default"}
    ]
    assert u1.schematic_symbol == symbol
    assert projection["symbol_definitions"][0]["name"] == "volt.test:Sensor"
    assert projection["symbol_instances"][0]["symbol_definition"] == "symbol_def:0"


def test_module_instance_component_resolves_library_symbol_default():
    design = volt.Design("module-library-symbol")
    library = volt.Library("volt.test")
    symbol = _two_pin_test_symbol("volt.test:Sensor")
    sensor = library.component(
        "Sensor",
        pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        schematic_symbol=symbol,
    )

    module = design.define_module("SensorBlock")
    module.instantiate(sensor, ref="U1")

    block = design.instantiate(module, ref="BLOCK_A")
    u1 = block.component("U1")
    schematic = design.schematic("Main")
    schematic.place(u1, at=(10, 20))

    projection = json.loads(schematic.to_json())

    assert u1.schematic_symbol == symbol
    assert projection["symbol_definitions"][0]["name"] == "volt.test:Sensor"
    assert projection["symbol_instances"][0]["component"] == "component:0"


def test_schematic_placement_can_select_symbol_variant_from_component_default():
    design = volt.Design("library-symbol-variant")
    library = volt.Library("volt.test")
    horizontal = _two_pin_test_symbol("volt.test:Sensor")
    vertical = _two_pin_test_symbol(
        "volt.test:SensorVertical", variant="vertical", label="VERT"
    )
    sensor = library.component(
        "Sensor",
        pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        schematic_symbol=(horizontal, vertical),
    )

    u1 = design.instantiate(sensor, ref="U1")
    schematic = design.schematic("Main")
    schematic.place(u1, at=(10, 20), variant="vertical")

    circuit = json.loads(design.to_json())
    projection = json.loads(schematic.to_json())

    assert circuit["component_definitions"][0]["schematic_symbols"] == [
        {"name": "volt.test:Sensor", "variant": "default"},
        {"name": "volt.test:SensorVertical", "variant": "vertical"},
    ]
    assert u1.schematic_symbol_variant("vertical") == vertical
    assert projection["symbol_definitions"][1]["name"] == "volt.test:SensorVertical"
    assert projection["symbol_instances"][0]["symbol_definition"] == "symbol_def:1"


def test_schematic_placement_rejects_unknown_component_symbol_variant():
    design = volt.Design("library-symbol-missing-variant")
    library = volt.Library("volt.test")
    sensor = library.component(
        "Sensor",
        pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        schematic_symbol=_two_pin_test_symbol("volt.test:Sensor"),
    )

    u1 = design.instantiate(sensor, ref="U1")
    schematic = design.schematic("Main")

    try:
        schematic.place(u1, at=(10, 20), variant="vertical")
    except ValueError as error:
        assert "No schematic symbol found for variant 'vertical'" in str(error)
    else:
        raise AssertionError("missing schematic symbol variants should be rejected")


def test_schematic_symbol_name_conflicts_reject_different_definitions():
    design = volt.Design("library-symbol-conflict")
    library = volt.Library("volt.test")
    first = library.component(
        "SensorA",
        pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        schematic_symbol=_two_pin_test_symbol("volt.test:Sensor", label="A"),
    )
    second = library.component(
        "SensorB",
        pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        schematic_symbol=_two_pin_test_symbol("volt.test:Sensor", label="B"),
    )

    design.instantiate(first, ref="U1")
    try:
        design.instantiate(second, ref="U2")
    except ValueError as error:
        assert "already exists with a different definition" in str(error)
    else:
        raise AssertionError("conflicting schematic symbol definitions should be rejected")


def test_schematic_placement_rejects_symbol_with_unknown_component_pin():
    design = volt.Design("bad-symbol")
    library = volt.Library("volt.test")
    bad_symbol = volt.SchematicSymbolSpec(
        "volt.test:Sensor",
        pins=(volt.SchematicSymbolSpec.pin("BOGUS", 99, (0, 0), "Left"),),
        primitives=(volt.SchematicSymbolSpec.line((0, 0), (10, 0)),),
    )
    sensor = library.component(
        "Sensor",
        pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        schematic_symbol=bad_symbol,
    )

    u1 = design.instantiate(sensor, ref="U1")
    schematic = design.schematic("Main")

    try:
        schematic.place(u1, at=(10, 20))
    except RuntimeError as error:
        assert "symbol pin does not match component pin" in str(error)
    else:
        raise AssertionError("incompatible schematic symbol should be rejected")


def test_stm32_usb_buck_library_exposes_native_components():
    design = volt.Design("stm32-library")

    mcu = design.instantiate(stm32_usb_buck.STM32F405RGTx, ref="U1")
    usb = design.instantiate(stm32_usb_buck.USB_B_MICRO, ref="J1")
    protection = design.instantiate(stm32_usb_buck.USBLC6_4SC6, ref="U2")
    regulator = design.instantiate(stm32_usb_buck.AP1117_15, ref="U3")

    circuit = json.loads(design.to_json())
    definitions = {definition["name"]: definition for definition in circuit["component_definitions"]}

    assert definitions["STM32F405RGTx"]["source"]["namespace"] == (
        "volt.benchmarks.stm32_usb_buck"
    )
    assert len(definitions["STM32F405RGTx"]["pins"]) == 64
    assert mcu["PA12"].index == 44
    assert usb["D+"].index == 66
    assert protection["VBUS"].index == 74
    assert regulator["VO"].index == 77

    stm32_part = circuit["components"][0]["selected_physical_part"]
    assert stm32_part["manufacturer_part"] == {
        "manufacturer": "STMicroelectronics",
        "part_number": "STM32F405RGT6",
    }
    assert stm32_part["footprint"] == {
        "library": "Package_QFP",
        "name": "LQFP-64_10x10mm_P0.5mm",
    }
    assert stm32_part["pin_pad_mappings"][44] == {"pin": "pin_def:44", "pad": "45"}


def test_repeated_pin_labels_require_explicit_single_pin_addressing():
    design = volt.Design("repeated-pins")
    package = design.define_component(
        "RepeatedSupply",
        pins=[
            volt.PinSpec("VDD", 19, role="power", terminal="power", direction="input"),
            volt.PinSpec("VDD", 32, role="power", terminal="power", direction="input"),
            volt.PinSpec("GPIO", 1, role="bidirectional"),
        ],
    )
    u1 = design.instantiate(package, ref="U1")

    try:
        u1["VDD"]
    except ValueError as error:
        assert "ambiguous" in str(error)
        assert "pins('VDD')" in str(error)
    else:
        raise AssertionError("repeated pin label should require explicit addressing")

    assert u1[19].index == 0
    assert u1["VDD_32"].index == 1
    assert u1["GPIO"].index == 2


def test_repeated_pin_group_connects_all_matching_package_pins():
    design = volt.Design("repeated-group")
    package = design.define_component(
        "RepeatedSupply",
        pins=[
            volt.PinSpec("VDD", 19, role="power", terminal="power", direction="input"),
            volt.PinSpec("VDD", 32, role="power", terminal="power", direction="input"),
            volt.PinSpec("VSS", 18, role="ground", terminal="ground", direction="passive"),
            volt.PinSpec("VSS", 63, role="ground", terminal="ground", direction="passive"),
        ],
    )
    u1 = design.instantiate(package, ref="U1")

    vdd = design.net("VDD", kind="power")
    gnd = design.net("GND", kind="ground")
    vdd += u1.pins("VDD")
    gnd += u1.pins("VSS")

    circuit = json.loads(design.to_json())
    nets = {net["name"]: net for net in circuit["nets"]}

    assert len(u1.pins("VDD")) == 2
    assert nets["VDD"]["pins"] == ["pin:0", "pin:1"]
    assert nets["GND"]["pins"] == ["pin:2", "pin:3"]


def test_stm32_repeated_supply_groups_connect_without_bespoke_code():
    design = volt.Design("stm32-repeated-supplies")
    mcu = design.instantiate(stm32_usb_buck.STM32F405RGTx, ref="U1")

    vdd = design.net("VDD", kind="power", voltage=3.3)
    gnd = design.net("GND", kind="ground")
    vdd += mcu.pins("VDD")
    gnd += mcu.pins("VSS")

    circuit = json.loads(design.to_json())
    nets = {net["name"]: net for net in circuit["nets"]}

    assert [pin.index for pin in mcu.pins("VDD")] == [18, 31, 47, 63]
    assert [pin.index for pin in mcu.pins("VSS")] == [17, 62]
    assert nets["VDD"]["pins"] == ["pin:18", "pin:31", "pin:47", "pin:63"]
    assert nets["GND"]["pins"] == ["pin:17", "pin:62"]


def test_pin_spec_electrical_semantics_are_kernel_owned():
    design = volt.Design("pin-semantics")

    timer = design.define_component(
        "Timer",
        pins=[
            volt.PinSpec(
                "RESET",
                4,
                role="input",
                terminal="signal",
                direction="input",
                signal="digital",
                drive="high_impedance",
                polarity="active_low",
                voltage_range=(0.0, 5.5),
            ),
            volt.PinSpec(
                "VCC",
                8,
                role="power",
                terminal="power",
                direction="input",
                voltage_range=(4.5, 16.0),
            ),
            volt.PinSpec("GND", 1, role="ground", terminal="ground", direction="passive"),
        ],
    )
    design.instantiate(timer, ref="U1")

    circuit = json.loads(design.to_json())
    pin_definitions = {pin["name"]: pin for pin in circuit["pin_definitions"]}

    reset = pin_definitions["RESET"]
    assert reset["terminal_kind"] == "Signal"
    assert reset["direction"] == "Input"
    assert reset["signal_domain"] == "Digital"
    assert reset["drive_kind"] == "HighImpedance"
    assert reset["polarity"] == "ActiveLow"
    assert reset["electrical_attributes"]["voltage_range"] == {
        "type": "range",
        "dimension": "voltage",
        "minimum": 0.0,
        "maximum": 5.5,
    }

    assert pin_definitions["VCC"]["terminal_kind"] == "Power"
    assert pin_definitions["VCC"]["electrical_attributes"]["voltage_range"]["minimum"] == 4.5
    assert pin_definitions["GND"]["terminal_kind"] == "Ground"


def test_component_selected_part_serializes():
    design = volt.Design("selected-part")
    r1 = design.R(resistance=330, tolerance=0.01, ref="R1")

    r1.select_part(
        manufacturer="Yageo",
        part_number="RC0603FR-07330RL",
        package="0603",
        footprint=("Resistor_SMD", "R_0603_1608Metric"),
        pin_pads={
            1: "1",
            2: "2",
        },
        properties={
            "supplier": "Digi-Key",
        },
        voltage_rating=75,
        power_rating=0.1,
    )

    circuit = json.loads(design.to_json())
    resistor = next(
        component for component in circuit["components"] if component["reference"] == "R1"
    )
    part = resistor["selected_physical_part"]

    assert part["manufacturer_part"] == {
        "manufacturer": "Yageo",
        "part_number": "RC0603FR-07330RL",
    }
    assert part["package"] == "0603"
    assert part["footprint"] == {
        "library": "Resistor_SMD",
        "name": "R_0603_1608Metric",
    }
    assert part["pin_pad_mappings"] == [
        {"pin": "pin_def:0", "pad": "1"},
        {"pin": "pin_def:1", "pad": "2"},
    ]
    assert part["properties"]["supplier"] == {"type": "string", "value": "Digi-Key"}
    assert part["electrical_attributes"]["voltage_rating"] == {
        "type": "quantity",
        "dimension": "voltage",
        "value": 75.0,
    }
    assert part["electrical_attributes"]["power_rating"] == {
        "type": "quantity",
        "dimension": "power",
        "value": 0.1,
    }


def test_custom_component_selected_part_accepts_named_pin_mappings():
    design = volt.Design("selected-custom")
    opamp = design.define_component(
        "OpAmp",
        pins=[
            volt.PinSpec("OUT", 1, role="output"),
            volt.PinSpec("IN-", 2, role="input"),
            volt.PinSpec("IN+", 3, role="input"),
            volt.PinSpec("V-", 4, role="power"),
            volt.PinSpec("V+", 8, role="power"),
        ],
    )
    u1 = design.instantiate(opamp, ref="U1")

    u1.select_part(
        manufacturer="Texas Instruments",
        part_number="TLV9002IDR",
        package="SOIC-8",
        footprint=("Package_SO", "SOIC-8_3.9x4.9mm_P1.27mm"),
        pin_pads={
            "OUT": "1",
            "IN-": "2",
            "IN+": "3",
            "V-": "4",
            "V+": "8",
        },
        voltage_rating=5.5,
    )

    circuit = json.loads(design.to_json())
    part = circuit["components"][0]["selected_physical_part"]

    assert part["manufacturer_part"]["manufacturer"] == "Texas Instruments"
    assert part["manufacturer_part"]["part_number"] == "TLV9002IDR"
    assert part["pin_pad_mappings"] == [
        {"pin": "pin_def:0", "pad": "1"},
        {"pin": "pin_def:1", "pad": "2"},
        {"pin": "pin_def:2", "pad": "3"},
        {"pin": "pin_def:3", "pad": "4"},
        {"pin": "pin_def:4", "pad": "8"},
    ]
    assert part["electrical_attributes"]["voltage_rating"] == {
        "type": "quantity",
        "dimension": "voltage",
        "value": 5.5,
    }


def test_selected_part_mapping_errors_are_rejected():
    design = volt.Design("bad-part")
    r1 = design.R(ref="R1")

    try:
        r1.select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-07330RL",
            package="0603",
            footprint=("Resistor_SMD", "R_0603_1608Metric"),
            pin_pads={1: "1"},
        )
    except RuntimeError:
        pass
    else:
        raise AssertionError("missing pin mapping should be rejected")

    try:
        r1.select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-07330RL",
            package="0603",
            footprint=("Resistor_SMD", "R_0603_1608Metric"),
            pin_pads={1: "1", 2: "1"},
        )
    except ValueError:
        pass
    else:
        raise AssertionError("duplicate pad mapping should be rejected")

    try:
        r1.select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-07330RL",
            package="0603",
            footprint=("Resistor_SMD", "R_0603_1608Metric"),
            pin_pads={1: "1", "1": "2"},
        )
    except ValueError:
        pass
    else:
        raise AssertionError("duplicate logical pin mapping should be rejected")

    try:
        r1.select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-07330RL",
            package="0603",
            footprint=("Resistor_SMD", "R_0603_1608Metric"),
            pin_pads={1: "1", "BOGUS": "2"},
        )
    except IndexError:
        pass
    else:
        raise AssertionError("unknown pin mapping should be rejected")


def test_invalid_selected_part_rating_does_not_select_part():
    design = volt.Design("bad-rating")

    try:
        design.C(ref="C1", voltage_rating=float("inf"))
    except ValueError:
        pass
    else:
        raise AssertionError("non-finite capacitor voltage rating should be rejected")

    capacitor = json.loads(design.to_json())["components"][0]
    assert capacitor["reference"] == "C1"
    assert "selected_physical_part" not in capacitor

    r1 = design.R(ref="R1")

    try:
        r1.select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-07330RL",
            package="0603",
            footprint=("Resistor_SMD", "R_0603_1608Metric"),
            pin_pads={1: "1", 2: "2"},
            voltage_rating=float("inf"),
        )
    except ValueError:
        pass
    else:
        raise AssertionError("non-finite selected-part rating should be rejected")

    circuit = json.loads(design.to_json())
    assert "selected_physical_part" not in circuit["components"][0]


def test_voltage_rating_diagnostic_is_inspectable():
    design = volt.Design("rating")
    vdd = design.net("VDD", kind="power", voltage=5.0)
    c1 = design.C(capacitance=100e-9, ref="C1")
    c1.select_part(
        manufacturer="Example",
        part_number="LOW-VOLTAGE-CAP",
        package="0603",
        footprint=("Capacitor_SMD", "C_0603"),
        pin_pads={1: "1", 2: "2"},
        voltage_rating=3.3,
    )

    vdd += c1[1]

    report = design.validate()

    assert report.has_errors
    assert "SELECTED_PART_VOLTAGE_RATING_EXCEEDED" in {
        diagnostic.code for diagnostic in report
    }


def test_pin_voltage_range_diagnostic_is_inspectable():
    design = volt.Design("pin-voltage")
    load = design.define_component(
        "Load",
        pins=[
            volt.PinSpec(
                "VCC",
                1,
                role="power",
                terminal="power",
                direction="input",
                voltage_range=(1.8, 3.6),
            ),
        ],
    )
    source = design.define_component(
        "Supply",
        pins=[
            volt.PinSpec(
                "OUT",
                1,
                role="power_output",
                terminal="power",
                direction="output",
            ),
        ],
    )
    u1 = design.instantiate(load, ref="U1")
    u2 = design.instantiate(source, ref="U2")

    vdd = design.net("VDD", kind="power", voltage=5.0)
    vdd += u1["VCC"], u2["OUT"]

    report = design.validate()

    assert report.has_errors
    assert "PIN_VOLTAGE_RANGE_VIOLATION" in {diagnostic.code for diagnostic in report}


def test_pcb_readiness_requires_selected_physical_parts():
    design = volt.Design("pcb-readiness")
    r1 = design.R("10k", ref="R1")
    signal = design.net("SIGNAL")
    signal += r1[1]

    logical_report = design.validate()
    pcb_report = design.validate_for_pcb()

    assert "PHYSICAL_PART_REQUIRED" not in {diagnostic.code for diagnostic in logical_report}
    assert "PHYSICAL_PART_REQUIRED" in {diagnostic.code for diagnostic in pcb_report}


def test_power_pin_semantics_drive_diagnostics():
    design = volt.Design("typed-power")
    load = design.define_component(
        "Load",
        pins=[
            volt.PinSpec(
                "VCC",
                1,
                role="power",
                terminal="power",
                direction="input",
                voltage_range=(3.0, 3.6),
            ),
            volt.PinSpec("GND", 2, role="ground", terminal="ground", direction="passive"),
        ],
    )
    u1 = design.instantiate(load, ref="U1")

    vcc = design.net("VCC", kind="power", voltage=3.3)
    vcc += u1["VCC"]
    gnd = design.net("GND", kind="ground")
    gnd += u1["GND"]

    report = design.validate()

    assert "POWER_INPUT_WITHOUT_SOURCE" in {diagnostic.code for diagnostic in report}


def test_module_authoring_serializes_kernel_owned_contents():
    design = volt.Design("module-authoring")
    resistor = design.define_component(
        "Resistor",
        pins=[
            volt.PinSpec("1", 1),
            volt.PinSpec("2", 2),
        ],
    )

    divider = design.define_module("Divider")
    vin = divider.port("VIN", kind="power", role="power_input")
    out = divider.port("OUT")
    r1 = divider.instantiate(resistor, ref="R1")
    vin += r1[1]
    out += r1[2]

    vbat = design.net("VBAT", kind="power", voltage=12.0)
    sense = design.net("SENSE")
    div_a = design.instantiate(divider, ref="DIV_A")
    vbat += div_a["VIN"]
    sense += div_a["OUT"]

    circuit = json.loads(design.to_json())

    module = circuit["module_definitions"][0]
    assert module["name"] == "Divider"
    assert module["components"] == [
        {
            "id": "module_component:0",
            "definition": "component_def:0",
            "reference": "R1",
            "properties": {},
        }
    ]
    assert module["connections"] == [
        {
            "net": "template_net:0",
            "component": "module_component:0",
            "pin": "pin_def:0",
        },
        {
            "net": "template_net:1",
            "component": "module_component:0",
            "pin": "pin_def:1",
        },
    ]

    instance = circuit["module_instances"][0]
    assert instance["name"] == "DIV_A"
    assert instance["component_origins"] == [
        {"template_component": "module_component:0", "component": "component:0"}
    ]
    assert instance["port_bindings"] == [
        {"port": "port:0", "parent_net": "net:0"},
        {"port": "port:1", "parent_net": "net:1"},
    ]

    component = div_a.component("R1")
    assert circuit["components"][component.index]["reference"] == "DIV_A/R1"


def test_module_authoring_exposes_hierarchy_inspection_views():
    design = volt.Design("module-inspection")
    resistor = design.define_component(
        "Resistor",
        pins=[
            volt.PinSpec("1", 1),
            volt.PinSpec("2", 2),
        ],
    )

    divider = design.define_module("Divider")
    vin = divider.port("VIN", kind="power", role="power_input")
    out = divider.port("OUT")
    r1 = divider.instantiate(resistor, ref="R1")
    vin += r1[1]
    out += r1[2]

    parent_vin = design.net("VIN", kind="power")
    parent_out = design.net("OUT")
    div_a = design.instantiate(divider, ref="DIV_A")
    parent_vin += div_a["VIN"]
    parent_out += div_a["OUT"]

    assert divider.template_nets() == (
        volt.TemplateNetInfo(index=0, name="VIN", kind="Power"),
        volt.TemplateNetInfo(index=1, name="OUT", kind="Signal"),
    )
    assert divider.ports() == (
        volt.PortInfo(
            index=0, name="VIN", internal_net=0, role="PowerInput", required=True
        ),
        volt.PortInfo(index=1, name="OUT", internal_net=1, role="Passive", required=True),
    )
    assert divider.components() == (
        volt.ModuleComponentInfo(index=0, definition=0, reference="R1"),
    )
    assert divider.connections() == (
        volt.ModuleConnectionInfo(net=0, component=0, pin_definition=0),
        volt.ModuleConnectionInfo(net=1, component=0, pin_definition=1),
    )
    assert div_a.net_origins() == (
        volt.ModuleNetOriginInfo(template_net=0, net=2),
        volt.ModuleNetOriginInfo(template_net=1, net=3),
    )
    assert div_a.component_origins() == (
        volt.ModuleComponentOriginInfo(module_component=0, component=0),
    )
    assert div_a.port_bindings() == (
        volt.PortBindingInfo(port=0, internal_net=2, parent_net=0),
        volt.PortBindingInfo(port=1, internal_net=3, parent_net=1),
    )


def test_python_schematic_placement_serializes_kernel_projection():
    design = volt.Design("schematic-placement")
    vcc = design.net("VCC", kind="power")
    r1 = design.R(resistance=330, ref="R1")
    d1 = design.LED(ref="D1")

    schematic = design.schematic("Main")
    first = schematic.place(r1, at=(40, 20), symbol="resistor")
    second = schematic.place(d1, at=(90, 20), symbol="led")
    wire = schematic.wire(vcc, [(20, 20), (40, 20)])
    label = schematic.label(vcc, at=(20, 16))

    projection = json.loads(schematic.to_json())

    assert first.index == 0
    assert second.index == 1
    assert wire.index == 0
    assert label.index == 0
    assert projection["format"] == "volt.schematic"
    assert projection["sheets"] == [
        {
            "id": "sheet:0",
            "name": "Main",
            "metadata": {
                "title": "Main",
                "size": {"width": 297.0, "height": 210.0},
                "title_block": [],
            },
            "symbol_instances": ["symbol_instance:0", "symbol_instance:1"],
            "wire_runs": ["wire_run:0"],
            "net_labels": ["net_label:0"],
            "junctions": [],
            "power_ports": [],
            "no_connect_markers": [],
            "sheet_ports": [],
            "symbol_fields": [],
        }
    ]
    assert [symbol["name"] for symbol in projection["symbol_definitions"]] == [
        "resistor",
        "led",
    ]
    assert projection["symbol_instances"] == [
        {
            "id": "symbol_instance:0",
            "sheet": "sheet:0",
            "symbol_definition": "symbol_def:0",
            "component": "component:0",
            "position": {"x": 40.0, "y": 20.0},
            "orientation": "Right",
        },
        {
            "id": "symbol_instance:1",
            "sheet": "sheet:0",
            "symbol_definition": "symbol_def:1",
            "component": "component:1",
            "position": {"x": 90.0, "y": 20.0},
            "orientation": "Right",
        },
    ]
    assert projection["wire_runs"] == [
        {
            "id": "wire_run:0",
            "sheet": "sheet:0",
            "net": "net:0",
            "points": [{"x": 20.0, "y": 20.0}, {"x": 40.0, "y": 20.0}],
            "route_intent": "Direct",
        }
    ]
    assert projection["net_labels"] == [
        {
            "id": "net_label:0",
            "sheet": "sheet:0",
            "net": "net:0",
            "position": {"x": 20.0, "y": 16.0},
            "orientation": "Right",
        }
    ]
    assert projection["junctions"] == []
    assert projection["power_ports"] == []
    assert projection["no_connect_markers"] == []
    assert projection["sheet_ports"] == []
    assert projection["symbol_fields"] == []


def test_python_schematic_json_round_trips_as_design_document():
    design = volt.Design("schematic-round-trip")
    vcc = design.net("VCC", kind="power")
    r1 = design.R(resistance=330, ref="R1")

    schematic = design.schematic("Main")
    schematic.place(r1, at=(40, 20), symbol="resistor")
    schematic.wire(vcc, [(20, 20), (40, 20)])
    schematic.label(vcc, at=(20, 16))

    json_text = schematic.to_json()

    loaded = design.load_schematic_json(json_text)

    assert loaded.name == "Main"
    assert json.loads(loaded.to_json()) == json.loads(json_text)
    assert not loaded.validate().has_errors
    assert '<polyline class="wire-run" data-net="net:0" points="20,20 40,20"/>' in loaded.to_svg()

    with TemporaryDirectory() as directory:
        path = Path(directory) / "schematic.volt.json"
        loaded.write_json(path)
        assert json.loads(path.read_text(encoding="utf-8")) == json.loads(json_text)
        reloaded = design.load_schematic(path)
        assert json.loads(reloaded.to_json()) == json.loads(json_text)


def test_python_schematic_json_load_rejects_stale_logical_ids():
    design = volt.Design("schematic-stale-reference")
    vcc = design.net("VCC", kind="power")
    r1 = design.R(resistance=330, ref="R1")

    schematic = design.schematic("Main")
    schematic.place(r1, at=(40, 20), symbol="resistor")
    schematic.wire(vcc, [(20, 20), (40, 20)])

    projection = json.loads(schematic.to_json())
    projection["wire_runs"][0]["net"] = "net:99"

    try:
        design.load_schematic_json(json.dumps(projection))
    except RuntimeError as error:
        assert str(error) == "Net reference points to a missing logical net: net:99"
    else:
        raise AssertionError("stale schematic logical references must fail at load time")


def test_python_schematic_symbol_handles_expose_pin_anchors():
    design = volt.Design("schematic-anchors")
    r1 = design.R(resistance=330, ref="R1")

    schematic = design.schematic("Main")
    symbol = schematic.place(r1, at=(40, 20), symbol="resistor")

    assert symbol.pin_anchor(1) == (40.0, 20.0)
    assert symbol.pin_anchor("2") == (60.0, 20.0)


def test_python_schematic_drawing_place_returns_authoring_handle_with_core_anchors():
    design = volt.Design("schematic-authoring-handle")
    vcc = design.net("VCC", kind="power")
    gnd = design.net("GND", kind="ground")
    r1 = design.R("10k", ref="R1")
    vcc += r1[1]
    gnd += r1[2]

    schematic = design.schematic("Main")
    logical_before = design.to_json()
    with schematic.drawing(at=(40, 20), direction="down", unit=20) as drawing:
        resistor = drawing.place(r1)

    assert resistor.symbol.index == 0
    assert resistor.index == resistor.symbol.index
    assert resistor.component is r1
    assert resistor.orientation == "Down"
    assert resistor.start.point == (40.0, 20.0)
    assert resistor.end.point == (40.0, 40.0)
    assert resistor.center.point == (40.0, 30.0)
    assert resistor["1"].pin.index == r1[1].index
    assert resistor[2].point == resistor.end.point
    assert resistor.pin("2").point == resistor.end.point
    assert tuple(anchor.number for anchor in resistor.pin_anchors()) == ("1", "2")

    vcc_port = schematic.power("VCC", net=vcc, at=resistor.start.left(20))
    gnd_port = schematic.ground(net=gnd, at=resistor.end.right(20))
    schematic.wire(vcc).from_(vcc_port).to(resistor.start).orthogonal()
    schematic.wire(gnd, points=(resistor.end, gnd_port.pin))

    assert design.to_json() == logical_before


def test_python_schematic_drawing_handle_resolves_pin_names_as_attributes_and_items():
    design = volt.Design("schematic-authoring-handle-names")
    component = design.define_component(
        "Probe",
        pins=[
            volt.PinSpec("LEFT", 1),
            volt.PinSpec("DATA+", 2),
        ],
        schematic_symbol=volt.SchematicSymbolSpec(
            "test:probe",
            pins=(
                volt.SchematicSymbolSpec.pin("left_pin", 1, (0, 0), "Left"),
                volt.SchematicSymbolSpec.pin("DATA+", 2, (20, 0), "Right"),
            ),
            primitives=(volt.SchematicSymbolSpec.line((0, 0), (20, 0)),),
        ),
    )
    p1 = design.instantiate(component, ref="P1")

    with design.schematic("Main").drawing(at=(10, 5)) as drawing:
        probe = drawing.place(p1)

    assert probe.left_pin.number == "1"
    assert probe["left_pin"].point == (10.0, 5.0)
    assert probe["DATA+"].number == "2"
    assert probe["2"].name == "DATA+"


def test_python_schematic_drawing_handle_reports_ambiguous_attribute_names():
    design = volt.Design("schematic-authoring-handle-ambiguous")
    component = design.define_component(
        "RepeatedSupply",
        pins=[
            volt.PinSpec("VDD", 19, role="power"),
            volt.PinSpec("VDD", 32, role="power"),
        ],
        schematic_symbol=volt.SchematicSymbolSpec(
            "test:repeated-supply",
            pins=(
                volt.SchematicSymbolSpec.pin("VDD", 19, (0, 0), "Left"),
                volt.SchematicSymbolSpec.pin("VDD", 32, (20, 0), "Right"),
            ),
            primitives=(volt.SchematicSymbolSpec.line((0, 0), (20, 0)),),
        ),
    )
    u1 = design.instantiate(component, ref="U1")

    with design.schematic("Main").drawing(at=(0, 0)) as drawing:
        handle = drawing.place(u1)

    try:
        handle.VDD
    except ValueError as error:
        message = str(error)
        assert "ambiguous" in message
        assert "bracket" in message
        assert "pin number" in message
    else:
        raise AssertionError("ambiguous pin names should not be exposed as attributes")

    assert handle[19].number == "19"
    assert handle["32"].number == "32"


def test_python_schematic_dsl_authors_anchors_routes_and_semantic_objects():
    design = volt.Design("schematic-dsl")
    vcc = design.net("VCC", kind="power")
    led_a = design.net("LED_A")
    gnd = design.net("GND", kind="ground")
    r1 = design.R("330 ohm", ref="R1")
    d1 = design.LED(ref="D1")
    test_point = design.define_component(
        "TestPoint",
        pins=[volt.PinSpec("NC", 1, requirement="optional")],
        schematic_symbol=volt.SchematicSymbolSpec(
            "test:point",
            pins=(volt.SchematicSymbolSpec.pin("NC", 1, (0, 0), "Right"),),
            primitives=(volt.SchematicSymbolSpec.circle((0, 0), 1.5),),
        ),
    )
    tp1 = design.instantiate(test_point, ref="TP1")

    vcc += r1[1]
    led_a += r1[2], d1["A"]
    gnd += d1["K"]
    assert {net.name for net in design.nets()} >= {"VCC", "LED_A", "GND"}
    assert tuple(pin.index for pin in led_a.pins()) == (r1[2].index, d1["A"].index)

    schematic = design.schematic("Main")
    resistor = schematic.place(r1, at=(40, 20), orient="down", symbol="resistor")
    led = schematic.place(d1, at=(90, 30), symbol="led")
    no_connect_target = schematic.place(tp1, at=(130, 70), symbol="test:point")

    r1_left = resistor.pin(1)
    r1_right = resistor.pin("2")
    led_anode = led.pin("A")
    led_cathode = led.pin(1)
    no_connect_pin = no_connect_target.pin("NC")

    assert resistor.orientation == "Down"
    assert r1_left.point == (40.0, 20.0)
    assert r1_left.pin.index == r1[1].index
    assert r1_right.point == (40.0, 40.0)
    assert tuple(anchor.number for anchor in resistor.pin_anchors()) == ("1", "2")
    assert resistor.pin_anchor("2") == r1_right.point
    assert led_anode.point == (110.0, 30.0)
    assert led_cathode.down(30).point == (90.0, 60.0)

    vcc_port = schematic.power("VCC", net=vcc, at=r1_left.left(20))
    ground_port = schematic.ground(net=gnd, at=led_cathode.down(30), orient="down")
    sheet_port = schematic.off_page("LED_A", net=led_a, at=led_anode.right(20))
    label = schematic.label(led_a, at=r1_right.right(8), orient="left")
    schematic.junction(led_a, at=r1_right.right(35))
    schematic.no_connect(no_connect_pin, reason="open test pad")

    schematic.wire(vcc).from_(vcc_port).to(r1_left).orthogonal()
    schematic.wire(led_a).from_(r1_right).to(led_anode).orthogonal()
    schematic.wire(gnd).from_(led_cathode).to(ground_port).orthogonal()
    schematic.wire(led_a).from_(r1_right).via(r1_right.right(30)).to(led_anode).orthogonal()
    schematic.wire(led_a, points=(sheet_port.pin, led_anode.right(8)))

    logical = json.loads(design.to_json())
    projection = json.loads(schematic.to_json())

    assert logical["nets"][0]["pins"] == [f"pin:{r1[1].index}"]
    assert logical["nets"][1]["pins"] == [f"pin:{r1[2].index}", f"pin:{d1['A'].index}"]
    assert logical["nets"][2]["pins"] == [f"pin:{d1['K'].index}"]
    assert logical.get("design_intent", {}).get("no_connect_pins", []) == []

    assert projection["symbol_instances"][0]["orientation"] == "Down"
    assert projection["wire_runs"][0]["route_intent"] == "Orthogonal"
    assert projection["wire_runs"][1]["points"] == [
        {"x": 40.0, "y": 40.0},
        {"x": 110.0, "y": 40.0},
        {"x": 110.0, "y": 30.0},
    ]
    assert projection["wire_runs"][3]["route_intent"] == "Orthogonal"
    assert projection["wire_runs"][3]["points"] == [
        {"x": 40.0, "y": 40.0},
        {"x": 70.0, "y": 40.0},
        {"x": 110.0, "y": 30.0},
    ]
    assert projection["wire_runs"][4]["route_intent"] == "Direct"
    assert projection["net_labels"][0]["orientation"] == "Left"
    assert label.orientation == "Left"
    assert projection["junctions"][0]["position"] == {"x": 75.0, "y": 40.0}
    assert projection["power_ports"][0]["kind"] == "Power"
    assert projection["power_ports"][0]["position"] == {"x": 20.0, "y": 20.0}
    assert projection["power_ports"][1]["kind"] == "Ground"
    assert projection["power_ports"][1]["orientation"] == "Down"
    assert projection["no_connect_markers"][0]["pin"] == f"pin:{no_connect_pin.pin.index}"
    assert projection["no_connect_markers"][0]["reason"] == "open test pad"
    assert projection["sheet_ports"][0]["name"] == "LED_A"
    assert projection["sheet_ports"][0]["kind"] == "OffPage"

    svg = schematic.to_svg()
    assert 'class="power-port power"' in svg
    assert 'class="no-connect-marker"' in svg
    assert 'class="sheet-port off-page"' in svg


def test_python_schematic_explicit_wire_points_are_normalized_once():
    design = volt.Design("schematic-wire-normalization")
    vcc = design.net("VCC", kind="power")
    original = volt._schematic_point
    calls = 0

    def counting_schematic_point(value, *, design):
        nonlocal calls
        calls += 1
        return original(value, design=design)

    schematic = design.schematic("Main")
    volt._schematic_point = counting_schematic_point
    try:
        schematic.wire(vcc, points=[(20, 20), (40, 20)])
    finally:
        volt._schematic_point = original

    assert calls == 2


def test_python_schematic_drawing_cursor_defaults_and_moves():
    design = volt.Design("schematic-drawing")
    schematic = design.schematic("Main")

    with schematic.drawing() as drawing:
        assert isinstance(drawing.here, volt.SchematicAnchor)
        assert drawing.here.point == (0.0, 0.0)
        assert drawing.direction == "Right"

        drawing.move(dx=20)
        assert drawing.here.point == (20.0, 0.0)
        assert drawing.direction == "Right"

        drawing.move_from(drawing.here, dy=-10, direction="up")
        assert drawing.here.point == (20.0, -10.0)
        assert drawing.direction == "Up"

    configured = schematic.drawing(at=(5, 6), direction="left", unit=25)
    assert configured.here.point == (5.0, 6.0)
    assert configured.direction == "Left"
    assert configured.unit == 25.0


def test_python_schematic_drawing_move_from_accepts_sheet_points_and_anchors():
    design = volt.Design("schematic-drawing-move-from")
    vcc = design.net("VCC", kind="power")
    schematic = design.schematic("Main")
    port = schematic.power("VCC", net=vcc, at=(10, 15))

    drawing = schematic.drawing()
    drawing.move_from(port, dx=5, dy=-5, direction="down")

    assert drawing.here.point == (15.0, 10.0)
    assert drawing.direction == "Down"

    drawing.move_from(drawing.here.right(10))

    assert drawing.here.point == (25.0, 10.0)
    assert drawing.direction == "Down"


def test_python_schematic_drawing_push_pop_and_hold_restore_cursor_state():
    design = volt.Design("schematic-drawing-stack")
    drawing = design.schematic("Main").drawing(at=(10, 20), direction="Left")

    drawing.push()
    drawing.move_from(drawing.here, dx=30, direction="Down")
    drawing.push()
    drawing.move_from(drawing.here, dy=40, direction="Right")

    drawing.pop()
    assert drawing.here.point == (40.0, 20.0)
    assert drawing.direction == "Down"

    with drawing.hold():
        drawing.move_from(drawing.here, dx=-5, dy=5, direction="Up")
        assert drawing.here.point == (35.0, 25.0)
        assert drawing.direction == "Up"

    assert drawing.here.point == (40.0, 20.0)
    assert drawing.direction == "Down"

    drawing.pop()
    assert drawing.here.point == (10.0, 20.0)
    assert drawing.direction == "Left"

    try:
        drawing.pop()
    except ValueError as error:
        assert str(error) == "Schematic drawing state stack is empty"
    else:
        raise AssertionError("empty schematic drawing state stack should be rejected")


def test_python_schematic_drawing_rejects_invalid_points_and_directions():
    design = volt.Design("schematic-drawing-invalid")
    other = volt.Design("schematic-drawing-other")
    schematic = design.schematic("Main")
    other_anchor = volt.SchematicAnchor((1, 2), design=other)

    try:
        schematic.drawing(at=(0, float("inf")))
    except ValueError as error:
        assert str(error) == "Schematic coordinates must be finite"
    else:
        raise AssertionError("non-finite drawing cursor point should be rejected")

    try:
        schematic.drawing(direction="North")
    except ValueError as error:
        assert str(error) == "Schematic orientation must be Right, Down, Left, or Up"
    else:
        raise AssertionError("invalid drawing direction should be rejected")

    drawing = schematic.drawing()
    try:
        drawing.move_from(other_anchor)
    except ValueError as error:
        assert str(error) == "Schematic anchor belongs to a different design"
    else:
        raise AssertionError("drawing cursor should reject cross-design anchors")


def test_python_schematic_drawing_rejects_invalid_units():
    design = volt.Design("schematic-drawing-invalid-unit")
    schematic = design.schematic("Main")

    non_positive_units = (0, -1)
    non_finite_units = (float("inf"), float("-inf"), float("nan"))
    for invalid_unit in (*non_positive_units, *non_finite_units):
        try:
            schematic.drawing(unit=invalid_unit)
        except ValueError as error:
            expected = (
                "Schematic drawing unit must be positive"
                if invalid_unit in non_positive_units
                else "Schematic coordinates must be finite"
            )
            assert str(error) == expected
        else:
            raise AssertionError("invalid drawing unit should be rejected")


def test_python_schematic_drawing_hold_restores_cursor_after_exception():
    design = volt.Design("schematic-drawing-hold-restore")
    drawing = design.schematic("Main").drawing(at=(2, 3), direction="Right")
    before = drawing.here.point, drawing.direction

    try:
        with drawing.hold():
            drawing.move(dx=10, dy=20)
            drawing.move_from(drawing.here, direction="Down")
            raise RuntimeError("boom")
    except RuntimeError:
        pass

    assert (drawing.here.point, drawing.direction) == before


def test_python_schematic_drawing_hold_restores_cursor_after_nested_push():
    design = volt.Design("schematic-drawing-hold-nested-push")
    drawing = design.schematic("Main").drawing(at=(1, 2), direction="Right")
    before = drawing.here.point, drawing.direction

    with drawing.hold():
        drawing.move(dx=10)
        drawing.push()
        drawing.move(dy=20)

    assert (drawing.here.point, drawing.direction) == before
    try:
        drawing.pop()
    except ValueError as error:
        assert str(error) == "Schematic drawing state stack is empty"
    else:
        raise AssertionError("hold should restore the drawing stack depth")


def test_python_schematic_drawing_session_does_not_mutate_logical_design():
    design = volt.Design("schematic-drawing-logical-boundary")
    design.R("10k", ref="R1")
    schematic = design.schematic("Main")
    before = design.to_json()
    projection_before = schematic.to_json()

    drawing = schematic.drawing()
    drawing.move(dx=20, dy=10)
    drawing.push()
    drawing.move_from((40, 50), direction="Down")
    with drawing.hold():
        drawing.move_from(volt.SchematicAnchor((5, 5), design=design), dx=1, dy=2)
    drawing.pop()

    assert design.to_json() == before
    assert schematic.to_json() == projection_before
    assert json.loads(before)["components"][0]["reference"] == "R1"


def test_python_schematic_dsl_rejects_invalid_references():
    design = volt.Design("schematic-dsl-reference-checks")
    other = volt.Design("other-schematic-dsl-reference-checks")
    vcc = design.net("VCC", kind="power")
    other_vcc = other.net("VCC", kind="power")
    r1 = design.R("10k", ref="R1")
    other_r1 = other.R("10k", ref="R1")

    schematic = design.schematic("Main")
    symbol = schematic.place(r1, at=(40, 20), symbol="resistor")
    other_symbol = other.schematic("Main").place(other_r1, at=(40, 20), symbol="resistor")
    handle = schematic.drawing(at=(80, 20)).place(r1)
    other_handle = other.schematic("Other").drawing(at=(80, 20)).place(other_r1)

    try:
        schematic.power("BAD", net=other_vcc, at=(0, 0))
    except ValueError as error:
        assert str(error) == "Net belongs to a different design"
    else:
        raise AssertionError("power ports must reject nets from another design")

    try:
        schematic.wire(vcc).from_(other_symbol.pin(1)).to(symbol.pin(1)).orthogonal()
    except ValueError as error:
        assert str(error) == "Schematic anchor belongs to a different design"
    else:
        raise AssertionError("wire anchors must reject pins from another design")

    try:
        schematic.no_connect(other_symbol.pin(1))
    except ValueError as error:
        assert str(error) == "Schematic anchor belongs to a different design"
    else:
        raise AssertionError("no-connect markers must reject pins from another design")

    try:
        schematic.wire(vcc).from_(other_handle.start).to(handle.start).orthogonal()
    except ValueError as error:
        assert str(error) == "Schematic anchor belongs to a different design"
    else:
        raise AssertionError("authoring handle anchors must reject pins from another design")


def test_detached_schematic_symbol_pin_helpers_report_missing_component_context():
    design = volt.Design("schematic-detached-symbol")
    r1 = design.R("10k", ref="R1")
    schematic = design.schematic("Main")
    placed = schematic.place(r1, at=(40, 20), symbol="resistor")
    detached = volt.SchematicSymbol(schematic, placed.index)

    try:
        detached.pin(1)
    except ValueError as error:
        assert str(error) == (
            "Schematic pin anchors require the Component handle returned by Schematic.place()"
        )
    else:
        raise AssertionError("detached symbol pin helpers should explain missing component context")


def test_python_schematic_writes_svg_projection():
    design = volt.Design("schematic-svg")
    vcc = design.net("VCC", kind="power")
    r1 = design.R(resistance=330, ref="R1")

    schematic = design.schematic("Main")
    schematic.place(r1, at=(40, 20), symbol="resistor")
    schematic.wire(vcc, [(20, 20), (40, 20)])
    schematic.label(vcc, at=(20, 16))

    svg = schematic.to_svg()

    assert svg.startswith('<svg xmlns="http://www.w3.org/2000/svg"')
    assert 'class="schematic-sheet"' in svg
    assert 'class="symbol-instance"' in svg
    assert 'data-component="component:0"' in svg
    assert '<polyline class="wire-run" data-net="net:0" points="20,20 40,20"/>' in svg
    assert '<text class="net-label" data-net="net:0" x="20" y="16"' in svg
    assert ">VCC</text>" in svg
    assert '<text class="reference" x="0" y="-12">R1</text>' in svg
    assert "pin-anchor" not in svg
    assert "pin-label" not in svg

    with TemporaryDirectory() as directory:
        path = Path(directory) / "schematic.svg"
        schematic.write_svg(path)
        assert path.read_text(encoding="utf-8") == svg


def test_python_schematic_readiness_reports_detached_net_stubs():
    design = volt.Design("schematic-readiness")
    vcc = design.net("VCC", kind="power")
    r1 = design.R(resistance=330, ref="R1")
    vcc += r1[1]

    schematic = design.schematic("Main")
    schematic.place(r1, at=(40, 20), symbol="resistor")
    schematic.wire(vcc, [(0, 12), (10, 12)])
    schematic.label(vcc, at=(0, 10))

    report = schematic.validate()

    assert report.has_errors
    assert len(report) == 1
    diagnostic = report[0]
    assert diagnostic.code == "SCHEMATIC_PIN_NET_NOT_VISUALLY_COVERED"
    assert diagnostic.message == "Schematic omits visual net coverage for R1 pin 1 (1) on VCC"
    assert [(entity.kind, entity.index) for entity in diagnostic.entities] == [
        ("component", r1.index),
        ("pin", r1[1].index),
        ("pin_definition", 0),
        ("net", vcc.index),
    ]


def test_python_schematic_validation_reports_quality_diagnostics():
    design = volt.Design("schematic-quality")
    vcc = design.net("VCC", kind="power")
    r1 = design.R(resistance=330, ref="R1")
    r2 = design.R(resistance=330, ref="R2")
    r3 = design.R(resistance=330, ref="R3")
    vcc += (r1[1], r2[1], r3[1])
    r1[2].mark_no_connect()

    schematic = design.schematic("Main")
    first = schematic.place(r1, at=(40, 20), symbol="resistor")
    second = schematic.place(r2, at=(80, 20), symbol="resistor")
    third = schematic.place(r3, at=(120, 20), symbol="resistor")
    schematic.label(vcc, at=first.pin(1))
    schematic.label(vcc, at=second.pin(1))
    schematic.label(vcc, at=third.pin(1))

    report = schematic.validate()
    codes = {diagnostic.code for diagnostic in report}

    assert "SCHEMATIC_NET_FRAGMENTED_PIN_LABELS" in codes
    assert "SCHEMATIC_NO_CONNECT_INTENT_NOT_MARKED" in codes
    assert not report.has_errors


def test_stm32_usb_buck_native_symbols_place_and_render():
    design = volt.Design("stm32-native-symbols")
    mcu = design.instantiate(stm32_usb_buck.STM32F405RGTx, ref="U1")
    resistor = design.instantiate(stm32_usb_buck.RESISTOR, ref="R1")

    schematic = design.schematic("Main")
    schematic.place(mcu, at=(60, 60))
    schematic.place(resistor, at=(160, 60))

    projection = json.loads(schematic.to_json())

    assert [symbol["name"] for symbol in projection["symbol_definitions"]] == [
        "volt.benchmarks.stm32_usb_buck:STM32F405RGTx",
        "volt.benchmarks.stm32_usb_buck:Resistor",
    ]
    stm32_symbol = projection["symbol_definitions"][0]
    resistor_symbol = projection["symbol_definitions"][1]
    assert len(stm32_symbol["pins"]) == 64
    assert any(pin["name"] == "PA11" and pin["number"] == "44" for pin in stm32_symbol["pins"])
    assert any(primitive["type"] == "rectangle" for primitive in stm32_symbol["primitives"])
    assert len(resistor_symbol["pins"]) == 2
    assert projection["sheets"][0]["symbol_instances"] == [
        "symbol_instance:0",
        "symbol_instance:1",
    ]

    svg = schematic.to_svg()
    assert 'data-component="component:0"' in svg
    assert 'data-component="component:1"' in svg
    assert ">U1</text>" in svg
    assert ">R1</text>" in svg


def test_python_schematic_handles_are_publicly_exported():
    assert "Schematic" in volt.__all__
    assert "SchematicAnchor" in volt.__all__
    assert "SchematicDrawing" in volt.__all__
    assert "PlacedSchematicElement" in volt.__all__
    assert "SchematicJunction" in volt.__all__
    assert "SchematicSymbol" in volt.__all__
    assert "SchematicPinAnchor" in volt.__all__
    assert "SchematicPort" in volt.__all__
    assert "SchematicWire" in volt.__all__
    assert "SchematicWireBuilder" in volt.__all__
    assert "SchematicNetLabel" in volt.__all__
    assert "SchematicNoConnect" in volt.__all__
    assert "SchematicSymbolSpec" in volt.__all__


def test_diagnostics_are_inspectable():
    design = volt.Design("incomplete")
    design.R("10k", ref="R1")

    report = design.validate()
    assert report.has_errors
    assert len(report) == 2
    assert {diagnostic.code for diagnostic in report} == {"UNCONNECTED_REQUIRED_PIN"}
    assert all(diagnostic.severity == "error" for diagnostic in report)
    assert all(diagnostic.entities for diagnostic in report)


if __name__ == "__main__":
    test_led_circuit_validates()
    test_natural_electrical_values_serialize_as_kernel_attributes()
    test_custom_component_definitions_are_kernel_owned()
    test_python_design_intent_serializes_as_kernel_design_intent()
    test_python_stub_net_intent_suppresses_only_intended_net_shape_diagnostics()
    test_python_no_connect_intent_suppresses_only_intended_missing_pin_diagnostics()
    test_library_component_instantiates_kernel_owned_definition_once()
    test_library_component_schematic_symbol_default_is_definition_owned()
    test_module_instance_component_resolves_library_symbol_default()
    test_schematic_placement_can_select_symbol_variant_from_component_default()
    test_schematic_placement_rejects_unknown_component_symbol_variant()
    test_schematic_symbol_name_conflicts_reject_different_definitions()
    test_schematic_placement_rejects_symbol_with_unknown_component_pin()
    test_stm32_usb_buck_library_exposes_native_components()
    test_repeated_pin_labels_require_explicit_single_pin_addressing()
    test_repeated_pin_group_connects_all_matching_package_pins()
    test_stm32_repeated_supply_groups_connect_without_bespoke_code()
    test_pin_spec_electrical_semantics_are_kernel_owned()
    test_component_selected_part_serializes()
    test_custom_component_selected_part_accepts_named_pin_mappings()
    test_selected_part_mapping_errors_are_rejected()
    test_invalid_selected_part_rating_does_not_select_part()
    test_voltage_rating_diagnostic_is_inspectable()
    test_pin_voltage_range_diagnostic_is_inspectable()
    test_pcb_readiness_requires_selected_physical_parts()
    test_power_pin_semantics_drive_diagnostics()
    test_module_authoring_serializes_kernel_owned_contents()
    test_module_authoring_exposes_hierarchy_inspection_views()
    test_python_schematic_placement_serializes_kernel_projection()
    test_python_schematic_symbol_handles_expose_pin_anchors()
    test_python_schematic_drawing_place_returns_authoring_handle_with_core_anchors()
    test_python_schematic_drawing_handle_resolves_pin_names_as_attributes_and_items()
    test_python_schematic_drawing_handle_reports_ambiguous_attribute_names()
    test_python_schematic_dsl_authors_anchors_routes_and_semantic_objects()
    test_python_schematic_explicit_wire_points_are_normalized_once()
    test_python_schematic_drawing_cursor_defaults_and_moves()
    test_python_schematic_drawing_move_from_accepts_sheet_points_and_anchors()
    test_python_schematic_drawing_push_pop_and_hold_restore_cursor_state()
    test_python_schematic_drawing_rejects_invalid_points_and_directions()
    test_python_schematic_drawing_rejects_invalid_units()
    test_python_schematic_drawing_hold_restores_cursor_after_exception()
    test_python_schematic_drawing_hold_restores_cursor_after_nested_push()
    test_python_schematic_drawing_session_does_not_mutate_logical_design()
    test_python_schematic_dsl_rejects_invalid_references()
    test_detached_schematic_symbol_pin_helpers_report_missing_component_context()
    test_python_schematic_writes_svg_projection()
    test_python_schematic_readiness_reports_detached_net_stubs()
    test_python_schematic_validation_reports_quality_diagnostics()
    test_stm32_usb_buck_native_symbols_place_and_render()
    test_python_schematic_handles_are_publicly_exported()
    test_diagnostics_are_inspectable()
