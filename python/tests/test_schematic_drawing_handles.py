import importlib
import json
import re

import volt

from helpers import _wire_points
from schematic_drawing_test_helpers import schematic_projection


def test_python_schematic_handle_classes_are_reexported_from_private_module():
    handles = importlib.import_module("volt._schematic_handles")
    schematic = importlib.import_module("volt.schematic")

    schematic_exports = (
        "PlacedSchematicElement",
        "SchematicAnchor",
        "SchematicJunction",
        "SchematicNetLabel",
        "SchematicNoConnect",
        "SchematicPinAnchor",
        "SchematicPort",
        "SchematicSignalStub",
        "SchematicSignalTag",
        "SchematicSymbol",
        "SchematicSymbolField",
        "SchematicTerminalStub",
        "SchematicWire",
    )
    top_level_exports = tuple(name for name in schematic_exports if hasattr(volt, name))

    for name in schematic_exports:
        assert getattr(schematic, name) is getattr(handles, name)
    for name in top_level_exports:
        assert getattr(volt, name) is getattr(handles, name)


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
    except AttributeError as error:
        message = str(error)
        assert "ambiguous" in message
        assert "bracket" in message
        assert "pin number" in message
    else:
        raise AssertionError("ambiguous pin names should not be exposed as attributes")

    # Ambiguity must not break hasattr or getattr-with-default
    assert not hasattr(handle, "VDD")
    assert getattr(handle, "VDD", None) is None

    assert handle[19].number == "19"
    assert handle["32"].number == "32"


def test_python_schematic_drawing_handle_start_end_raise_for_single_pin():
    design = volt.Design("schematic-authoring-handle-single-pin")
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
    with design.schematic("Main").drawing(at=(5, 5)) as drawing:
        handle = drawing.place(tp1)

    try:
        handle.start
    except ValueError as error:
        assert "start" in str(error)
        assert "two pin anchors" in str(error)
    else:
        raise AssertionError("start must raise for single-pin components")

    try:
        handle.end
    except ValueError as error:
        assert "end" in str(error)
        assert "two pin anchors" in str(error)
    else:
        raise AssertionError("end must raise for single-pin components")

    # center still works with one pin
    assert handle.center.point == (5.0, 5.0)
    assert handle["NC"].number == "1"


def test_python_schematic_drawing_handle_dir_exposes_unique_pin_names_only():
    design = volt.Design("schematic-authoring-handle-dir")
    component = design.define_component(
        "MixedPins",
        pins=[
            volt.PinSpec("IN", 1),
            volt.PinSpec("OUT", 2),
            volt.PinSpec("VDD", 3, role="power"),
            volt.PinSpec("VDD", 4, role="power"),
        ],
        schematic_symbol=volt.SchematicSymbolSpec(
            "test:mixed",
            pins=(
                volt.SchematicSymbolSpec.pin("IN", 1, (0, 0), "Left"),
                volt.SchematicSymbolSpec.pin("OUT", 2, (20, 0), "Right"),
                volt.SchematicSymbolSpec.pin("VDD", 3, (10, 10), "Up"),
                volt.SchematicSymbolSpec.pin("VDD", 4, (10, -10), "Down"),
            ),
            primitives=(volt.SchematicSymbolSpec.line((0, 0), (20, 0)),),
        ),
    )
    m1 = design.instantiate(component, ref="M1")
    with design.schematic("Main").drawing(at=(0, 0)) as drawing:
        handle = drawing.place(m1)

    listed = dir(handle)
    assert "IN" in listed
    assert "OUT" in listed
    assert "VDD" not in listed  # ambiguous - must not appear
    assert "start" in listed
    assert "end" in listed
    assert "center" in listed
    assert "symbol" in listed


