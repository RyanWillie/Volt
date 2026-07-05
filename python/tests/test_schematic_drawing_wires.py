import json
import re

import volt

from helpers import _wire_points
from schematic_drawing_test_helpers import schematic_projection


def test_python_schematic_wire_shape_shortcuts_lower_to_expected_points():
    cases = {
        "-": [(0.0, 0.0), (40.0, 20.0)],
        "-|": [(0.0, 0.0), (40.0, 0.0), (40.0, 20.0)],
        "|-": [(0.0, 0.0), (0.0, 20.0), (40.0, 20.0)],
        "|-|": [(0.0, 0.0), (0.0, 10.0), (40.0, 10.0), (40.0, 20.0)],
        "n": [(0.0, 0.0), (0.0, 10.0), (40.0, 10.0), (40.0, 20.0)],
        "-|-": [(0.0, 0.0), (20.0, 0.0), (20.0, 20.0), (40.0, 20.0)],
        "c": [(0.0, 0.0), (20.0, 0.0), (20.0, 20.0), (40.0, 20.0)],
    }

    for shape, expected in cases.items():
        design = volt.Design(f"shape-{shape}")
        net = design.net("ROUTE")
        schematic = design.schematic("Main")

        schematic.wire(net).at((0, 0)).to((40, 20)).shape(shape)
        projection = schematic_projection(schematic)

        assert _wire_points(projection) == expected
        expected_intent = "Direct" if shape == "-" else "Orthogonal"
        assert projection["wire_runs"][0]["route_intent"] == expected_intent


def test_python_schematic_wire_shape_k_controls_first_bend():
    design = volt.Design("shape-k")
    net = design.net("ROUTE")
    schematic = design.schematic("Main")

    schematic.connect((0, 0), (40, 20), net=net, shape="-|", k=10)
    schematic.connect((0, 40), (40, 20), net=net, shape="|-", k=-5)
    schematic.wire(net).at((60, 0)).to((100, 20)).shape("c", k=15)
    schematic.wire(net).at((120, 0)).to((160, 20)).shape("n", k=5)

    projection = schematic_projection(schematic)

    assert _wire_points(projection, 0) == [
        (0.0, 0.0),
        (10.0, 0.0),
        (10.0, 20.0),
        (40.0, 20.0),
    ]
    assert _wire_points(projection, 1) == [
        (0.0, 40.0),
        (0.0, 35.0),
        (40.0, 35.0),
        (40.0, 20.0),
    ]
    assert _wire_points(projection, 2) == [
        (60.0, 0.0),
        (75.0, 0.0),
        (75.0, 20.0),
        (100.0, 20.0),
    ]
    assert _wire_points(projection, 3) == [
        (120.0, 0.0),
        (120.0, 5.0),
        (160.0, 5.0),
        (160.0, 20.0),
    ]


def test_python_schematic_drawing_wire_tox_toy_and_line_update_cursor():
    design = volt.Design("schematic-wire-cursor")
    net = design.net("LOOP")
    schematic = design.schematic("Main")

    with schematic.drawing(at=(10, 10), unit=20) as drawing:
        drawing.wire(net).tox(50).toy((70, 30))
        assert drawing.here.point == (50.0, 30.0)

        drawing.line(net).right(20).down(10).to((20, 60))
        assert drawing.here.point == (20.0, 60.0)

    projection = schematic_projection(schematic)

    assert _wire_points(projection, 0) == [
        (10.0, 10.0),
        (50.0, 10.0),
        (50.0, 30.0),
    ]
    assert _wire_points(projection, 1) == [
        (50.0, 30.0),
        (70.0, 30.0),
        (70.0, 40.0),
        (20.0, 60.0),
    ]


def test_python_schematic_wire_shortcuts_use_existing_collision_rules():
    design = volt.Design("schematic-wire-shape-collision")
    vcc = design.net("VCC", kind="power")
    gnd = design.net("GND", kind="ground")
    schematic = design.schematic("Main")

    schematic.connect((0, 0), (40, 0), net=vcc, shape="-")
    before = schematic.to_json()

    try:
        schematic.connect((10, 0), (30, 10), net=gnd, shape="-|")
    except volt.InvalidStateError as error:
        assert str(error) == "Schematic wire run collides with a different logical net"
        assert isinstance(error, RuntimeError)
    else:
        raise AssertionError("shape shortcuts must keep schematic collision checks")

    assert schematic.to_json() == before


