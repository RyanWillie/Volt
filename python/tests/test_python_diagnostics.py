import json

import pytest

import volt
from volt import _volt
from volt.diagnostics import _diagnostic_from_dict
from volt.project import _flat_diagnostic_payload, _report_diagnostics


def test_pcb_visual_diagnostic_codes_are_exported_in_stable_order():
    assert volt.PCB_VISUAL_DIAGNOSTIC_CODES == tuple(_volt.pcb_visual_diagnostic_codes())
    assert volt.PCB_VISUAL_DIAGNOSTIC_CODES == (
        "PCB_VISUAL_PLACEMENT_OVERLAP",
        "PCB_VISUAL_PLACEMENT_CROWDING",
        "PCB_VISUAL_REFERENCE_DESIGNATOR_HIDDEN",
        "PCB_VISUAL_REFERENCE_DESIGNATOR_UNREADABLE",
        "PCB_VISUAL_LABEL_OVERLAP",
        "PCB_VISUAL_LABEL_OUTSIDE_BOARD",
        "PCB_VISUAL_ROUTE_READABILITY_CONFLICT",
        "PCB_VISUAL_BOARD_FEATURE_ANNOTATION_MISSING",
    )


def test_erc_and_drc_diagnostic_contracts_are_exported_in_stable_order():
    assert volt.DIAGNOSTIC_CATEGORIES == tuple(_volt.diagnostic_categories())
    assert volt.ERC_DIAGNOSTIC_CODES == tuple(_volt.erc_diagnostic_codes())
    assert volt.DRC_DIAGNOSTIC_CODES == tuple(_volt.drc_diagnostic_codes())

    assert volt.DIAGNOSTIC_CATEGORIES == (
        "general",
        "erc",
        "drc",
        "pcb.board",
        "pcb.visual",
    )
    assert volt.ERC_DIAGNOSTIC_CODES == (
        "PIN_MUST_NOT_CONNECT",
        "PIN_INTENTIONAL_NO_CONNECT_IS_CONNECTED",
        "UNCONNECTED_REQUIRED_PIN",
        "EMPTY_NET",
        "SINGLE_PIN_NET",
        "UNBOUND_REQUIRED_PORT",
        "PIN_GROUND_ON_NON_GROUND_NET",
        "PIN_POWER_ON_GROUND_NET",
        "POWER_INPUT_WITHOUT_SOURCE",
        "SELECTED_PART_VOLTAGE_RATING_EXCEEDED",
        "PIN_VOLTAGE_RANGE_VIOLATION",
        "NET_CLASS_VOLTAGE_EXCEEDED",
        "MULTIPLE_OUTPUTS_ON_NET",
        "INPUT_SIGNAL_DOMAIN_MISMATCH",
    )
    assert volt.DRC_DIAGNOSTIC_CODES == (
        "PCB_TRACK_WIDTH_BELOW_MINIMUM",
        "PCB_VIA_DRILL_BELOW_MINIMUM",
        "PCB_VIA_ANNULAR_BELOW_MINIMUM",
        "PCB_COPPER_OUTSIDE_OUTLINE",
        "PCB_COPPER_CLEARANCE_VIOLATION",
        "PCB_KEEPOUT_COPPER_VIOLATION",
        "PCB_KEEPOUT_VIA_VIOLATION",
        "PCB_KEEPOUT_PLACEMENT_VIOLATION",
        "PCB_NET_UNROUTED",
        "PCB_TRACK_WIDTH_BELOW_NET_CLASS",
        "PCB_VIA_DRILL_BELOW_NET_CLASS",
        "PCB_VIA_DIAMETER_BELOW_NET_CLASS",
        "PCB_COPPER_ON_DISALLOWED_LAYER",
    )


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

def test_input_signal_domain_mismatch_diagnostic_is_inspectable():
    design = volt.Design("input-only")
    left = design.define_component(
        "LeftReceiver",
        pins=[
            volt.PinSpec(
                "IN",
                1,
                role="digital_input",
                terminal="signal",
                direction="input",
                signal="digital",
            ),
        ],
    )
    right = design.define_component(
        "RightReceiver",
        pins=[
            volt.PinSpec(
                "IN",
                1,
                role="analog_input",
                terminal="signal",
                direction="input",
                signal="analog",
            ),
        ],
    )
    u1 = design.instantiate(left, ref="U1")
    u2 = design.instantiate(right, ref="U2")

    sense = design.net("SENSE")
    sense += u1["IN"], u2["IN"]

    report = design.validate()

    diagnostic = next(
        item for item in report if item.code == "INPUT_SIGNAL_DOMAIN_MISMATCH"
    )
    assert diagnostic.category == "erc"
    assert diagnostic.severity == "error"
    assert [(entity.kind, entity.index) for entity in diagnostic.entities] == [
        ("net", sense.index),
        ("pin", u1["IN"].index),
        ("pin", u2["IN"].index),
    ]

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


