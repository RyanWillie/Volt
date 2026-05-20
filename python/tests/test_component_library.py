import json

import volt
from volt.libraries import stm32_usb_buck

from helpers import (
    _common_catalog_components,
    _definition_for_component,
    _two_pin_test_symbol,
)


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

def test_common_catalog_components_have_namespaced_default_symbol_refs():
    design = volt.Design("common-default-symbols")
    cases = _common_catalog_components(design)

    circuit = json.loads(design.to_json())

    for reference, component, expected_symbol, _expected_numbers in cases:
        assert component.schematic_symbol == expected_symbol
        definition = _definition_for_component(circuit, reference)
        assert definition["schematic_symbols"] == [
            {"name": expected_symbol, "variant": "default"}
        ]

def test_common_catalog_symbols_place_through_drawing_and_render():
    design = volt.Design("common-default-symbol-drawing")
    cases = _common_catalog_components(design)
    schematic = design.schematic("Main")

    placed = []
    with schematic.drawing(at=(20, 20), unit=20) as drawing:
        for index, (_reference, component, _symbol, _numbers) in enumerate(cases):
            placed.append(
                drawing.place(
                    component,
                    at=(20 + (index % 4) * 45, 20 + (index // 4) * 35),
                )
            )

    projection = json.loads(schematic.to_json())
    svg = schematic.to_svg()

    assert [symbol["name"] for symbol in projection["symbol_definitions"]] == [
        expected_symbol for _reference, _component, expected_symbol, _numbers in cases
    ]
    assert len(projection["symbol_instances"]) == len(cases)
    assert all(symbol["primitives"] for symbol in projection["symbol_definitions"])
    assert all(
        tuple(anchor.number for anchor in element.pin_anchors()) == expected_numbers
        for element, (_reference, _component, _symbol, expected_numbers) in zip(placed, cases)
    )
    assert placed[0].start.point == (20.0, 20.0)
    assert placed[0].end.point == (40.0, 20.0)
    assert tuple(anchor.name for anchor in placed[10].pin_anchors()) == ("+", "-")
    assert placed[12].IN.number == "3"
    assert placed[12].OUT.number == "2"
    assert placed[13]["IN+"].number == "3"
    assert placed[13]["IN-"].number == "2"

    for _reference, component, _symbol, _numbers in cases:
        assert f'data-component="component:{component.index}"' in svg
    assert "symbol-line" in svg
    assert "symbol-rectangle" in svg
    assert "symbol-circle" in svg

def test_legacy_common_symbol_names_still_place_and_resolve():
    design = volt.Design("legacy-common-symbol-names")
    schematic = design.schematic("Main")
    placements = [
        ("resistor", design.R("10k", ref="R1"), (20, 20)),
        ("capacitor", design.C("100nF", ref="C1"), (70, 20)),
        ("led", design.LED(ref="D1"), (120, 20)),
        ("connector_1x02", design.connector_1x02(ref="J1"), (170, 20)),
    ]

    placed = [
        schematic.place(component, at=point, symbol=symbol_name)
        for symbol_name, component, point in placements
    ]

    projection = json.loads(schematic.to_json())
    assert [symbol["name"] for symbol in projection["symbol_definitions"]] == [
        symbol_name for symbol_name, _component, _point in placements
    ]
    assert tuple(anchor.number for anchor in placed[0].pin_anchors()) == ("1", "2")
    assert tuple(anchor.number for anchor in placed[1].pin_anchors()) == ("1", "2")
    assert tuple(anchor.number for anchor in placed[2].pin_anchors()) == ("1", "2")
    assert tuple(anchor.name for anchor in placed[3].pin_anchors()) == ("+", "-")
    assert tuple(anchor.number for anchor in placed[3].pin_anchors()) == ("1", "2")

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
        message = str(error)
        assert "No schematic symbol variant 'vertical'" in message
        assert "U1" in message
        assert "sheet 'Main'" in message
    else:
        raise AssertionError("missing schematic symbol variants should be rejected")

def test_schematic_placement_missing_default_symbol_reports_author_context():
    design = volt.Design("library-symbol-missing-default")
    sensor = design.define_component(
        "Sensor",
        pins=[volt.PinSpec("OUT", 1)],
    )
    u1 = design.instantiate(sensor, ref="U1")
    schematic = design.schematic("Main")
    before = schematic.to_json()

    try:
        schematic.place(u1, at=(10, 20))
    except ValueError as error:
        message = str(error)
        assert "No default schematic symbol" in message
        assert "U1" in message
        assert "sheet 'Main'" in message
        assert "pass symbol=" in message
        assert "schematic_symbol=" in message
    else:
        raise AssertionError("missing default schematic symbols should explain the fix")

    assert schematic.to_json() == before

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

def test_default_catalog_symbol_name_conflicts_reject_different_definitions():
    design = volt.Design("default-catalog-symbol-conflict")
    r1 = design.R("10k", ref="R1")
    schematic = design.schematic("Main")
    schematic.place(r1, at=(10, 20))

    try:
        schematic.register_symbol(
            _two_pin_test_symbol("volt.passives:resistor", label="CUSTOM")
        )
    except ValueError as error:
        assert "already exists with a different definition" in str(error)
    else:
        raise AssertionError("default catalog symbol name conflicts should be rejected")

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

def test_schematic_placed_symbol_ambiguous_pin_name_reports_author_context():
    design = volt.Design("schematic-ambiguous-pin-context")
    supply = design.define_component(
        "Supply",
        pins=[
            volt.PinSpec("VDD", 1, role="power"),
            volt.PinSpec("VDD", 2, role="power"),
        ],
        schematic_symbol=volt.SchematicSymbolSpec(
            "volt.test:Supply",
            pins=(
                volt.SchematicSymbolSpec.pin("VDD", 1, (0, 0), "Left"),
                volt.SchematicSymbolSpec.pin("VDD", 2, (20, 0), "Right"),
            ),
            primitives=(volt.SchematicSymbolSpec.line((0, 0), (20, 0)),),
        ),
    )
    u1 = design.instantiate(supply, ref="U1")
    schematic = design.schematic("Main")
    placed = schematic.place(u1, at=(10, 20))

    try:
        placed.pin("VDD")
    except ValueError as error:
        message = str(error)
        assert "ambiguous" in message
        assert "VDD" in message
        assert "U1" in message
        assert "sheet 'Main'" in message
        assert "'1'" in message
        assert "'2'" in message
        assert "pins('VDD')" in message
    else:
        raise AssertionError("ambiguous schematic pin names should carry author context")

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

def test_pcb_readiness_requires_selected_physical_parts():
    design = volt.Design("pcb-readiness")
    r1 = design.R("10k", ref="R1")
    signal = design.net("SIGNAL")
    signal += r1[1]

    logical_report = design.validate()
    pcb_report = design.validate_for_pcb()

    assert "PHYSICAL_PART_REQUIRED" not in {diagnostic.code for diagnostic in logical_report}
    assert "PHYSICAL_PART_REQUIRED" in {diagnostic.code for diagnostic in pcb_report}

def test_stm32_usb_buck_native_symbols_place_and_render():
    design = volt.Design("stm32-native-symbols")
    mcu = design.instantiate(stm32_usb_buck.STM32F405RGTx, ref="U1")
    resistor = design.instantiate(stm32_usb_buck.RESISTOR, ref="R1")

    schematic = design.schematic("Main")
    with schematic.drawing() as drawing:
        drawing.place(mcu, at=(60, 60)).label_ref()
        drawing.place(resistor, at=(160, 60)).label_ref()

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
