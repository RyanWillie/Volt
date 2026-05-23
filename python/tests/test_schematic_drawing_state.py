import json
import re

import volt

from helpers import _wire_points
from schematic_drawing_test_helpers import schematic_projection


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


def test_python_schematic_drawing_junction_delegates_to_sheet():
    design = volt.Design("schematic-drawing-junction")
    net = design.net("TRACE")
    schematic = design.schematic("Main")

    with schematic.drawing(at=(12, 34)) as drawing:
        explicit = drawing.junction(net, at=(10, 20))
        implicit = drawing.junction(net)

    projection = schematic_projection(schematic)

    assert explicit.index == 0
    assert implicit.index == 1
    assert projection["junctions"] == [
        {
            "id": "junction:0",
            "sheet": "sheet:0",
            "net": f"net:{net.index}",
            "position": {"x": 10.0, "y": 20.0},
        },
        {
            "id": "junction:1",
            "sheet": "sheet:0",
            "net": f"net:{net.index}",
            "position": {"x": 12.0, "y": 34.0},
        },
    ]


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


def test_python_schematic_drawing_stack_generates_directional_anchors_without_cursor_drift():
    design = volt.Design("schematic-drawing-anchor-stack")
    schematic = design.schematic("Main")
    drawing = schematic.drawing(at=(10, 20), direction="Down", unit=12)
    before = drawing.here.point, drawing.direction
    logical_before = design.to_json()
    projection_before = schematic.to_json()

    row = drawing.stack(count=3, direction="Right", pitch=5)
    column = drawing.stack(count=3, direction="Down", pitch=7, at=(2, 3))
    implicit = drawing.stack(count=2)

    with drawing.hold():
        shifted = drawing.stack(count=2, direction="Left", pitch=4, at=row[1].up(1))
        drawing.move_from(shifted[-1].down(6), direction="Left")
        assert drawing.here.point == (11.0, 25.0)
        assert drawing.direction == "Left"

    assert [anchor.point for anchor in row] == [
        (10.0, 20.0),
        (15.0, 20.0),
        (20.0, 20.0),
    ]
    assert [anchor.point for anchor in column] == [
        (2.0, 3.0),
        (2.0, 10.0),
        (2.0, 17.0),
    ]
    assert [anchor.point for anchor in implicit] == [
        (10.0, 20.0),
        (10.0, 32.0),
    ]
    assert (drawing.here.point, drawing.direction) == before
    assert schematic.to_json() == projection_before
    assert design.to_json() == logical_before


def test_python_schematic_drawing_stack_does_not_materialize_pending_elements():
    design = volt.Design("schematic-drawing-stack-pure")
    resistor_part = design.R("10k", ref="R1")
    schematic = design.schematic("Main")
    drawing = schematic.drawing(unit=10)

    pending = drawing.two_terminal(resistor_part).right()
    instances_before = schematic_projection(schematic)["symbol_instances"]
    anchors = drawing.stack(count=2, direction="Down", pitch=5)

    assert [anchor.point for anchor in anchors] == [(10.0, 0.0), (10.0, 5.0)]
    assert schematic_projection(schematic)["symbol_instances"] == instances_before
    assert pending.start.point == (0.0, 0.0)
    assert schematic_projection(schematic)["symbol_instances"][0]["position"] == {
        "x": 0.0,
        "y": 0.0,
    }


def test_python_schematic_drawing_stack_anchors_compose_inside_frame_operations():
    design = volt.Design("schematic-drawing-frame-stack")
    vcc = design.net("VCC", kind="power")
    gnd = design.net("GND", kind="ground")
    sig = design.net("SIG")
    probe = design.test_point(ref="TP1")
    capacitor = design.C("100 nF", ref="C1")
    sig += probe["TP"], capacitor[1]
    gnd += capacitor[2]

    schematic = design.schematic("Main")
    drawing = schematic.drawing(at=(10, 20), direction="Left")
    before = drawing.here.point, drawing.direction

    with drawing.frame((100, 50), direction="Down"):
        anchors = drawing.stack(count=6, direction="Down", pitch=10)
        placed = drawing.place(probe, at=anchors[0])
        cap = drawing.two_terminal(capacitor).at(anchors[1]).right()
        stub = drawing.signal_stub(
            sig,
            at=anchors[2],
            side="Right",
            length=4,
            label_gap=1,
            label="SIG",
        )
        vcc_port = drawing.power("VCC", net=vcc, at=anchors[3])
        gnd_port = drawing.ground(net=gnd, at=anchors[4])
        drawing.connect(anchors[0].right(2), anchors[5].right(2), net=sig, shape="-")

    projection = schematic_projection(schematic)

    assert [anchor.point for anchor in anchors] == [
        (100.0, 50.0),
        (100.0, 60.0),
        (100.0, 70.0),
        (100.0, 80.0),
        (100.0, 90.0),
        (100.0, 100.0),
    ]
    assert placed.TP.point == (100.0, 50.0)
    assert cap.start.point == (100.0, 60.0)
    assert cap.end.point == (120.0, 60.0)
    assert stub.start.point == (100.0, 70.0)
    assert stub.end.point == (104.0, 70.0)
    assert vcc_port.pin.point == (100.0, 80.0)
    assert gnd_port.pin.point == (100.0, 90.0)
    assert [_wire_points(projection, index) for index in range(2)] == [
        [(100.0, 70.0), (104.0, 70.0)],
        [(102.0, 50.0), (102.0, 100.0)],
    ]
    assert [label["position"] for label in projection["net_labels"]] == [
        {"x": 105.0, "y": 70.0},
    ]
    assert [port["position"] for port in projection["power_ports"]] == [
        {"x": 100.0, "y": 80.0},
        {"x": 100.0, "y": 90.0},
    ]
    assert (drawing.here.point, drawing.direction) == before


def test_python_schematic_drawing_stack_rejects_invalid_inputs_clearly():
    design = volt.Design("schematic-drawing-stack-invalid")
    schematic = design.schematic("Main")
    drawing = schematic.drawing()
    before = schematic.to_json()

    invalid_cases = (
        ({"count": 1.5}, TypeError, "integer"),
        ({"count": True}, TypeError, "integer"),
        ({"count": -1}, ValueError, "negative"),
        ({"count": 1, "pitch": 0}, ValueError, "positive"),
        ({"count": 1, "direction": "diagonal"}, ValueError, "Right, Down, Left, or Up"),
    )

    for kwargs, error_type, message in invalid_cases:
        try:
            drawing.stack(**kwargs)
        except error_type as error:
            assert message in str(error)
        else:
            raise AssertionError(f"stack should reject {kwargs!r}")

    assert drawing.stack(count=0) == ()
    assert schematic.to_json() == before


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
        message = str(error)
        assert "Cannot pop schematic drawing cursor state" in message
        assert "sheet 'Main'" in message
        assert "stack is empty" in message
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
        message = str(error)
        assert "different design" in message
        assert "sheet 'Main'" in message
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
        message = str(error)
        assert "Cannot pop schematic drawing cursor state" in message
        assert "sheet 'Main'" in message
        assert "stack is empty" in message
    else:
        raise AssertionError("hold should restore the drawing stack depth")
