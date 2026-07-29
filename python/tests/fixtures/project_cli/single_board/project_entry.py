import os
from pathlib import Path

import volt


FIXTURE_LIBRARY = volt.Library("volt.tests.project_cli", version="1.0.0")


def _record_source_execution():
    sentinel = os.environ.get("VOLT_TEST_SOURCE_SENTINEL")
    if sentinel:
        with Path(sentinel).open("a", encoding="utf-8") as handle:
            handle.write("executed\n")


def _profile():
    return volt.CapabilityProfile(
        name="Verified project CLI fixture",
        source="Volt test fixture",
        as_of="2026-07-28",
        minimum_track_width=0.01,
        minimum_via_drill=0.01,
        minimum_via_annular=0.02,
    )


def _footprint():
    return volt.Footprint(
        ("fixture", "R_0603"),
        pads=(
            volt.FootprintPad.surface_mount(
                "1",
                at=(-0.75, 0),
                size=(0.8, 0.95),
            ),
            volt.FootprintPad.surface_mount(
                "2",
                at=(0.75, 0),
                size=(0.8, 0.95),
            ),
        ),
    )


RESISTOR_1K = FIXTURE_LIBRARY.part(
    "R_0603_1K",
    pins=(volt.PinSpec("1", 1), volt.PinSpec("2", 2)),
    symbol=volt.SchematicSymbolSpec(
        "volt.tests.project_cli:R_0603",
        pins=(
            volt.SchematicSymbolSpec.pin("1", 1, (-10, 0), "Right"),
            volt.SchematicSymbolSpec.pin("2", 2, (10, 0), "Left"),
        ),
        primitives=(
            volt.SchematicSymbolSpec.line((-10, 0), (-5, 0)),
            volt.SchematicSymbolSpec.rectangle((-5, -2), (5, 2)),
            volt.SchematicSymbolSpec.line((5, 0), (10, 0)),
        ),
    ),
    footprint=_footprint(),
    pads={1: "1", 2: "2"},
    value="1k",
    manufacturer="Fixture",
    mpn="R-1K",
    package="0603",
    model_3d=volt.PartModel3D(Path(__file__).with_name("r_0603.step").resolve()),
    prefix="R",
)


def main():
    _record_source_execution()
    project = volt.Project(
        "verified-cli-single",
        version="1.0.0",
        description="Single-board verified CLI fixture",
    )

    @project.design
    def design():
        result = volt.Design("controller")
        r1 = result.instantiate(
            RESISTOR_1K, ref="R1", properties=dict(RESISTOR_1K.properties)
        )
        r2 = result.instantiate(
            RESISTOR_1K, ref="R2", properties=dict(RESISTOR_1K.properties)
        )
        for resistor in (r1, r2):
            resistor.dnp(False)
        signal = result.net("SIGNAL")
        return_path = result.net("RETURN")
        signal += r1[2], r2[1]
        return_path += r1[1], r2[2]
        return result

    @project.schematic
    def schematic(context):
        design = context.design()
        sheet = design.schematic("Main")
        nets = {net.name: net for net in design.nets()}
        with sheet.drawing(unit=20) as drawing:
            r1 = (
                drawing.two_terminal(design.component("R1"))
                .at((80, 60))
                .right()
                .label_ref(loc="top")
                .label_value(loc="bottom")
            )
            r2 = (
                drawing.two_terminal(design.component("R2"))
                .at((140, 60))
                .right()
                .label_ref(loc="top")
                .label_value(loc="bottom")
            )
            drawing.net_label(nets["RETURN"], at=r1.start)
            drawing.net_label(nets["SIGNAL"], at=r1.end)
            drawing.net_label(nets["SIGNAL"], at=r2.start)
            drawing.net_label(nets["RETURN"], at=r2.end)
        return sheet

    @project.board
    def board(context):
        design = context.design()
        result = design.add_board("Main")
        result.set_capability_profile(_profile())
        result.set_rectangular_outline(origin=(0, 0), size=(20, 10))
        front = result.add_layer("F.Cu", role="copper", side="top")
        back = result.add_layer("B.Cu", role="copper", side="bottom")
        result.set_layer_stack((front, back), thickness=1.6)
        result.place(design.component("R1"), at=(6, 5))
        result.place(design.component("R2"), at=(14, 5))
        nets = {net.name: net for net in design.nets()}
        result.add_track(
            nets["SIGNAL"],
            layer=front,
            points=((6.75, 5), (13.25, 5)),
            width=0.25,
        )
        result.add_track(
            nets["RETURN"],
            layer=front,
            points=((5.25, 5), (5.25, 7), (14.75, 7), (14.75, 5)),
            width=0.25,
        )
        return result

    return project
