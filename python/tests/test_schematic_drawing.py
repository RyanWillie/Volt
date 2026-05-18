import json

import volt

from helpers import _wire_points


def test_python_schematic_two_terminal_grammar_places_chain_and_preserves_logical_json():
    design = volt.Design("schematic-two-terminal-chain")
    r1 = design.R("330 ohm", ref="R1")
    d1 = design.LED(ref="D1")
    d2 = design.diode(ref="D2")
    schematic = design.schematic("Main")
    logical_before = design.to_json()

    drawing = schematic.drawing(unit=20)
    with drawing as d:
        resistor = d.R(r1).right().label_value()
        led = d.LED(d1).right()
        diode = d.two_terminal(d2).right()

    projection = json.loads(schematic.to_json())

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

def test_python_schematic_label_sugar_uses_symbol_fields_and_net_labels():
    design = volt.Design("schematic-label-sugar")
    sig = design.net("SIG")
    r1 = design.R("10k", ref="R1")
    c1 = design.C(capacitance=100e-9, ref="C1")
    sig += r1[2], c1[1]

    schematic = design.schematic("Main")
    logical_before = design.to_json()

    with schematic.drawing(unit=20) as drawing:
        resistor = drawing.R(r1).right().label_ref().label_value()
        capacitor = drawing.C(c1).down().label("100n", loc="bottom", ofst=5)
        capacitor.label_value(loc="right", ofst=6)
        drawing.net_label("SIG", at=resistor.end.right(8), orient="left")

    projection = json.loads(schematic.to_json())
    fields = projection["symbol_fields"]

    assert [field["name"] for field in fields] == [
        "reference",
        "value",
        "label",
        "value",
    ]
    assert [field["value"] for field in fields] == ["R1", "10k", "100n", "1e-07 F"]
    assert fields[0]["position"] == {"x": 10.0, "y": -10.0}
    assert fields[1]["position"] == {"x": 10.0, "y": 10.0}
    assert fields[2]["position"] == {"x": 20.0, "y": 25.0}
    assert fields[3]["position"] == {"x": 26.0, "y": 10.0}
    assert projection["net_labels"] == [
        {
            "id": "net_label:0",
            "sheet": "sheet:0",
            "net": f"net:{sig.index}",
            "position": {"x": 28.0, "y": 0.0},
            "orientation": "Left",
        }
    ]
    assert design.to_json() == logical_before

    svg = schematic.to_svg()
    assert 'data-field="reference"' in svg
    assert ">R1</text>" in svg
    assert ">10k</text>" in svg
    assert ">100n</text>" in svg
    assert ">SIG</text>" in svg


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

    projection = json.loads(schematic.to_json())

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
            "orientation": "Left",
            "label": "SWDIO",
        }
    ]

    svg = schematic.to_svg()
    assert f'<polyline class="wire-run" data-net="net:{sig.index}" points="40,40 34,40"/>' in svg
    assert f'<text class="net-label" data-net="net:{sig.index}" x="33" y="40"' in svg
    assert ">SWDIO</text>" in svg
    assert ">SUPPORT/SWDIO</text>" not in svg
    assert 'class="sheet-port off-page"' not in svg


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

    projection = json.loads(schematic.to_json())

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

    projection = json.loads(schematic.to_json())

    assert stub.side == placed[1].orientation
    assert group[0].side == placed[2].orientation
    assert {label["label"] for label in projection["net_labels"]} == {"DIO", "CLK"}


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

        assert schematic.to_json() == before


def test_python_schematic_label_sugar_rejects_invalid_inputs_clearly():
    design = volt.Design("schematic-label-invalid")
    other = volt.Design("schematic-label-invalid-other")
    sig = design.net("SIG")
    r1 = design.R("10k", ref="R1")
    schematic = design.schematic("Main")
    other_anchor = volt.SchematicAnchor((0, 0), design=other)

    with schematic.drawing(unit=20) as drawing:
        resistor = drawing.R(r1).right()
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

