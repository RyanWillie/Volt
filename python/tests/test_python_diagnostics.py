import json

import volt


def test_voltage_rating_diagnostic_is_inspectable():
    design = volt.Design("rating")
    vdd = design.net("VDD", kind="power", voltage=5.0)
    c1 = design.C(capacitance=100e-9, ref="C1")
    c1.select_part(
        manufacturer="Example",
        part_number="LOW-VOLTAGE-CAP",
        package="0603",
        footprint=("Capacitor_SMD", "C_0603"),
        pin_pads={1: "1", 2: "2"},
        voltage_rating=3.3,
    )

    vdd += c1[1]

    report = design.validate()

    assert report.has_errors
    assert "SELECTED_PART_VOLTAGE_RATING_EXCEEDED" in {
        diagnostic.code for diagnostic in report
    }

def test_pin_voltage_range_diagnostic_is_inspectable():
    design = volt.Design("pin-voltage")
    load = design.define_component(
        "Load",
        pins=[
            volt.PinSpec(
                "VCC",
                1,
                role="power",
                terminal="power",
                direction="input",
                voltage_range=(1.8, 3.6),
            ),
        ],
    )
    source = design.define_component(
        "Supply",
        pins=[
            volt.PinSpec(
                "OUT",
                1,
                role="power_output",
                terminal="power",
                direction="output",
            ),
        ],
    )
    u1 = design.instantiate(load, ref="U1")
    u2 = design.instantiate(source, ref="U2")

    vdd = design.net("VDD", kind="power", voltage=5.0)
    vdd += u1["VCC"], u2["OUT"]

    report = design.validate()

    assert report.has_errors
    assert "PIN_VOLTAGE_RANGE_VIOLATION" in {diagnostic.code for diagnostic in report}

def test_power_pin_semantics_drive_diagnostics():
    design = volt.Design("typed-power")
    load = design.define_component(
        "Load",
        pins=[
            volt.PinSpec(
                "VCC",
                1,
                role="power",
                terminal="power",
                direction="input",
                voltage_range=(3.0, 3.6),
            ),
            volt.PinSpec("GND", 2, role="ground", terminal="ground", direction="passive"),
        ],
    )
    u1 = design.instantiate(load, ref="U1")

    vcc = design.net("VCC", kind="power", voltage=3.3)
    vcc += u1["VCC"]
    gnd = design.net("GND", kind="ground")
    gnd += u1["GND"]

    report = design.validate()

    assert "POWER_INPUT_WITHOUT_SOURCE" in {diagnostic.code for diagnostic in report}

def test_python_schematic_readiness_reports_detached_net_stubs():
    design = volt.Design("schematic-readiness")
    vcc = design.net("VCC", kind="power")
    r1 = design.R(resistance=330, ref="R1")
    vcc += r1[1]

    schematic = design.schematic("Main")
    schematic.place(r1, at=(40, 20), symbol="resistor")
    schematic.wire(vcc, [(0, 12), (10, 12)])
    schematic.label(vcc, at=(0, 10))

    report = schematic.validate()

    assert report.has_errors
    assert len(report) == 1
    diagnostic = report[0]
    assert diagnostic.code == "SCHEMATIC_PIN_NET_NOT_VISUALLY_COVERED"
    assert (
        diagnostic.message
        == "Schematic sheet 'Main' omits visual net coverage for R1 pin 1 (1) on VCC"
    )
    assert [(entity.kind, entity.index) for entity in diagnostic.entities] == [
        ("sheet", 0),
        ("component", r1.index),
        ("pin", r1[1].index),
        ("pin_definition", 0),
        ("net", vcc.index),
    ]

def test_python_schematic_validation_reports_quality_diagnostics():
    design = volt.Design("schematic-quality")
    vcc = design.net("VCC", kind="power")
    r1 = design.R(resistance=330, ref="R1")
    r2 = design.R(resistance=330, ref="R2")
    r3 = design.R(resistance=330, ref="R3")
    vcc += (r1[1], r2[1], r3[1])
    r1[2].mark_no_connect()

    schematic = design.schematic("Main")
    first = schematic.place(r1, at=(40, 20), symbol="resistor")
    second = schematic.place(r2, at=(80, 20), symbol="resistor")
    third = schematic.place(r3, at=(120, 20), symbol="resistor")
    schematic.label(vcc, at=first.pin(1))
    schematic.label(vcc, at=second.pin(1))
    schematic.label(vcc, at=third.pin(1))

    report = schematic.validate()
    codes = {diagnostic.code for diagnostic in report}

    assert "SCHEMATIC_NET_FRAGMENTED_PIN_LABELS" in codes
    assert "SCHEMATIC_NO_CONNECT_INTENT_NOT_MARKED" in codes
    assert not report.has_errors

def test_python_schematic_readability_reports_presentation_diagnostics():
    design = volt.Design("schematic-readability")
    scoped = design.net("PWR/OUT_3V3", kind="power")
    r1 = design.R("10k", ref="R1")

    schematic = design.schematic(
        "Main",
        size=(100, 80),
        margins=(10, 10, 10, 10),
        revision="A",
    )
    power = schematic.region("Power", x=10, y=10, w=20, h=20)
    power.label(scoped, at=(35, 5), orient="down")
    schematic.label(scoped, at=(20, 65))
    with schematic.drawing(at=(40, 20)) as drawing:
        placed = drawing.place(r1, symbol="resistor")
        placed.label_ref()

    report = schematic.validate_readability()
    codes = {diagnostic.code for diagnostic in report}

    assert "SCHEMATIC_OBJECT_OUTSIDE_AUTHORED_REGION" in codes
    assert "SCHEMATIC_OBJECT_OVERLAPS_TITLE_BLOCK" in codes
    assert "SCHEMATIC_TEXT_NOT_HORIZONTAL" in codes
    assert "SCHEMATIC_OVERLONG_DISPLAY_LABEL" in codes
    assert "SCHEMATIC_PASSIVE_VALUE_FIELD_MISSING" in codes
    assert "SCHEMATIC_OBJECT_OUTSIDE_AUTHORED_REGION" not in {
        diagnostic.code for diagnostic in schematic.validate()
    }
    assert any(diagnostic.severity == "error" for diagnostic in report)

def test_python_schematic_readability_reports_authored_region_content_overlap():
    design = volt.Design("region-content-overlap")
    net = design.net("REGION_TEST")
    schematic = design.schematic("Main", size=(100, 80), margins=(10, 10, 10, 10))
    left = schematic.region("Left", x=10, y=10, w=40, h=30)
    right = schematic.region("Right", x=30, y=10, w=40, h=30)

    left.wire(net, [(18, 10), (48, 10)])
    right.wire(net, [(0, 10), (30, 10)])

    report = schematic.validate_readability()
    diagnostics = [
        diagnostic
        for diagnostic in report
        if diagnostic.code == "SCHEMATIC_AUTHORED_REGION_CONTENT_OVERLAP"
    ]

    assert len(diagnostics) == 1
    assert diagnostics[0].severity == "error"
    assert "SCHEMATIC_AUTHORED_REGION_CONTENT_OVERLAP" not in {
        diagnostic.code for diagnostic in schematic.validate()
    }

def test_diagnostics_are_inspectable():
    design = volt.Design("incomplete")
    design.R("10k", ref="R1")

    report = design.validate()
    assert report.has_errors
    assert len(report) == 2
    assert {diagnostic.code for diagnostic in report} == {"UNCONNECTED_REQUIRED_PIN"}
    assert all(diagnostic.severity == "error" for diagnostic in report)
    assert all(diagnostic.entities for diagnostic in report)