def test_pcb_visual_diagnostic_overlay_contract_is_inspectable():
    diagnostic = _diagnostic_from_dict(
        {
            "severity": "warning",
            "category": "pcb.visual",
            "code": "PCB_VISUAL_REFERENCE_DESIGNATOR_UNREADABLE",
            "message": "Reference designator is difficult to read",
            "entities": [
                {"kind": "board", "index": 0},
                {"kind": "board_text", "index": 2},
            ],
            "overlays": [
                {
                    "kind": "bounding_box",
                    "points": [[10.0, 20.0], [14.0, 22.0]],
                    "entities": [{"kind": "board_text", "index": 2}],
                    "layers": [{"kind": "board_layer", "index": 0}],
                },
                {
                    "kind": "segment",
                    "points": [[10.0, 21.0], [14.0, 21.0]],
                    "entities": [],
                    "layers": [{"kind": "board_layer", "index": 0}],
                },
            ],
        }
    )

    assert diagnostic.category == "pcb.visual"
    assert diagnostic.entities == (
        volt.DiagnosticEntity("board", 0),
        volt.DiagnosticEntity("board_text", 2),
    )
    assert diagnostic.overlays[0].kind == "bounding_box"
    assert diagnostic.overlays[0].points == ((10.0, 20.0), (14.0, 22.0))
    assert diagnostic.overlays[0].entities == (volt.DiagnosticEntity("board_text", 2),)
    assert diagnostic.overlays[0].layers == (volt.DiagnosticEntity("board_layer", 0),)


def test_erc_and_drc_diagnostic_payload_categories_and_references_are_preserved():
    erc = _diagnostic_from_dict(
        {
            "severity": "error",
            "category": "erc",
            "code": "MULTIPLE_OUTPUTS_ON_NET",
            "message": "Net has multiple output drivers",
            "entities": [
                {"kind": "net", "index": 3},
                {"kind": "pin", "index": 4},
                {"kind": "pin", "index": 7},
            ],
        }
    )
    drc = _diagnostic_from_dict(
        {
            "severity": "error",
            "category": "drc",
            "code": "PCB_COPPER_CLEARANCE_VIOLATION",
            "message": "Copper on different nets violates required clearance",
            "entities": [
                {"kind": "board_track", "index": 0},
                {"kind": "board_via", "index": 1},
                {"kind": "net", "index": 2},
                {"kind": "net", "index": 5},
                {"kind": "board_layer", "index": 0},
            ],
        }
    )

    assert erc.category == "erc"
    assert erc.entities == (
        volt.DiagnosticEntity("net", 3),
        volt.DiagnosticEntity("pin", 4),
        volt.DiagnosticEntity("pin", 7),
    )
    assert drc.category == "drc"
    assert drc.entities == (
        volt.DiagnosticEntity("board_track", 0),
        volt.DiagnosticEntity("board_via", 1),
        volt.DiagnosticEntity("net", 2),
        volt.DiagnosticEntity("net", 5),
        volt.DiagnosticEntity("board_layer", 0),
    )


def test_project_diagnostics_preserve_pcb_visual_overlay_payloads():
    diagnostics = [
        volt.Diagnostic(
            severity="warning",
            category="pcb.visual",
            code="PCB_VISUAL_LABEL_OVERLAP",
            message="Labels overlap",
            entities=(volt.DiagnosticEntity("board", 0),),
            overlays=(
                volt.DiagnosticOverlay(
                    "polygon",
                    ((1.0, 1.0), (3.0, 1.0), (3.0, 2.0)),
                    entities=(volt.DiagnosticEntity("board_text", 0),),
                    layers=(volt.DiagnosticEntity("board_layer", 0),),
                ),
            ),
        )
    ]

    [project_diagnostic] = _report_diagnostics(
        "board",
        "pcb:Main",
        "pcb.visual",
        diagnostics,
        design="demo",
        board="Main",
    )

    payload = _flat_diagnostic_payload(project_diagnostic)

    assert project_diagnostic.category == "pcb.visual"
    assert payload == {
        "severity": "warning",
        "category": "pcb.visual",
        "code": "PCB_VISUAL_LABEL_OVERLAP",
        "message": "Labels overlap",
        "entities": [{"kind": "board", "index": 0}],
        "overlays": [
            {
                "kind": "polygon",
                "points": [[1.0, 1.0], [3.0, 1.0], [3.0, 2.0]],
                "entities": [{"kind": "board_text", "index": 0}],
                "layers": [{"kind": "board_layer", "index": 0}],
            }
        ],
    }


def test_python_diagnostic_overlay_rejects_invalid_layers_and_geometry():
    with pytest.raises(ValueError, match="board_layer"):
        volt.DiagnosticOverlay(
            "point",
            ((1.0, 2.0),),
            layers=(volt.DiagnosticEntity("component", 0),),
        )

    with pytest.raises(ValueError, match="at least three"):
        volt.DiagnosticOverlay("polygon", ((0.0, 0.0), (1.0, 1.0)))

    with pytest.raises(ValueError, match="finite"):
        volt.DiagnosticOverlay("point", ((float("inf"), 0.0),))
