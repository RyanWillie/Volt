import volt

from parts import INPUT_5V, LIBRARY, RED_LED, RESISTOR_330R
from project_tests import register_project_tests


def _capability_profile() -> volt.CapabilityProfile:
    return volt.CapabilityProfile(
        name="Volt reference two-layer process",
        source="Volt 5 V LED indicator sample",
        as_of="2026-07-30",
        minimum_track_width=0.20,
        minimum_via_drill=0.30,
        minimum_via_annular=0.70,
        minimum_clearances=(("track", "track", 0.20), ("track", "pad", 0.20)),
        supported_copper_layer_counts=(2,),
        board_thickness_range=(1.5, 1.7),
        available_copper_weights=(1.0,),
        drill_diameter_range=(0.30, 6.0),
    )


def main() -> volt.Project:
    project = volt.Project(
        "5v-led-indicator",
        version="1.0.0",
        description="A complete 5 V red LED indicator reference project",
    )
    project.use_library(LIBRARY)

    @project.design
    def design():
        result = volt.Design("5v-led-indicator")
        connector = result.instantiate(
            INPUT_5V, ref="J1", properties=dict(INPUT_5V.properties)
        ).dnp(False)
        resistor = result.instantiate(
            RESISTOR_330R, ref="R1", properties=dict(RESISTOR_330R.properties)
        ).dnp(False)
        led = result.instantiate(
            RED_LED, ref="D1", properties=dict(RED_LED.properties)
        ).dnp(False)

        five_volts = result.net("+5V", kind="power", voltage=5.0)
        led_anode = result.net("LED_A")
        ground = result.net("GND", kind="ground")
        five_volts += connector["+5V"], resistor[1]
        led_anode += resistor[2], led["A"]
        ground += led["K"], connector["GND"]
        return result

    @project.schematic
    def schematic(context):
        design = context.design()
        sheet = design.schematic(
            "Main",
            size=(260, 140),
            orientation="landscape",
            title="5 V LED Indicator",
            number=1,
            page_count=1,
            revision="A",
            date="2026-07-30",
            project="Volt 5 V LED Indicator",
            file="main.py",
            margins=(10, 10, 10, 10),
            coordinate_zones=(6, 4),
            grid={"spacing": 5, "visible": False},
        )
        nets = {net.name: net for net in design.nets()}
        with sheet.drawing(unit=1) as drawing:
            connector = (
                drawing.place(design.component("J1"), at=(30, 45))
                .label_ref(loc="top", offset=5)
                .label_value(loc="bottom", offset=8)
            )
            resistor = (
                drawing.two_terminal(design.component("R1"))
                .at(connector["+5V"].right(25))
                .right(45)
                .label_ref(loc="top", offset=8)
                .label_value(loc="bottom", offset=10)
            )
            led = (
                drawing.two_terminal(design.component("D1"))
                .at(resistor.end.right(25))
                .right(45)
                .label_ref(loc="top", offset=8)
                .label_value(loc="bottom", offset=10)
            )
            drawing.connect(
                connector["+5V"], resistor.start, net=nets["+5V"], shape="-"
            )
            drawing.connect(resistor.end, led.start, net=nets["LED_A"], shape="-")
            drawing.connect(
                led.end,
                connector["GND"],
                net=nets["GND"],
                shape="|-",
                k=25,
            )
        return sheet

    @project.board
    def board(context):
        design = context.design()
        board = design.add_board("Indicator")
        board.set_capability_profile(_capability_profile())
        board.set_design_rules(
            copper_clearance=0.25,
            min_track_width=0.25,
            min_via_drill=0.35,
            min_via_annular=0.75,
            board_outline_clearance=0.25,
            package_assembly_clearance=0.25,
        )
        board.set_rectangular_outline(origin=(0.0, 0.0), size=(28.0, 14.0))
        front = board.add_layer("F.Cu", role="copper", side="top", copper_weight=1.0)
        back = board.add_layer("B.Cu", role="copper", side="bottom", copper_weight=1.0)
        silk = board.add_layer("F.SilkS", role="silkscreen", side="top")
        board.set_layer_stack((front, back), thickness=1.6)

        nets = {net.name: net for net in design.nets()}
        with board.layout(unit=1.0, grid=0.25) as layout:
            connector = layout.place(
                design.component("J1"),
                at=layout.snap((4.0, 7.0)),
                orient="right",
                locked=True,
            )
            resistor = (
                layout.two_pad(design.component("R1"))
                .at(layout.snap(connector["+5V"].right(7.0)))
                .anchor("start")
                .right()
            )
            led = (
                layout.two_pad(design.component("D1"))
                .at(layout.snap(resistor.end.right(5.0)))
                .anchor("start")
                .right()
            )

            layout.connect(
                connector["+5V"],
                resistor.start,
                layer=front,
                net=nets["+5V"],
                width=0.35,
            )
            layout.connect(
                resistor.end,
                led.start,
                layer=front,
                net=nets["LED_A"],
                width=0.25,
            )
            ground_lane = layout.snap(led.end.down(3.0))
            layout.connect(
                led.end,
                connector["GND"],
                layer=front,
                net=nets["GND"],
                width=0.35,
                through=(
                    ground_lane,
                    ground_lane.tox(connector["GND"]),
                ),
            )

            layout.text(
                "5V LED REV A",
                at=board.edge("bottom").center().up(1.0),
                layer=silk,
                size=0.8,
            )
        return board

    register_project_tests(project)
    return project
