import volt


def _delivery_profile():
    return volt.CapabilityProfile(
        name="Project framework delivery fixture",
        source="Volt Python test fixture",
        as_of="2026-07-24",
        minimum_track_width=0.01,
        minimum_via_drill=0.01,
        minimum_via_annular=0.02,
    )


def _rectangle(width, height):
    return (
        (-width / 2.0, -height / 2.0),
        (width / 2.0, -height / 2.0),
        (width / 2.0, height / 2.0),
        (-width / 2.0, height / 2.0),
    )


def _offset_rectangle(x, y, width, height):
    return tuple((px + x, py + y) for px, py in _rectangle(width, height))


def _passive_0603(ref):
    markings = ()
    if tuple(ref) == ("leds", "LED_0603_1608Metric"):
        markings = (volt.FootprintMarking.polarity(_offset_rectangle(0.55, 0.0, 0.12, 0.60)),)

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
        courtyard=_rectangle(2.50, 1.40),
        body=_rectangle(1.60, 0.80),
        fabrication_outline=_rectangle(1.60, 0.80),
        assembly_outline=_rectangle(1.60, 0.80),
        markings=markings,
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


def _fixture_symbol(name, pins):
    return volt.SchematicSymbolSpec.block(
        f"volt.tests.project:{name}",
        pins=tuple(
            volt.SchematicSymbolSpec.block_pin(
                pin.name,
                pin.number,
                side="left" if index % 2 == 0 else "right",
            )
            for index, pin in enumerate(pins)
        ),
        center_label=name,
    )


def _native_fixture_parts():
    library = volt.Library("volt.tests.project", version="1.0.0")

    def add(name, pins, *, footprint, pads, manufacturer, mpn, package, prefix, value=None):
        pins = tuple(pins)
        return library.part(
            name,
            pins=pins,
            symbol=_fixture_symbol(name, pins),
            footprint=footprint,
            pads=pads,
            manufacturer=manufacturer,
            mpn=mpn,
            package=package,
            prefix=prefix,
            value=value,
        )

    return {
        "header": add(
            "Header-1x02",
            (volt.PinSpec("1", 1), volt.PinSpec("2", 2)),
            footprint=_header_1x02(),
            pads={1: "1", 2: "2"},
            manufacturer="Generic",
            mpn="HDR-1x02",
            package="2.54mm-1x02",
            prefix="J",
        ),
        "resistor": add(
            "Resistor-330R",
            (volt.PinSpec("1", 1), volt.PinSpec("2", 2)),
            footprint=_passive_0603(("passives", "R_0603_1608Metric")),
            pads={1: "1", 2: "2"},
            manufacturer="Yageo",
            mpn="RC0603FR-07330RL",
            package="0603",
            prefix="R",
            value="330",
        ),
        "led": add(
            "Status-LED",
            (volt.PinSpec("A", 1), volt.PinSpec("K", 2)),
            footprint=_passive_0603(("leds", "LED_0603_1608Metric")),
            pads={"A": "1", "K": "2"},
            manufacturer="Lite-On",
            mpn="LTST-C190KRKT",
            package="0603",
            prefix="D",
        ),
    }


def _minimal_design(name="status-led"):
    design = volt.Design(name)
    vcc = design.net("VCC", kind="power")
    led_a = design.net("LED_A")
    gnd = design.net("GND", kind="ground")
    j1 = design.connector_1x02(ref="J1")
    r1 = design.R("330", ref="R1")
    d1 = design.LED(ref="D1")
    for component in (j1, r1, d1):
        component.dnp(True)
    vcc += j1[1], r1[1]
    led_a += r1[2], d1["A"]
    gnd += d1["K"], j1[2]
    return design


def _board_ready_design(name="status-led"):
    parts = _native_fixture_parts()
    design = volt.Design(name)
    vcc = design.net("VCC", kind="power")
    led_a = design.net("LED_A")
    gnd = design.net("GND", kind="ground")
    j1 = design.instantiate(parts["header"], ref="J1").dnp(False)
    r1 = design.instantiate(parts["resistor"], ref="R1").dnp(False)
    d1 = design.instantiate(parts["led"], ref="D1").dnp(False)
    vcc += j1[1], r1[1]
    led_a += r1[2], d1["A"]
    gnd += d1["K"], j1[2]
    return design


def _overvoltage_exact_part_design(name="overvoltage-exact-part"):
    library = volt.Library("volt.tests.project.overvoltage", version="1.0.0")
    pins = (volt.PinSpec("1", 1), volt.PinSpec("2", 2))
    part = library.part(
        "Rated-3V9",
        pins=pins,
        symbol=_fixture_symbol("Rated-3V9", pins),
        footprint=_passive_0603(("passives", "R_0603_1608Metric")),
        pads={1: "1", 2: "2"},
        manufacturer="Volt Tests",
        mpn="RATED-3V9",
        package="0603",
        prefix="R",
        voltage_rating=3.9,
    )
    design = volt.Design(name)
    component = design.instantiate(part, ref="R1")
    vdd = design.net("VDD", kind="power", voltage=5.0)
    ground = design.net("GND", kind="ground")
    vdd += component[1]
    ground += component[2]
    return design


def _stage_schematic(design):
    sheet = design.schematic("Main", size=(420, 240), margins=(8, 8, 8, 8))
    nets = {net.name: net for net in design.nets()}
    vcc = nets["VCC"]
    led_a = nets["LED_A"]
    gnd = nets["GND"]

    with sheet.drawing(unit=20) as drawing:
        header = drawing.place(
            design.component("J1"),
            at=(330, 100),
            orient="Right",
        ).label_ref(loc="left")
        resistor = (
            drawing.two_terminal(design.component("R1"))
            .at(header[1].left(60))
            .left()
            .label_ref(loc="top", offset=15)
            .label_value(loc="bottom", offset=15)
        )
        led = (
            drawing.two_terminal(design.component("D1"))
            .at(resistor.end.left(100))
            .left()
            .label_ref(loc="top", offset=15)
        )
        supply = drawing.power("VCC", net=vcc, at=header[1].up(50))
        header_ground = drawing.ground("GND", net=gnd, at=header[2].down(50))
        led_ground = drawing.ground("GND", net=gnd, at=led.end.down(50))

        drawing.connect(supply.pin, header[1], net=vcc, shape="-")
        drawing.connect(header[1], resistor.start, net=vcc, shape="-").dot()
        drawing.connect(resistor.end, led.start, net=led_a, shape="-")
        drawing.connect(header[2], header_ground.pin, net=gnd, shape="-")
        drawing.connect(led.end, led_ground.pin, net=gnd, shape="-")
        drawing.net_label(led_a, at=resistor.end.up(30))

    return sheet


def _stage_board(design):
    pcb = design.add_board("Main")
    pcb.set_capability_profile(_delivery_profile())
    pcb.set_rectangular_outline(origin=(0, 0), size=(20, 10))
    pcb.place(design.component("J1"), at=(4, 5), locked=True)
    pcb.place(design.component("R1"), at=(10, 5))
    pcb.place(design.component("D1"), at=(15, 5), rotation=180)
    return pcb
