import json
import re

import volt

from helpers import _wire_points
from schematic_drawing_test_helpers import schematic_projection


def test_python_schematic_local_signal_stub_sugar_emits_wire_and_label_only():
    design = volt.Design("schematic-local-signal-stub")
    sig = design.net("SUPPORT/SWDIO")
    probe = design.test_point(ref="TP1")
    sig += probe["TP"]

    schematic = design.schematic("Main")
    logical_before = design.to_json()

    with schematic.drawing(at=(40, 40), unit=20) as drawing:
        placed = drawing.place(probe)
        stub = drawing.signal_stub(
            sig,
            at=placed.TP,
            side="left",
            length=6,
            label_gap=1,
            label="SWDIO",
        )

    projection = schematic_projection(schematic)

    assert design.to_json() == logical_before
    assert stub.start.point == (40.0, 40.0)
    assert stub.end.point == (34.0, 40.0)
    assert stub.label_position.point == (33.0, 40.0)
    assert projection["wire_runs"] == [
        {
            "id": "wire_run:0",
            "sheet": "sheet:0",
            "net": f"net:{sig.index}",
            "points": [{"x": 40.0, "y": 40.0}, {"x": 34.0, "y": 40.0}],
            "route_intent": "Direct",
        }
    ]
    assert projection["net_labels"] == [
        {
            "id": "net_label:0",
            "sheet": "sheet:0",
            "net": f"net:{sig.index}",
            "position": {"x": 33.0, "y": 40.0},
            "text_position": {"x": 33.0, "y": 36.0},
            "orientation": "Left",
            "label": "SWDIO",
        }
    ]

    svg = schematic.to_svg()
    assert f'<polyline class="wire-run" data-net="net:{sig.index}" points="40,40 34,40"/>' in svg
    assert f'<text class="net-label" data-net="net:{sig.index}" x="33" y="36"' in svg
    assert ">SWDIO</text>" in svg
    assert ">SUPPORT/SWDIO</text>" not in svg
    assert 'class="sheet-port off-page"' not in svg


def test_python_schematic_signal_tag_sugar_emits_short_wire_and_port_tag():
    design = volt.Design("schematic-signal-tag")
    sig = design.net("USB/MCU_USB_DP")
    probe = design.test_point(ref="TP1")
    sig += probe["TP"]

    schematic = design.schematic("Main")
    logical_before = design.to_json()

    with schematic.drawing(at=(40, 40), unit=20) as drawing:
        placed = drawing.place(probe)
        tag = drawing.signal_tag(
            sig,
            at=placed.TP,
            side="right",
            length=6,
            label="USB D+",
        )

    projection = schematic_projection(schematic)

    assert design.to_json() == logical_before
    assert tag.start.point == (40.0, 40.0)
    assert tag.end.point == (46.0, 40.0)
    assert tag.port.pin.point == (46.0, 40.0)
    assert projection["net_labels"] == []
    assert projection["wire_runs"] == [
        {
            "id": "wire_run:0",
            "sheet": "sheet:0",
            "net": f"net:{sig.index}",
            "points": [{"x": 40.0, "y": 40.0}, {"x": 46.0, "y": 40.0}],
            "route_intent": "Direct",
        }
    ]
    assert projection["sheet_ports"] == [
        {
            "id": "sheet_port:0",
            "sheet": "sheet:0",
            "net": f"net:{sig.index}",
            "name": "USB D+",
            "kind": "Bidirectional",
            "position": {"x": 46.0, "y": 40.0},
            "orientation": "Right",
        }
    ]

    svg = schematic.to_svg()
    assert f'<polyline class="wire-run" data-net="net:{sig.index}" points="40,40 46,40"/>' in svg
    assert f'<g class="sheet-port bidirectional" data-net="net:{sig.index}"' in svg
    assert ">USB D+</text>" in svg
    assert ">USB/MCU_USB_DP</text>" not in svg


