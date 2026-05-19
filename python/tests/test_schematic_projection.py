import json

import volt


def test_python_schematic_placement_serializes_kernel_projection():
    design = volt.Design("schematic-placement")
    vcc = design.net("VCC", kind="power")
    r1 = design.R(resistance=330, ref="R1")
    d1 = design.LED(ref="D1")

    schematic = design.schematic("Main")
    first = schematic.place(r1, at=(40, 20), symbol="resistor")
    second = schematic.place(d1, at=(90, 20), symbol="led")
    wire = schematic.wire(vcc, [(20, 20), (40, 20)])
    label = schematic.label(vcc, at=(20, 16))

    projection = json.loads(schematic.to_json())

    assert first.index == 0
    assert second.index == 1
    assert wire.index == 0
    assert label.index == 0
    assert projection["format"] == "volt.schematic"
    assert projection["sheets"] == [
        {
            "id": "sheet:0",
            "name": "Main",
            "metadata": {
                "title": "Main",
                "orientation": "Landscape",
                "size": {"width": 297.0, "height": 210.0},
                "title_block": [],
                "frame": {
                    "visible": True,
                    "margins": {
                        "left": 10.0,
                        "top": 10.0,
                        "right": 10.0,
                        "bottom": 10.0,
                    },
                },
            },
            "regions": [],
            "symbol_instances": ["symbol_instance:0", "symbol_instance:1"],
            "wire_runs": ["wire_run:0"],
            "net_labels": ["net_label:0"],
            "junctions": [],
            "power_ports": [],
            "no_connect_markers": [],
            "sheet_ports": [],
            "symbol_fields": [],
        }
    ]
    assert [symbol["name"] for symbol in projection["symbol_definitions"]] == [
        "resistor",
        "led",
    ]
    assert projection["symbol_instances"] == [
        {
            "id": "symbol_instance:0",
            "sheet": "sheet:0",
            "symbol_definition": "symbol_def:0",
            "component": "component:0",
            "position": {"x": 40.0, "y": 20.0},
            "orientation": "Right",
        },
        {
            "id": "symbol_instance:1",
            "sheet": "sheet:0",
            "symbol_definition": "symbol_def:1",
            "component": "component:1",
            "position": {"x": 90.0, "y": 20.0},
            "orientation": "Right",
        },
    ]
    assert projection["wire_runs"] == [
        {
            "id": "wire_run:0",
            "sheet": "sheet:0",
            "net": "net:0",
            "points": [{"x": 20.0, "y": 20.0}, {"x": 40.0, "y": 20.0}],
            "route_intent": "Direct",
        }
    ]
    assert projection["net_labels"] == [
        {
            "id": "net_label:0",
            "sheet": "sheet:0",
            "net": "net:0",
            "position": {"x": 20.0, "y": 16.0},
            "orientation": "Right",
        }
    ]
    assert projection["junctions"] == []
    assert projection["power_ports"] == []
    assert projection["no_connect_markers"] == []
    assert projection["sheet_ports"] == []
    assert projection["symbol_fields"] == []

def test_python_schematic_symbol_handles_expose_pin_anchors():
    design = volt.Design("schematic-anchors")
    r1 = design.R(resistance=330, ref="R1")

    schematic = design.schematic("Main")
    symbol = schematic.place(r1, at=(40, 20), symbol="resistor")

    assert symbol.pin_anchor(1) == (40.0, 20.0)
    assert symbol.pin_anchor("2") == (60.0, 20.0)