def test_python_schematic_two_terminal_grammar_length_anchor_drop_and_hold():
    design = volt.Design("schematic-two-terminal-placement-options")
    c1 = design.C("100nF", ref="C1")
    l1 = design.L("10uH", ref="L1")
    schematic = design.schematic("Main")
    drawing = schematic.drawing(at=(10, 10), unit=20)

    with drawing as d:
        capacitor = d.C(c1).at((10, 10)).anchor("center").right(1.5).drop("start")
        assert capacitor.start.point == (-5.0, 10.0)
        assert capacitor.center.point == (10.0, 10.0)
        assert capacitor.end.point == (25.0, 10.0)
        assert d.here.point == (-5.0, 10.0)

        with d.hold():
            inductor = d.L(l1).right(2)
            assert inductor.start.point == (-5.0, 10.0)
            assert inductor.end.point == (35.0, 10.0)
            assert d.here.point == (35.0, 10.0)

        assert d.here.point == (-5.0, 10.0)

    projection = json.loads(schematic.to_json())

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
        reversed_led = drawing.LED(d1).right().reverse()
        flipped_diode = drawing.D(d2).right().flip()

    projection = json.loads(schematic.to_json())
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
            drawing.R(object()).right()
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
            d.R(r1, symbol="missing-symbol").right()
    except ValueError as error:
        assert str(error) == "Unknown schematic symbol"
    else:
        raise AssertionError("unknown two-terminal symbol should fail when materialized")

    projection = json.loads(schematic.to_json())
    assert projection["symbol_instances"] == []
    assert drawing.here.point == (5.0, 6.0)
    assert drawing.direction == "Up"

    with drawing as d:
        recovered = d.R(r1).right()

    assert recovered.start.point == (5.0, 6.0)
    assert recovered.end.point == (25.0, 6.0)

def test_python_schematic_two_terminal_dir_has_no_placement_side_effects():
    design = volt.Design("schematic-two-terminal-dir-side-effects")
    r1 = design.R("10k", ref="R1")
    schematic = design.schematic("Main")

    with schematic.drawing(unit=20) as drawing:
        resistor = drawing.R(r1)
        listing = dir(resistor)
        assert "right" in listing
        assert json.loads(schematic.to_json())["symbol_instances"] == []

        resistor.right()
        assert drawing.here.point == (20.0, 0.0)

    assert json.loads(schematic.to_json())["symbol_instances"][0]["component"] == "component:0"

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
        resistor = drawing.R(r1).right()
        led = drawing.LED(d1).right().reverse()

        drawing.connect(resistor.end, led.start)
        drawing.power("+3V3", at=resistor.start)
        drawing.ground(at=led.end)

    projection = json.loads(schematic.to_json())
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
        resistor = drawing.R(r1).right()
        led = drawing.LED(d1).right().reverse()
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
    assert json.loads(schematic.to_json())["wire_runs"] == []

def test_python_schematic_drawing_connect_requires_explicit_net_for_plain_coordinate():
    design = volt.Design("schematic-explicit-coordinate-net")
    vcc = design.net("VCC", kind="power")
    r1 = design.R("10k", ref="R1")
    vcc += r1[1]

    schematic = design.schematic("Main")
    logical_before = design.to_json()

    with schematic.drawing(unit=20) as drawing:
        resistor = drawing.R(r1).right()
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

    projection = json.loads(schematic.to_json())

    assert design.to_json() == logical_before
    assert len(projection["wire_runs"]) == 1
    assert projection["wire_runs"][0]["net"] == f"net:{vcc.index}"

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
        projection = json.loads(schematic.to_json())

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

    projection = json.loads(schematic.to_json())

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

    projection = json.loads(schematic.to_json())

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
    except RuntimeError as error:
        assert "collides with a different logical net" in str(error)
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
        resistor = drawing.R(r1).right()
        drawing.move_from(resistor.end.right(40).down(20), direction="Down")
        led = drawing.LED(d1).down().reverse()
        vcc_port = drawing.power("VCC", net=vcc, at=resistor.start.left(20))
        gnd_port = drawing.ground(net=gnd, at=led.end.down(20))

        drawing.connect(vcc_port, resistor.start, net=vcc, shape="-")
        drawing.connect(resistor.end, led.start, shape="-|", k=20)
        drawing.connect(led.end, gnd_port, net=gnd, shape="|-", k=10)

    projection = json.loads(schematic.to_json())

    assert design.to_json() == logical_before
    assert len(projection["wire_runs"]) == 3
    assert _wire_points(projection, 1) == [
        (20.0, 0.0),
        (40.0, 0.0),
        (40.0, 20.0),
        (60.0, 20.0),
    ]
    assert not schematic.validate().has_errors

def test_python_schematic_power_and_ground_require_explicit_net_for_non_pin_anchor():
    design = volt.Design("schematic-explicit-port-net")
    vcc = design.net("VCC", kind="power")
    gnd = design.net("GND", kind="ground")
    r1 = design.R("10k", ref="R1")
    vcc += r1[1]
    gnd += r1[2]

    schematic = design.schematic("Main")

    with schematic.drawing(unit=20) as drawing:
        resistor = drawing.R(r1).right()
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

    projection = json.loads(schematic.to_json())

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
    projection = json.loads(schematic.to_json())

    assert placed.TP.point == (102.0, 53.0)
    assert (drawing.here.point, drawing.direction) == before
    assert [label["position"] for label in projection["net_labels"]] == [
        {"x": 104.0, "y": 55.0},
        {"x": 4.0, "y": 5.0},
    ]


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
