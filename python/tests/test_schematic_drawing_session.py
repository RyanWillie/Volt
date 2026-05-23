import json
import re

import volt

from helpers import _wire_points
from schematic_drawing_test_helpers import schematic_projection


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
        message = str(error)
        assert "different design" in message
        assert "VCC" in message
        assert "sheet 'Main'" in message
    else:
        raise AssertionError("power ports must reject nets from another design")

    try:
        schematic.wire(vcc).from_(other_symbol.pin(1)).to(symbol.pin(1)).orthogonal()
    except ValueError as error:
        message = str(error)
        assert "different design" in message
        assert "R1 pin 1 (1)" in message
        assert "sheet 'Main'" in message
    else:
        raise AssertionError("wire anchors must reject pins from another design")

    try:
        schematic.no_connect(other_symbol.pin(1))
    except ValueError as error:
        message = str(error)
        assert "different design" in message
        assert "R1 pin 1 (1)" in message
        assert "sheet 'Main'" in message
    else:
        raise AssertionError("no-connect markers must reject pins from another design")

    try:
        schematic.wire(vcc).from_(other_handle.start).to(handle.start).orthogonal()
    except ValueError as error:
        message = str(error)
        assert "different design" in message
        assert "R1 pin 1 (1)" in message
        assert "sheet 'Main'" in message
    else:
        raise AssertionError("authoring handle anchors must reject pins from another design")
