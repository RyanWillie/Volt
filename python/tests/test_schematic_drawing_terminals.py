import json
import re

import volt

from helpers import _wire_points
from schematic_drawing_test_helpers import schematic_projection


def test_python_schematic_power_and_ground_require_explicit_net_for_non_pin_anchor():
    design = volt.Design("schematic-explicit-port-net")
    vcc = design.net("VCC", kind="power")
    gnd = design.net("GND", kind="ground")
    r1 = design.R("10k", ref="R1")
    vcc += r1[1]
    gnd += r1[2]

    schematic = design.schematic("Main")

    with schematic.drawing(unit=20) as drawing:
        resistor = drawing.two_terminal(r1).right()
        _ = resistor.start
        before = schematic.to_json()

        try:
            drawing.power("VCC", at=(0, -20))
        except ValueError as error:
            assert "non-pin anchor" in str(error)
        else:
            raise AssertionError("power ports on plain coordinates must require explicit net")

        try:
            drawing.ground(at=(0, 20))
        except ValueError as error:
            assert "non-pin anchor" in str(error)
        else:
            raise AssertionError("ground ports on plain coordinates must require explicit net")

        assert schematic.to_json() == before


def test_python_schematic_terminal_marker_is_generic_and_net_bound():
    design = volt.Design("schematic-generic-terminal-marker")
    sig = design.net("SIG")
    other = design.net("OTHER")
    probe = design.test_point(ref="TP1")
    sig += probe["TP"]

    schematic = design.schematic("Main")
    logical_before = design.to_json()

    with schematic.drawing(unit=10) as drawing:
        placed = drawing.place(probe)
        inferred = drawing.terminal(at=placed.TP, kind="Ground")
        explicit = drawing.terminal("LOCAL", net=sig, at=(20, 10), kind="Power", orient="Right")
        before_invalid = schematic.to_json()

        try:
            drawing.terminal("FLOATING", at=(30, 10))
        except ValueError as error:
            assert "terminal marker" in str(error)
            assert "non-pin anchor" in str(error)
        else:
            raise AssertionError("terminal markers on plain coordinates must require explicit net")

        try:
            drawing.terminal(sig, net=other, at=(40, 10))
        except ValueError as error:
            assert "either first or as net=" in str(error)
        else:
            raise AssertionError("terminal marker nets must not be passed twice")

        assert schematic.to_json() == before_invalid

    projection = schematic_projection(schematic)

    assert design.to_json() == logical_before
    assert inferred.net.index == sig.index
    assert explicit.net.index == sig.index
    assert projection["power_ports"] == [
        {
            "id": "power_port:0",
            "sheet": "sheet:0",
            "net": f"net:{sig.index}",
            "kind": "Ground",
            "position": {"x": 0.0, "y": 0.0},
            "orientation": "Down",
            "label_position": {"x": 0.0, "y": 12.2},
        },
        {
            "id": "power_port:1",
            "sheet": "sheet:0",
            "net": f"net:{sig.index}",
            "kind": "Power",
            "position": {"x": 20.0, "y": 10.0},
            "orientation": "Right",
            "label": "LOCAL",
            "label_position": {"x": 33.4, "y": 10.0},
        },
    ]


def test_python_schematic_terminal_stub_draws_existing_net_wire_and_marker_only():
    design = volt.Design("schematic-generic-terminal-stub")
    sig = design.net("SIG")
    probe = design.test_point(ref="TP1")
    sig += probe["TP"]

    schematic = design.schematic("Main")
    logical_before = design.to_json()

    with schematic.drawing(unit=10) as drawing:
        placed = drawing.place(probe)
        stub = drawing.terminal_stub("SIG_REF", at=placed.TP, kind="Power", length=12)

    projection = schematic_projection(schematic)

    assert design.to_json() == logical_before
    assert stub.net.index == sig.index
    assert stub.side == placed.TP.orientation
    assert stub.start.point == (0.0, 0.0)
    assert stub.end.point == (-12.0, 0.0)
    assert stub.port.pin.point == (-12.0, 0.0)
    assert projection["wire_runs"] == [
        {
            "id": "wire_run:0",
            "sheet": "sheet:0",
            "net": f"net:{sig.index}",
            "points": [{"x": 0.0, "y": 0.0}, {"x": -12.0, "y": 0.0}],
            "route_intent": "Direct",
        }
    ]
    assert projection["power_ports"] == [
        {
            "id": "power_port:0",
            "sheet": "sheet:0",
            "net": f"net:{sig.index}",
            "kind": "Power",
            "position": {"x": -12.0, "y": 0.0},
            "orientation": "Up",
            "label": "SIG_REF",
        }
    ]


