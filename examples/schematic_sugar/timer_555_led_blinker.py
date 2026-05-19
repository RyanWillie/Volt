"""Volt-native schematic sugar example for a generic 555 LED blinker."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import volt


@dataclass(frozen=True)
class ExampleArtifacts:
    logical_json: Path
    schematic_json: Path
    schematic_svg: Path


def _timer_symbol() -> volt.SchematicSymbolSpec:
    return volt.SchematicSymbolSpec.ic(
        "examples.schematic_sugar:NE555",
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
        center_label="555",
        bottom_label="timer",
    )


def build_design() -> tuple[volt.Design, dict[str, volt.Net], dict[str, volt.Component]]:
    design = volt.Design("sugar-555-led-blinker")
    timer_definition = design.define_component(
        "NE555",
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
        "RA": design.R("100k", ref="RA"),
        "RB": design.R("47k", ref="RB"),
        "CT": design.C("1 uF", ref="CT"),
        "CCTRL": design.C("10 nF", ref="CCTRL"),
        "RLED": design.R("1k", ref="RLED"),
        "DLED": design.LED(ref="DLED"),
    }

    timer = parts["U1"]
    nets["+5V"] += timer["VCC"], timer["RESET"], parts["RA"][1]
    nets["DISCH"] += timer["DISCH"], parts["RA"][2], parts["RB"][1]
    nets["TIMING"] += timer["TRIG"], timer["THRESH"], parts["RB"][2], parts["CT"][1]
    nets["CTRL"] += timer["CTRL"], parts["CCTRL"][1]
    nets["OUT"] += timer["OUT"], parts["RLED"][1]
    nets["LED_A"] += parts["RLED"][2], parts["DLED"]["A"]
    nets["GND"] += (
        timer["GND"],
        parts["CT"][2],
        parts["CCTRL"][2],
        parts["DLED"]["K"],
    )
    return design, nets, parts


def author_schematic(
    design: volt.Design,
    nets: dict[str, volt.Net],
    parts: dict[str, volt.Component],
) -> volt.Schematic:
    sheet = design.schematic("555 LED Blinker")

    with sheet.drawing(at=(80, 40), unit=20) as drawing:
        timer = (
            drawing.place(parts["U1"])
            .label_ref(loc="top", ofst=14)
            .label_value(loc="bottom", ofst=20)
        )

        with drawing.hold():
            drawing.move_from(timer.DISCH.left(36).up(22))
            ra = drawing.R(parts["RA"]).down().label_ref(loc="left").label_value(loc="right")
            rb = drawing.R(parts["RB"]).at(timer.DISCH.left(36)).down().label_value(loc="left")
            timing_cap = (
                drawing.C(parts["CT"])
                .at(rb.end)
                .down()
                .label_ref(loc="left")
                .label_value(loc="right")
            )

        with drawing.hold():
            control_cap = (
                drawing.C(parts["CCTRL"])
                .at(timer.CTRL.right(24))
                .down()
                .label_ref(loc="right")
                .label_value(loc="right", ofst=18)
            )

        with drawing.hold():
            led_resistor = (
                drawing.R(parts["RLED"]).at(timer.OUT.right(24)).right().label_value(loc="top")
            )
            led = (
                drawing.LED(parts["DLED"])
                .at(led_resistor.end.right(6))
                .right()
                .reverse()
                .label_ref(loc="top")
            )

        vcc = drawing.power("+5V", net=nets["+5V"], at=timer.VCC.up(18), orient="Up")
        ground = drawing.ground("GND", net=nets["GND"], at=timer.GND.down(22), orient="Down")

        drawing.ortho_lines(
            (
                (nets["+5V"], timer.VCC, vcc),
                (nets["+5V"], timer.RESET, vcc),
                (nets["+5V"], ra.start, vcc),
                (nets["DISCH"], timer.DISCH, rb.start),
                (nets["DISCH"], ra.end, rb.start),
                (nets["TIMING"], timer.THRESH, timing_cap.start),
                (nets["TIMING"], timer.TRIG, timing_cap.start),
                (nets["CTRL"], timer.CTRL, control_cap.start),
                (nets["OUT"], timer.OUT, led_resistor.start),
                (nets["LED_A"], led_resistor.end, led.start),
                (nets["GND"], timer.GND, ground),
                (nets["GND"], timing_cap.end, ground),
                (nets["GND"], control_cap.end, ground),
                (nets["GND"], led.end, ground),
            ),
            shape="-|",
            k=-18,
        )
        drawing.local_label(nets["TIMING"], at=timing_cap.start, side="Left", offset=4)
        drawing.local_label(nets["OUT"], at=led_resistor.start, side="Up", offset=5)

    return sheet


def build_example() -> tuple[volt.Design, volt.Schematic]:
    design, nets, parts = build_design()
    schematic = author_schematic(design, nets, parts)
    return design, schematic


def write_artifacts(output_dir: Path | str | None = None) -> ExampleArtifacts:
    if output_dir is None:
        output_dir = Path(__file__).resolve().parent / "artifacts"
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    design, schematic = build_example()
    report = schematic.validate()
    if report.has_errors:
        codes = ", ".join(diagnostic.code for diagnostic in report)
        raise RuntimeError(f"555 LED blinker schematic readiness failed: {codes}")

    logical_json = output_path / "timer_555_led_blinker.volt.json"
    schematic_json = output_path / "timer_555_led_blinker.volt.schematic.json"
    schematic_svg = output_path / "timer_555_led_blinker.svg"

    design.write(logical_json)
    schematic.write_json(schematic_json)
    schematic.write_svg(schematic_svg)
    return ExampleArtifacts(logical_json, schematic_json, schematic_svg)


if __name__ == "__main__":
    write_artifacts()