def test_python_schematic_local_label_and_aligned_signal_stubs_are_side_aware():
    design = volt.Design("schematic-local-signal-stub-column")
    swdio = design.net("SWDIO")
    swclk = design.net("SWCLK")
    header = design.connector_1x02(ref="J1")
    swdio += header[1]
    swclk += header[2]

    schematic = design.schematic("Main")

    with schematic.drawing(unit=20) as drawing:
        placed = drawing.place(header, at=(40, 20))
        label = drawing.local_label(swdio, at=placed[1], side="right", offset=3)
        stubs = drawing.signal_stubs(
            ((swdio, placed[1]), (swclk, placed[2])),
            side="right",
            length=5,
            label_gap=2,
        )
        column = drawing.signal_stubs(
            (swdio, swclk),
            at=(70, 20),
            side="right",
            pitch=6,
            length=4,
            label_gap=1,
        )

    projection = schematic_projection(schematic)

    assert label.orientation == "Right"
    assert [stub.end.point for stub in stubs] == [(45.0, 20.0), (45.0, 28.0)]
    assert [stub.start.point for stub in column] == [(70.0, 20.0), (70.0, 26.0)]
    assert [stub.end.point for stub in column] == [(74.0, 20.0), (74.0, 26.0)]
    assert [wire["points"] for wire in projection["wire_runs"]] == [
        [{"x": 40.0, "y": 20.0}, {"x": 45.0, "y": 20.0}],
        [{"x": 40.0, "y": 28.0}, {"x": 45.0, "y": 28.0}],
        [{"x": 70.0, "y": 20.0}, {"x": 74.0, "y": 20.0}],
        [{"x": 70.0, "y": 26.0}, {"x": 74.0, "y": 26.0}],
    ]
    assert [label["position"] for label in projection["net_labels"]] == [
        {"x": 43.0, "y": 20.0},
        {"x": 47.0, "y": 20.0},
        {"x": 47.0, "y": 28.0},
        {"x": 75.0, "y": 20.0},
        {"x": 75.0, "y": 26.0},
    ]
    assert [label["orientation"] for label in projection["net_labels"]] == [
        "Right",
        "Right",
        "Right",
        "Right",
        "Right",
    ]


def test_python_schematic_signal_stubs_infer_anchor_side_and_accept_group_labels():
    design = volt.Design("schematic-signal-stub-anchor-defaults")
    swdio = design.net("SWDIO")
    swclk = design.net("SWCLK")
    header = design.connector_1x02(ref="J1")
    swdio += header[1]
    swclk += header[2]

    schematic = design.schematic("Main")

    with schematic.drawing(unit=20) as drawing:
        placed = drawing.place(header, at=(40, 20))
        stub = drawing.signal_stub(swdio, at=placed[1], length=5, label="DIO")
        group = drawing.signal_stubs(
            ((swclk, placed[2], "CLK"),),
            length=4,
            label_gap=1,
        )

    projection = schematic_projection(schematic)

    assert stub.side == placed[1].orientation
    assert group[0].side == placed[2].orientation
    assert {label["label"] for label in projection["net_labels"]} == {"DIO", "CLK"}


def test_python_schematic_signal_stubs_infer_base_side_for_generated_stack():
    design = volt.Design("schematic-signal-stub-stack-anchor-default")
    first = design.net("FIRST")
    second = design.net("SECOND")

    schematic = design.schematic("Main")

    with schematic.drawing(unit=20) as drawing:
        base = drawing.power("FIRST", net=first, at=(40, 20), orient="Down")
        stubs = drawing.signal_stubs(
            (first, second),
            at=base,
            pitch=6,
            length=5,
            label_gap=1,
        )

    assert [stub.side for stub in stubs] == ["Down", "Down"]
    assert [stub.start.point for stub in stubs] == [(40.0, 20.0), (46.0, 20.0)]
    assert [stub.end.point for stub in stubs] == [(40.0, 25.0), (46.0, 25.0)]


def test_python_schematic_signal_stub_group_preserves_base_pin_net_validation():
    design = volt.Design("schematic-local-signal-stub-group-validation")
    swdio = design.net("SWDIO")
    swclk = design.net("SWCLK")
    header = design.connector_1x02(ref="J1")
    swdio += header[1]
    swclk += header[2]

    schematic = design.schematic("Main")

    with schematic.drawing(unit=20) as drawing:
        placed = drawing.place(header, at=(40, 20))
        try:
            drawing.signal_stubs((swclk,), at=placed[1], side="right")
        except ValueError as error:
            message = str(error)
            assert "the pin belongs to SWDIO" in message
            assert "signal stub" in message
        else:
            raise AssertionError("grouped signal stubs should validate the base pin net")


