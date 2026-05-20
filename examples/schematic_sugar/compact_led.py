"""Compact Volt-native SchemDraw-style LED schematic example."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import volt


@dataclass(frozen=True)
class ExampleArtifacts:
    logical_json: Path
    schematic_json: Path
    schematic_svg: Path


def build_design() -> tuple[volt.Design, dict[str, volt.Net], dict[str, volt.Component]]:
    design = volt.Design("sugar-led")

    nets = {
        "+3V3": design.net("+3V3", kind="power", voltage=3.3),
        "LED_A": design.net("LED_A"),
        "GND": design.net("GND", kind="ground"),
    }
    parts = {
        "R1": design.R("330 ohm", ref="R1"),
        "D1": design.LED(ref="D1"),
    }

    nets["+3V3"] += parts["R1"][1]
    nets["LED_A"] += parts["R1"][2], parts["D1"]["A"]
    nets["GND"] += parts["D1"]["K"]
    return design, nets, parts


def author_schematic(
    design: volt.Design,
    nets: dict[str, volt.Net],
    parts: dict[str, volt.Component],
) -> volt.Schematic:
    sheet = design.schematic("Main")

    with sheet.drawing(at=(20, 20), unit=20) as drawing:
        resistor = drawing.two_terminal(parts["R1"]).right().label_ref().label_value()
        led = drawing.two_terminal(parts["D1"]).right().reverse().label_ref()

        drawing.connect(resistor.end, led.start)
        drawing.power("+3V3", at=resistor.start)
        drawing.ground(at=led.end)
        drawing.net_label(nets["LED_A"], at=resistor.end.up(10))

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
        raise RuntimeError(f"compact LED schematic readiness failed: {codes}")

    logical_json = output_path / "compact_led.volt.json"
    schematic_json = output_path / "compact_led.volt.schematic.json"
    schematic_svg = output_path / "compact_led.svg"

    design.write(logical_json)
    schematic.write_json(schematic_json)
    schematic.write_svg(schematic_svg)
    return ExampleArtifacts(logical_json, schematic_json, schematic_svg)


if __name__ == "__main__":
    write_artifacts()
