import json
import re

import volt

from helpers import _wire_points
from schematic_drawing_test_helpers import schematic_projection


def test_python_schematic_drawing_frame_scopes_tuple_coordinates_and_restores_state():
    design = volt.Design("schematic-drawing-frame")
    sig = design.net("SIG")
    probe = design.test_point(ref="TP1")
    sig += probe["TP"]

    schematic = design.schematic("Main")
    drawing = schematic.drawing(at=(10, 20), direction="Left")
    before = drawing.here.point, drawing.direction

    with drawing.frame((100, 50), direction="Down") as frame:
        placed = frame.place(probe, at=(2, 3))
        frame.net_label(sig, at=(4, 5), label="IN_FRAME")
        assert frame.direction == "Down"

    drawing.net_label(sig, at=(4, 5), label="OUTSIDE")
    projection = schematic_projection(schematic)

    assert placed.TP.point == (102.0, 53.0)
    assert (drawing.here.point, drawing.direction) == before
    assert [label["position"] for label in projection["net_labels"]] == [
        {"x": 104.0, "y": 55.0},
        {"x": 4.0, "y": 5.0},
    ]


def test_python_schematic_drawing_frame_nests_and_preserves_absolute_anchors():
    design = volt.Design("schematic-drawing-frame-nested")
    sig = design.net("SIG")
    probe = design.test_point(ref="TP1")
    sig += probe["TP"]

    schematic = design.schematic("Main")
    drawing = schematic.drawing(at=(10, 20), direction="Left")
    absolute_anchor = volt.SchematicAnchor((5, 6), design=design)

    with drawing.frame((100, 50)):
        port = drawing.power("SIG", net=sig, at=(10, 10), orient="Down")
        drawing.net_label(sig, at=absolute_anchor, label="ABS")
        drawing.net_label(sig, at=port, label="PORT")
        with drawing.frame((20, 30)):
            placed = drawing.place(probe, at=(1, 2))
            drawing.net_label(sig, at=(3, 4), label="NESTED")

    projection = schematic_projection(schematic)

    assert port.pin.point == (110.0, 60.0)
    assert placed.TP.point == (121.0, 82.0)
    assert [label["position"] for label in projection["net_labels"]] == [
        {"x": 5.0, "y": 6.0},
        {"x": 110.0, "y": 60.0},
        {"x": 123.0, "y": 84.0},
    ]


def test_python_schematic_drawing_frame_restores_state_after_exception():
    design = volt.Design("schematic-drawing-frame-exception")
    sig = design.net("SIG")
    schematic = design.schematic("Main")
    drawing = schematic.drawing(at=(10, 20), direction="Left")
    before = drawing.here.point, drawing.direction

    try:
        with drawing.frame((100, 50), direction="Down"):
            drawing.move(dx=7, dy=9)
            drawing.push()
            drawing.net_label(sig, at=(1, 2), label="INSIDE")
            raise RuntimeError("boom")
    except RuntimeError:
        pass

    drawing.net_label(sig, at=(1, 2), label="OUTSIDE")
    projection = schematic_projection(schematic)

    assert (drawing.here.point, drawing.direction) == before
    assert [label["position"] for label in projection["net_labels"]] == [
        {"x": 101.0, "y": 52.0},
        {"x": 1.0, "y": 2.0},
    ]
    try:
        drawing.pop()
    except ValueError as error:
        assert "stack is empty" in str(error)
    else:
        raise AssertionError("frame should restore the drawing stack depth")


