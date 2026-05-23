import json
import re

import volt

from helpers import _wire_points
from schematic_drawing_test_helpers import schematic_projection


def test_python_schematic_two_terminal_grammar_places_chain_and_preserves_logical_json():
    design = volt.Design("schematic-two-terminal-chain")
    r1 = design.R("330 ohm", ref="R1")
    d1 = design.LED(ref="D1")
    d2 = design.diode(ref="D2")
    schematic = design.schematic("Main")
    logical_before = design.to_json()

    drawing = schematic.drawing(unit=20)
    with drawing as d:
        resistor = d.two_terminal(r1).right().label_value()
        led = d.two_terminal(d1).right()
        diode = d.two_terminal(d2).right()

    projection = schematic_projection(schematic)

    assert resistor.orientation == "Right"
    assert led.orientation == "Right"
    assert resistor.start.point == (0.0, 0.0)
    assert resistor.end.point == (20.0, 0.0)
    assert resistor.center.point == (10.0, 0.0)
    assert led.start.point == (20.0, 0.0)
    assert led.end.point == (40.0, 0.0)
    assert led.center.point == (30.0, 0.0)
    assert diode.start.point == (40.0, 0.0)
    assert diode.end.point == (60.0, 0.0)
    assert drawing.here.point == (60.0, 0.0)

    assert projection["symbol_instances"] == [
        {
            "id": "symbol_instance:0",
            "sheet": "sheet:0",
            "symbol_definition": "symbol_def:0",
            "component": "component:0",
            "position": {"x": 0.0, "y": 0.0},
            "orientation": "Right",
        },
        {
            "id": "symbol_instance:1",
            "sheet": "sheet:0",
            "symbol_definition": "symbol_def:1",
            "component": "component:1",
            "position": {"x": 20.0, "y": 0.0},
            "orientation": "Right",
        },
        {
            "id": "symbol_instance:2",
            "sheet": "sheet:0",
            "symbol_definition": "symbol_def:2",
            "component": "component:2",
            "position": {"x": 40.0, "y": 0.0},
            "orientation": "Right",
        },
    ]
    assert projection["symbol_fields"][0]["name"] == "value"
    assert projection["symbol_fields"][0]["value"] == "330 ohm"
    assert design.to_json() == logical_before


def test_python_schematic_two_terminal_endpoint_grammar_and_junction_dots():
    design = volt.Design("schematic-two-terminal-endpoints")
    sig = design.net("SIG")
    ret = design.net("RET")
    wire_net = design.net("WIRE")
    r1 = design.R("10k", ref="R1")
    c1 = design.C("1 uF", ref="C1")
    r2 = design.R("22k", ref="R2")
    r3 = design.R("47k", ref="R3")
    r4 = design.R("100k", ref="R4")
    sig += r1[1], r2[1], r3[1], r4[1]
    ret += r1[2], c1[1], c1[2], r2[2], r3[2], r4[2]

    schematic = design.schematic("Main")
    logical_before = design.to_json()

    with schematic.drawing(unit=10) as drawing:
        r_at_to = (
            drawing.two_terminal(r1)
            .at((10, 10))
            .to((40, 10))
            .label_value()
            .idot()
            .dot()
        )
        c_toy = drawing.two_terminal(c1).at(r_at_to.end).toy(50)
        r_endpoints = drawing.two_terminal(r2).endpoints((10, 70), (40, 70))
        r_tox = drawing.two_terminal(r3).at((60, 10)).tox(90)
        r_to_vertical = drawing.two_terminal(r4).at((90, 20)).to((90, 50))
        drawing.wire(wire_net).endpoints((0, 0), (10, 0)).idot().dot().direct()

    projection = schematic_projection(schematic)

    assert r_at_to.start.point == (10.0, 10.0)
    assert r_at_to.end.point == (40.0, 10.0)
    assert c_toy.start.point == (40.0, 10.0)
    assert c_toy.end.point == (40.0, 50.0)
    assert r_endpoints.start.point == (10.0, 70.0)
    assert r_endpoints.end.point == (40.0, 70.0)
    assert r_tox.start.point == (60.0, 10.0)
    assert r_tox.end.point == (90.0, 10.0)
    assert r_to_vertical.start.point == (90.0, 20.0)
    assert r_to_vertical.end.point == (90.0, 50.0)
    assert [instance["orientation"] for instance in projection["symbol_instances"]] == [
        "Right",
        "Down",
        "Right",
        "Right",
        "Down",
    ]
    assert _wire_points(projection, 0) == [(0.0, 0.0), (10.0, 0.0)]
    assert [
        (junction["net"], junction["position"])
        for junction in projection["junctions"]
    ] == [
        (f"net:{sig.index}", {"x": 10.0, "y": 10.0}),
        (f"net:{ret.index}", {"x": 40.0, "y": 10.0}),
        (f"net:{wire_net.index}", {"x": 0.0, "y": 0.0}),
        (f"net:{wire_net.index}", {"x": 10.0, "y": 0.0}),
    ]
    assert design.to_json() == logical_before


