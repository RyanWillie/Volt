import hashlib
import json
from pathlib import Path

import volt
from volt.libraries import stm32_usb_buck

from helpers import (
    _common_catalog_components,
    _definition_for_component,
    _two_pin_test_symbol,
)


def test_library_public_symbol_classes_stay_on_public_import_surface():
    import volt.library as library

    assert volt.SchematicSymbolSpec is library.SchematicSymbolSpec
    assert volt.SchematicBlockPinSpec is library.SchematicBlockPinSpec
    assert volt.SchematicSymbolSpec.__module__ == "volt.library"
    assert isinstance(
        library._default_two_terminal_symbol_spec("resistor"),
        library.SchematicSymbolSpec,
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
    assert "role" not in circuit["pin_definitions"][0]
    assert circuit["pin_definitions"][0]["terminal_kind"] == "Power"
    assert circuit["pin_definitions"][0]["direction"] == "Input"
    assert circuit["pin_definitions"][1]["terminal_kind"] == "Signal"
    assert circuit["pin_definitions"][1]["direction"] == "Output"
    assert circuit["pin_definitions"][2]["terminal_kind"] == "Ground"


def test_pin_spec_role_preset_rejects_contradictory_explicit_semantics():
    design = volt.Design("pin-preset-contradiction")

    try:
        design.define_component(
            "Broken",
            pins=[volt.PinSpec("VDD", 1, role="power", terminal="ground")],
        )
    except ValueError as exc:
        assert "PinSpec role preset contradicts explicit terminal kind" in str(exc)
    else:
        raise AssertionError("expected contradictory pin preset to be rejected")

    component = design.define_component(
        "Valid",
        pins=[volt.PinSpec("VDD", 1, role="power")],
    )
    design.instantiate(component, ref="U1")
    circuit = json.loads(design.to_json())
    assert "role" not in circuit["pin_definitions"][0]
    assert circuit["pin_definitions"][0]["terminal_kind"] == "Power"
    assert circuit["pin_definitions"][0]["direction"] == "Input"


def test_library_part_build_emits_kernel_owned_artifact_without_role_sugar():
    library = volt.Library("volt.test", version="1.2.3")
    library.part(
        "AP1117-15",
        pins=[
            volt.PinSpec("GND", 1, role="ground"),
            volt.PinSpec("VO", 2, role="power_output", voltage_range=(1.5, 1.5)),
            volt.PinSpec("VI", 3, role="power_input", voltage_range=(2.5, 18.0)),
        ],
        symbol=volt.SchematicSymbolSpec(
            "volt.power:regulator_3pin",
            pins=(
                volt.SchematicSymbolSpec.pin("GND", 1, (0, 0)),
                volt.SchematicSymbolSpec.pin("VO", 2, (10, -5)),
                volt.SchematicSymbolSpec.pin("VI", 3, (10, 5)),
            ),
            primitives=(),
        ),
        manufacturer="Diodes Incorporated",
        mpn="AP1117E15G-13",
        package="SOT-223-3",
        footprint=volt.Footprint(
            ("Package_TO_SOT_SMD", "SOT-223-3_TabPin2"),
            pads=(
                volt.FootprintPad.surface_mount("1", at=(-1.0, 0.0), size=(0.6, 0.6)),
                volt.FootprintPad.surface_mount("2", at=(0.0, 0.0), size=(0.6, 0.6)),
                volt.FootprintPad.surface_mount("3", at=(1.0, 0.0), size=(0.6, 0.6)),
                volt.FootprintPad.surface_mount("4", at=(0.0, 2.0), size=(1.8, 1.8)),
            ),
            courtyard=((-2.4, -1.2), (2.4, -1.2), (2.4, 3.2), (-2.4, 3.2)),
            body=((-1.9, -0.8), (1.9, -0.8), (1.9, 2.8), (-1.9, 2.8)),
            fabrication_outline=(
                (-1.7, -0.6),
                (1.7, -0.6),
                (1.7, 2.6),
                (-1.7, 2.6),
            ),
            assembly_outline=(
                (-2.0, -0.9),
                (2.0, -0.9),
                (2.0, 2.9),
                (-2.0, 2.9),
            ),
            markings=(
                volt.FootprintMarking.pin_1(
                    ((-1.9, -0.8), (-1.65, -0.8), (-1.9, -0.55))
                ),
            ),
        ),
        pads={1: "1", 2: ("2", "4"), 3: "3"},
    )

    artifact = library.build().part("AP1117-15").artifact
    assert artifact is not None
    document = json.loads(artifact.bytes)

    assert artifact.sha256 == "sha256:" + hashlib.sha256(artifact.bytes).hexdigest()
    assert volt._volt.content_hash(artifact.bytes) == artifact.sha256
    assert artifact.bytes == library.build().part("AP1117-15").artifact.bytes
    assert document["format"] == "volt.part"
    assert document["version"] == 4
    assert document["identity"] == {
        "namespace": "volt.test",
        "name": "AP1117-15",
        "version": "1.2.3",
    }
    assert b'"role"' not in artifact.bytes
    assert document["pins"][0]["terminal_kind"] == "Ground"
    assert document["pins"][0]["direction"] == "Passive"
    assert document["pins"][1]["terminal_kind"] == "Power"
    assert document["pins"][1]["direction"] == "Output"
    assert document["pins"][2]["terminal_kind"] == "Power"
    assert document["pins"][2]["direction"] == "Input"
    assert document["orderable_part"]["mpn"] == "AP1117E15G-13"
    assert document["orderable_part"]["pin_pad_mappings"] == [
        {"pin_number": "1", "pad": "1"},
        {"pin_number": "2", "pad": "2"},
        {"pin_number": "2", "pad": "4"},
        {"pin_number": "3", "pad": "3"},
    ]
    assert document["symbols"][0]["pins"] == [
        {"name": "GND", "number": "1"},
        {"name": "VO", "number": "2"},
        {"name": "VI", "number": "3"},
    ]
    assert [pad["label"] for pad in document["orderable_part"]["footprint"]["pads"]] == [
        "1",
        "2",
        "3",
        "4",
    ]
    assert document["orderable_part"]["footprint"]["courtyard"] == [
        {"x_mm": -2.4, "y_mm": -1.2},
        {"x_mm": 2.4, "y_mm": -1.2},
        {"x_mm": 2.4, "y_mm": 3.2},
        {"x_mm": -2.4, "y_mm": 3.2},
    ]
    assert document["orderable_part"]["footprint"]["body"] == [
        {"x_mm": -1.9, "y_mm": -0.8},
        {"x_mm": 1.9, "y_mm": -0.8},
        {"x_mm": 1.9, "y_mm": 2.8},
        {"x_mm": -1.9, "y_mm": 2.8},
    ]
    assert document["orderable_part"]["footprint"]["fabrication_outline"] == [
        {"x_mm": -1.7, "y_mm": -0.6},
        {"x_mm": 1.7, "y_mm": -0.6},
        {"x_mm": 1.7, "y_mm": 2.6},
        {"x_mm": -1.7, "y_mm": 2.6},
    ]
    assert document["orderable_part"]["footprint"]["assembly_outline"] == [
        {"x_mm": -2.0, "y_mm": -0.9},
        {"x_mm": 2.0, "y_mm": -0.9},
        {"x_mm": 2.0, "y_mm": 2.9},
        {"x_mm": -2.0, "y_mm": 2.9},
    ]
    assert document["orderable_part"]["footprint"]["markings"] == [
        {
            "kind": "pin_1",
            "polygon": [
                {"x_mm": -1.9, "y_mm": -0.8},
                {"x_mm": -1.65, "y_mm": -0.8},
                {"x_mm": -1.9, "y_mm": -0.55},
            ],
        }
    ]


def test_library_part_build_reports_pin_role_contradictions():
    library = volt.Library("volt.test")
    library.part(
        "Broken",
        pins=[volt.PinSpec("VDD", 1, role="power", terminal="ground")],
        manufacturer="Example",
        mpn="BROKEN-1",
        package="SOT-23",
        footprint=volt.Footprint(
            ("Package_TO_SOT_SMD", "SOT-23"),
            pads=(volt.FootprintPad.surface_mount("1", at=(0.0, 0.0), size=(0.6, 0.6)),),
        ),
        pads={1: "1"},
    )

    result = library.build()

    assert not result.ok
    assert any(
        diagnostic.code == "LIBRARY_PART_ARTIFACT_INVALID"
        and "PinSpec role preset contradicts explicit terminal kind" in diagnostic.message
        for diagnostic in result.diagnostics
    )

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


def test_schematic_symbol_text_metadata_is_kernel_owned():
    symbol = volt.SchematicSymbolSpec(
        "volt.test:Styled",
        pins=(volt.SchematicSymbolSpec.pin("1", 1, (0, 0), "Left"),),
        primitives=(
            volt.SchematicSymbolSpec.text(
                "DATA",
                (2, 3),
                align="start",
                baseline="middle",
                font_size=3.25,
            ),
        ),
    )
    design = volt.Design("symbol-text-metadata")
    component = design.define_component(
        "Sensor",
        pins=[volt.PinSpec("1", 1)],
        schematic_symbol=symbol,
    )
    u1 = design.instantiate(component, ref="U1")
    schematic = design.schematic("Main")
    schematic.place(u1, at=(10, 20))

    projection = json.loads(schematic.to_json())
    primitive = projection["symbol_definitions"][0]["primitives"][0]

    assert primitive["horizontal_alignment"] == "Start"
    assert primitive["vertical_alignment"] == "Middle"
    assert primitive["font_size"] == 3.25

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

    selected_parts = {
        component["reference"]: component["selected_physical_part"]
        for component in circuit["components"]
    }
    assert selected_parts["J1"]["footprint"] == {
        "library": "connectors",
        "name": "USB_Micro-B_Receptacle",
    }
    assert selected_parts["U2"]["footprint"] == {
        "library": "Package_TO_SOT_SMD",
        "name": "SOT-23-6",
    }
    assert selected_parts["U3"]["footprint"] == {
        "library": "Package_TO_SOT_SMD",
        "name": "SOT-223-3_TabPin2",
    }
    assert selected_parts["U3"]["pin_pad_mappings"] == [
        {"pin": "pin_def:76", "pad": "1"},
        {"pin": "pin_def:77", "pad": "2"},
        {"pin": "pin_def:77", "pad": "4"},
        {"pin": "pin_def:78", "pad": "3"},
    ]


def test_stm32_usb_buck_library_selected_parts_resolve_builtin_footprints():
    board_ready_components = (
        stm32_usb_buck.STM32F405RGTx,
        stm32_usb_buck.USB_B_MICRO,
        stm32_usb_buck.USBLC6_4SC6,
        stm32_usb_buck.AP1117_15,
        stm32_usb_buck.FERRITE_BEAD,
        stm32_usb_buck.JTAG_SWD_10,
        stm32_usb_buck.CONNECTOR_1X04,
        stm32_usb_buck.DIODE,
        stm32_usb_buck.ZENER_DIODE,
        stm32_usb_buck.RESISTOR,
        stm32_usb_buck.CAPACITOR,
        stm32_usb_buck.INDUCTOR,
    )
    for component in board_ready_components:
        assert isinstance(component.physical_part.footprint, volt.Footprint), component.name

    design = volt.Design("stm32-library-pcb")
    components = {
        "J1": design.instantiate(stm32_usb_buck.USB_B_MICRO, ref="J1"),
        "U2": design.instantiate(stm32_usb_buck.USBLC6_4SC6, ref="U2"),
        "U3": design.instantiate(stm32_usb_buck.AP1117_15, ref="U3"),
        "R1": design.instantiate(stm32_usb_buck.RESISTOR, ref="R1"),
        "C1": design.instantiate(stm32_usb_buck.CAPACITOR, ref="C1"),
        "L1": design.instantiate(stm32_usb_buck.INDUCTOR, ref="L1"),
        "D1": design.instantiate(stm32_usb_buck.DIODE, ref="D1"),
    }
    board = design.board("First-board library parts")

    for index, component in enumerate(components.values()):
        board.place(component, at=(index * 8.0, 0.0))

    resolutions = board.resolve_pads()
    assert all(resolution.status != "invalid" for resolution in resolutions)
    by_reference = {
        reference: [
            resolution
            for resolution in resolutions
            if resolution.component == component.index
        ]
        for reference, component in components.items()
    }

    assert [resolution.pad_label for resolution in by_reference["J1"]] == [
        "1",
        "2",
        "3",
        "4",
        "5",
        "6",
        "M1",
        "M2",
    ]
    assert [resolution.status for resolution in by_reference["J1"][-2:]] == [
        "non_electrical",
        "non_electrical",
    ]
    assert [resolution.pad_label for resolution in by_reference["U3"]] == ["1", "2", "3", "4"]
    assert by_reference["U3"][3].pad == 3
    assert by_reference["U3"][1].pin == by_reference["U3"][3].pin
    assert [resolution.pad_label for resolution in by_reference["R1"]] == ["1", "2"]
    assert [resolution.pad_label for resolution in by_reference["C1"]] == ["1", "2"]
    assert [resolution.pad_label for resolution in by_reference["L1"]] == ["1", "2"]
    assert [resolution.pad_label for resolution in by_reference["D1"]] == ["1", "2"]

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


def test_component_selected_part_model_3d_serializes(tmp_path):
    design = volt.Design("selected-part-model-3d")
    r1 = design.R(resistance=330, ref="R1")
    asset_path = Path(tmp_path) / "resistor-body.glb"
    asset_path.write_bytes(b"placeholder-glb")

    r1.select_part(
        manufacturer="Yageo",
        part_number="RC0603FR-07330RL",
        package="0603",
        footprint=("Resistor_SMD", "R_0603_1608Metric"),
        pin_pads={
            1: "1",
            2: "2",
        },
        model_3d=volt.PartModel3D(
            asset_path,
            offset=(0.5, -0.25, 0.8),
            rotation=15,
        ),
    )

    circuit = json.loads(design.to_json())
    resistor = next(
        component for component in circuit["components"] if component["reference"] == "R1"
    )

    assert resistor["selected_physical_part"]["model_3d"] == {
        "format": "glb",
        "file_name": "resistor-body.glb",
        "translation_mm": [0.5, -0.25, 0.8],
        "rotation_deg": 15.0,
    }


def _resistor_0603_footprint():
    return volt.Footprint(
        library="Resistor_SMD",
        name="R_0603_1608Metric",
        pads=(
            volt.FootprintPad.surface_mount("1", at=(-0.75, 0.0), size=(0.80, 0.95)),
            volt.FootprintPad.surface_mount("2", at=(0.75, 0.0), size=(0.80, 0.95)),
        ),
    )


def _tie_and_mechanical_footprint():
    return volt.Footprint(
        ("volt.test", "TieAndMechanical"),
        pads=(
            volt.FootprintPad.surface_mount("1", at=(-1.0, 0.0), size=(0.6, 0.6)),
            volt.FootprintPad.surface_mount("2", at=(0.0, 0.0), size=(0.6, 0.6)),
            volt.FootprintPad.surface_mount("4", at=(1.0, 0.0), size=(0.6, 0.6)),
            volt.FootprintPad.through_hole(
                "MH",
                at=(0.0, 2.0),
                size=(1.8, 1.8),
                drill=volt.FootprintDrill(1.0, plating="non_plated"),
                layers="mechanical_hole",
                mechanical_role="mounting",
            ),
        ),
    )


def _library_resistor_part(name="R_0603_10K"):
    return volt.Part(
        name=name,
        pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        symbol=_two_pin_test_symbol(f"volt.test:{name}"),
        footprint=_resistor_0603_footprint(),
        pads={1: "1", 2: "2"},
        value="10k",
        manufacturer="Yageo",
        mpn="RC0603FR-0710KL",
        package="0603",
        prefix="R",
    )


def _resistor_0603_family(library, **overrides):
    defaults = {
        "prefix": "R",
        "package": "0603",
        "pins": [volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        "symbol": _two_pin_test_symbol("volt.test:R_0603"),
        "footprint": _resistor_0603_footprint(),
        "pads": {1: "1", 2: "2"},
        "manufacturer": "Yageo",
        "properties": {"kind": "resistor"},
    }
    defaults.update(overrides)
    return library.part_family(**defaults)


def test_library_parts_family_registers_repeated_resistor_catalog_parts():
    library = volt.Library("volt.test.passives")
    r0603 = _resistor_0603_family(library)

    ten_k = r0603.part("10K", mpn="RC0603FR-0710KL")
    hundred_k = r0603.part("100K", mpn="RC0603FR-07100KL")

    assert isinstance(ten_k, volt.Part)
    assert ten_k.name == "R_0603_10K"
    assert ten_k.value == "10K"
    assert ten_k.properties["kind"] == "resistor"
    assert ten_k.properties["value"] == "10K"
    assert hundred_k.name == "R_0603_100K"
    assert library["R_0603_10K"] is ten_k
    assert library["R_0603_100K"] is hundred_k
    assert isinstance(library.parts, tuple)
    assert not callable(library.parts)
    assert [part.name for part in library.parts] == ["R_0603_100K", "R_0603_10K"]

    result = library.build()

    assert result.ok
    assert [part.name for part in result.parts] == ["R_0603_100K", "R_0603_10K"]
    assert all(part.board_ready for part in result.parts)


def test_library_parts_family_overrides_are_isolated_snapshots():
    default_pads = {1: ["1"], 2: "2"}
    default_properties = {
        "kind": "resistor",
        "series": {"name": "RC"},
        "tags": ["default"],
    }
    default_ratings = {"power": {"max": 0.1}}
    default_extensions = {"lifecycle": {"status": "active"}}
    library = volt.Library("volt.test.passives")
    r0603 = _resistor_0603_family(
        library,
        pads=default_pads,
        properties=default_properties,
        ratings=default_ratings,
        extensions=default_extensions,
        source_version="catalog-v1",
    )

    default_pads[1].append("9")
    default_properties["series"]["name"] = "changed"
    default_properties["tags"].append("changed")
    default_ratings["power"]["max"] = 0.25
    default_extensions["lifecycle"]["status"] = "changed"

    override_pads = {1: ["1"], 2: "2"}
    override_properties = {"tolerance": {"percent": 1}}
    override_ratings = {"resistance": {"tolerance": "1%"}}
    override_extensions = {"stock": {"sku": "RC0603-10K"}}
    ten_k = r0603.part(
        "10K",
        mpn="RC0603FR-0710KL",
        pads=override_pads,
        properties=override_properties,
        ratings=override_ratings,
        extensions=override_extensions,
        source_name="catalog/R0603/10K",
        source_version="catalog-v2",
    )

    override_pads[1].append("9")
    override_properties["tolerance"]["percent"] = 5
    override_ratings["resistance"]["tolerance"] = "5%"
    override_extensions["stock"]["sku"] = "changed"

    hundred_k = r0603.part(
        "100K",
        value="100 kohm",
        part_number="RC0603FR-07100KL",
        manufacturer="KOA",
        package="0603",
        pads={1: "1", 2: "2"},
        source_name="catalog/R0603/100K",
    )

    assert ten_k.pads[1] == ("1",)
    assert ten_k.properties["kind"] == "resistor"
    assert ten_k.properties["series"]["name"] == "RC"
    assert ten_k.properties["tags"] == ("default",)
    assert ten_k.properties["tolerance"]["percent"] == 1
    assert ten_k.ratings["power"]["max"] == 0.1
    assert ten_k.ratings["resistance"]["tolerance"] == "1%"
    assert ten_k.extensions["lifecycle"]["status"] == "active"
    assert ten_k.extensions["stock"]["sku"] == "RC0603-10K"
    assert ten_k.source_name == "catalog/R0603/10K"
    assert ten_k.source_version == "catalog-v2"
    assert hundred_k.name == "R_0603_100K"
    assert hundred_k.value == "100 kohm"
    assert hundred_k.mpn == "RC0603FR-07100KL"
    assert hundred_k.manufacturer == "KOA"
    assert hundred_k.package == "0603"
    assert "tolerance" not in hundred_k.properties
    assert hundred_k.source_name == "catalog/R0603/100K"
    assert hundred_k.source_version == "catalog-v1"


def test_library_parts_family_duplicate_names_fail_at_library_boundary():
    library = volt.Library("volt.test.passives")
    r0603 = _resistor_0603_family(library)

    r0603.part("10K", mpn="RC0603FR-0710KL")

    try:
        r0603.part("10K", mpn="RC0603FR-0710KL")
    except ValueError as error:
        assert "already exists" in str(error)
        assert "R_0603_10K" in str(error)
    else:
        raise AssertionError("helper-created part names should use Library.add duplicates")

    direct_library = volt.Library("volt.test.direct.passives")
    direct_library.add(_library_resistor_part())
    direct_r0603 = _resistor_0603_family(direct_library)

    try:
        direct_r0603.part("10K", mpn="RC0603FR-0710KL")
    except ValueError as error:
        assert "already exists" in str(error)
        assert "R_0603_10K" in str(error)
    else:
        raise AssertionError("direct part names should block helper duplicates")


def test_library_result_orders_helper_and_direct_parts_deterministically():
    library = volt.Library("volt.test.passives")
    r0603 = _resistor_0603_family(library)

    zeta = library.add(_library_resistor_part(name="Z_Direct_1K"))
    ten_k = r0603.part("10K", mpn="RC0603FR-0710KL")
    one_k = r0603.part("1K", mpn="RC0603FR-071KL")

    result = library.build()

    assert library["Z_Direct_1K"] is zeta
    assert library["R_0603_10K"] is ten_k
    assert library["R_0603_1K"] is one_k
    assert [part.name for part in library.parts] == [
        "R_0603_10K",
        "R_0603_1K",
        "Z_Direct_1K",
    ]
    assert [part.name for part in result.parts] == [
        "R_0603_10K",
        "R_0603_1K",
        "Z_Direct_1K",
    ]


def test_library_build_validates_board_ready_part():
    library = volt.Library("volt.test.passives")
    part = _library_resistor_part()

    library.add(part)
    result = library.build()

    assert result.ok
    assert tuple(result.diagnostics) == ()
    part_result = result.part("R_0603_10K")
    assert part_result.schematic_ready
    assert part_result.board_ready
    assert part_result.serializable
    assert part_result.has_footprint
    assert part_result.pad_mapping_complete
    assert library["R_0603_10K"] is part


def test_part_orderable_same_numbered_preserves_artifact_pin_pad_mappings():
    library = volt.Library("volt.test.passives")
    library.add(
        volt.Part(
            name="SameNumbered",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            symbol=_two_pin_test_symbol("volt.test:SameNumbered"),
            orderable_part=volt.OrderablePart.same_numbered(
                manufacturer="Yageo",
                part_number="RC0603FR-0710KL",
                package="0603",
                footprint=_resistor_0603_footprint(),
            ),
            prefix="R",
        )
    )

    result = library.build()
    part_result = result.part("SameNumbered")
    artifact = part_result.artifact

    assert result.ok
    assert part_result.board_ready
    assert part_result.pad_mapping_complete
    assert artifact is not None
    assert json.loads(artifact.bytes)["orderable_part"]["pin_pad_mappings"] == [
        {"pin_number": "1", "pad": "1"},
        {"pin_number": "2", "pad": "2"},
    ]


def test_part_instantiation_requires_library_identity():
    part = _library_resistor_part()
    design = volt.Design("unbound-part")

    try:
        design.instantiate(part, ref="R1")
    except ValueError as error:
        assert "Library" in str(error)
    else:
        raise AssertionError("unbound parts should not be directly instantiated")


def test_library_part_is_immutable_after_construction():
    library = volt.Library("volt.test.passives")
    part = _library_resistor_part()
    library.add(part)

    try:
        part.name = "Changed"
    except AttributeError as error:
        assert "immutable" in str(error)
    else:
        raise AssertionError("part mutation should be rejected")

    assert library["R_0603_10K"] is part
    assert part.name == "R_0603_10K"


def test_library_part_collection_fields_are_immutable_snapshots():
    pads = {1: ["1"], 2: "2"}
    properties = {"metadata": {"bin": "A"}, "aliases": ["R10K"]}
    physical_properties = {"assembly": {"feeder": "F1"}}
    ratings = {"voltage": {"max": 50}}
    extensions = {"tags": ["passive"]}
    symbol_primitive = volt.SchematicSymbolSpec.line((0, 0), (20, 0))
    symbol = volt.SchematicSymbolSpec(
        "volt.test:R_0603_10K_nested",
        pins=(
            volt.SchematicSymbolSpec.pin("1", 1, (0, 0), "Left"),
            volt.SchematicSymbolSpec.pin("2", 2, (20, 0), "Right"),
        ),
        primitives=(symbol_primitive,),
    )
    library = volt.Library("volt.test.passives")
    part = volt.Part(
        name="R_0603_10K_nested",
        pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        symbol=symbol,
        footprint=_resistor_0603_footprint(),
        pads=pads,
        value="10k",
        manufacturer="Yageo",
        mpn="RC0603FR-0710KL",
        package="0603",
        properties=properties,
        physical_properties=physical_properties,
        ratings=ratings,
        prefix="R",
        extensions=extensions,
    )

    pads[1].append("9")
    properties["metadata"]["bin"] = "B"
    physical_properties["assembly"]["feeder"] = "F2"
    ratings["voltage"]["max"] = 100
    extensions["tags"].append("changed")
    symbol_primitive["start"]["x"] = 99
    library.add(part)

    def assert_rejects_mutation(callback):
        try:
            callback()
        except (AttributeError, TypeError):
            return
        raise AssertionError("part collection mutation should be rejected")

    def mutate_pads():
        part.pads[1] += ("9",)

    def mutate_properties():
        part.properties["metadata"]["bin"] = "B"

    def mutate_physical_properties():
        part.physical_properties["assembly"]["feeder"] = "F2"

    def mutate_ratings():
        part.ratings["voltage"]["max"] = 100

    def mutate_extensions():
        part.extensions["tags"][0] = "changed"

    def mutate_symbol_primitive():
        part.schematic_symbols[0].primitives[0]["start"]["x"] = 99

    assert tuple(part.pads[1]) == ("1",)
    assert part.properties["metadata"]["bin"] == "A"
    assert part.physical_properties["assembly"]["feeder"] == "F1"
    assert part.ratings["voltage"]["max"] == 50
    assert part.extensions["tags"] == ("passive",)
    assert part.schematic_symbols[0].primitives[0]["start"]["x"] == 0.0

    for mutation in (
        mutate_pads,
        mutate_properties,
        mutate_physical_properties,
        mutate_ratings,
        mutate_extensions,
        mutate_symbol_primitive,
    ):
        assert_rejects_mutation(mutation)

    result = library.build()

    assert result.part("R_0603_10K_nested").serializable
    payload = part._to_dict()
    assert payload["schematic_symbols"][0]["primitives"][0]["start"]["x"] == 0.0
    json.dumps(payload)


def test_project_instantiates_imported_part_without_manual_footprint_cache():
    library = volt.Library("volt.test.passives")
    resistor_part = _library_resistor_part()
    library.add(resistor_part)
    project = volt.Project("part-project")

    @project.design
    def design():
        d = volt.Design("part-project")
        r1 = d.instantiate(resistor_part, ref="R1")
        left = d.net("LEFT")
        right = d.net("RIGHT")
        r1.dnp(False)
        left += r1[1]
        right += r1[2]
        return d

    @project.board
    def board(context):
        design = context.design()
        pcb = design.board("Main")
        pcb.set_rectangular_outline(origin=(0.0, 0.0), size=(20.0, 12.0))
        pcb.place(design.component("R1"), at=(10.0, 6.0))
        return pcb

    result = project.run()

    assert result.ok
    document = json.loads(result.board().to_json())
    definitions = document["board"]["footprint_definitions"]
    assert [definition["ref"] for definition in definitions] == [
        {"library": "Resistor_SMD", "name": "R_0603_1608Metric"}
    ]
    assert document["board"]["placements"][0]["footprint"] == "footprint_def:0"


def test_part_pin_pad_mapping_supports_tied_pads():
    library = volt.Library("volt.test.connectors")
    connector = volt.Part(
        name="TieAndMechanical",
        pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        symbol=_two_pin_test_symbol("volt.test:TieAndMechanical"),
        footprint=_tie_and_mechanical_footprint(),
        pads={1: "1", 2: ("2", "4")},
        manufacturer="Volt",
        mpn="TIE-MECH",
        package="custom",
        prefix="J",
    )
    library.add(connector)
    design = volt.Design("part-tied-pads")
    j1 = design.instantiate(connector, ref="J1")
    a_net = design.net("A")
    a_net += j1[1]
    tied_net = design.net("B")
    tied_net += j1[2]
    board = design.board()
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(20.0, 12.0))
    board.place(j1, at=(10.0, 6.0))

    result = library.build()
    resolutions = {resolution.pad_label: resolution for resolution in board.resolve_pads()}

    assert result.ok
    assert resolutions["2"].pin == j1[2].index
    assert resolutions["4"].pin == j1[2].index
    assert resolutions["2"].net == tied_net.index
    assert resolutions["4"].net == tied_net.index


def test_part_validation_reports_unknown_pad_label():
    library = volt.Library("volt.test.bad")
    library.add(
        volt.Part(
            name="BadPad",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            symbol=_two_pin_test_symbol("volt.test:BadPad"),
            footprint=_resistor_0603_footprint(),
            pads={1: "1", 2: "9"},
            manufacturer="Yageo",
            mpn="BADPAD",
            package="0603",
        )
    )

    result = library.build()

    assert not result.ok
    assert [diagnostic.code for diagnostic in result.diagnostics] == [
        "LIBRARY_PART_ARTIFACT_INVALID",
    ]
    assert result.part("BadPad").board_ready is False


def test_part_validation_rejects_closed_footprint_polygons_at_artifact_boundary():
    library = volt.Library("volt.test.bad")
    library.add(
        volt.Part(
            name="ClosedCourtyard",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            symbol=_two_pin_test_symbol("volt.test:ClosedCourtyard"),
            footprint=volt.Footprint(
                ("volt.test", "ClosedCourtyard"),
                pads=(
                    volt.FootprintPad.surface_mount("1", at=(-0.75, 0.0), size=(0.8, 0.95)),
                    volt.FootprintPad.surface_mount("2", at=(0.75, 0.0), size=(0.8, 0.95)),
                ),
                courtyard=((-1.2, -0.8), (1.2, -0.8), (1.2, 0.8), (-1.2, 0.8), (-1.2, -0.8)),
            ),
            pads={1: "1", 2: "2"},
            manufacturer="Yageo",
            mpn="CLOSEDCOURTYARD",
            package="0603",
        )
    )

    result = library.build()
    part_result = result.part("ClosedCourtyard")

    assert not result.ok
    assert part_result.artifact is None
    assert [diagnostic.code for diagnostic in result.diagnostics] == [
        "LIBRARY_PART_ARTIFACT_INVALID",
    ]
    assert "must not repeat vertices" in result.diagnostics[0].message


def test_part_validation_reports_unresolvable_pad_mapping_key_not_board_ready():
    library = volt.Library("volt.test.bad")
    library.add(
        volt.Part(
            name="BadPadKey",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            symbol=_two_pin_test_symbol("volt.test:BadPadKey"),
            footprint=volt.Footprint(
                ("volt.test", "BadPadKey"),
                pads=(
                    volt.FootprintPad.surface_mount("1", at=(-1.0, 0.0), size=(0.6, 0.6)),
                    volt.FootprintPad.surface_mount("2", at=(0.0, 0.0), size=(0.6, 0.6)),
                    volt.FootprintPad.surface_mount("3", at=(1.0, 0.0), size=(0.6, 0.6)),
                ),
            ),
            pads={1: "1", 2: "2", 99: "3"},
            manufacturer="Yageo",
            mpn="BADPADKEY",
            package="0603",
        )
    )

    result = library.build()

    assert not result.ok
    assert [diagnostic.code for diagnostic in result.diagnostics] == [
        "LIBRARY_PART_ARTIFACT_INVALID",
    ]
    assert result.part("BadPadKey").board_ready is False


def test_part_validation_rejects_missing_symbol_projection_at_artifact_boundary():
    library = volt.Library("volt.test.bad")
    library.add(
        volt.Part(
            name="NoSymbol",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            footprint=_resistor_0603_footprint(),
            pads={1: "1", 2: "2"},
            manufacturer="Yageo",
            mpn="NOSYMBOL",
            package="0603",
        )
    )

    result = library.build()

    assert not result.ok
    assert [diagnostic.code for diagnostic in result.diagnostics] == [
        "LIBRARY_PART_ARTIFACT_INVALID",
    ]
    assert "at least one schematic symbol projection" in result.diagnostics[0].message
    assert result.part("NoSymbol").schematic_ready is False


def test_part_validation_allows_mechanical_pads_without_logical_pin_mapping():
    library = volt.Library("volt.test.mechanical")
    library.add(
        volt.Part(
            name="MechanicalPad",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            symbol=_two_pin_test_symbol("volt.test:MechanicalPad"),
            footprint=_tie_and_mechanical_footprint(),
            pads={1: "1", 2: ("2", "4")},
            manufacturer="Volt",
            mpn="TIE-MECH",
            package="custom",
        )
    )

    result = library.build()

    assert result.ok
    assert tuple(result.diagnostics) == ()


def test_part_validation_rejects_unknown_mechanical_pad_role():
    library = volt.Library("volt.test.mechanical")
    library.add(
        volt.Part(
            name="TypoMechanicalPad",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            symbol=_two_pin_test_symbol("volt.test:TypoMechanicalPad"),
            footprint=volt.Footprint(
                ("volt.test", "TypoMechanicalPad"),
                pads=(
                    volt.FootprintPad.surface_mount("1", at=(-1.0, 0.0), size=(0.6, 0.6)),
                    volt.FootprintPad.surface_mount("2", at=(0.0, 0.0), size=(0.6, 0.6)),
                    volt.FootprintPad.surface_mount(
                        "MP",
                        at=(1.0, 0.0),
                        size=(0.6, 0.6),
                        mechanical_role="mountng",
                    ),
                ),
            ),
            pads={1: "1", 2: "2"},
            manufacturer="Volt",
            mpn="TYPO-MECH",
            package="custom",
        )
    )

    result = library.build()

    assert not result.ok
    assert [(diagnostic.code, diagnostic.severity) for diagnostic in result.diagnostics] == [
        ("LIBRARY_PART_ARTIFACT_INVALID", "error")
    ]
    assert "Unknown footprint pad mechanical role" in result.diagnostics[0].message
    assert result.part("TypoMechanicalPad").board_ready is False


def test_part_validation_reports_lineup_diagnostics_and_non_serializable_data():
    library = volt.Library("volt.test.incomplete")
    library.add(
        volt.Part(
            name="MissingPin",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            symbol=_two_pin_test_symbol("volt.test:MissingPin"),
            footprint=_resistor_0603_footprint(),
            pads={1: "1"},
            manufacturer="Yageo",
            mpn="MISSING",
            package="0603",
        )
    )
    library.add(
        volt.Part(
            name="MissingElectricalPad",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            symbol=_two_pin_test_symbol("volt.test:MissingElectricalPad"),
            footprint=volt.Footprint(
                ("volt.test", "ExtraElectrical"),
                pads=(
                    *_resistor_0603_footprint().pads,
                    volt.FootprintPad.surface_mount("3", at=(2.25, 0.0), size=(0.6, 0.6)),
                ),
            ),
            pads={1: "1", 2: "2"},
            manufacturer="Yageo",
            mpn="EXTRA",
            package="0603",
        )
    )
    library.add(
        volt.Part(
            name="NonSerializable",
            pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
            symbol=_two_pin_test_symbol("volt.test:NonSerializable"),
            footprint=_resistor_0603_footprint(),
            pads={1: "1", 2: "2"},
            manufacturer="Yageo",
            mpn="NON-SERIAL",
            package="0603",
            extensions={"factory": object()},
        )
    )

    result = library.build()

    assert not result.ok
    assert [(diagnostic.source, diagnostic.code) for diagnostic in result.diagnostics] == [
        ("part:MissingElectricalPad", "PART_PAD_WITHOUT_PIN"),
        ("part:MissingPin", "PART_PIN_WITHOUT_PAD"),
        ("part:MissingPin", "PART_PAD_WITHOUT_PIN"),
        ("part:NonSerializable", "LIBRARY_PART_NON_SERIALIZABLE"),
    ]
    assert [
        (diagnostic.category, diagnostic.severity)
        for diagnostic in result.diagnostics
        if diagnostic.code.startswith("PART_")
    ] == [
        ("part.lineup", "warning"),
        ("part.lineup", "warning"),
        ("part.lineup", "warning"),
    ]


def test_library_result_is_deterministic():
    library = volt.Library("volt.test.deterministic")
    library.add(
        volt.Part(
            name="Zeta",
            pins=[volt.PinSpec("1", 1)],
            footprint=_resistor_0603_footprint(),
            pads={1: "9"},
            manufacturer="Yageo",
            mpn="ZETA",
            package="0603",
        )
    )
    library.add(
        volt.Part(
            name="Alpha",
            pins=[],
            footprint=None,
            pads={},
        )
    )

    first = library.build().to_dict()
    second = library.build().to_dict()

    assert first == second
    assert [
        (diagnostic["source"], diagnostic["code"])
        for diagnostic in first["diagnostics"]["diagnostics"]
    ] == [
        ("part:Alpha", "LIBRARY_PART_MISSING_PINS"),
        ("part:Alpha", "LIBRARY_PART_MISSING_FOOTPRINT"),
        ("part:Zeta", "LIBRARY_PART_ARTIFACT_INVALID"),
    ]


def test_part_ref_only_missing_geometry_still_reports_unresolved_footprint():
    library = volt.Library("volt.test.missing_geometry")
    resistor = volt.Part(
        name="MissingGeometry",
        pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        footprint=("missing", "NotARealFootprint"),
        pads={1: "1", 2: "2"},
        manufacturer="Yageo",
        mpn="RC0603FR-071KL",
        package="0603",
        prefix="R",
    )
    library.add(resistor)
    design = volt.Design("part-missing-footprint")
    r1 = design.instantiate(resistor, ref="R1")
    left = design.net("LEFT")
    right = design.net("RIGHT")
    left += r1[1]
    right += r1[2]
    board = design.board()
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(20.0, 12.0))
    board.place(r1, at=(10.0, 6.0))

    result = library.build()

    assert not result.ok
    assert [diagnostic.code for diagnostic in result.diagnostics] == [
        "LIBRARY_PART_MISSING_FOOTPRINT_GEOMETRY"
    ]
    assert "PCB_FOOTPRINT_UNRESOLVED" in {diagnostic.code for diagnostic in board.validate()}