def test_python_schematic_power_and_ground_stubs_are_presentation_only():
    design = volt.Design("schematic-power-ground-stubs")
    vcc = design.net("VCC", kind="power")
    gnd = design.net("GND", kind="ground")
    r1 = design.R("10k", ref="R1")
    vcc += r1[1]
    gnd += r1[2]

    schematic = design.schematic("Main")
    logical_before = design.to_json()

    with schematic.drawing(unit=10) as drawing:
        resistor = drawing.two_terminal(r1).right()
        power = drawing.power_stub("VCC", at=resistor.start, length=8)
        ground = drawing.ground_stub(at=resistor.end, length=6)

    projection = schematic_projection(schematic)

    assert design.to_json() == logical_before
    assert power.end.point == (0.0, -8.0)
    assert ground.end.point == (10.0, 6.0)
    assert [wire["points"] for wire in projection["wire_runs"]] == [
        [{"x": 0.0, "y": 0.0}, {"x": 0.0, "y": -8.0}],
        [{"x": 10.0, "y": 0.0}, {"x": 10.0, "y": 6.0}],
    ]
    assert [(port["kind"], port["orientation"]) for port in projection["power_ports"]] == [
        ("Power", "Up"),
        ("Ground", "Down"),
    ]


def test_python_schematic_terminal_stub_rejects_invalid_inputs_without_projection():
    design = volt.Design("schematic-terminal-stub-invalid")
    sig = design.net("SIG")
    wrong = design.net("WRONG")
    other = volt.Design("schematic-terminal-stub-invalid-other")
    other_net = other.net("OTHER")
    probe = design.test_point(ref="TP1")
    sig += probe["TP"]

    schematic = design.schematic("Main")

    with schematic.drawing(unit=10) as drawing:
        placed = drawing.place(probe)
        before = schematic.to_json()

        try:
            drawing.terminal_stub("SIG", at=placed.TP, length=0)
        except ValueError as error:
            assert "positive" in str(error).lower() or "length" in str(error).lower()
        else:
            raise AssertionError("terminal_stub should reject zero length")

        try:
            drawing.terminal_stub("SIG", at=placed.TP, side="diagonal")
        except ValueError as error:
            assert "Right, Down, Left, or Up" in str(error)
        else:
            raise AssertionError("terminal_stub should reject invalid sides")

        try:
            drawing.terminal_stub("FLOATING", at=(20, 20))
        except ValueError as error:
            message = str(error)
            assert "terminal stub" in message
            assert "non-pin anchor" in message
        else:
            raise AssertionError("terminal_stub should require a net for plain coordinates")

        try:
            drawing.terminal_stub("WRONG", net=wrong, at=placed.TP)
        except ValueError as error:
            message = str(error)
            assert "WRONG" in message
            assert "SIG" in message
            assert "terminal stub" in message
        else:
            raise AssertionError("terminal_stub should reject a mismatched pin net")

        try:
            drawing.terminal_stub(other_net, at=(30, 30))
        except ValueError as error:
            message = str(error)
            assert "different design" in message
            assert "terminal stub" in message
        else:
            raise AssertionError("terminal_stub should reject cross-design nets")

        assert schematic.to_json() == before


def test_python_schematic_anchor_axis_alignment_is_pure_authoring_geometry():
    design = volt.Design("schematic-anchor-axis-alignment")
    schematic = design.schematic("Main")
    logical_before = design.to_json()
    projection_before = schematic.to_json()

    with schematic.drawing(at=(10, 20), unit=10) as drawing:
        base = drawing.node((10, 20))
        target = drawing.node((40, 55))
        assert base.tox(target).point == (40.0, 20.0)
        assert base.toy(target).point == (10.0, 55.0)
        assert base.tox(25).toy(35).point == (25.0, 35.0)

    assert schematic.to_json() == projection_before
    assert design.to_json() == logical_before


def test_python_schematic_terminal_marker_rejects_invalid_kind():
    design = volt.Design("schematic-terminal-marker-kind")
    sig = design.net("SIG")
    probe = design.test_point(ref="TP1")
    sig += probe["TP"]
    schematic = design.schematic("Main")

    with schematic.drawing(unit=10) as drawing:
        placed = drawing.place(probe)

        try:
            drawing.terminal(at=placed.TP, kind="invalid")
        except ValueError as error:
            assert "Power or Ground" in str(error)
        else:
            raise AssertionError("terminal marker must reject unknown kind strings")

        try:
            drawing.terminal(at=placed.TP, kind=42)
        except TypeError as error:
            assert "kind" in str(error).lower() or "string" in str(error).lower()
        else:
            raise AssertionError("terminal marker must reject non-string kind")

        try:
            drawing.terminal("", net=sig, at=(0, 0))
        except ValueError as error:
            assert "terminal marker" in str(error)
            assert "empty" in str(error)
        else:
            raise AssertionError("terminal marker must reject empty name string")


def test_python_schematic_explicit_wire_points_are_normalized_once():
    design = volt.Design("schematic-wire-normalization")
    vcc = design.net("VCC", kind="power")
    original = volt._schematic_point
    calls = 0

    # This is intentionally white-box: duplicate normalization has no distinct
    # serialized output, but it regressed cross-design error context before.
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