def test_python_schematic_drawing_place_returns_authoring_handle_with_core_anchors():
    design = volt.Design("schematic-authoring-handle")
    vcc = design.net("VCC", kind="power")
    gnd = design.net("GND", kind="ground")
    r1 = design.R("10k", ref="R1")
    vcc += r1[1]
    gnd += r1[2]

    schematic = design.schematic("Main")
    logical_before = design.to_json()
    with schematic.drawing(at=(40, 20), direction="down", unit=20) as drawing:
        resistor = drawing.place(r1)

    assert resistor.symbol.index == 0
    assert resistor.index == resistor.symbol.index
    assert resistor.component is r1
    assert resistor.orientation == "Down"
    assert resistor.start.point == (40.0, 20.0)
    assert resistor.end.point == (40.0, 40.0)
    assert resistor.center.point == (40.0, 30.0)
    assert resistor["1"].pin.index == r1[1].index
    assert resistor[2].point == resistor.end.point
    assert resistor.pin("2").point == resistor.end.point
    assert tuple(anchor.number for anchor in resistor.pin_anchors()) == ("1", "2")

    vcc_port = schematic.power("VCC", net=vcc, at=resistor.start.left(20))
    gnd_port = schematic.ground(net=gnd, at=resistor.end.right(20))
    schematic.wire(vcc).from_(vcc_port).to(resistor.start).orthogonal()
    schematic.wire(gnd, points=(resistor.end, gnd_port.pin))

    assert design.to_json() == logical_before

def test_python_schematic_dsl_authors_anchors_routes_and_semantic_objects():
    design = volt.Design("schematic-dsl")
    vcc = design.net("VCC", kind="power")
    led_a = design.net("LED_A")
    gnd = design.net("GND", kind="ground")
    r1 = design.R("330 ohm", ref="R1")
    d1 = design.LED(ref="D1")
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

    vcc += r1[1]
    led_a += r1[2], d1["A"]
    gnd += d1["K"]
    assert {net.name for net in design.nets()} >= {"VCC", "LED_A", "GND"}
    assert tuple(pin.index for pin in led_a.pins()) == (r1[2].index, d1["A"].index)

    schematic = design.schematic("Main")
    resistor = schematic.place(r1, at=(40, 20), orient="down", symbol="resistor")
    led = schematic.place(d1, at=(90, 30), symbol="led")
    no_connect_target = schematic.place(tp1, at=(130, 70), symbol="test:point")

    r1_left = resistor.pin(1)
    r1_right = resistor.pin("2")
    led_anode = led.pin("A")
    led_cathode = led.pin(1)
    no_connect_pin = no_connect_target.pin("NC")

    assert resistor.orientation == "Down"
    assert r1_left.point == (40.0, 20.0)
    assert r1_left.pin.index == r1[1].index
    assert r1_right.point == (40.0, 40.0)
    assert tuple(anchor.number for anchor in resistor.pin_anchors()) == ("1", "2")
    assert resistor.pin_anchor("2") == r1_right.point
    assert led_anode.point == (110.0, 30.0)
    assert led_cathode.down(30).point == (90.0, 60.0)

    vcc_port = schematic.power("VCC", net=vcc, at=r1_left.left(20))
    ground_port = schematic.ground(net=gnd, at=led_cathode.down(30), orient="down")
    sheet_port = schematic.off_page("LED_A", net=led_a, at=led_anode.right(20))
    label = schematic.label(led_a, at=r1_right.right(8), orient="left")
    schematic.junction(led_a, at=r1_right.right(35))
    schematic.no_connect(no_connect_pin, reason="open test pad")

    schematic.wire(vcc).from_(vcc_port).to(r1_left).orthogonal()
    schematic.wire(led_a).from_(r1_right).to(led_anode).orthogonal()
    schematic.wire(gnd).from_(led_cathode).to(ground_port).orthogonal()
    schematic.wire(led_a).from_(r1_right).via(r1_right.right(30)).to(led_anode).orthogonal()
    schematic.wire(led_a, points=(sheet_port.pin, led_anode.right(8)))

    logical = json.loads(design.to_json())
    projection = json.loads(schematic.to_json())

    assert logical["nets"][0]["pins"] == [f"pin:{r1[1].index}"]
    assert logical["nets"][1]["pins"] == [f"pin:{r1[2].index}", f"pin:{d1['A'].index}"]
    assert logical["nets"][2]["pins"] == [f"pin:{d1['K'].index}"]
    assert logical.get("design_intent", {}).get("no_connect_pins", []) == []

    assert projection["symbol_instances"][0]["orientation"] == "Down"
    assert projection["wire_runs"][0]["route_intent"] == "Orthogonal"
    assert projection["wire_runs"][1]["points"] == [
        {"x": 40.0, "y": 40.0},
        {"x": 110.0, "y": 40.0},
        {"x": 110.0, "y": 30.0},
    ]
    assert projection["wire_runs"][3]["route_intent"] == "Orthogonal"
    assert projection["wire_runs"][3]["points"] == [
        {"x": 40.0, "y": 40.0},
        {"x": 70.0, "y": 40.0},
        {"x": 110.0, "y": 30.0},
    ]
    assert projection["wire_runs"][4]["route_intent"] == "Direct"
    assert projection["net_labels"][0]["orientation"] == "Left"
    assert label.orientation == "Left"
    assert projection["junctions"][0]["position"] == {"x": 75.0, "y": 40.0}
    assert projection["power_ports"][0]["kind"] == "Power"
    assert projection["power_ports"][0]["position"] == {"x": 20.0, "y": 20.0}
    assert projection["power_ports"][1]["kind"] == "Ground"
    assert projection["power_ports"][1]["orientation"] == "Down"
    assert projection["no_connect_markers"][0]["pin"] == f"pin:{no_connect_pin.pin.index}"
    assert projection["no_connect_markers"][0]["reason"] == "open test pad"
    assert projection["sheet_ports"][0]["name"] == "LED_A"
    assert projection["sheet_ports"][0]["kind"] == "OffPage"

    svg = schematic.to_svg()
    assert 'class="power-port power"' in svg
    assert 'class="no-connect-marker"' in svg
    assert 'class="sheet-port off-page"' in svg