def test_python_schematic_wire_shortcuts_reject_invalid_shapes_clearly():
    design = volt.Design("schematic-wire-invalid-shape")
    net = design.net("ROUTE")
    schematic = design.schematic("Main")
    before = schematic.to_json()

    try:
        schematic.wire(net).at((0, 0)).to((20, 20)).shape("z")
    except ValueError as error:
        message = str(error)
        assert "Invalid schematic wire shape 'z'" in message
        assert "ROUTE" in message
        assert "sheet 'Main'" in message
        assert "expected one of -, -|, |-, |-|, n, -|-, or c" in message
    else:
        raise AssertionError("invalid wire shapes should be rejected")

    assert schematic.to_json() == before

    drawing = schematic.drawing(at=(5, 6))
    try:
        drawing.wire(net).to((25, 26), shape="bad")
    except ValueError as error:
        message = str(error)
        assert "Invalid schematic wire shape 'bad'" in message
        assert "sheet 'Main'" in message
    else:
        raise AssertionError("drawing wire invalid shapes should restore authoring state")

    assert drawing.here.point == (5.0, 6.0)
    drawing.move(dx=1)
    assert schematic.to_json() == before

    drawing = schematic.drawing(at=(50, 60))
    try:
        drawing.wire(net).to((70, 60)).via((70, 80)).shape("-|")
    except ValueError as error:
        message = str(error)
        assert "need exactly two endpoints" in message
        assert "sheet 'Main'" in message
    else:
        raise AssertionError("shape endpoint errors should restore drawing authoring state")

    assert drawing.here.point == (50.0, 60.0)
    drawing.move(dx=1)
    assert schematic.to_json() == before

    drawing = schematic.drawing(at=(90, 100))
    try:
        drawing.wire(net).shape("-|")
    except ValueError as error:
        message = str(error)
        assert "need exactly two endpoints" in message
        assert "sheet 'Main'" in message
    else:
        raise AssertionError("single-endpoint shape calls should restore drawing state")

    assert drawing.here.point == (90.0, 100.0)
    drawing.move(dx=1)
    assert schematic.to_json() == before


def test_python_schematic_wire_shape_rejects_reuse_after_materialization():
    design = volt.Design("schematic-wire-shape-reuse")
    net = design.net("ROUTE")
    schematic = design.schematic("Main")

    builder = schematic.wire(net).at((0, 0)).to((20, 20))
    builder.shape("-|")
    before = schematic.to_json()

    try:
        builder.shape("|-")
    except ValueError as error:
        assert str(error) == "Cannot modify a materialized schematic wire"
    else:
        raise AssertionError("materialized schematic wire builders should reject reuse")

    assert schematic.to_json() == before


def test_python_schematic_drawing_wire_without_endpoint_clears_pending():
    design = volt.Design("schematic-wire-missing-endpoint")
    net = design.net("ROUTE")
    schematic = design.schematic("Main")
    drawing = schematic.drawing(at=(5, 6))
    before = schematic.to_json()

    drawing.wire(net)
    try:
        drawing.move(dx=10)
    except ValueError as error:
        assert str(error) == "Schematic drawing wire needs an endpoint before materialization"
    else:
        raise AssertionError("drawing wires without endpoints should fail clearly")

    assert schematic.to_json() == before
    drawing.move(dx=10)
    assert drawing.here.point == (15.0, 6.0)


def test_python_schematic_wire_shortcuts_draw_rectangular_loop_without_logical_mutation():
    design = volt.Design("schematic-wire-logical-boundary")
    vcc = design.net("VCC", kind="power")
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
        drawing.move_from(resistor.end.right(40).down(20), direction="Down")
        led = drawing.two_terminal(d1).down().reverse()
        vcc_port = drawing.power("VCC", net=vcc, at=resistor.start.left(20))
        gnd_port = drawing.ground(net=gnd, at=led.end.down(20))

        drawing.connect(vcc_port, resistor.start, net=vcc, shape="-")
        drawing.connect(resistor.end, led.start, shape="-|", k=20)
        drawing.connect(led.end, gnd_port, net=gnd, shape="|-", k=10)

    projection = schematic_projection(schematic)

    assert design.to_json() == logical_before
    assert len(projection["wire_runs"]) == 3
    assert _wire_points(projection, 1) == [
        (20.0, 0.0),
        (40.0, 0.0),
        (40.0, 20.0),
        (60.0, 20.0),
    ]
    assert not schematic.validate().has_errors
