"""Medium Volt-native schematic sugar example for a regulator support fragment."""

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
    design = volt.Design("sugar-regulator-fragment")

    nets = {
        "+5V": design.net("+5V", kind="power", voltage=5.0),
        "+3V3": design.net("+3V3", kind="power", voltage=3.3),
        "GND": design.net("GND", kind="ground"),
        "NRST": design.net("NRST").mark_stub(),
        "SWDIO": design.net("SWDIO").mark_stub(),
        "SWCLK": design.net("SWCLK").mark_stub(),
    }
    parts = {
        "U1": design.regulator(ref="U1"),
        "CIN": design.C("10 uF", ref="CIN"),
        "COUT": design.C("10 uF", ref="COUT"),
        "RRESET": design.R("10k", ref="RRESET"),
        "TP1": design.test_point(ref="TP1"),
        "TP2": design.test_point(ref="TP2"),
        "J1": design.connector_1x03(ref="J1"),
    }

    nets["+5V"] += parts["U1"]["IN"], parts["CIN"][1]
    nets["+3V3"] += parts["U1"]["OUT"], parts["COUT"][1], parts["RRESET"][1]
    nets["GND"] += parts["U1"]["GND"], parts["CIN"][2], parts["COUT"][2]
    nets["NRST"] += parts["RRESET"][2], parts["TP1"]["TP"], parts["J1"][1]
    nets["SWDIO"] += parts["J1"][2]
    nets["SWCLK"] += parts["J1"][3]
    parts["TP2"]["TP"].mark_no_connect()
    return design, nets, parts


def author_schematic(
    design: volt.Design,
    nets: dict[str, volt.Net],
    parts: dict[str, volt.Component],
) -> volt.Schematic:
    sheet = design.schematic("Regulator and SWD")

    with sheet.drawing(unit=20) as drawing:
        regulator = (
            drawing.place(parts["U1"], at=(80, 30))
            .label_ref(loc="top")
            .label("3.3 V regulator", loc="bottom", ofst=18)
        )
        input_cap = (
            drawing.C(parts["CIN"])
            .at(regulator.IN.left(40))
            .down()
            .label_ref(loc="left")
            .label_value(loc="right")
        )
        output_cap = (
            drawing.C(parts["COUT"])
            .at(regulator.OUT.right(25))
            .down()
            .label_ref(loc="left")
            .label_value(loc="right")
        )
        reset_pullup = (
            drawing.R(parts["RRESET"])
            .at(output_cap.start.right(55))
            .down()
            .label_ref(loc="right")
            .label_value(loc="right", ofst=18)
        )

        drawing.power("+5V", at=regulator.IN)
        drawing.power("+3V3", at=regulator.OUT)
        drawing.net_label(nets["+3V3"], at=output_cap.start.up(12))
        drawing.connect(input_cap.start, regulator.IN, shape="-")
        drawing.connect(regulator.OUT, output_cap.start, shape="-")
        drawing.connect(output_cap.start, reset_pullup.start, shape="-")
        drawing.connect(input_cap.end, regulator.GND, net=nets["GND"], shape="-")
        drawing.connect(regulator.GND, output_cap.end, net=nets["GND"], shape="-")
        ground = drawing.ground(net=nets["GND"], at=regulator.GND.down(24))
        drawing.connect(regulator.GND, ground, net=nets["GND"], shape="-")

        reset_tp = drawing.place(parts["TP1"], at=reset_pullup.end.right(28)).label_ref(
            loc="bottom"
        )
        header = drawing.place(
            parts["J1"],
            at=reset_tp.TP.right(20).down(20),
            orient="Right",
        ).label_ref(loc="top")
        drawing.connect(reset_pullup.end, reset_tp.TP, shape="-")
        drawing.connect(reset_tp.TP, header[1], shape="-|", k=24)

        drawing.signal_stub(nets["NRST"], at=header[1], side="Right", length=18)
        drawing.signal_stub(nets["SWDIO"], at=header[2], side="Right", length=18)
        drawing.signal_stub(nets["SWCLK"], at=header[3], side="Right", length=18)

        unused_pad = drawing.place(parts["TP2"], at=header[3].down(34)).label_ref(
            loc="bottom"
        )
        drawing.no_connect(unused_pad.TP, reason="reserved pad not populated")

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
        raise RuntimeError(f"regulator fragment schematic readiness failed: {codes}")

    logical_json = output_path / "regulator_fragment.volt.json"
    schematic_json = output_path / "regulator_fragment.volt.schematic.json"
    schematic_svg = output_path / "regulator_fragment.svg"

    design.write(logical_json)
    schematic.write_json(schematic_json)
    schematic.write_svg(schematic_svg)
    return ExampleArtifacts(logical_json, schematic_json, schematic_svg)


if __name__ == "__main__":
    write_artifacts()
