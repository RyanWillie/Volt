"""Schematic stage for the 555 LED blinker example."""

from __future__ import annotations

import volt

SHEET_FILE = "timer_555_led_blinker/schematic.py"


def build_schematic(context: volt.BuildContext) -> volt.Schematic:
    design = context.design()
    nets = context.resource("nets", dict)
    parts = context.resource("parts", dict)

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