def test_python_schematic_connect_returns_chainable_endpoint_dots():
    design = volt.Design("schematic-connect-endpoint-dots")
    sig = design.net("SIG")
    r1 = design.R("10k", ref="R1")
    r2 = design.R("22k", ref="R2")
    sig += r1[2], r2[1]

    schematic = design.schematic("Main")
    logical_before = design.to_json()

    with schematic.drawing(unit=20) as drawing:
        left = drawing.two_terminal(r1).right()
        right = drawing.two_terminal(r2).at(left.end.right(20)).right()
        wire = drawing.connect(left.end, right.start, shape="-").idot().dot()

    projection = schematic_projection(schematic)

    assert wire.index == 0
    assert projection["wire_runs"] == [
        {
            "id": "wire_run:0",
            "sheet": "sheet:0",
            "net": f"net:{sig.index}",
            "points": [{"x": 20.0, "y": 0.0}, {"x": 40.0, "y": 0.0}],
            "route_intent": "Direct",
        }
    ]
    assert [
        (junction["net"], junction["position"])
        for junction in projection["junctions"]
    ] == [
        (f"net:{sig.index}", {"x": 20.0, "y": 0.0}),
        (f"net:{sig.index}", {"x": 40.0, "y": 0.0}),
    ]
    assert design.to_json() == logical_before


def test_python_schematic_endpoint_dots_require_existing_nets():
    design = volt.Design("schematic-two-terminal-dot-net-safety")
    r1 = design.R("10k", ref="R1")
    schematic = design.schematic("Main")

    with schematic.drawing(unit=10) as drawing:
        placed = drawing.two_terminal(r1).right()
        try:
            placed.idot()
        except ValueError as error:
            assert "is not connected to any logical net" in str(error)
        else:
            raise AssertionError("two-terminal endpoint dots must not invent nets")


def test_python_schematic_two_terminal_grammar_length_anchor_drop_and_hold():
    design = volt.Design("schematic-two-terminal-placement-options")
    c1 = design.C("100nF", ref="C1")
    l1 = design.L("10uH", ref="L1")
    schematic = design.schematic("Main")
    drawing = schematic.drawing(at=(10, 10), unit=20)

    with drawing as d:
        capacitor = d.two_terminal(c1).at((10, 10)).anchor("center").right(1.5).drop("start")
        assert capacitor.start.point == (-5.0, 10.0)
        assert capacitor.center.point == (10.0, 10.0)
        assert capacitor.end.point == (25.0, 10.0)
        assert d.here.point == (-5.0, 10.0)

        with d.hold():
            inductor = d.two_terminal(l1).right(2)
            assert inductor.start.point == (-5.0, 10.0)
            assert inductor.end.point == (35.0, 10.0)
            assert d.here.point == (35.0, 10.0)

        assert d.here.point == (-5.0, 10.0)

    projection = schematic_projection(schematic)

    assert projection["symbol_instances"][0]["position"] == {"x": -5.0, "y": 10.0}
    assert projection["symbol_instances"][0]["orientation"] == "Right"
    assert projection["symbol_instances"][1]["position"] == {"x": -5.0, "y": 10.0}
    assert projection["symbol_instances"][1]["orientation"] == "Right"
    assert capacitor.pin_anchor(2) == (25.0, 10.0)
    assert inductor.pin_anchor(2) == (35.0, 10.0)