def test_python_schematic_drawing_annotation_helpers_stay_presentation_only():
    design = volt.Design("schematic-drawing-annotation-sugar")
    swdio = design.net("SWDIO")
    probe_definition = design.define_component(
        "Probe",
        pins=[
            volt.PinSpec("SWDIO", 1),
            volt.PinSpec("NC", 2, requirement="optional"),
        ],
        schematic_symbol=volt.SchematicSymbolSpec(
            "test:probe",
            pins=(
                volt.SchematicSymbolSpec.pin("SWDIO", 1, (0, 0), "Left"),
                volt.SchematicSymbolSpec.pin("NC", 2, (20, 0), "Right"),
            ),
            primitives=(volt.SchematicSymbolSpec.line((0, 0), (20, 0)),),
        ),
    )
    probe = design.instantiate(probe_definition, ref="TP1")
    swdio += probe["SWDIO"]

    schematic = design.schematic("Main")
    logical_before = design.to_json()

    with schematic.drawing(at=(40, 40), unit=20) as drawing:
        placed = drawing.place(probe)
        drawing.net_label(swdio, at=placed.SWDIO.left(10), orient="right")
        drawing.off_page("SWDIO", at=placed.SWDIO.left(20), orient="left")
        drawing.sheet_port("SWDIO_IN", net=swdio, at=placed.SWDIO.left(30), kind="Input")
        drawing.no_connect(placed.NC, reason="test pad not populated")

    projection = json.loads(schematic.to_json())
    logical = json.loads(design.to_json())

    assert design.to_json() == logical_before
    assert logical.get("design_intent", {}).get("no_connect_pins", []) == []
    assert projection["net_labels"][0]["net"] == f"net:{swdio.index}"
    assert projection["sheet_ports"][0]["name"] == "SWDIO"
    assert projection["sheet_ports"][0]["kind"] == "OffPage"
    assert projection["sheet_ports"][0]["net"] == f"net:{swdio.index}"
    assert projection["sheet_ports"][1]["name"] == "SWDIO_IN"
    assert projection["sheet_ports"][1]["kind"] == "Input"
    assert projection["no_connect_markers"][0]["pin"] == f"pin:{placed.NC.pin.index}"
    assert projection["no_connect_markers"][0]["reason"] == "test pad not populated"