def test_python_schematic_drawing_connect_infers_shared_pin_net_and_readiness():
    design = volt.Design("schematic-inferred-led")
    vcc = design.net("+3V3", kind="power")
    led_a = design.net("LED_A")
    gnd = design.net("GND", kind="ground")
    r1 = design.R("330 ohm", ref="R1")
    d1 = design.LED(ref="D1")
    vcc += r1[1]
    led_a += r1[2], d1["A"]
    gnd += d1["K"]

    schematic = design.schematic("Main")
    logical_before = design.to_json()

    with schematic.drawing(unit=20) as drawing:
        resistor = drawing.two_terminal(r1).right()
        led = drawing.two_terminal(d1).right().reverse()

        drawing.connect(resistor.end, led.start)
        drawing.power("+3V3", at=resistor.start)
        drawing.ground(at=led.end)

    projection = schematic_projection(schematic)
    report = schematic.validate()

    assert design.to_json() == logical_before
    assert len(projection["wire_runs"]) == 1
    assert projection["wire_runs"][0]["net"] == f"net:{led_a.index}"
    assert projection["wire_runs"][0]["points"][0] == {"x": 20.0, "y": 0.0}
    assert projection["wire_runs"][0]["points"][-1] == {"x": 20.0, "y": 0.0}
    assert [port["net"] for port in projection["power_ports"]] == [
        f"net:{vcc.index}",
        f"net:{gnd.index}",
    ]
    assert len(report) == 0
    assert not report.has_errors


def test_python_schematic_drawing_connect_rejects_different_pin_nets_without_wire():
    design = volt.Design("schematic-inferred-mismatch")
    led_a = design.net("LED_A")
    gnd = design.net("GND", kind="ground")
    r1 = design.R("330 ohm", ref="R1")
    d1 = design.LED(ref="D1")
    led_a += r1[2], d1["A"]
    gnd += d1["K"]

    schematic = design.schematic("Main")
    logical_before = design.to_json()

    with schematic.drawing(unit=20) as drawing:
        resistor = drawing.two_terminal(r1).right()
        led = drawing.two_terminal(d1).right().reverse()
        _ = (resistor.end, led.end)
        before = schematic.to_json()

        try:
            drawing.connect(resistor.end, led.end)
        except ValueError as error:
            message = str(error)
            assert "different logical nets" in message
            assert "sheet 'Main'" in message
            assert "R1 pin 2 (2)" in message
            assert "D1 pin 1 (K)" in message
            assert "LED_A" in message
            assert "GND" in message
        else:
            raise AssertionError("different logical nets must not be visually auto-wired")

        assert schematic.to_json() == before

    assert design.to_json() == logical_before
    assert schematic_projection(schematic)["wire_runs"] == []


def test_python_schematic_drawing_connect_requires_explicit_net_for_plain_coordinate():
    design = volt.Design("schematic-explicit-coordinate-net")
    vcc = design.net("VCC", kind="power")
    r1 = design.R("10k", ref="R1")
    vcc += r1[1]

    schematic = design.schematic("Main")
    logical_before = design.to_json()

    with schematic.drawing(unit=20) as drawing:
        resistor = drawing.two_terminal(r1).right()
        _ = resistor.start
        before = schematic.to_json()

        try:
            drawing.connect(resistor.start, (0, -20))
        except ValueError as error:
            message = str(error)
            assert "explicit net" in message
            assert "sheet 'Main'" in message
        else:
            raise AssertionError("pin-to-coordinate wires must require explicit net")

        assert schematic.to_json() == before
        drawing.connect(resistor.start, (0, -20), net=vcc)

    projection = schematic_projection(schematic)

    assert design.to_json() == logical_before
    assert len(projection["wire_runs"]) == 1
    assert projection["wire_runs"][0]["net"] == f"net:{vcc.index}"


def test_python_schematic_endpoint_mutations_reject_mismatched_label_net():
    design = volt.Design("schematic-endpoint-label-net-validation")
    sig = design.net("SIG")
    wrong = design.net("WRONG")
    r1 = design.R("10k", ref="R1")
    sig += r1[1]

    schematic = design.schematic("Main")
    logical_before = design.to_json()

    resistor = schematic.place(r1, at=(0, 0), symbol="resistor")
    before = schematic.to_json()

    try:
        schematic.net_label(wrong, at=resistor.pin(1))
    except ValueError as error:
        message = str(error)
        assert "the pin belongs to SIG" in message
        assert "instead of WRONG" in message
        assert "net label" in message
    else:
        raise AssertionError("net labels must reject mismatched pin endpoint nets")

    assert schematic.to_json() == before
    schematic.net_label(sig, at=resistor.pin(1))

    projection = schematic_projection(schematic)

    assert design.to_json() == logical_before
    assert len(projection["net_labels"]) == 1
    assert projection["net_labels"][0]["net"] == f"net:{sig.index}"