def test_python_schematic_two_terminal_grammar_reverse_and_flip_presentations():
    design = volt.Design("schematic-two-terminal-presentation-options")
    d1 = design.LED(ref="D1")
    d2 = design.diode(ref="D2")
    schematic = design.schematic("Main")
    logical_before = design.to_json()

    with schematic.drawing(unit=20) as drawing:
        reversed_led = drawing.two_terminal(d1).right().reverse()
        flipped_diode = drawing.two_terminal(d2).right().flip()

    projection = schematic_projection(schematic)
    reversed_symbol = projection["symbol_definitions"][0]
    flipped_symbol = projection["symbol_definitions"][1]

    assert reversed_led.start.number == "2"
    assert reversed_led.start.point == (0.0, 0.0)
    assert reversed_led.end.number == "1"
    assert reversed_led.end.point == (20.0, 0.0)
    assert flipped_diode.start.point == (20.0, 0.0)
    assert flipped_diode.end.point == (40.0, 0.0)
    assert [pin["number"] for pin in reversed_symbol["pins"]] == ["2", "1"]
    assert any(
        primitive["type"] == "line" and primitive["start"]["y"] > 0
        for primitive in flipped_symbol["primitives"]
    )
    assert design.to_json() == logical_before


def test_python_schematic_two_terminal_grammar_rejects_invalid_components_clearly():
    design = volt.Design("schematic-two-terminal-invalid")
    test_point = design.test_point(ref="TP1")
    bare_definition = design.define_component(
        "BareTwoPin",
        pins=[volt.PinSpec("1", 1), volt.PinSpec("2", 2)],
    )
    bare = design.instantiate(bare_definition, ref="U1")
    schematic = design.schematic("Main")

    with schematic.drawing() as drawing:
        try:
            drawing.two_terminal(object()).right()
        except TypeError as error:
            assert str(error) == "Two-terminal placement expects a Component handle"
        else:
            raise AssertionError("two-terminal placement should reject non-components")

        try:
            drawing.two_terminal(test_point).right()
        except ValueError as error:
            assert str(error) == "Two-terminal placement requires exactly two component pins"
        else:
            raise AssertionError("two-terminal placement should reject one-pin components")

        try:
            drawing.two_terminal(bare).right()
        except ValueError as error:
            message = str(error)
            assert "No default schematic symbol" in message
            assert "U1" in message
            assert "sheet 'Main'" in message
        else:
            raise AssertionError("two-terminal placement should reject missing symbols")


def test_python_schematic_two_terminal_failed_materialization_restores_cursor():
    design = volt.Design("schematic-two-terminal-failed-materialization")
    r1 = design.R("10k", ref="R1")
    schematic = design.schematic("Main")
    drawing = schematic.drawing(at=(5, 6), direction="Up")

    try:
        with drawing as d:
            d.two_terminal(r1, symbol="missing-symbol").right()
    except ValueError as error:
        assert str(error) == "Unknown schematic symbol"
    else:
        raise AssertionError("unknown two-terminal symbol should fail when materialized")

    projection = schematic_projection(schematic)
    assert projection["symbol_instances"] == []
    assert drawing.here.point == (5.0, 6.0)
    assert drawing.direction == "Up"

    with drawing as d:
        recovered = d.two_terminal(r1).right()

    assert recovered.start.point == (5.0, 6.0)
    assert recovered.end.point == (25.0, 6.0)


def test_python_schematic_two_terminal_dir_has_no_placement_side_effects():
    design = volt.Design("schematic-two-terminal-dir-side-effects")
    r1 = design.R("10k", ref="R1")
    schematic = design.schematic("Main")

    with schematic.drawing(unit=20) as drawing:
        resistor = drawing.two_terminal(r1)
        listing = dir(resistor)
        assert "right" in listing
        assert schematic_projection(schematic)["symbol_instances"] == []

        resistor.right()
        assert drawing.here.point == (20.0, 0.0)

    assert schematic_projection(schematic)["symbol_instances"][0]["component"] == "component:0"
