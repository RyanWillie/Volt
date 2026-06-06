import volt


def _passive_0603(ref):
    return volt.FootprintDefinition(
        ref,
        pads=(
            volt.FootprintPad.surface_mount(
                "1",
                at=(-0.75, 0.0),
                size=(0.8, 0.95),
                shape="rounded_rectangle",
            ),
            volt.FootprintPad.surface_mount(
                "2",
                at=(0.75, 0.0),
                size=(0.8, 0.95),
                shape="rounded_rectangle",
            ),
        ),
    )


def _header_1x02():
    return volt.FootprintDefinition(
        ("connectors", "PinHeader_1x02_P2.54mm"),
        pads=(
            volt.FootprintPad.through_hole(
                "1",
                at=(0.0, -1.27),
                size=(1.7, 1.7),
                drill=volt.FootprintDrill(1.0),
            ),
            volt.FootprintPad.through_hole(
                "2",
                at=(0.0, 1.27),
                size=(1.7, 1.7),
                drill=volt.FootprintDrill(1.0),
            ),
        ),
    )


def _minimal_design(name="status-led"):
    design = volt.Design(name)
    vcc = design.net("VCC", kind="power")
    led_a = design.net("LED_A")
    gnd = design.net("GND", kind="ground")
    j1 = design.connector_1x02(ref="J1")
    r1 = design.R("330", ref="R1")
    d1 = design.LED(ref="D1")
    vcc += j1[1], r1[1]
    led_a += r1[2], d1["A"]
    gnd += d1["K"], j1[2]
    return design


def _board_ready_design(name="status-led"):
    design = _minimal_design(name)
    design.component("J1").select_part(
        manufacturer="Generic",
        part_number="HDR-1x02",
        package="2.54mm-1x02",
        footprint=_header_1x02(),
        pin_pads={1: "1", 2: "2"},
    )
    design.component("R1").select_part(
        manufacturer="Yageo",
        part_number="RC0603FR-07330RL",
        package="0603",
        footprint=_passive_0603(("passives", "R_0603_1608Metric")),
        pin_pads={1: "1", 2: "2"},
    )
    design.component("D1").select_part(
        manufacturer="Lite-On",
        part_number="LTST-C190KRKT",
        package="0603",
        footprint=_passive_0603(("leds", "LED_0603_1608Metric")),
        pin_pads={"A": "1", "K": "2"},
    )
    return design


def _stage_schematic(design):
    sheet = design.schematic("Main", size=(220, 150), margins=(8, 8, 8, 8))
    nets = {net.name: net for net in design.nets()}
    vcc = nets["VCC"]
    led_a = nets["LED_A"]
    gnd = nets["GND"]

    with sheet.drawing(unit=20) as drawing:
        header = drawing.place(
            design.component("J1"),
            at=(45, 60),
            orient="Right",
        ).label_ref(loc="left")
        resistor = (
            drawing.two_terminal(design.component("R1"))
            .at(header[1].right(35))
            .right()
            .label_ref(loc="top", offset=6)
            .label_value(loc="bottom", offset=10)
        )
        led = (
            drawing.two_terminal(design.component("D1"))
            .at(resistor.end.right(18))
            .right()
            .reverse()
            .label_ref(loc="top", offset=8)
        )
        supply = drawing.power("VCC", net=vcc, at=header[1].up(18))
        header_ground = drawing.ground("GND", net=gnd, at=header[2].down(18))
        led_ground = drawing.ground("GND", net=gnd, at=led.end.down(24))

        drawing.connect(supply.pin, header[1], net=vcc, shape="-")
        drawing.connect(header[1], resistor.start, net=vcc, shape="-").dot()
        drawing.connect(resistor.end, led.start, net=led_a, shape="-")
        drawing.connect(header[2], header_ground.pin, net=gnd, shape="-")
        drawing.connect(led.end, led_ground.pin, net=gnd, shape="-")
        drawing.net_label(led_a, at=resistor.end.up(12))

    return sheet


def _stage_board(design):
    pcb = design.board("Main")
    pcb.set_rectangular_outline(origin=(0, 0), size=(20, 10))
    pcb.place(design.component("J1"), at=(4, 5), locked=True)
    pcb.place(design.component("R1"), at=(10, 5))
    pcb.place(design.component("D1"), at=(15, 5), rotation=180)
    return pcb
