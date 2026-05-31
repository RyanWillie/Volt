"""Generate routed regulator LED board benchmark artifacts."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

import volt

EXAMPLE_SLUG = "routed_regulator_led_board"
SHEET_FILE = "routed_regulator_led_board/main.py"


@dataclass(frozen=True)
class ExampleArtifacts:
    logical_json: Path
    schematic_json: Path
    schematic_svg: Path
    schematic_body_svg: Path
    schematic_svg_pages: tuple[Path, ...]
    pcb_json: Path
    pcb_svg: Path
    validation_report: Path


def _validation_report_payload(report: volt.DiagnosticReport) -> dict:
    counts = {"errors": 0, "warnings": 0, "infos": 0}
    diagnostics = []
    for diagnostic in report:
        if diagnostic.severity == "error":
            counts["errors"] += 1
        elif diagnostic.severity == "warning":
            counts["warnings"] += 1
        else:
            counts["infos"] += 1
        diagnostics.append(
            {
                "severity": diagnostic.severity,
                "code": diagnostic.code,
                "message": diagnostic.message,
                "entities": [
                    {"kind": entity.kind, "index": entity.index}
                    for entity in diagnostic.entities
                ],
            }
        )
    return {"summary": counts, "diagnostics": diagnostics}


def validation_report_json(reports: dict[str, volt.DiagnosticReport]) -> str:
    report_payloads = {
        name: _validation_report_payload(report) for name, report in reports.items()
    }
    counts = {"errors": 0, "warnings": 0, "infos": 0}
    diagnostics = []
    for name, payload in report_payloads.items():
        for severity, count in payload["summary"].items():
            counts[severity] += count
        for diagnostic in payload["diagnostics"]:
            diagnostics.append({"source": name, **diagnostic})
    return json.dumps(
        {
            "summary": counts,
            "diagnostics": diagnostics,
            "reports": report_payloads,
        },
        indent=2,
        sort_keys=True,
    ) + "\n"


def _require_clean(reports: dict[str, volt.DiagnosticReport]) -> None:
    diagnostics = [
        f"{name}:{diagnostic.code}"
        for name, report in reports.items()
        for diagnostic in report
    ]
    if diagnostics:
        raise RuntimeError(
            "Routed regulator LED board validation failed: " + ", ".join(diagnostics)
        )


def _passive_0603(ref: tuple[str, str]) -> volt.FootprintDefinition:
    return volt.FootprintDefinition(
        ref,
        pads=(
            volt.FootprintPad.surface_mount(
                "1", at=(-0.75, 0.0), size=(0.80, 0.95), shape="rounded_rectangle"
            ),
            volt.FootprintPad.surface_mount(
                "2", at=(0.75, 0.0), size=(0.80, 0.95), shape="rounded_rectangle"
            ),
        ),
    )


def _header_1x02() -> volt.FootprintDefinition:
    return volt.FootprintDefinition(
        ("connectors", "PinHeader_1x02_P2.54mm_Vertical"),
        pads=(
            volt.FootprintPad.through_hole(
                "1",
                at=(0.0, -1.27),
                size=(1.70, 1.70),
                drill=volt.FootprintDrill(1.00),
            ),
            volt.FootprintPad.through_hole(
                "2",
                at=(0.0, 1.27),
                size=(1.70, 1.70),
                drill=volt.FootprintDrill(1.00),
            ),
        ),
    )


def _regulator_sot_223() -> volt.FootprintDefinition:
    return volt.FootprintDefinition(
        ("volt.examples.routed_regulator_led_board", "SOT-223-3_TabPin2"),
        pads=(
            volt.FootprintPad.surface_mount(
                "1", at=(-3.00, 2.00), size=(0.95, 1.10), shape="rounded_rectangle"
            ),
            volt.FootprintPad.surface_mount(
                "2", at=(0.0, 2.00), size=(0.95, 1.10), shape="rounded_rectangle"
            ),
            volt.FootprintPad.surface_mount(
                "3", at=(3.00, 2.00), size=(0.95, 1.10), shape="rounded_rectangle"
            ),
            volt.FootprintPad.surface_mount(
                "4", at=(0.0, -2.00), size=(3.20, 1.35), shape="rounded_rectangle"
            ),
        ),
    )


FOOTPRINTS = {
    "header_1x02": _header_1x02(),
    "regulator": _regulator_sot_223(),
    "capacitor_0603": _passive_0603(("passives", "C_0603_1608Metric")),
    "resistor_0603": _passive_0603(("passives", "R_0603_1608Metric")),
    "led_0603": _passive_0603(("leds", "LED_0603_1608Metric")),
}


def build_design() -> tuple[volt.Design, dict[str, volt.Net], dict[str, volt.Component]]:
    design = volt.Design("routed-regulator-led-board")
    nets = {
        "VIN": design.net("VIN", kind="power", voltage=5.0),
        "+3V3": design.net("+3V3", kind="power", voltage=3.3),
        "LED_A": design.net("LED_A"),
        "GND": design.net("GND", kind="ground"),
    }
    parts = {
        "J1": design.power_input_header_1x02(ref="J1"),
        "U1": design.regulator(ref="U1"),
        "CIN": design.C("1 uF", ref="C1"),
        "COUT": design.C("1 uF", ref="C2"),
        "RLED": design.R("330 ohm", ref="R1", resistance=330, tolerance=0.01),
        "DLED": design.LED(ref="D1"),
    }

    nets["VIN"] += parts["J1"][1], parts["U1"]["IN"], parts["CIN"][1]
    nets["+3V3"] += parts["U1"]["OUT"], parts["COUT"][1], parts["RLED"][1]
    nets["LED_A"] += parts["RLED"][2], parts["DLED"]["A"]
    nets["GND"] += (
        parts["J1"][2],
        parts["U1"]["GND"],
        parts["CIN"][2],
        parts["COUT"][2],
        parts["DLED"]["K"],
    )

    parts["J1"].select_part(
        manufacturer="Generic",
        part_number="HDR-1x02-2.54mm",
        package="2.54mm-1x02",
        footprint=FOOTPRINTS["header_1x02"],
        pin_pads={1: "1", 2: "2"},
    )
    parts["U1"].select_part(
        manufacturer="STMicroelectronics",
        part_number="LD1117S33TR",
        package="SOT-223",
        footprint=FOOTPRINTS["regulator"],
        pin_pads={"GND": "1", "OUT": ("2", "4"), "IN": "3"},
        voltage_rating=6.0,
    )
    for component, part_number in (
        (parts["CIN"], "GRM188R61A105KA61D"),
        (parts["COUT"], "GRM188R61A105KA61D"),
    ):
        component.select_part(
            manufacturer="Murata",
            part_number=part_number,
            package="0603",
            footprint=FOOTPRINTS["capacitor_0603"],
            pin_pads={1: "1", 2: "2"},
            voltage_rating=10.0,
        )
    parts["RLED"].select_part(
        manufacturer="Yageo",
        part_number="RC0603FR-07330RL",
        package="0603",
        footprint=FOOTPRINTS["resistor_0603"],
        pin_pads={1: "1", 2: "2"},
        power_rating=0.1,
    )
    parts["DLED"].select_part(
        manufacturer="Lite-On",
        part_number="LTST-C190KRKT",
        package="0603",
        footprint=FOOTPRINTS["led_0603"],
        pin_pads={"A": "1", "K": "2"},
    )
    return design, nets, parts


def build_schematic(
    design: volt.Design,
    nets: dict[str, volt.Net],
    parts: dict[str, volt.Component],
) -> volt.Schematic:
    sheet = design.schematic(
        "Regulator LED Board",
        size=(340, 220),
        orientation="landscape",
        title="Regulator LED Board",
        number=1,
        page_count=1,
        revision="A",
        date="2026-05-31",
        project="Volt PCB",
        file=SHEET_FILE,
        margins=(8, 8, 8, 8),
        coordinate_zones=(4, 3),
        grid={"spacing": 5, "visible": False},
    )

    with sheet.drawing(unit=20) as drawing:
        header = drawing.place(parts["J1"], at=(40, 80), orient="Right").label_ref(loc="left")
        regulator = drawing.place(parts["U1"], at=(125, 65)).label_ref(loc="top")
        cin = (
            drawing.two_terminal(parts["CIN"])
            .at(header[1].right(45))
            .down()
            .label_ref(loc="left", offset=8)
            .label_value(loc="left", offset=22)
        )
        cout = (
            drawing.two_terminal(parts["COUT"])
            .at(regulator.OUT.right(35))
            .down()
            .label_ref(loc="right", offset=8)
            .label_value(loc="right", offset=22)
        )
        rled = (
            drawing.two_terminal(parts["RLED"])
            .at(regulator.OUT.right(65))
            .right()
            .label_ref(loc="top", offset=8)
            .label_value(loc="bottom", offset=8)
        )
        led = (
            drawing.two_terminal(parts["DLED"])
            .at(rled.end.right(20))
            .right()
            .reverse()
            .label_ref(loc="top", offset=8)
        )
        ground = drawing.ground("GND", net=nets["GND"], at=regulator.GND.down(28))

        drawing.connect(header[1], cin.start, net=nets["VIN"], shape="-").dot()
        drawing.connect(cin.start, regulator.IN, net=nets["VIN"], shape="-")
        drawing.connect(regulator.OUT, cout.start, net=nets["+3V3"], shape="-").dot()
        drawing.connect(cout.start, rled.start, net=nets["+3V3"], shape="-")
        drawing.connect(rled.end, led.start, net=nets["LED_A"], shape="-")
        drawing.connect(regulator.GND, ground.pin, net=nets["GND"], shape="-")
        drawing.connect(header[2], ground.pin, net=nets["GND"], shape="-")
        drawing.connect(cin.end, ground.pin, net=nets["GND"], shape="-")
        drawing.connect(cout.end, ground.pin, net=nets["GND"], shape="-")
        drawing.connect(led.end, ground.pin, net=nets["GND"], shape="-")

    return sheet


def build_board(
    design: volt.Design,
    nets: dict[str, volt.Net],
    parts: dict[str, volt.Component],
) -> volt.Board:
    board = design.board("Routed Regulator LED Board")
    front = board.add_layer("F.Cu", role="copper", side="top")
    back = board.add_layer("B.Cu", role="copper", side="bottom")
    silk = board.add_layer("F.SilkS", role="silkscreen", side="top")
    board.set_layer_stack((front, back), thickness=1.6)
    board.set_design_rules(
        copper_clearance=0.20,
        min_track_width=0.25,
        min_via_drill=0.30,
        min_via_annular=0.70,
        board_outline_clearance=0.25,
    )
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(70.0, 42.0))
    board.add_mounting_hole("MH1", at=(4.0, 4.0), diameter=2.7)
    board.add_mounting_hole("MH2", at=(66.0, 4.0), diameter=2.7)
    board.add_mounting_hole("MH3", at=(4.0, 38.0), diameter=2.7)
    board.add_mounting_hole("MH4", at=(66.0, 38.0), diameter=2.7)

    board.place(parts["J1"], at=(7.0, 21.0), rotation=0.0, side="top", locked=True)
    board.place(parts["U1"], at=(30.0, 21.0), rotation=0.0, side="top", locked=True)
    board.place(parts["CIN"], at=(16.0, 10.0), rotation=0.0, side="top")
    board.place(parts["COUT"], at=(43.0, 10.0), rotation=0.0, side="top")
    board.place(parts["RLED"], at=(43.0, 31.0), rotation=0.0, side="top")
    board.place(parts["DLED"], at=(56.0, 31.0), rotation=0.0, side="top")
    board.add_text("3V3 LED", at=(36.0, 38.0), layer=silk, size=1.3)

    board.add_track(nets["VIN"], layer=front, points=((7.0, 19.73), (15.25, 10.0)), width=0.35)
    board.add_track(nets["VIN"], layer=front, points=((15.25, 10.0), (15.25, 7.0)), width=0.30)
    board.add_via(nets["VIN"], at=(15.25, 7.0), start_layer=front, end_layer=back)
    board.add_via(nets["VIN"], at=(36.0, 23.0), start_layer=front, end_layer=back)
    board.add_track(nets["VIN"], layer=back, points=((15.25, 7.0), (36.0, 23.0)), width=0.30)
    board.add_track(nets["VIN"], layer=front, points=((36.0, 23.0), (33.0, 23.0)), width=0.30)

    board.add_track(nets["+3V3"], layer=front, points=((30.0, 23.0), (42.25, 10.0)), width=0.30)
    board.add_track(nets["+3V3"], layer=front, points=((30.0, 23.0), (30.0, 19.0)), width=0.45)
    board.add_track(nets["+3V3"], layer=front, points=((30.0, 19.0), (42.25, 10.0)), width=0.30)
    board.add_track(nets["+3V3"], layer=front, points=((30.0, 23.0), (42.25, 31.0)), width=0.30)

    board.add_track(nets["LED_A"], layer=front, points=((43.75, 31.0), (55.25, 31.0)), width=0.25)

    board.add_track(nets["GND"], layer=front, points=((7.0, 22.27), (20.0, 37.0)), width=0.35)
    board.add_track(nets["GND"], layer=front, points=((27.0, 23.0), (20.0, 37.0)), width=0.35)
    board.add_track(nets["GND"], layer=front, points=((16.75, 10.0), (20.0, 37.0)), width=0.35)
    board.add_track(
        nets["GND"],
        layer=front,
        points=((43.75, 10.0), (62.0, 10.0), (62.0, 37.0), (50.0, 37.0)),
        width=0.35,
    )
    board.add_track(nets["GND"], layer=front, points=((56.75, 31.0), (50.0, 37.0)), width=0.35)
    board.add_track(nets["GND"], layer=front, points=((50.0, 37.0), (20.0, 37.0)), width=0.35)
    board.add_via(nets["GND"], at=(20.0, 37.0), start_layer=front, end_layer=back)
    board.add_track(nets["GND"], layer=back, points=((20.0, 37.0), (50.0, 37.0)), width=0.35)
    return board


def build_example() -> tuple[volt.Design, volt.Schematic, volt.Board]:
    design, nets, parts = build_design()
    schematic = build_schematic(design, nets, parts)
    board = build_board(design, nets, parts)
    return design, schematic, board


def write_artifacts(output_dir: Path | str | None = None) -> ExampleArtifacts:
    if output_dir is None:
        output_dir = Path(__file__).resolve().parent / "artifacts"
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    design, schematic, board = build_example()
    reports = {
        "logical_design": design.validate(),
        "pcb_readiness": design.validate_for_pcb(),
        "schematic_readiness": schematic.validate(),
        "schematic_readability": schematic.validate_readability(),
        "pcb_board": board.validate(),
    }
    _require_clean(reports)

    logical_json = output_path / f"{EXAMPLE_SLUG}.volt.json"
    schematic_json = output_path / f"{EXAMPLE_SLUG}.volt.schematic.json"
    schematic_svg = output_path / f"{EXAMPLE_SLUG}.svg"
    schematic_body_svg = output_path / f"{EXAMPLE_SLUG}.body.svg"
    schematic_svg_pages_dir = output_path / f"{EXAMPLE_SLUG}.pages"
    pcb_json = output_path / f"{EXAMPLE_SLUG}.volt.pcb.json"
    pcb_svg = output_path / f"{EXAMPLE_SLUG}.pcb.svg"
    validation_report = output_path / f"{EXAMPLE_SLUG}.validation.json"

    if schematic_svg_pages_dir.exists():
        for page_path in schematic_svg_pages_dir.glob("*.svg"):
            page_path.unlink()
    design.write(logical_json)
    schematic.write_json(schematic_json)
    schematic.write_svg(schematic_svg)
    schematic.write_body_svg(schematic_body_svg)
    schematic_svg_pages = schematic.write_svg_pages(
        schematic_svg_pages_dir,
        prefix=EXAMPLE_SLUG,
    )
    board.write_json(pcb_json)
    board.write_svg(pcb_svg)
    validation_report.write_text(validation_report_json(reports), encoding="utf-8")
    return ExampleArtifacts(
        logical_json=logical_json,
        schematic_json=schematic_json,
        schematic_svg=schematic_svg,
        schematic_body_svg=schematic_body_svg,
        schematic_svg_pages=schematic_svg_pages,
        pcb_json=pcb_json,
        pcb_svg=pcb_svg,
        validation_report=validation_report,
    )


if __name__ == "__main__":
    write_artifacts()
