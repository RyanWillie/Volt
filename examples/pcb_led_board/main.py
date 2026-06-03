"""Generate the Volt-native first-board LED PCB example artifacts."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

import volt

EXAMPLE_SLUG = "pcb_led_board"
SHEET_FILE = "examples/pcb_led_board/main.py"


@dataclass(frozen=True)
class ExampleArtifacts:
    logical_json: Path
    schematic_json: Path
    schematic_svg: Path
    schematic_body_svg: Path
    schematic_svg_pages: tuple[Path, ...]
    pcb_json: Path
    pcb_svg: Path
    kicad_pcb: Path
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
        raise RuntimeError("PCB LED board example validation failed: " + ", ".join(diagnostics))


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


def build_design() -> tuple[volt.Design, dict[str, volt.Net], dict[str, volt.Component]]:
    design = volt.Design("pcb-led-board")

    nets = {
        "+3V3": design.net("+3V3", kind="power", voltage=3.3),
        "LED_A": design.net("LED_A"),
        "GND": design.net("GND", kind="ground"),
    }
    parts = {
        "J1": design.connector_1x02(ref="J1"),
        "R1": design.R("330 ohm", ref="R1", resistance=330, tolerance=0.01),
        "D1": design.LED(ref="D1"),
    }

    nets["+3V3"] += parts["J1"][1], parts["R1"][1]
    nets["LED_A"] += parts["R1"][2], parts["D1"]["A"]
    nets["GND"] += parts["D1"]["K"], parts["J1"][2]

    parts["J1"].select_part(
        manufacturer="Generic",
        part_number="HDR-1x02-2.54mm",
        package="2.54mm-1x02",
        footprint=_header_1x02(),
        pin_pads={1: "1", 2: "2"},
    )
    parts["R1"].select_part(
        manufacturer="Yageo",
        part_number="RC0603FR-07330RL",
        package="0603",
        footprint=_passive_0603(("passives", "R_0603_1608Metric")),
        pin_pads={1: "1", 2: "2"},
        power_rating=0.1,
    )
    parts["D1"].select_part(
        manufacturer="Lite-On",
        part_number="LTST-C190KRKT",
        package="0603",
        footprint=_passive_0603(("leds", "LED_0603_1608Metric")),
        pin_pads={"A": "1", "K": "2"},
    )
    return design, nets, parts


def author_schematic(
    design: volt.Design,
    nets: dict[str, volt.Net],
    parts: dict[str, volt.Component],
) -> volt.Schematic:
    sheet = design.schematic(
        "First Board LED",
        size=(220, 150),
        orientation="landscape",
        title="First Board LED Reference Schematic",
        number=1,
        page_count=1,
        revision="A",
        date="2026-05-30",
        project="Volt PCB LED Board",
        file=SHEET_FILE,
        margins=(8, 8, 8, 8),
        coordinate_zones=(4, 3),
        grid={"spacing": 5, "visible": False},
    )

    with sheet.drawing(unit=20) as drawing:
        header = drawing.place(parts["J1"], at=(45, 60), orient="Right").label_ref(loc="left")
        resistor = (
            drawing.two_terminal(parts["R1"])
            .at(header[1].right(35))
            .right()
            .label_ref(loc="top", offset=6)
            .label_value(loc="bottom", offset=10)
        )
        led = (
            drawing.two_terminal(parts["D1"])
            .at(resistor.end.right(18))
            .right()
            .reverse()
            .label_ref(loc="top", offset=8)
        )
        supply = drawing.power("+3V3", net=nets["+3V3"], at=header[1].up(18))
        header_ground = drawing.ground("GND", net=nets["GND"], at=header[2].down(18))
        led_ground = drawing.ground("GND", net=nets["GND"], at=led.end.down(24))

        drawing.connect(supply.pin, header[1], net=nets["+3V3"], shape="-")
        drawing.connect(header[1], resistor.start, net=nets["+3V3"], shape="-").dot()
        drawing.connect(resistor.end, led.start, net=nets["LED_A"], shape="-")
        drawing.connect(header[2], header_ground.pin, net=nets["GND"], shape="-")
        drawing.connect(led.end, led_ground.pin, net=nets["GND"], shape="-")
        drawing.net_label(nets["LED_A"], at=resistor.end.up(12))

    return sheet


def build_board(
    design: volt.Design,
    nets: dict[str, volt.Net],
    parts: dict[str, volt.Component],
) -> volt.Board:
    board = design.board("First Board LED")
    front = board.add_layer("F.Cu", role="copper", side="top")
    back = board.add_layer("B.Cu", role="copper", side="bottom")
    board.set_layer_stack((front, back), thickness=1.6)
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(32.0, 18.0))
    board.add_mounting_hole("MH1", at=(3.0, 3.0), diameter=2.7)
    board.add_mounting_hole("MH2", at=(29.0, 15.0), diameter=2.7)

    board.place(parts["J1"], at=(5.0, 9.0), rotation=0.0, side="top", locked=True)
    board.place(parts["R1"], at=(15.0, 7.0), rotation=0.0, side="top")
    board.place(parts["D1"], at=(24.0, 7.0), rotation=180.0, side="top")
    board.add_track(nets["+3V3"], layer=front, points=[(5.0, 7.73), (14.25, 7.0)], width=0.25)
    board.add_track(
        nets["LED_A"],
        layer=front,
        points=[(15.75, 7.0), (20.0, 3.0), (24.75, 7.0)],
        width=0.25,
    )
    board.add_track(nets["GND"], layer=front, points=[(5.0, 10.27), (23.25, 7.0)], width=0.25)
    return board


def build_example() -> tuple[volt.Design, volt.Schematic, volt.Board]:
    design, nets, parts = build_design()
    schematic = author_schematic(design, nets, parts)
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
    kicad_pcb = output_path / f"{EXAMPLE_SLUG}.kicad_pcb"
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
    kicad_export = board.write_kicad_pcb(kicad_pcb)
    if kicad_export.warnings:
        raise RuntimeError(
            "PCB LED board KiCad export reported loss: "
            + ", ".join(warning.construct for warning in kicad_export.warnings)
        )
    validation_report.write_text(validation_report_json(reports), encoding="utf-8")
    return ExampleArtifacts(
        logical_json=logical_json,
        schematic_json=schematic_json,
        schematic_svg=schematic_svg,
        schematic_body_svg=schematic_body_svg,
        schematic_svg_pages=schematic_svg_pages,
        pcb_json=pcb_json,
        pcb_svg=pcb_svg,
        kicad_pcb=kicad_pcb,
        validation_report=validation_report,
    )


if __name__ == "__main__":
    write_artifacts()
