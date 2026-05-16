import json

import volt


def test_python_design_intent_serializes_as_kernel_design_intent():
    design = volt.Design("intent")
    mcu = design.define_component(
        "MCU",
        pins=[volt.PinSpec("PB2", 1, role="input")],
    )
    u1 = design.instantiate(mcu, ref="U1")

    boot = design.net("BOOT_TRACE").mark_stub()
    u1["PB2"].mark_no_connect()

    circuit = json.loads(design.to_json())

    assert boot.index == 0
    assert u1["PB2"].index == 0
    assert circuit["design_intent"] == {
        "stub_nets": ["net:0"],
        "no_connect_pins": ["pin:0"],
    }

def test_python_stub_net_intent_suppresses_only_intended_net_shape_diagnostics():
    design = volt.Design("stub-validation")
    probe_definition = design.define_component(
        "Probe",
        pins=[volt.PinSpec("SWDIO", 1, requirement="optional")],
    )
    tp1 = design.instantiate(probe_definition, ref="TP1")

    design.net("BOOT_TRACE").mark_stub()
    swdio = design.net("SWDIO").mark_stub()
    swdio += tp1["SWDIO"]
    design.net("REAL_FLOATING_NET")

    report = design.validate()

    assert [diagnostic.code for diagnostic in report] == ["EMPTY_NET"]
    assert report[0].entities[0].kind == "net"
    assert report[0].entities[0].index == 2

def test_python_no_connect_intent_suppresses_only_intended_missing_pin_diagnostics():
    design = volt.Design("no-connect-validation")
    mcu = design.define_component(
        "MCU",
        pins=[
            volt.PinSpec("PB2", 1, role="input"),
            volt.PinSpec("PB3", 2, role="input"),
        ],
    )
    u1 = design.instantiate(mcu, ref="U1")

    u1["PB2"].mark_no_connect()

    report = design.validate()

    assert [diagnostic.code for diagnostic in report] == ["UNCONNECTED_REQUIRED_PIN"]
    assert report[0].entities[0].kind == "pin"
    assert report[0].entities[0].index == u1["PB3"].index

def test_python_schematic_no_connect_marker_does_not_mutate_logical_intent():
    design = volt.Design("schematic-no-connect-boundary")
    test_point = design.define_component(
        "TestPoint",
        pins=[volt.PinSpec("NC", 1)],
        schematic_symbol=volt.SchematicSymbolSpec(
            "test:point",
            pins=(volt.SchematicSymbolSpec.pin("NC", 1, (0, 0), "Right"),),
            primitives=(volt.SchematicSymbolSpec.circle((0, 0), 1.5),),
        ),
    )
    tp1 = design.instantiate(test_point, ref="TP1")
    schematic = design.schematic("Main")
    pin = schematic.place(tp1, at=(10, 20), symbol="test:point").pin("NC")

    schematic.no_connect(pin, reason="open test pad")

    logical = json.loads(design.to_json())
    projection = json.loads(schematic.to_json())
    report = design.validate()

    assert logical.get("design_intent", {}).get("no_connect_pins", []) == []
    assert projection["no_connect_markers"][0]["pin"] == f"pin:{pin.pin.index}"
    assert [diagnostic.code for diagnostic in report] == ["UNCONNECTED_REQUIRED_PIN"]
    assert report[0].entities[0].index == pin.pin.index