def test_python_schematic_drawing_frame_offsets_point_accepting_operations():
    design = volt.Design("schematic-drawing-frame-operations")
    vcc = design.net("VCC", kind="power")
    gnd = design.net("GND", kind="ground")
    sig = design.net("SIG")
    aux = design.net("AUX")
    probe = design.test_point(ref="TP1")
    resistor_part = design.R("10k", ref="R1")
    probe["TP"].mark_no_connect()

    schematic = design.schematic("Main")

    with schematic.drawing(direction="Left") as drawing:
        with drawing.frame((100, 50), direction="Down"):
            placed_probe = drawing.place(probe, at=(1, 2))
            resistor = drawing.two_terminal(resistor_part).at((10, 0)).right()
            drawing.connect((0, 0), (10, 0), net=sig, shape="-")
            drawing.wire(sig).at((0, 5)).to((10, 5)).direct()
            drawing.local_label(sig, at=(2, 8), side="Right", offset=3)
            stub = drawing.signal_stub(
                sig,
                at=(3, 10),
                side="Right",
                length=4,
                label_gap=1,
                label="S1",
            )
            stubs = drawing.signal_stubs(
                ((sig, (4, 14), "S2"), (aux, (4, 18), "AUX")),
                side="Right",
                length=4,
                label_gap=1,
            )
            drawing.junction(sig, at=(5, 22))
            drawing.power("VCC", net=vcc, at=(6, 24), orient="Up")
            drawing.ground(net=gnd, at=(8, 24), orient="Down")
            sheet_port = drawing.sheet_port("SIG", net=sig, at=(10, 24))
            drawing.off_page("AUX", net=aux, at=(12, 24))
            drawing.net_label(sig, at=sheet_port, label="PORT")
            drawing.no_connect(placed_probe.TP, reason="reserved")

    projection = schematic_projection(schematic)

    assert placed_probe.TP.point == (101.0, 52.0)
    assert resistor.start.point == (110.0, 50.0)
    assert resistor.end.point == (130.0, 50.0)
    assert stub.start.point == (103.0, 60.0)
    assert stub.end.point == (107.0, 60.0)
    assert [item.start.point for item in stubs] == [(104.0, 64.0), (104.0, 68.0)]
    assert [item.end.point for item in stubs] == [(108.0, 64.0), (108.0, 68.0)]
    assert [_wire_points(projection, index) for index in range(5)] == [
        [(100.0, 50.0), (110.0, 50.0)],
        [(100.0, 55.0), (110.0, 55.0)],
        [(103.0, 60.0), (107.0, 60.0)],
        [(104.0, 64.0), (108.0, 64.0)],
        [(104.0, 68.0), (108.0, 68.0)],
    ]
    assert [label["position"] for label in projection["net_labels"]] == [
        {"x": 105.0, "y": 58.0},
        {"x": 108.0, "y": 60.0},
        {"x": 109.0, "y": 64.0},
        {"x": 109.0, "y": 68.0},
        {"x": 110.0, "y": 74.0},
    ]
    assert projection["junctions"][0]["position"] == {"x": 105.0, "y": 72.0}
    assert [port["position"] for port in projection["power_ports"]] == [
        {"x": 106.0, "y": 74.0},
        {"x": 108.0, "y": 74.0},
    ]
    assert [port["position"] for port in projection["sheet_ports"]] == [
        {"x": 110.0, "y": 74.0},
        {"x": 112.0, "y": 74.0},
    ]
    assert projection["no_connect_markers"][0]["position"] == {"x": 101.0, "y": 52.0}


def test_python_schematic_drawing_frame_signal_stubs_absolute_anchor_passes_through():
    design = volt.Design("schematic-drawing-frame-stub-anchor")
    sig = design.net("SIG")
    aux = design.net("AUX")

    schematic = design.schematic("Main")
    absolute_anchor = volt.SchematicAnchor((7, 9), design=design)

    with schematic.drawing() as drawing:
        with drawing.frame((100, 50)):
            stubs = drawing.signal_stubs(
                ((sig, (2, 4), "TUPLE"), (aux, absolute_anchor, "ABS")),
                side="Right",
                length=4,
                label_gap=1,
            )

    # Tuple anchor (2, 4) is translated by the frame origin (100, 50).
    assert stubs[0].start.point == (102.0, 54.0)
    assert stubs[0].end.point == (106.0, 54.0)
    # Absolute SchematicAnchor passes through the frame without any offset.
    assert stubs[1].start.point == (7.0, 9.0)
    assert stubs[1].end.point == (11.0, 9.0)
