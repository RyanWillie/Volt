import json
import re

import volt

from helpers import _wire_points
from schematic_drawing_test_helpers import schematic_projection


def test_python_schematic_label_sugar_uses_symbol_fields_and_net_labels():
    design = volt.Design("schematic-label-sugar")
    sig = design.net("SIG")
    r1 = design.R("10k", ref="R1")
    c1 = design.C(capacitance=100e-9, ref="C1")
    sig += r1[2], c1[1]

    schematic = design.schematic("Main")
    logical_before = design.to_json()

    with schematic.drawing(unit=20) as drawing:
        resistor = drawing.two_terminal(r1).right().label_ref().label_value()
        capacitor = drawing.two_terminal(c1).down().label("100n", loc="bottom", ofst=5)
        capacitor.label_value(loc="right", ofst=6)
        drawing.net_label("SIG", at=resistor.end.right(8), orient="left")

    projection = schematic_projection(schematic)
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
    assert fields[3]["position"] == {"x": 32.0, "y": 10.0}
    assert projection["net_labels"] == [
        {
            "id": "net_label:0",
            "sheet": "sheet:0",
            "net": f"net:{sig.index}",
            "position": {"x": 28.0, "y": 0.0},
            "text_position": {"x": 32.0, "y": 0.0},
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


def test_python_schematic_default_field_placement_keeps_rotated_labels_upright():
    design = volt.Design("schematic-default-field-placement")
    r1 = design.R("10k", ref="R1")
    c1 = design.C(capacitance=100e-9, ref="C1")

    schematic = design.schematic("Main")

    with schematic.drawing(unit=20) as drawing:
        drawing.two_terminal(r1).right().label_ref().label_value()
        drawing.two_terminal(c1).at((40, 0)).down().label_ref().label_value()

    fields = schematic_projection(schematic)["symbol_fields"]

    assert [(field["name"], field["value"], field["orientation"]) for field in fields] == [
        ("reference", "R1", "Right"),
        ("value", "10k", "Right"),
        ("reference", "C1", "Right"),
        ("value", "1e-07 F", "Right"),
    ]
    assert [field["position"] for field in fields] == [
        {"x": 10.0, "y": -10.0},
        {"x": 10.0, "y": 10.0},
        {"x": 30.0, "y": 10.0},
        {"x": 54.0, "y": 10.0},
    ]

    svg_texts = re.findall(r">([^<>]+)</text>", schematic.to_svg())
    assert svg_texts.count("R1") == 1
    assert svg_texts.count("C1") == 1


def test_python_schematic_builtin_two_terminal_symbols_do_not_embed_identity_text():
    design = volt.Design("schematic-clean-builtins")
    components = (
        design.R("10k", ref="R1"),
        design.C("100nF", ref="C1"),
        design.L("10uH", ref="L1"),
        design.diode(ref="D1"),
        design.LED(ref="D2"),
    )

    schematic = design.schematic("Main")

    with schematic.drawing(unit=20) as drawing:
        for index, component in enumerate(components):
            drawing.two_terminal(component).at((0, index * 20)).right().label_ref()

    projection = schematic_projection(schematic)
    built_in_definitions = {
        definition["name"].split("#", maxsplit=1)[0]: definition
        for definition in projection["symbol_definitions"]
    }

    assert set(built_in_definitions) == {
        "volt.passives:resistor",
        "volt.passives:capacitor",
        "volt.passives:inductor",
        "volt.discretes:diode",
        "volt.optos:led",
    }
    for definition in built_in_definitions.values():
        assert [
            primitive
            for primitive in definition["primitives"]
            if primitive["type"] == "text"
        ] == []
    assert [
        primitive for primitive in built_in_definitions["volt.passives:resistor"]["primitives"]
        if primitive["type"] == "rectangle"
    ] == []
    assert built_in_definitions["volt.passives:resistor"]["primitives"] == [
        {
            "type": "line",
            "start": {"x": 0.0, "y": 0.0},
            "end": {"x": 5.0, "y": 0.0},
            "role": "TerminalLeadStart",
        },
        {
            "type": "line",
            "start": {"x": 5.0, "y": 0.0},
            "end": {"x": 6.5, "y": -3.0},
        },
        {
            "type": "line",
            "start": {"x": 6.5, "y": -3.0},
            "end": {"x": 8.0, "y": 3.0},
        },
        {
            "type": "line",
            "start": {"x": 8.0, "y": 3.0},
            "end": {"x": 9.5, "y": -3.0},
        },
        {
            "type": "line",
            "start": {"x": 9.5, "y": -3.0},
            "end": {"x": 11.0, "y": 3.0},
        },
        {
            "type": "line",
            "start": {"x": 11.0, "y": 3.0},
            "end": {"x": 12.5, "y": -3.0},
        },
        {
            "type": "line",
            "start": {"x": 12.5, "y": -3.0},
            "end": {"x": 14.0, "y": 3.0},
        },
        {
            "type": "line",
            "start": {"x": 14.0, "y": 3.0},
            "end": {"x": 15.0, "y": 0.0},
        },
        {
            "type": "line",
            "start": {"x": 15.0, "y": 0.0},
            "end": {"x": 20.0, "y": 0.0},
            "role": "TerminalLeadEnd",
        },
    ]

    svg_texts = re.findall(r">([^<>]+)</text>", schematic.to_svg())
    assert not {"R", "C", "L", "D"} & set(svg_texts)
    for reference in ("R1", "C1", "L1", "D1", "D2"):
        assert svg_texts.count(reference) == 1


def test_python_schematic_generic_ic_symbol_builder_places_stable_pin_anchors():
    symbol = volt.SchematicSymbolSpec.ic(
        "test:Timer",
        pins=(
            volt.SchematicSymbolSpec.ic_pin("DISCH", 7, side="left", slot=1),
            volt.SchematicSymbolSpec.ic_pin("TRIG", 2, side="left", slot=2),
            volt.SchematicSymbolSpec.ic_pin("THRESH", 6, side="left", slot=3),
            volt.SchematicSymbolSpec.ic_pin("GND", 1, side="left", slot=4),
            volt.SchematicSymbolSpec.ic_pin("OUT", 3, side="right", slot=2),
            volt.SchematicSymbolSpec.ic_pin("CTRL", 5, side="right", slot=3),
            volt.SchematicSymbolSpec.ic_pin("RESET", 4, side="top", slot=2),
            volt.SchematicSymbolSpec.ic_pin("VCC", 8, side="top", slot=4),
        ),
        width=50,
        height=50,
        lead_length=10,
        pin_pitch=10,
        pin_label_offset=4,
        center_label="555",
        bottom_label="timer",
    )

    design = volt.Design("schematic-generic-ic")
    timer_definition = design.define_component(
        "Timer",
        pins=[
            volt.PinSpec("GND", 1, role="ground"),
            volt.PinSpec("TRIG", 2, role="input"),
            volt.PinSpec("OUT", 3, role="output"),
            volt.PinSpec("RESET", 4, role="input"),
            volt.PinSpec("CTRL", 5, role="input"),
            volt.PinSpec("THRESH", 6, role="input"),
            volt.PinSpec("DISCH", 7, role="output"),
            volt.PinSpec("VCC", 8, role="power"),
        ],
        schematic_symbol=symbol,
    )
    u1 = design.instantiate(timer_definition, ref="U1", properties={"value": "NE555"})
    vcc = design.net("+5V", kind="power")
    trigger = design.net("TIMING")
    output = design.net("OUT")
    vcc += u1["VCC"], u1["RESET"]
    trigger += u1["TRIG"], u1["THRESH"]
    output += u1["OUT"]

    schematic = design.schematic("Main")
    logical_before = design.to_json()

    with schematic.drawing(at=(40, 20), unit=10) as drawing:
        timer = drawing.place(u1).label_ref(loc="top").label_value(loc="bottom")
        drawing.power("+5V", at=timer.VCC, orient="Up")
        drawing.signal_stub(trigger, at=timer.TRIG, side="Left", label="TIMING")
        drawing.signal_stub(output, at=timer.OUT, side="Right")

    projection = schematic_projection(schematic)

    assert design.to_json() == logical_before
    assert projection["symbol_definitions"] == [
        {
            "id": "symbol_def:0",
            "name": "test:Timer",
            "pins": [
                {
                    "name": "DISCH",
                    "number": "7",
                    "anchor": {"x": 0.0, "y": 10.0},
                    "orientation": "Left",
                },
                {
                    "name": "TRIG",
                    "number": "2",
                    "anchor": {"x": 0.0, "y": 20.0},
                    "orientation": "Left",
                },
                {
                    "name": "THRESH",
                    "number": "6",
                    "anchor": {"x": 0.0, "y": 30.0},
                    "orientation": "Left",
                },
                {
                    "name": "GND",
                    "number": "1",
                    "anchor": {"x": 0.0, "y": 40.0},
                    "orientation": "Left",
                },
                {
                    "name": "OUT",
                    "number": "3",
                    "anchor": {"x": 70.0, "y": 20.0},
                    "orientation": "Right",
                },
                {
                    "name": "CTRL",
                    "number": "5",
                    "anchor": {"x": 70.0, "y": 30.0},
                    "orientation": "Right",
                },
                {
                    "name": "RESET",
                    "number": "4",
                    "anchor": {"x": 30.0, "y": -10.0},
                    "orientation": "Up",
                },
                {
                    "name": "VCC",
                    "number": "8",
                    "anchor": {"x": 50.0, "y": -10.0},
                    "orientation": "Up",
                },
            ],
            "primitives": projection["symbol_definitions"][0]["primitives"],
        }
    ]
    primitives = projection["symbol_definitions"][0]["primitives"]
    assert primitives[0] == {
        "type": "rectangle",
        "first_corner": {"x": 10.0, "y": 0.0},
        "second_corner": {"x": 60.0, "y": 50.0},
    }
    assert {
        "type": "text",
        "text": "555",
        "anchor": {"x": 35.0, "y": 25.0},
        "orientation": "Right",
        "vertical_alignment": "Middle",
    } in primitives
    assert {
        "type": "text",
        "text": "timer",
        "anchor": {"x": 35.0, "y": 64.0},
        "orientation": "Right",
        "vertical_alignment": "Top",
    } in primitives
    assert {
        "type": "text",
        "text": "TRIG",
        "anchor": {"x": 14.0, "y": 20.0},
        "orientation": "Right",
        "horizontal_alignment": "Start",
        "vertical_alignment": "Middle",
    } in primitives
    assert {
        "type": "text",
        "text": "OUT",
        "anchor": {"x": 56.0, "y": 20.0},
        "orientation": "Right",
        "horizontal_alignment": "End",
        "vertical_alignment": "Middle",
    } in primitives
    assert {
        "type": "text",
        "text": "RESET",
        "anchor": {"x": 30.0, "y": 4.0},
        "orientation": "Right",
        "vertical_alignment": "Top",
    } in primitives
    assert timer.TRIG.point == (40.0, 40.0)
    assert timer.OUT.point == (110.0, 40.0)
    assert timer.VCC.point == (90.0, 10.0)
    assert [field["value"] for field in projection["symbol_fields"]] == ["U1", "NE555"]
    assert schematic_projection(schematic) == projection


def test_python_schematic_generic_ic_symbol_builder_defaults_are_compact():
    symbol = volt.SchematicSymbolSpec.ic(
        "test:CompactTimer",
        pins=(
            volt.SchematicSymbolSpec.ic_pin("DISCH", 7, side="left", slot=1),
            volt.SchematicSymbolSpec.ic_pin("THRESH", 6, side="left", slot=2),
            volt.SchematicSymbolSpec.ic_pin("TRIG", 2, side="left", slot=3),
            volt.SchematicSymbolSpec.ic_pin("OUT", 3, side="right", slot=2),
            volt.SchematicSymbolSpec.ic_pin("CTRL", 5, side="right", slot=3),
            volt.SchematicSymbolSpec.ic_pin("RESET", 4, side="top", slot=2),
            volt.SchematicSymbolSpec.ic_pin("VCC", 8, side="top", slot=4),
            volt.SchematicSymbolSpec.ic_pin("GND", 1, side="bottom", slot=3),
        ),
        center_label="555",
        pin_numbers=True,
    )

    assert [pin._to_dict() for pin in symbol.pins] == [
        {"name": "DISCH", "number": "7", "anchor": {"x": 0.0, "y": 10.0}, "orientation": "Left"},
        {"name": "THRESH", "number": "6", "anchor": {"x": 0.0, "y": 20.0}, "orientation": "Left"},
        {"name": "TRIG", "number": "2", "anchor": {"x": 0.0, "y": 30.0}, "orientation": "Left"},
        {"name": "OUT", "number": "3", "anchor": {"x": 62.0, "y": 20.0}, "orientation": "Right"},
        {"name": "CTRL", "number": "5", "anchor": {"x": 62.0, "y": 30.0}, "orientation": "Right"},
        {"name": "RESET", "number": "4", "anchor": {"x": 26.0, "y": -6.0}, "orientation": "Up"},
        {"name": "VCC", "number": "8", "anchor": {"x": 46.0, "y": -6.0}, "orientation": "Up"},
        {"name": "GND", "number": "1", "anchor": {"x": 36.0, "y": 46.0}, "orientation": "Down"},
    ]
    assert symbol.primitives[0] == {
        "type": "rectangle",
        "first_corner": {"x": 6.0, "y": 0.0},
        "second_corner": {"x": 56.0, "y": 40.0},
    }


def test_python_schematic_generic_block_builder_allows_per_side_layout_and_pin_numbers():
    symbol = volt.SchematicSymbolSpec.block(
        "test:Peripheral",
        pins=(
            volt.SchematicSymbolSpec.block_pin("IN", 11, side="left", slot=1),
            volt.SchematicSymbolSpec.block_pin("EN", 12, side="left", slot=2),
            volt.SchematicSymbolSpec.block_pin("OUT", 21, side="right", slot=1),
            volt.SchematicSymbolSpec.block_pin("VDD", 31, side="top", slot=1),
            volt.SchematicSymbolSpec.block_pin("GND", 41, side="bottom", slot=1),
        ),
        width=40,
        height=36,
        lead_length=10,
        pin_pitch=10,
        pin_label_offset=3,
        side_layouts=(
            volt.SchematicSymbolSpec.side_layout(
                "left",
                pad=6,
                pin_pitch=12,
                lead_length=14,
                pin_label_offset=5,
                pin_number_offset=4,
            ),
            volt.SchematicSymbolSpec.side_layout(
                "right",
                pad=8,
                lead_length=6,
                pin_label_offset=2,
                pin_number_offset=3,
            ),
            volt.SchematicSymbolSpec.side_layout(
                "top",
                pad=16,
                lead_length=5,
                pin_number_offset=6,
            ),
            volt.SchematicSymbolSpec.side_layout(
                "bottom",
                pad=24,
                lead_length=7,
                pin_label_offset=4,
                pin_number_offset=5,
            ),
        ),
        pin_numbers=True,
        pin_number_offset=2,
    )

    assert [pin._to_dict() for pin in symbol.pins] == [
        {
            "name": "IN",
            "number": "11",
            "anchor": {"x": 0.0, "y": 6.0},
            "orientation": "Left",
        },
        {
            "name": "EN",
            "number": "12",
            "anchor": {"x": 0.0, "y": 18.0},
            "orientation": "Left",
        },
        {
            "name": "OUT",
            "number": "21",
            "anchor": {"x": 60.0, "y": 8.0},
            "orientation": "Right",
        },
        {
            "name": "VDD",
            "number": "31",
            "anchor": {"x": 30.0, "y": -5.0},
            "orientation": "Up",
        },
        {
            "name": "GND",
            "number": "41",
            "anchor": {"x": 38.0, "y": 43.0},
            "orientation": "Down",
        },
    ]
    assert symbol.primitives[0] == {
        "type": "rectangle",
        "first_corner": {"x": 14.0, "y": 0.0},
        "second_corner": {"x": 54.0, "y": 36.0},
    }
    text_primitives = [
        primitive for primitive in symbol.primitives if primitive["type"] == "text"
    ]
    assert {
        "type": "text",
        "text": "IN",
        "anchor": {"x": 19.0, "y": 6.0},
        "orientation": "Right",
        "horizontal_alignment": "Start",
        "vertical_alignment": "Middle",
    } in text_primitives
    assert {
        "type": "text",
        "text": "11",
        "anchor": {"x": 10.0, "y": 10.0},
        "orientation": "Right",
        "horizontal_alignment": "End",
        "vertical_alignment": "Bottom",
    } in text_primitives
    assert {
        "type": "text",
        "text": "OUT",
        "anchor": {"x": 52.0, "y": 8.0},
        "orientation": "Right",
        "horizontal_alignment": "End",
        "vertical_alignment": "Middle",
    } in text_primitives
    assert {
        "type": "text",
        "text": "21",
        "anchor": {"x": 57.0, "y": 11.0},
        "orientation": "Right",
        "horizontal_alignment": "Start",
        "vertical_alignment": "Bottom",
    } in text_primitives
    assert {
        "type": "text",
        "text": "VDD",
        "anchor": {"x": 30.0, "y": 3.0},
        "orientation": "Right",
        "vertical_alignment": "Top",
    } in text_primitives
    assert {
        "type": "text",
        "text": "31",
        "anchor": {"x": 36.0, "y": -6.0},
        "orientation": "Right",
        "horizontal_alignment": "Start",
        "vertical_alignment": "Bottom",
    } in text_primitives
    assert {
        "type": "text",
        "text": "GND",
        "anchor": {"x": 38.0, "y": 32.0},
        "orientation": "Right",
        "vertical_alignment": "Bottom",
    } in text_primitives
    assert {
        "type": "text",
        "text": "41",
        "anchor": {"x": 43.0, "y": 41.0},
        "orientation": "Right",
        "horizontal_alignment": "Start",
        "vertical_alignment": "Top",
    } in text_primitives


def test_python_schematic_ortho_lines_lower_to_existing_wire_runs_without_logical_mutation():
    design = volt.Design("schematic-ortho-lines")
    a = design.net("A")
    b = design.net("B")
    c = design.net("C")
    header = design.connector_1x03(ref="J1")
    target = design.connector_1x03(ref="J2")
    a += header[1], target[1]
    b += header[2], target[2]
    c += header[3]

    schematic = design.schematic("Main")
    logical_before = design.to_json()

    with schematic.drawing(unit=10) as drawing:
        left = drawing.place(header, at=(10, 10))
        right = drawing.place(target, at=(70, 10))
        wires = drawing.ortho_lines(
            (
                (left[1], right[1]),
                (left[2], right[2]),
                (c, left[3], left[3].right(24)),
            ),
            shape="-|-",
            k=18,
        )

    projection = schematic_projection(schematic)

    assert design.to_json() == logical_before
    assert tuple(wire.index for wire in wires) == (0, 1, 2)
    assert [wire["net"] for wire in projection["wire_runs"]] == [
        f"net:{a.index}",
        f"net:{b.index}",
        f"net:{c.index}",
    ]
    assert [_wire_points(projection, index) for index in range(3)] == [
        [(10.0, 10.0), (28.0, 10.0), (70.0, 10.0)],
        [(10.0, 18.0), (28.0, 18.0), (70.0, 18.0)],
        [(10.0, 26.0), (28.0, 26.0), (34.0, 26.0)],
    ]
    assert {wire["route_intent"] for wire in projection["wire_runs"]} == {"Orthogonal"}


def test_ic_symbol_builder_rejects_invalid_pin_layout():
    pins_with_duplicate_slot = (
        volt.SchematicSymbolSpec.ic_pin("A", 1, side="left", slot=1),
        volt.SchematicSymbolSpec.ic_pin("B", 2, side="left", slot=1),
    )
    try:
        volt.SchematicSymbolSpec.ic("test_duplicate_slot_ic", pins=pins_with_duplicate_slot)
    except ValueError as error:
        assert "slot" in str(error).lower()
    else:
        raise AssertionError("ic builder should reject duplicate side slots")

    pins_with_duplicate_number = (
        volt.SchematicSymbolSpec.ic_pin("A", 1, side="left"),
        volt.SchematicSymbolSpec.ic_pin("B", 1, side="right"),
    )
    try:
        volt.SchematicSymbolSpec.ic("test_duplicate_number_ic", pins=pins_with_duplicate_number)
    except ValueError as error:
        assert "number" in str(error).lower()
    else:
        raise AssertionError("ic builder should reject duplicate pin numbers")

    try:
        volt.SchematicSymbolSpec.ic_pin("BAD", 9, side="diagonal")
    except ValueError as error:
        assert "left, right, top, or bottom" in str(error)
    else:
        raise AssertionError("ic pin side should reject unsupported values")

    try:
        volt.SchematicSymbolSpec.ic(
            "test_duplicate_side_layout_ic",
            pins=(volt.SchematicSymbolSpec.ic_pin("A", 1, side="left"),),
            side_layouts=(
                volt.SchematicSymbolSpec.side_layout("left", pad=6),
                volt.SchematicSymbolSpec.side_layout("l", pad=8),
            ),
        )
    except ValueError as error:
        assert "side layout" in str(error).lower()
        assert "duplicated" in str(error).lower()
    else:
        raise AssertionError("ic builder should reject duplicate side layout entries")


def test_ortho_lines_rejects_malformed_entries():
    design = volt.Design("schematic-ortho-lines-invalid")
    net = design.net("N")
    header = design.connector_1x02(ref="J1")
    target = design.connector_1x02(ref="J2")
    net += header[1], target[1]
    schematic = design.schematic("Main")

    with schematic.drawing(unit=10) as drawing:
        left = drawing.place(header, at=(10, 10))
        right = drawing.place(target, at=(40, 10))

        try:
            drawing.ortho_lines(("bad",))
        except TypeError as error:
            assert "Ortho line entries" in str(error)
        else:
            raise AssertionError("non-tuple ortho line entries should be rejected")

        try:
            drawing.ortho_lines(((left[1], right[1], left[2], right[2]),))
        except TypeError as error:
            assert "Ortho line entries" in str(error)
        else:
            raise AssertionError("4-tuple ortho line entries should be rejected")

        try:
            drawing.ortho_lines(((123, left[1], right[1]),))
        except TypeError as error:
            assert "explicit nets must be Net handles" in str(error)
        else:
            raise AssertionError("explicit non-Net ortho line nets should be rejected")
