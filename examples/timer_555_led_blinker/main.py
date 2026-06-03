"""Generate the Volt-native 555 LED blinker example artifacts."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

import volt

EXAMPLE_SLUG = "timer_555_led_blinker"
SHEET_FILE = "timer_555_led_blinker/main.py"


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


def validation_report_json(
    reports: dict[str, volt.DiagnosticReport],
) -> str:
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
        raise RuntimeError("555 LED blinker validation failed: " + ", ".join(diagnostics))


def _timer_symbol() -> volt.SchematicSymbolSpec:
    return volt.SchematicSymbolSpec.ic(
        "volt.examples.timer_555_led_blinker:NE555",
        pins=(
            volt.SchematicSymbolSpec.ic_pin("DISCH", 7, side="left", slot=1, label="DIS"),
            volt.SchematicSymbolSpec.ic_pin("THRESH", 6, side="left", slot=2, label="THR"),
            volt.SchematicSymbolSpec.ic_pin("TRIG", 2, side="left", slot=3, label="TRG"),
            volt.SchematicSymbolSpec.ic_pin("OUT", 3, side="right", slot=2),
            volt.SchematicSymbolSpec.ic_pin("CTRL", 5, side="right", slot=3, label="CTL"),
            volt.SchematicSymbolSpec.ic_pin("RESET", 4, side="top", slot=2, label="RST"),
            volt.SchematicSymbolSpec.ic_pin("VCC", 8, side="top", slot=4, label="Vcc"),
            volt.SchematicSymbolSpec.ic_pin("GND", 1, side="bottom", slot=3),
        ),
        center_label="555",
        pin_numbers=True,
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


def _timer_dip_8() -> volt.FootprintDefinition:
    return volt.FootprintDefinition(
        ("packages", "DIP-8_W7.62mm"),
        pads=(
            volt.FootprintPad.through_hole(
                "1", at=(-3.81, 3.81), size=(1.60, 1.60), drill=volt.FootprintDrill(0.80)
            ),
            volt.FootprintPad.through_hole(
                "2", at=(-3.81, 1.27), size=(1.60, 1.60), drill=volt.FootprintDrill(0.80)
            ),
            volt.FootprintPad.through_hole(
                "3", at=(-3.81, -1.27), size=(1.60, 1.60), drill=volt.FootprintDrill(0.80)
            ),
            volt.FootprintPad.through_hole(
                "4", at=(-3.81, -3.81), size=(1.60, 1.60), drill=volt.FootprintDrill(0.80)
            ),
            volt.FootprintPad.through_hole(
                "5", at=(3.81, -3.81), size=(1.60, 1.60), drill=volt.FootprintDrill(0.80)
            ),
            volt.FootprintPad.through_hole(
                "6", at=(3.81, -1.27), size=(1.60, 1.60), drill=volt.FootprintDrill(0.80)
            ),
            volt.FootprintPad.through_hole(
                "7", at=(3.81, 1.27), size=(1.60, 1.60), drill=volt.FootprintDrill(0.80)
            ),
            volt.FootprintPad.through_hole(
                "8", at=(3.81, 3.81), size=(1.60, 1.60), drill=volt.FootprintDrill(0.80)
            ),
        ),
    )


def _axial_resistor() -> volt.FootprintDefinition:
    return volt.FootprintDefinition(
        ("passives", "R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal"),
        pads=(
            volt.FootprintPad.through_hole(
                "1", at=(-3.81, 0.0), size=(1.60, 1.60), drill=volt.FootprintDrill(0.80)
            ),
            volt.FootprintPad.through_hole(
                "2", at=(3.81, 0.0), size=(1.60, 1.60), drill=volt.FootprintDrill(0.80)
            ),
        ),
    )


def _radial_capacitor() -> volt.FootprintDefinition:
    return volt.FootprintDefinition(
        ("passives", "C_Radial_D5.0mm_P2.54mm"),
        pads=(
            volt.FootprintPad.through_hole(
                "1", at=(-1.27, 0.0), size=(1.50, 1.50), drill=volt.FootprintDrill(0.70)
            ),
            volt.FootprintPad.through_hole(
                "2", at=(1.27, 0.0), size=(1.50, 1.50), drill=volt.FootprintDrill(0.70)
            ),
        ),
    )


def _led_5mm() -> volt.FootprintDefinition:
    return volt.FootprintDefinition(
        ("leds", "LED_D5.0mm"),
        pads=(
            volt.FootprintPad.through_hole(
                "1", at=(-1.27, 0.0), size=(1.50, 1.50), drill=volt.FootprintDrill(0.70)
            ),
            volt.FootprintPad.through_hole(
                "2", at=(1.27, 0.0), size=(1.50, 1.50), drill=volt.FootprintDrill(0.70)
            ),
        ),
    )


FOOTPRINTS = {
    "header_1x02": _header_1x02(),
    "timer_dip_8": _timer_dip_8(),
    "resistor_axial": _axial_resistor(),
    "capacitor_radial": _radial_capacitor(),
    "led_5mm": _led_5mm(),
}


def build_design() -> tuple[volt.Design, dict[str, volt.Net], dict[str, volt.Component]]:
    design = volt.Design("timer-555-led-blinker")
    timer_definition = design.define_component(
        "NE555",
        source=("volt.examples.timer_555_led_blinker", "ne555", "1.0.0"),
        pins=[
            volt.PinSpec("GND", 1, role="ground", terminal="ground"),
            volt.PinSpec("TRIG", 2, role="input", signal="analog"),
            volt.PinSpec("OUT", 3, role="output", signal="digital"),
            volt.PinSpec("RESET", 4, role="input", signal="digital"),
            volt.PinSpec("CTRL", 5, role="input", signal="analog"),
            volt.PinSpec("THRESH", 6, role="input", signal="analog"),
            volt.PinSpec("DISCH", 7, role="output", signal="analog"),
            volt.PinSpec("VCC", 8, role="power", terminal="power"),
        ],
        schematic_symbol=_timer_symbol(),
    )

    nets = {
        "+5V": design.net("+5V", kind="power", voltage=5.0),
        "GND": design.net("GND", kind="ground"),
        "DISCH": design.net("DISCH"),
        "TIMING": design.net("TIMING"),
        "CTRL": design.net("CTRL"),
        "OUT": design.net("OUT"),
        "LED_A": design.net("LED_A"),
    }
    parts = {
        "J1": design.connector_1x02(ref="J1"),
        "U1": design.instantiate(timer_definition, ref="U1", properties={"value": "NE555"}),
        "RA": design.R("100 kOhm", ref="R1"),
        "RB": design.R("47 kOhm", ref="R2"),
        "CT": design.C("1 uF", ref="C1"),
        "CCTRL": design.C("10 nF", ref="C2"),
        "RLED": design.R("1 kOhm", ref="R3"),
        "DLED": design.LED(ref="D1"),
    }

    timer = parts["U1"]
    nets["+5V"] += parts["J1"][1], timer["VCC"], timer["RESET"], parts["RA"][1]
    nets["DISCH"] += timer["DISCH"], parts["RA"][2], parts["RB"][1]
    nets["TIMING"] += timer["TRIG"], timer["THRESH"], parts["RB"][2], parts["CT"][1]
    nets["CTRL"] += timer["CTRL"], parts["CCTRL"][1]
    nets["OUT"] += timer["OUT"], parts["RLED"][1]
    nets["LED_A"] += parts["RLED"][2], parts["DLED"]["A"]
    nets["GND"] += (
        parts["J1"][2],
        timer["GND"],
        parts["CT"][2],
        parts["CCTRL"][2],
        parts["DLED"]["K"],
    )

    parts["J1"].select_part(
        manufacturer="Generic",
        part_number="HDR-1x02-2.54mm",
        package="2.54mm-1x02",
        footprint=FOOTPRINTS["header_1x02"],
        pin_pads={1: "1", 2: "2"},
    )
    timer.select_part(
        manufacturer="Texas Instruments",
        part_number="NE555P",
        package="PDIP-8",
        footprint=FOOTPRINTS["timer_dip_8"],
        pin_pads={
            "GND": "1",
            "TRIG": "2",
            "OUT": "3",
            "RESET": "4",
            "CTRL": "5",
            "THRESH": "6",
            "DISCH": "7",
            "VCC": "8",
        },
        voltage_rating=16.0,
    )
    for component, part_number, power_rating in (
        (parts["RA"], "RES-0.25W-100K", 0.25),
        (parts["RB"], "RES-0.25W-47K", 0.25),
        (parts["RLED"], "RES-0.25W-1K", 0.25),
    ):
        component.select_part(
            manufacturer="Generic",
            part_number=part_number,
            package="Axial-7.62mm",
            footprint=FOOTPRINTS["resistor_axial"],
            pin_pads={1: "1", 2: "2"},
            power_rating=power_rating,
        )
    for component, part_number, voltage_rating in (
        (parts["CT"], "CAP-RAD-1UF-2.54MM", 16.0),
        (parts["CCTRL"], "CAP-RAD-10NF-2.54MM", 50.0),
    ):
        component.select_part(
            manufacturer="Generic",
            part_number=part_number,
            package="Radial-2.54mm",
            footprint=FOOTPRINTS["capacitor_radial"],
            pin_pads={1: "1", 2: "2"},
            voltage_rating=voltage_rating,
        )
    parts["DLED"].select_part(
        manufacturer="Generic",
        part_number="LED-5MM-RED",
        package="5mm",
        footprint=FOOTPRINTS["led_5mm"],
        pin_pads={"A": "1", "K": "2"},
    )
    return design, nets, parts


def build_schematic(
    design: volt.Design,
    nets: dict[str, volt.Net],
    parts: dict[str, volt.Component],
) -> volt.Schematic:
    sheet = design.schematic(
        "555 LED Blinker",
        size=(340, 240),
        orientation="landscape",
        title="555 LED Blinker Reference Schematic",
        number=1,
        page_count=1,
        revision="A",
        date="2026-05-19",
        project="Volt 555 LED Blinker",
        file=SHEET_FILE,
        margins=(10, 8, 10, 8),
        coordinate_zones=(6, 4),
        grid={"spacing": 5, "visible": False},
    )

    with sheet.drawing(at=(140, 80), unit=20) as drawing:
        header = drawing.place(parts["J1"], at=(72, 84), orient="Right").label_ref(loc="left")
        timer = (
            drawing.place(parts["U1"])
            .label_ref(loc="top", offset=4)
            .label_value(loc="bottom", offset=14, align="end")
        )

        ground = drawing.ground("GND", at=timer.GND, orient="Down")
        drawing.power_stub("+5V", at=header[1], net=nets["+5V"], side="Up", length=20)
        drawing.ground_stub("GND", at=header[2], net=nets["GND"], side="Down", length=20)

        ra = (
            drawing.two_terminal(parts["RA"])
            .at(timer.DISCH.up(drawing.unit))
            .to(timer.DISCH)
            .label_ref(loc="left", offset=8)
            .label_value(loc="left", offset=24)
        )
        rb = (
            drawing.two_terminal(parts["RB"])
            .at(timer.DISCH)
            .toy(timer.THRESH)
            .label_ref(loc="left", offset=8)
            .label_value(loc="left", offset=22)
            .idot()
            .dot()
        )
        timing_cap = (
            drawing.two_terminal(parts["CT"])
            .at(timer.TRIG)
            .toy(ground.pin)
            .label_ref(loc="left", offset=8)
            .label_value(loc="left", offset=22)
            .idot()
            .dot()
        )
        control_cap = (
            drawing.two_terminal(parts["CCTRL"])
            .at(timer.CTRL)
            .toy(ground.pin)
            .label_ref(loc="right", offset=8)
            .label_value(loc="bottom", offset=8)
            .dot()
        )
        led_resistor = (
            drawing.two_terminal(parts["RLED"])
            .at(timer.OUT)
            .right()
            .label_value(loc="top", offset=8)
        )
        led = (
            drawing.two_terminal(parts["DLED"])
            .reverse()
            .toy(ground.pin)
            .label_ref(loc="right", offset=14)
        )

        drawing.power_stub("+5V", at=timer.VCC)
        drawing.connect(timer.RESET, timer.VCC, shape="-").dot()
        drawing.power_stub("+5V", at=ra.start)

        drawing.connect(timer.THRESH, timer.TRIG, shape="-")
        drawing.wire(nets["GND"]).at(timing_cap.end).tox(ground.pin).direct()
        drawing.wire(nets["GND"]).at(control_cap.end).tox(ground.pin).direct()
        drawing.wire(nets["GND"]).at(led.end).tox(ground.pin).direct()

        drawing.local_label(
            nets["TIMING"],
            at=timing_cap.start,
            side="Left",
            offset=34,
            orient="Right",
        )

    return sheet


def build_board(
    design: volt.Design,
    nets: dict[str, volt.Net],
    parts: dict[str, volt.Component],
) -> volt.Board:
    board = design.board("555 LED Blinker")
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
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(90.0, 56.0))
    board.add(volt.Hole(center=(4.0, 4.0), diameter=2.7, label="MH1", role="mounting"))
    board.add(volt.Hole(center=(86.0, 4.0), diameter=2.7, label="MH2", role="mounting"))
    board.add(volt.Hole(center=(4.0, 52.0), diameter=2.7, label="MH3", role="mounting"))
    board.add(volt.Hole(center=(86.0, 52.0), diameter=2.7, label="MH4", role="mounting"))

    board.place(parts["J1"], at=(8.0, 28.0), rotation=0.0, side="top", locked=True)
    board.place(parts["U1"], at=(42.0, 28.0), rotation=0.0, side="top", locked=True)
    board.place(parts["RA"], at=(58.0, 22.0), rotation=0.0, side="top")
    board.place(parts["RB"], at=(58.0, 30.0), rotation=0.0, side="top")
    board.place(parts["CT"], at=(68.0, 38.0), rotation=0.0, side="top")
    board.place(parts["CCTRL"], at=(58.0, 16.0), rotation=0.0, side="top")
    board.place(parts["RLED"], at=(28.0, 20.0), rotation=180.0, side="top")
    board.place(parts["DLED"], at=(17.0, 20.0), rotation=0.0, side="top")
    board.add(volt.Text("555 BLINK", at=(61.0, 51.0), layer=silk, size=1.4))

    board.add_track(nets["+5V"], layer=front, points=((8.0, 26.73), (8.0, 8.0), (12.0, 8.0)), width=0.35)
    board.add_via(nets["+5V"], at=(12.0, 8.0), start_layer=front, end_layer=back)
    board.add_track(
        nets["+5V"],
        layer=back,
        points=((12.0, 8.0), (54.19, 8.0), (54.19, 22.0)),
        width=0.35,
    )
    board.add_track(nets["+5V"], layer=back, points=((38.19, 24.19), (38.19, 8.0)), width=0.35)
    board.add_track(
        nets["+5V"],
        layer=back,
        points=((45.81, 31.81), (50.0, 31.81), (50.0, 8.0)),
        width=0.35,
    )

    board.add_track(nets["GND"], layer=front, points=((8.0, 29.27), (8.0, 48.0)), width=0.35)
    board.add_via(nets["GND"], at=(8.0, 48.0), start_layer=front, end_layer=back)
    board.add_track(
        nets["GND"],
        layer=back,
        points=((8.0, 48.0), (69.27, 48.0), (69.27, 38.0)),
        width=0.35,
    )
    board.add_track(nets["GND"], layer=back, points=((38.19, 31.81), (38.19, 48.0)), width=0.35)
    board.add_track(nets["GND"], layer=back, points=((18.27, 20.0), (18.27, 48.0)), width=0.35)
    board.add_track(
        nets["GND"],
        layer=back,
        points=((59.27, 16.0), (59.27, 48.0)),
        width=0.35,
    )

    board.add_track(
        nets["DISCH"],
        layer=front,
        points=((45.81, 29.27), (54.19, 30.0), (54.19, 25.0), (66.0, 25.0), (66.0, 22.0), (61.81, 22.0)),
        width=0.25,
    )
    board.add_track(
        nets["TIMING"],
        layer=front,
        points=((38.19, 29.27), (34.0, 29.27), (34.0, 38.0), (66.73, 38.0)),
        width=0.25,
    )
    board.add_track(nets["TIMING"], layer=front, points=((45.81, 26.73), (43.0, 26.73), (43.0, 38.0)), width=0.25)
    board.add_track(nets["TIMING"], layer=front, points=((61.81, 30.0), (61.81, 38.0)), width=0.25)

    board.add_track(nets["CTRL"], layer=front, points=((45.81, 24.19), (56.73, 16.0)), width=0.25)
    board.add_track(nets["OUT"], layer=front, points=((38.19, 26.73), (34.0, 26.73), (34.0, 20.0), (36.0, 20.0)), width=0.25)
    board.add_via(nets["OUT"], at=(36.0, 20.0), start_layer=front, end_layer=back)
    board.add_track(nets["OUT"], layer=back, points=((36.0, 20.0), (31.81, 20.0)), width=0.25)
    board.add_track(
        nets["LED_A"],
        layer=front,
        points=((24.19, 20.0), (24.19, 15.0), (15.73, 15.0), (15.73, 20.0)),
        width=0.25,
    )
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
    # Content-tight body SVG is for docs/previews; full sheet/page SVGs remain document artifacts.
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