def test_python_schematic_two_terminal_between_anchors_is_generic_and_presentation_only():
    design = volt.Design("schematic-between-anchors")
    a_net = design.net("A")
    b_net = design.net("B")
    link_definition = design.define_component(
        "GenericLink",
        pins=[volt.PinSpec("A", 1), volt.PinSpec("B", 2)],
        schematic_symbol=volt.SchematicSymbolSpec(
            "test:generic-link",
            pins=(
                volt.SchematicSymbolSpec.pin("A", 1, (0, 0), "Left"),
                volt.SchematicSymbolSpec.pin("B", 2, (10, 0), "Right"),
            ),
            primitives=(volt.SchematicSymbolSpec.line((0, 0), (10, 0)),),
        ),
    )
    link = design.instantiate(link_definition, ref="XL1")
    a_net += link["A"]
    b_net += link["B"]

    schematic = design.schematic("Main")
    logical_before = design.to_json()
    with schematic.drawing(unit=10) as drawing:
        start = drawing.node((20, 30))
        end = start.down(30)
        placed = drawing.two_terminal(link).between(start, end).label_ref(loc="right")

    projection = json.loads(schematic.to_json())

    assert design.to_json() == logical_before
    assert placed.start.point == (20.0, 30.0)
    assert placed.end.point == (20.0, 60.0)
    assert placed.orientation == "Down"
    assert [symbol["name"] for symbol in projection["symbol_definitions"]] == [
        "test:generic-link",
        "test:generic-link#two-terminal-scaled-30",
    ]
    assert projection["symbol_instances"][0]["symbol_definition"] == "symbol_def:1"
    assert projection["symbol_fields"][0]["value"] == "XL1"


def test_python_schematic_existing_net_connect_accepts_multiple_anchors_without_mutating_logic():
    design = volt.Design("schematic-multi-anchor-connect")
    bus = design.net("BUS")
    pad_definition = design.define_component(
        "Pad",
        pins=[volt.PinSpec("PAD", 1)],
        schematic_symbol=volt.SchematicSymbolSpec(
            "test:pad",
            pins=(volt.SchematicSymbolSpec.pin("PAD", 1, (0, 0), "Right"),),
            primitives=(volt.SchematicSymbolSpec.circle((0, 0), 1.5),),
        ),
    )
    first_pad = design.instantiate(pad_definition, ref="TP1")
    second_pad = design.instantiate(pad_definition, ref="TP2")
    bus += first_pad["PAD"], second_pad["PAD"]

    schematic = design.schematic("Main")
    logical_before = design.to_json()
    with schematic.drawing(unit=10) as drawing:
        first = drawing.place(first_pad, at=(20, 20)).PAD
        second = drawing.place(second_pad, at=(60, 40)).PAD
        node = drawing.node(first.right(20))
        wire = drawing.connect(bus, first, node, second)
        junction = drawing.junction(bus, at=node)

    projection = json.loads(schematic.to_json())

    assert design.to_json() == logical_before
    assert wire.index == 0
    assert junction.index == 0
    assert projection["wire_runs"] == [
        {
            "id": "wire_run:0",
            "sheet": "sheet:0",
            "net": f"net:{bus.index}",
            "points": [
                {"x": 20.0, "y": 20.0},
                {"x": 40.0, "y": 20.0},
                {"x": 60.0, "y": 20.0},
                {"x": 60.0, "y": 40.0},
            ],
            "route_intent": "Orthogonal",
        }
    ]
    assert projection["junctions"][0]["position"] == {"x": 40.0, "y": 20.0}


def test_python_schematic_between_rejects_identical_anchors():
    design = volt.Design("schematic-between-identical")
    r1 = design.R("1k", ref="R1")
    schematic = design.schematic("Main")
    with schematic.drawing() as drawing:
        anchor = drawing.node((10, 20))
        try:
            drawing.two_terminal(r1).between(anchor, anchor)
        except ValueError as error:
            assert "distinct" in str(error)
        else:
            raise AssertionError("between() with identical anchors should raise ValueError")


def test_python_schematic_between_rejects_diagonal_anchors():
    design = volt.Design("schematic-between-diagonal")
    r1 = design.R("1k", ref="R1")
    schematic = design.schematic("Main")
    with schematic.drawing() as drawing:
        start = drawing.node((10, 20))
        end = start.offset(dx=10, dy=10)
        try:
            drawing.two_terminal(r1).between(start, end)
        except ValueError as error:
            assert "aligned" in str(error)
        else:
            raise AssertionError("between() with diagonal anchors should raise ValueError")


