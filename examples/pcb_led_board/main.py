"""Generate the Volt-native first-board LED PCB example artifacts."""

from __future__ import annotations

from pathlib import Path

import volt

EXAMPLE_SLUG = "pcb_led_board"
SHEET_FILE = "examples/pcb_led_board/main.py"
MODEL_DIR = Path(__file__).resolve().parent / "models"


def _require_clean(result: volt.ProjectResult) -> None:
    diagnostics = [
        f"{diagnostic.source}:{diagnostic.code}"
        for diagnostic in result.diagnostics
    ]
    failures = [
        f"{failure.stage}:{failure.name}"
        for failure in result.test_failures()
    ]
    if diagnostics or failures:
        details = ", ".join((*diagnostics, *failures))
        raise RuntimeError("PCB LED board example validation failed: " + details)


def _rectangle(width: float, height: float) -> tuple[tuple[float, float], ...]:
    return (
        (-width / 2.0, -height / 2.0),
        (width / 2.0, -height / 2.0),
        (width / 2.0, height / 2.0),
        (-width / 2.0, height / 2.0),
    )


def _offset_rectangle(
    x: float, y: float, width: float, height: float
) -> tuple[tuple[float, float], ...]:
    return tuple((px + x, py + y) for px, py in _rectangle(width, height))


def _passive_0603(ref: tuple[str, str], *, polarity: bool = False) -> volt.FootprintDefinition:
    markings = ()
    if polarity:
        markings = (volt.FootprintMarking.polarity(_offset_rectangle(0.55, 0.0, 0.12, 0.60)),)
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
        courtyard=_rectangle(2.50, 1.40),
        body=_rectangle(1.60, 0.80),
        fabrication_outline=_rectangle(1.60, 0.80),
        assembly_outline=_rectangle(1.60, 0.80),
        markings=markings,
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
        model_3d=volt.PartModel3D(
            MODEL_DIR / "r_0603_body.step",
            offset=(0.0, 0.0, 0.35),
        ),
    )
    parts["D1"].select_part(
        manufacturer="Lite-On",
        part_number="LTST-C190KRKT",
        package="0603",
        footprint=_passive_0603(("leds", "LED_0603_1608Metric"), polarity=True),
        pin_pads={"A": "1", "K": "2"},
    )
    for part in parts.values():
        part.dnp(False)
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
    board = design.add_board("First Board LED")
    front = board.add_layer("F.Cu", role="copper", side="top")
    back = board.add_layer("B.Cu", role="copper", side="bottom")
    board.set_layer_stack((front, back), thickness=1.6)
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(32.0, 18.0))
    board.add(volt.Hole(center=(3.0, 3.0), diameter=2.7, role="mounting", label="MH1"))
    board.add(volt.Hole(center=(29.0, 15.0), diameter=2.7, role="mounting", label="MH2"))

    with board.layout(unit=1.0) as layout:
        header = layout.place(
            parts["J1"],
            at=board.edge("left").center().right(5.0),
            orient="right",
            locked=True,
        )
        resistor = layout.two_pad(parts["R1"]).at((15.0, 7.0)).anchor("center").right()
        led = layout.two_pad(parts["D1"]).at(resistor.center.right(9.0)).anchor("center").left()

        layout.connect(header[1], resistor[1], layer=front, width=0.25, mode="direct")
        layout.connect(
            resistor[2],
            led.A,
            layer=front,
            width=0.25,
            through=(layout.node((20.0, 3.0)),),
            mode="direct",
        )
        layout.connect(header[2], led.K, layer=front, width=0.25, mode="direct")
    return board


def build_example() -> tuple[volt.Design, volt.Schematic, volt.Board]:
    result = run_project()
    return result.design(), result.schematic(), result.board()


def build_project() -> volt.Project:
    project = volt.Project("pcb-led-board", description="First-board LED PCB example")

    @project.design
    def design():
        project_design, nets, parts = build_design()
        return (
            project_design,
            volt.ProjectResource("nets", nets),
            volt.ProjectResource("parts", parts),
        )

    @project.schematic
    def schematic(context: volt.BuildContext) -> volt.Schematic:
        return author_schematic(
            context.design(),
            context.resource("nets", dict),
            context.resource("parts", dict),
        )

    @project.board
    def board(context: volt.BuildContext) -> volt.Board:
        return build_board(
            context.design(),
            context.resource("nets", dict),
            context.resource("parts", dict),
        )

    @project.design.test
    def led_path_is_connected(check) -> None:
        check.net("+3V3").connects("J1.1", "R1.1")
        check.net("LED_A").connects("R1.2", "D1.A")
        check.net("GND").connects("D1.K", "J1.2")
        check.no_connection("+3V3", "GND")

    @project.schematic.test
    def schematic_places_design_parts(check) -> None:
        check.places("J1", "R1", "D1")

    @project.board.test
    def board_places_design_parts(check) -> None:
        check.has_outline()
        check.places("J1", "R1", "D1")

    return project


def run_project() -> volt.ProjectResult:
    return build_project().run()


def write_artifacts(output_dir: Path | str | None = None) -> volt.ProjectArtifactPaths:
    if output_dir is None:
        output_dir = Path(__file__).resolve().parent / "artifacts"
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    result = run_project()
    _require_clean(result)

    project_bundle = output_path / f"{EXAMPLE_SLUG}.volt"
    result.write(project_bundle)
    kicad_export = result.board().to_kicad_pcb()
    if kicad_export.warnings:
        raise RuntimeError(
            "PCB LED board KiCad export reported loss: "
            + ", ".join(warning.construct for warning in kicad_export.warnings)
        )
    artifacts = result.write_artifacts(output_path, slug=EXAMPLE_SLUG)
    return artifacts


if __name__ == "__main__":
    write_artifacts()