def test_component_select_part_accepts_public_footprint_object():
    design = volt.Design("selected-part-footprint-object")
    r1 = design.R(ref="R1")
    footprint = _resistor_0603_footprint()

    r1.select_part(
        manufacturer="Yageo",
        part_number="RC0603FR-07330RL",
        package="0603",
        footprint=footprint,
        pin_pads={1: "1", 2: "2"},
    )

    circuit = json.loads(design.to_json())
    part = circuit["components"][0]["selected_physical_part"]

    assert part["footprint"] == {
        "library": "Resistor_SMD",
        "name": "R_0603_1608Metric",
    }
    assert "pads" not in part["footprint"]


def test_footprint_rejects_empty_public_identity():
    try:
        volt.Footprint(library="", name="R_0603_1608Metric", pads=())
    except ValueError:
        pass
    else:
        raise AssertionError("empty footprint library should be rejected")

    try:
        volt.Footprint(library="Resistor_SMD", name="", pads=())
    except ValueError:
        pass
    else:
        raise AssertionError("empty footprint name should be rejected")


def test_physical_part_specs_accept_and_reuse_public_footprint_object():
    footprint = _resistor_0603_footprint()
    library = volt.Library("volt.test")
    resistor = library.component(
        "Resistor",
        pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        physical_part=volt.PhysicalPartSpec(
            manufacturer="Yageo",
            part_number="RC0603FR-07330RL",
            package="0603",
            footprint=footprint,
            pin_pads={1: "1", 2: "2"},
        ),
    )
    jumper = library.component(
        "Jumper",
        pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
        physical_part=volt.PhysicalPartSpec.same_numbered(
            manufacturer="Keystone",
            part_number="5015",
            package="0603",
            footprint=footprint,
        ),
    )
    design = volt.Design("library-footprint-object")

    design.instantiate(resistor, ref="R1")
    design.instantiate(jumper, ref="JP1")
    circuit = json.loads(design.to_json())

    assert resistor.physical_part.footprint is footprint
    assert jumper.physical_part.footprint is footprint
    assert [
        component["selected_physical_part"]["footprint"]
        for component in circuit["components"]
    ] == [
        {"library": "Resistor_SMD", "name": "R_0603_1608Metric"},
        {"library": "Resistor_SMD", "name": "R_0603_1608Metric"},
    ]
    assert all(
        "pads" not in component["selected_physical_part"]["footprint"]
        for component in circuit["components"]
    )


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
    except ValueError as error:
        assert str(error) == "Physical part must map every pin in the component definition"
        assert isinstance(error, volt.InvalidArgumentError)
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
            pin_pads={1: ("1", "1"), 2: "2"},
        )
    except ValueError:
        pass
    else:
        raise AssertionError("duplicate pad labels in a tied-pad mapping should be rejected")

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


def test_selected_part_mapping_accepts_tied_physical_pads():
    design = volt.Design("tied-pads")
    regulator = design.instantiate(stm32_usb_buck.AP1117_15, ref="U1")

    circuit = json.loads(design.to_json())
    part = circuit["components"][0]["selected_physical_part"]

    assert part["footprint"] == {
        "library": "Package_TO_SOT_SMD",
        "name": "SOT-223-3_TabPin2",
    }
    assert part["pin_pad_mappings"] == [
        {"pin": "pin_def:0", "pad": "1"},
        {"pin": "pin_def:1", "pad": "2"},
        {"pin": "pin_def:1", "pad": "4"},
        {"pin": "pin_def:2", "pad": "3"},
    ]
    assert regulator["VO"].index == 1

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