def test_python_schematic_net_connect_requires_at_least_two_anchors():
    design = volt.Design("schematic-connect-too-few")
    bus = design.net("BUS")
    pad_def = design.define_component(
        "Pad",
        pins=[volt.PinSpec("PAD", 1)],
        schematic_symbol=volt.SchematicSymbolSpec(
            "test:pad",
            pins=(volt.SchematicSymbolSpec.pin("PAD", 1, (0, 0), "Right"),),
            primitives=(volt.SchematicSymbolSpec.circle((0, 0), 1.5),),
        ),
    )
    pad = design.instantiate(pad_def, ref="TP1")
    bus += pad["PAD"]
    schematic = design.schematic("Main")
    with schematic.drawing() as drawing:
        tp = drawing.place(pad, at=(20, 20)).PAD
        try:
            drawing.connect(bus, tp)
        except ValueError as error:
            assert "two anchors" in str(error)
        else:
            raise AssertionError("connect(net, single_anchor) should raise ValueError")


def test_python_schematic_net_connect_rejects_pin_on_wrong_net():
    design = volt.Design("schematic-connect-net-mismatch")
    a_net = design.net("A")
    b_net = design.net("B")
    pad_def = design.define_component(
        "Pad",
        pins=[volt.PinSpec("PAD", 1)],
        schematic_symbol=volt.SchematicSymbolSpec(
            "test:pad",
            pins=(volt.SchematicSymbolSpec.pin("PAD", 1, (0, 0), "Right"),),
            primitives=(volt.SchematicSymbolSpec.circle((0, 0), 1.5),),
        ),
    )
    pad_a = design.instantiate(pad_def, ref="TP1")
    pad_b = design.instantiate(pad_def, ref="TP2")
    a_net += pad_a["PAD"]
    b_net += pad_b["PAD"]
    schematic = design.schematic("Main")
    with schematic.drawing() as drawing:
        tp_a = drawing.place(pad_a, at=(20, 20)).PAD
        tp_b = drawing.place(pad_b, at=(60, 20)).PAD
        try:
            drawing.connect(a_net, tp_a, tp_b)
        except ValueError as error:
            message = str(error)
            assert "TP2" in message
            assert "net A" in message or "A (net:" in message
        else:
            raise AssertionError("connect() with pin on wrong net should raise ValueError")


def test_python_schematic_node_with_offsets_returns_absolute_anchor():
    design = volt.Design("schematic-node-offset")
    schematic = design.schematic("Main")
    with schematic.drawing(unit=10) as drawing:
        base = drawing.node((30, 40))
        shifted = drawing.node((30, 40), dx=5, dy=-3)
    assert base.point == (30.0, 40.0)
    assert shifted.point == (35.0, 37.0)


def test_detached_schematic_symbol_pin_helpers_report_missing_component_context():
    design = volt.Design("schematic-detached-symbol")
    r1 = design.R("10k", ref="R1")
    schematic = design.schematic("Main")
    placed = schematic.place(r1, at=(40, 20), symbol="resistor")
    detached = volt.SchematicSymbol(schematic, placed.index)

    try:
        detached.pin(1)
    except ValueError as error:
        message = str(error)
        assert "Component handle returned by Schematic.place()" in message
        assert "sheet 'Main'" in message
    else:
        raise AssertionError("detached symbol pin helpers should explain missing component context")

def test_python_schematic_handles_are_publicly_exported():
    assert "Schematic" in volt.__all__
    assert "SchematicAnchor" in volt.__all__
    assert "SchematicDrawing" in volt.__all__
    assert "PlacedSchematicElement" in volt.__all__
    assert "SchematicJunction" in volt.__all__
    assert "SchematicSymbol" in volt.__all__
    assert "SchematicPinAnchor" in volt.__all__
    assert "SchematicPort" in volt.__all__
    assert "SchematicWire" in volt.__all__
    assert "SchematicWireBuilder" in volt.__all__
    assert "SchematicNetLabel" in volt.__all__
    assert "SchematicNoConnect" in volt.__all__
    assert "SchematicSignalStub" in volt.__all__
    assert "SchematicBlockPinSpec" in volt.__all__
    assert "SchematicSymbolSpec" in volt.__all__
    assert "SchematicSymbolField" in volt.__all__
    assert "SchematicTwoTerminalElement" in volt.__all__
