"""Generate the Volt-native 555 LED blinker example artifacts."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

import volt

SHEET_FILE = "examples/timer_555_led_blinker/main.py"


@dataclass(frozen=True)
class ExampleArtifacts:
    logical_json: Path
    schematic_json: Path
    schematic_svg: Path
    schematic_svg_pages: tuple[Path, ...]
    validation_report: Path


def validation_report_json(report: volt.DiagnosticReport) -> str:
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
    return json.dumps(
        {"summary": counts, "diagnostics": diagnostics},
        indent=2,
        sort_keys=True,
    ) + "\n"


def _timer_symbol() -> volt.SchematicSymbolSpec:
    return volt.SchematicSymbolSpec.ic(
        "volt.examples.timer_555_led_blinker:NE555",
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
        bottom_label="timer",
    )


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
        "U1": design.instantiate(timer_definition, ref="U1", properties={"value": "NE555"}),
        "RA": design.R("100 kOhm", ref="R1"),
        "RB": design.R("47 kOhm", ref="R2"),
        "CT": design.C("1 uF", ref="C1"),
        "CCTRL": design.C("10 nF", ref="C2"),
        "RLED": design.R("1 kOhm", ref="R3"),
        "DLED": design.LED(ref="D1"),
    }

    timer = parts["U1"]
    nets["+5V"] += timer["VCC"], timer["RESET"], parts["RA"][1]
    nets["DISCH"] += timer["DISCH"], parts["RA"][2], parts["RB"][1]
    nets["TIMING"] += timer["TRIG"], timer["THRESH"], parts["RB"][2], parts["CT"][1]
    nets["CTRL"] += timer["CTRL"], parts["CCTRL"][1]
    nets["OUT"] += timer["OUT"], parts["RLED"][1]
    nets["LED_A"] += parts["RLED"][2], parts["DLED"]["A"]
    nets["GND"] += timer["GND"], parts["CT"][2], parts["CCTRL"][2], parts["DLED"]["K"]
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
        timer = drawing.place(parts["U1"]).label_ref().label_value()

        disch_node = drawing.node(timer.DISCH.left(48))
        timing_node = drawing.node(timer.TRIG.left(48))
        timing_vcc = drawing.node(disch_node.up(28))
        timing_ground = drawing.node(timing_node.down(34))
        timing_vcc_tag = drawing.node(timing_vcc.up(18))
        timing_ground_tag = drawing.node(timing_ground.down(18))
        control_node = drawing.node(timer.CTRL.right(40))
        control_ground = drawing.node(control_node.down(38))
        control_ground_tag = drawing.node(control_ground.down(18))
        output_node = drawing.node(timer.OUT.right(34))
        led_resistor_end = drawing.node(output_node.right(20))
        led_anode = drawing.node(led_resistor_end.right(5))
        led_cathode = drawing.node(led_anode.right(30))
        timer_vcc = drawing.node(timer.VCC.up(24))
        reset_vcc = drawing.node(timer.RESET.up(24))
        timer_ground = drawing.node(timer.GND.down(24))
        led_ground = drawing.node(led_cathode.down(18))

        ra = (
            drawing.two_terminal(parts["RA"])
            .between(timing_vcc, disch_node)
            .label_ref()
            .label_value()
        )
        rb = (
            drawing.two_terminal(parts["RB"])
            .between(disch_node, timing_node)
            .label_ref()
            .label_value()
        )
        timing_cap = (
            drawing.two_terminal(parts["CT"])
            .between(timing_node, timing_ground)
            .label_ref()
            .label_value()
        )
        control_cap = (
            drawing.two_terminal(parts["CCTRL"])
            .between(control_node, control_ground)
            .label_ref()
            .label_value()
        )
        led_resistor = (
            drawing.two_terminal(parts["RLED"])
            .between(output_node, led_resistor_end)
            .label_ref()
            .label_value(loc="top")
        )
        led = (
            drawing.two_terminal(parts["DLED"])
            .between(led_cathode, led_anode)
            .label_ref()
        )

        drawing.connect(nets["+5V"], timer.VCC, timer_vcc)
        drawing.connect(nets["+5V"], timer.RESET, reset_vcc)
        drawing.connect(nets["+5V"], ra.start, timing_vcc_tag)
        drawing.connect(nets["DISCH"], timer.DISCH, disch_node)
        drawing.connect(nets["TIMING"], timer.THRESH, timer.TRIG)
        drawing.connect(nets["TIMING"], timer.TRIG, timing_node)
        drawing.connect(nets["CTRL"], timer.CTRL, control_node)
        drawing.connect(nets["OUT"], timer.OUT, output_node)
        drawing.connect(nets["LED_A"], led_resistor.end, led.end)
        drawing.connect(nets["GND"], timer.GND, timer_ground)
        drawing.connect(nets["GND"], timing_cap.end, timing_ground_tag)
        drawing.connect(nets["GND"], control_cap.end, control_ground_tag)
        drawing.connect(nets["GND"], led.start, led_ground)
        drawing.junction(nets["DISCH"], at=disch_node)
        drawing.junction(nets["TIMING"], at=timing_node)
        drawing.local_label(
            nets["TIMING"], at=timing_cap.start.left(18), side="Left", offset=10, orient="Right"
        )
        drawing.local_label(
            nets["OUT"], at=led_resistor.start.up(16), side="Up", offset=10, orient="Right"
        )
        drawing.local_label(nets["+5V"], at=timer_vcc.up(6), side="Up", offset=6, orient="Right")
        drawing.local_label(
            nets["+5V"], at=timing_vcc_tag.up(6), side="Up", offset=6, orient="Right"
        )
        drawing.local_label(
            nets["GND"], at=timer_ground.down(6), side="Down", offset=8, orient="Right"
        )
        drawing.local_label(
            nets["GND"], at=timing_ground_tag.down(6), side="Down", offset=8, orient="Right"
        )
        drawing.local_label(
            nets["GND"], at=control_ground_tag.down(6), side="Down", offset=8, orient="Right"
        )
        drawing.local_label(nets["GND"], at=led_ground.down(6), side="Down", offset=8, orient="Right")

    return sheet


def build_example() -> tuple[volt.Design, volt.Schematic]:
    design, nets, parts = build_design()
    schematic = build_schematic(design, nets, parts)
    return design, schematic


def require_schematic_ready(schematic: volt.Schematic) -> None:
    readiness = schematic.validate()
    readability = schematic.validate_readability()
    failing_codes = [
        diagnostic.code
        for report in (readiness, readability)
        for diagnostic in report
        if diagnostic.severity == "error"
    ]
    if failing_codes:
        raise RuntimeError(
            "555 LED blinker schematic validation failed: " + ", ".join(failing_codes)
        )


def write_artifacts(output_dir: Path | str | None = None) -> ExampleArtifacts:
    if output_dir is None:
        output_dir = Path(__file__).resolve().parent / "artifacts"
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    design, schematic = build_example()
    logical_json = output_path / "timer_555_led_blinker.volt.json"
    schematic_json = output_path / "timer_555_led_blinker.volt.schematic.json"
    schematic_svg = output_path / "timer_555_led_blinker.svg"
    schematic_svg_pages_dir = output_path / "timer_555_led_blinker.pages"
    validation_report = output_path / "timer_555_led_blinker.validation.json"

    require_schematic_ready(schematic)
    if schematic_svg_pages_dir.exists():
        for page_path in schematic_svg_pages_dir.glob("*.svg"):
            page_path.unlink()
    design.write(logical_json)
    schematic.write_json(schematic_json)
    schematic.write_svg(schematic_svg)
    schematic_svg_pages = schematic.write_svg_pages(
        schematic_svg_pages_dir,
        prefix="timer_555_led_blinker",
    )
    validation_report.write_text(validation_report_json(design.validate()), encoding="utf-8")
    return ExampleArtifacts(
        logical_json=logical_json,
        schematic_json=schematic_json,
        schematic_svg=schematic_svg,
        schematic_svg_pages=schematic_svg_pages,
        validation_report=validation_report,
    )


if __name__ == "__main__":
    write_artifacts()