def test_python_schematic_signal_stub_sugar_rejects_invalid_inputs_clearly():
    design = volt.Design("signal-stub-invalid")
    other = volt.Design("signal-stub-invalid-other")
    sig = design.net("SIG")
    wrong = design.net("WRONG")
    other_net = other.net("X")
    probe = design.test_point(ref="TP1")
    sig += probe["TP"]
    schematic = design.schematic("Main")

    with schematic.drawing(at=(0, 0), unit=20) as drawing:
        placed = drawing.place(probe)
        before = schematic.to_json()

        try:
            drawing.local_label(other_net, at=(0, 0))
        except ValueError as error:
            assert "different design" in str(error)
            assert "sheet 'Main'" in str(error)
        else:
            raise AssertionError("local_label should reject cross-design net")

        try:
            drawing.local_label(sig, at=(0, 0), offset=-1)
        except ValueError as error:
            assert "negative" in str(error).lower() or "offset" in str(error).lower()
        else:
            raise AssertionError("local_label should reject negative offset")

        try:
            drawing.signal_stub(other_net, at=(0, 0))
        except ValueError as error:
            assert "different design" in str(error)
            assert "sheet 'Main'" in str(error)
        else:
            raise AssertionError("signal_stub should reject cross-design net")

        try:
            drawing.signal_stub(wrong, at=placed.TP)
        except ValueError as error:
            assert "WRONG" in str(error)
            assert "sheet 'Main'" in str(error)
        else:
            raise AssertionError("signal_stub should reject net mismatch at pin anchor")

        try:
            drawing.signal_stub(sig, at=(0, 0), length=0)
        except ValueError as error:
            assert "positive" in str(error).lower() or "length" in str(error).lower()
        else:
            raise AssertionError("signal_stub should reject zero length")

        try:
            drawing.signal_stub(sig, at=(0, 0), length=-4)
        except ValueError as error:
            assert "positive" in str(error).lower() or "length" in str(error).lower()
        else:
            raise AssertionError("signal_stub should reject negative length")

        try:
            schematic.signal_stubs((sig, wrong), at=None)
        except TypeError as error:
            assert "anchor" in str(error).lower() or "at=" in str(error)
        else:
            raise AssertionError("signal_stubs should require anchors when at= is absent")

        for item in ((sig, 42), (sig, 42, "SIG")):
            try:
                drawing.signal_stubs((item,), at=(0, 0))
            except TypeError as error:
                assert "Signal stub entries" in str(error)
            else:
                raise AssertionError("malformed signal_stubs entries should be rejected")

        try:
            drawing.signal_stubs(((sig, placed.TP, 123),), side="right")
        except TypeError as error:
            assert "Signal stub labels must be strings" in str(error)
        else:
            raise AssertionError("non-string signal stub labels should be rejected")

        assert schematic.to_json() == before


def test_python_schematic_label_sugar_rejects_invalid_inputs_clearly():
    design = volt.Design("schematic-label-invalid")
    other = volt.Design("schematic-label-invalid-other")
    sig = design.net("SIG")
    r1 = design.R("10k", ref="R1")
    schematic = design.schematic("Main")
    other_anchor = volt.SchematicAnchor((0, 0), design=other)

    with schematic.drawing(unit=20) as drawing:
        resistor = drawing.two_terminal(r1).right()
        _ = resistor.start
        before = schematic.to_json()

        try:
            resistor.label(123)
        except TypeError as error:
            assert str(error) == "Schematic element labels must be strings"
        else:
            raise AssertionError("element labels should reject non-string text")

        try:
            resistor.label("x", offset=4, ofst=4)
        except ValueError as error:
            assert str(error) == "Use either offset= or ofst= for schematic element labels"
        else:
            raise AssertionError("element labels should reject competing offsets")

        try:
            drawing.net_label("MISSING", at=(0, 0))
        except ValueError as error:
            assert "existing logical net named 'MISSING'" in str(error)
        else:
            raise AssertionError("net label sugar should require an existing logical net")

        try:
            drawing.net_label(123, at=(0, 0))
        except TypeError as error:
            assert str(error) == "Schematic net labels expect a Net handle or existing net name"
        else:
            raise AssertionError("net label sugar should reject non-string, non-net names")

        try:
            drawing.net_label(sig, at=other_anchor)
        except ValueError as error:
            message = str(error)
            assert "different design" in message
            assert "sheet 'Main'" in message
        else:
            raise AssertionError("net label sugar should reject cross-design anchors")

        try:
            drawing.sheet_port("MISSING", at=(0, 0))
        except ValueError as error:
            assert "existing logical net named 'MISSING'" in str(error)
        else:
            raise AssertionError("sheet port sugar should require an existing logical net")

        try:
            drawing.sheet_port(123, at=(0, 0))
        except TypeError as error:
            assert str(error) == "Schematic sheet port names must be strings"
        else:
            raise AssertionError("sheet port sugar should reject non-string names")

        try:
            drawing.sheet_port("SIG", at=other_anchor)
        except ValueError as error:
            message = str(error)
            assert "different design" in message
            assert "sheet 'Main'" in message
        else:
            raise AssertionError("sheet port sugar should reject cross-design anchors")

        assert schematic.to_json() == before
