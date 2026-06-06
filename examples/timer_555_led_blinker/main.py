"""Generate the Volt-native 555 LED blinker example artifacts."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import volt

EXAMPLE_SLUG = "timer_555_led_blinker"
SHEET_FILE = "timer_555_led_blinker/main.py"


@dataclass(frozen=True)
class ExampleArtifacts:
    project_bundle: Path
    logical_json: Path
    schematic_json: Path
    schematic_svg: Path
    schematic_body_svg: Path
    schematic_svg_pages: tuple[Path, ...]
    pcb_json: Path
    pcb_svg: Path
    validation_report: Path


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
        raise RuntimeError("555 LED blinker validation failed: " + details)


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


def _front_smd_pad(
    label: str,
    *,
    at: tuple[float, float],
    size: tuple[float, float],
    shape: str = "rounded_rectangle",
    mechanical_role: str | None = None,
) -> volt.FootprintPad:
    return volt.FootprintPad.surface_mount(
        label,
        at=at,
        size=size,
        shape=shape,
        mechanical_role=mechanical_role,
    )


def _jst_ph_smd_1x02() -> volt.FootprintDefinition:
    return volt.FootprintDefinition(
        ("Connector_JST", "JST_PH_S2B-PH-SM4-TB_1x02-1MP_P2.00mm_Horizontal"),
        pads=(
            _front_smd_pad("1", at=(-1.0, -2.85), size=(1.0, 3.5)),
            _front_smd_pad("2", at=(1.0, -2.85), size=(1.0, 3.5)),
            _front_smd_pad(
                "MP1",
                at=(-3.35, 2.9),
                size=(1.5, 3.4),
                mechanical_role="mechanical_support",
            ),
            _front_smd_pad(
                "MP2",
                at=(3.35, 2.9),
                size=(1.5, 3.4),
                mechanical_role="mechanical_support",
            ),
        ),
    )


def _timer_soic_8() -> volt.FootprintDefinition:
    return volt.FootprintDefinition(
        ("KiCad_Package_SO", "SOIC-8_3.9x4.9mm_P1.27mm"),
        pads=(
            _front_smd_pad("1", at=(-2.475, -1.905), size=(1.95, 0.6)),
            _front_smd_pad("2", at=(-2.475, -0.635), size=(1.95, 0.6)),
            _front_smd_pad("3", at=(-2.475, 0.635), size=(1.95, 0.6)),
            _front_smd_pad("4", at=(-2.475, 1.905), size=(1.95, 0.6)),
            _front_smd_pad("5", at=(2.475, 1.905), size=(1.95, 0.6)),
            _front_smd_pad("6", at=(2.475, 0.635), size=(1.95, 0.6)),
            _front_smd_pad("7", at=(2.475, -0.635), size=(1.95, 0.6)),
            _front_smd_pad("8", at=(2.475, -1.905), size=(1.95, 0.6)),
        ),
    )


def _resistor_0805() -> volt.FootprintDefinition:
    return volt.FootprintDefinition(
        ("Resistor_SMD", "R_0805_2012Metric"),
        pads=(
            _front_smd_pad("1", at=(-0.9125, 0.0), size=(1.025, 1.4)),
            _front_smd_pad("2", at=(0.9125, 0.0), size=(1.025, 1.4)),
        ),
    )


def _capacitor_0805() -> volt.FootprintDefinition:
    return volt.FootprintDefinition(
        ("Capacitor_SMD", "C_0805_2012Metric"),
        pads=(
            _front_smd_pad("1", at=(-0.95, 0.0), size=(1.0, 1.45)),
            _front_smd_pad("2", at=(0.95, 0.0), size=(1.0, 1.45)),
        ),
    )


def _led_0805() -> volt.FootprintDefinition:
    return volt.FootprintDefinition(
        ("LED_SMD", "LED_0805_2012Metric"),
        pads=(
            _front_smd_pad("1", at=(-0.9375, 0.0), size=(0.975, 1.4)),
            _front_smd_pad("2", at=(0.9375, 0.0), size=(0.975, 1.4)),
        ),
    )


FOOTPRINTS = {
    "jst_ph_smd_1x02": _jst_ph_smd_1x02(),
    "timer_soic_8": _timer_soic_8(),
    "resistor_0805": _resistor_0805(),
    "capacitor_0805": _capacitor_0805(),
    "led_0805": _led_0805(),
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
        "CDEC": design.C("100 nF", ref="C3"),
        "RLED": design.R("1 kOhm", ref="R3"),
        "DLED": design.LED(ref="D1"),
    }

    timer = parts["U1"]
    nets["+5V"] += (
        parts["J1"][1],
        timer["VCC"],
        timer["RESET"],
        parts["RA"][1],
        parts["CDEC"][1],
    )
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
        parts["CDEC"][2],
        parts["DLED"]["K"],
    )

    parts["J1"].select_part(
        manufacturer="JST",
        part_number="S2B-PH-SM4-TB(LF)(SN)",
        package="JST-PH-SMD-1x02",
        footprint=FOOTPRINTS["jst_ph_smd_1x02"],
        pin_pads={1: "1", 2: "2"},
    )
    timer.select_part(
        manufacturer="Texas Instruments",
        part_number="NE555DR",
        package="SOIC-8",
        footprint=FOOTPRINTS["timer_soic_8"],
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
        (parts["RA"], "RMCF0805FT100K", 0.125),
        (parts["RB"], "RMCF0805FT47K0", 0.125),
        (parts["RLED"], "RMCF0805FT1K00", 0.125),
    ):
        component.select_part(
            manufacturer="Stackpole",
            part_number=part_number,
            package="0805",
            footprint=FOOTPRINTS["resistor_0805"],
            pin_pads={1: "1", 2: "2"},
            power_rating=power_rating,
        )
    for component, part_number, voltage_rating in (
        (parts["CT"], "CL21B105KBFNNNE", 50.0),
        (parts["CCTRL"], "CL21B103KBANNNC", 50.0),
        (parts["CDEC"], "CL21B104KBCNNNC", 50.0),
    ):
        component.select_part(
            manufacturer="Samsung Electro-Mechanics",
            part_number=part_number,
            package="0805",
            footprint=FOOTPRINTS["capacitor_0805"],
            pin_pads={1: "1", 2: "2"},
            voltage_rating=voltage_rating,
        )
    parts["DLED"].select_part(
        manufacturer="Lite-On",
        part_number="LTST-C171KRKT",
        package="0805",
        footprint=FOOTPRINTS["led_0805"],
        pin_pads={"K": "1", "A": "2"},
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
        decoupling_cap = (
            drawing.two_terminal(parts["CDEC"])
            .at(timer.VCC.right(80))
            .down()
            .label_ref(loc="right", offset=8)
            .label_value(loc="right", offset=24)
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
        drawing.power_stub("+5V", at=decoupling_cap.start)
        drawing.ground_stub("GND", at=decoupling_cap.end)

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
        min_track_width=0.20,
        min_via_drill=0.30,
        min_via_annular=0.70,
        board_outline_clearance=0.25,
    )
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(48.0, 32.0))
    board.add(volt.Hole(center=(4.0, 4.0), diameter=2.2, role="mounting"))
    board.add(volt.Hole(center=(44.0, 4.0), diameter=2.2, role="mounting"))
    board.add(volt.Hole(center=(4.0, 28.0), diameter=2.2, role="mounting"))
    board.add(volt.Hole(center=(44.0, 28.0), diameter=2.2, role="mounting"))
    board.add_zone(
        outline=((1.0, 1.0), (47.0, 1.0), (47.0, 31.0), (1.0, 31.0)),
        layers=(back,),
        net=nets["GND"],
    )

    with board.layout(unit=1.0, grid=0.5) as layout:
        header = layout.place(
            parts["J1"],
            at=board.edge("left").center().right(7),
            orient="right",
            locked=True,
        )
        timer = layout.place(
            parts["U1"],
            at=(23.0, 15.0),
            orient="right",
            locked=True,
        )

        with layout.hold():
            ra = (
                layout.two_pad(parts["RA"])
                .at((36.0, 10.0))
                .anchor("center")
                .right()
            )
            rb = (
                layout.two_pad(parts["RB"])
                .at((36.0, 14.0))
                .anchor("center")
                .right()
            )
            timing_junction = layout.snap(rb.end.right(2.0))
            ct = (
                layout.two_pad(parts["CT"])
                .at(timing_junction.down(3.0))
                .down()
            )

        with layout.hold():
            cctrl = (
                layout.two_pad(parts["CCTRL"])
                .at((31.0, 21.0))
                .anchor("center")
                .down()
            )
            cdec = (
                layout.two_pad(parts["CDEC"])
                .at((29.0, 10.0))
                .anchor("center")
                .right()
            )

        with layout.hold():
            rled = (
                layout.two_pad(parts["RLED"])
                .at((15.5, 20.0))
                .anchor("center")
                .down()
            )
            dled = layout.place(parts["DLED"], at=(12.5, 24.0), orient="right")

        board.add_text("555 SMD", at=(28.0, 29.5), layer=silk, size=1.0)
        board.add_text("+5V", at=(4.8, 10.2), layer=silk, size=0.7)
        board.add_text("GND", at=(7.5, 10.2), layer=silk, size=0.7)
        board.add_text("K", at=(10.2, 25.9), layer=silk, size=0.7)

        def horizontal_drop(pad, dx):
            return pad.tox(layout.snap_x(pad.x + dx))

        gnd_drops = (
            (header[2], horizontal_drop(header[2], 2.0)),
            (timer.GND, horizontal_drop(timer.GND, -2.0)),
            (cdec.end, horizontal_drop(cdec.end, 1.55)),
            (cctrl.end, horizontal_drop(cctrl.end, 1.6)),
            (ct.end, horizontal_drop(ct.end, 1.65)),
            (dled.K, horizontal_drop(dled.K, -1.6)),
        )
        for pad, drop in gnd_drops:
            drop_anchor = drop
            layout.route(nets["GND"], layer=front, width=0.30).at(pad).to(drop_anchor)
            layout.via(
                nets["GND"],
                at=drop_anchor,
                start_layer=front,
                end_layer=back,
            )

        power_rail = layout.snap(header[1].up(5.15))
        layout.route(nets["+5V"], layer=front, width=0.30).at(header[1]).toy(
            power_rail
        ).tox(cdec.start).to(cdec.start)
        layout.route(nets["+5V"], layer=front, width=0.30).at(cdec.start).to(timer.VCC)
        layout.route(nets["+5V"], layer=front, width=0.30).at(cdec.start).toy(
            power_rail
        ).tox(ra.start).to(ra.start)
        layout.route(nets["+5V"], layer=front, width=0.25).at(timer.RESET).tox(
            timer.RESET.left(3.025)
        ).toy(timer.VCC.up(2.095)).tox(timer.VCC).to(timer.VCC)

        disch_escape = layout.snap(timer.DISCH.right(2.5))
        disch_bus = layout.snap(ra.end.right(1.6).down(2.0))
        layout.route(nets["DISCH"], layer=front).at(timer.DISCH).tox(disch_escape).to(
            rb.start
        )
        layout.route(nets["DISCH"], layer=front).at(timer.DISCH).tox(disch_escape).toy(
            disch_bus
        ).tox(disch_bus).toy(ra.end).to(ra.end)

        timing_escape = layout.snap(timer.TRIG.right(3.0))
        layout.route(nets["TIMING"], layer=front).at(timer.TRIG).tox(timing_escape).toy(
            timer.THRESH
        ).to(timer.THRESH)
        layout.route(nets["TIMING"], layer=front).at(timer.THRESH).tox(
            timing_junction
        ).toy(timing_junction).to(rb.end)
        layout.route(nets["TIMING"], layer=front).at(timing_junction).to(ct.start)

        ctrl_escape = layout.snap(timer.CTRL.right(4.5))
        layout.route(nets["CTRL"], layer=front).at(timer.CTRL).tox(ctrl_escape).toy(
            cctrl.start
        ).to(cctrl.start)
        out_escape = layout.snap(timer.OUT.right(1.5))
        layout.route(nets["OUT"], layer=front).at(timer.OUT).tox(out_escape).toy(
            rled.start
        ).to(rled.start)
        layout.route(nets["LED_A"], layer=front).at(rled.end).toy(dled.A).to(dled.A)
    return board


def build_example() -> tuple[volt.Design, volt.Schematic, volt.Board]:
    result = run_project()
    return result.design(), result.schematic(), result.board()


def build_project() -> volt.Project:
    project = volt.Project(
        "timer-555-led-blinker",
        description="555 LED blinker reference design",
    )
    context: dict[str, tuple[volt.Design, dict[str, volt.Net], dict[str, volt.Component]]] = {}

    @project.design
    def design() -> volt.Design:
        context["design"] = build_design()
        return context["design"][0]

    @project.schematic
    def schematic(design: volt.Design) -> volt.Schematic:
        _, nets, parts = context["design"]
        return build_schematic(design, nets, parts)

    @project.board
    def board(design: volt.Design) -> volt.Board:
        _, nets, parts = context["design"]
        return build_board(design, nets, parts)

    @project.design.test
    def power_and_ground_are_separate(check) -> None:
        check.net("+5V").connects("J1.1", "U1.VCC", "U1.RESET")
        check.net("GND").connects("J1.2", "U1.GND", "D1.K")
        check.no_connection("+5V", "GND")

    @project.schematic.test
    def schematic_places_design_parts(check) -> None:
        check.places("J1", "U1", "R1", "R2", "C1", "C2", "C3", "R3", "D1")

    @project.board.test
    def board_places_design_parts(check) -> None:
        check.has_outline()
        check.places("J1", "U1", "R1", "R2", "C1", "C2", "C3", "R3", "D1")

    return project


def run_project() -> volt.ProjectResult:
    return build_project().run()


def write_artifacts(output_dir: Path | str | None = None) -> ExampleArtifacts:
    if output_dir is None:
        output_dir = Path(__file__).resolve().parent / "artifacts"
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    result = run_project()
    _require_clean(result)
    design = result.design()
    schematic = result.schematic()
    board = result.board()

    project_bundle = output_path / f"{EXAMPLE_SLUG}.volt"
    logical_json = output_path / f"{EXAMPLE_SLUG}.volt.json"
    schematic_json = output_path / f"{EXAMPLE_SLUG}.volt.schematic.json"
    schematic_svg = output_path / f"{EXAMPLE_SLUG}.svg"
    schematic_body_svg = output_path / f"{EXAMPLE_SLUG}.body.svg"
    schematic_svg_pages_dir = output_path / f"{EXAMPLE_SLUG}.pages"
    pcb_json = output_path / f"{EXAMPLE_SLUG}.volt.pcb.json"
    pcb_svg = output_path / f"{EXAMPLE_SLUG}.pcb.svg"
    validation_report = output_path / f"{EXAMPLE_SLUG}.validation.json"

    result.write(project_bundle)
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
    pcb_svg.write_text(
        board.to_svg(pad_net_overlays=False, ratsnest_edges=False),
        encoding="utf-8",
    )
    validation_report.write_text(
        (project_bundle / "diagnostics" / "diagnostics.json").read_text(encoding="utf-8"),
        encoding="utf-8",
    )
    return ExampleArtifacts(
        project_bundle=project_bundle,
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
