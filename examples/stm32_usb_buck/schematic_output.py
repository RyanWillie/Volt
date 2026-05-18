"""Schematic projection for the STM32 USB buck benchmark."""

from __future__ import annotations

import volt
from volt.libraries import stm32_usb_buck as lib

from .stm32_board import Stm32UsbBuckBoard

# Use the generic two-terminal symbols so drawing.C/R can orient the passives.
TWO_TERMINAL_CAPACITOR = "capacitor"
TWO_TERMINAL_RESISTOR = "resistor"
SHEET_OPTIONS = {
    "size": "A4",
    "orientation": "landscape",
    "revision": "A",
    "date": "2026-05-18",
    "project": "Volt STM32 USB Buck",
    "margins": (12, 10, 12, 10),
    "coordinate_zones": (10, 6),
    "grid": {"spacing": 5, "visible": True},
}
SHEET_FILE = "examples/stm32_usb_buck/schematic_output.py"


def _display_reference(component: volt.Component) -> str:
    return component.reference.rsplit("/", 1)[-1]


def _external_supply_symbol() -> volt.SchematicSymbolSpec:
    return volt.SchematicSymbolSpec(
        "volt.examples.stm32_usb_buck:ExternalSupply",
        pins=(
            volt.SchematicSymbolSpec.pin("OUT", 1, (44, 8), "Right"),
            volt.SchematicSymbolSpec.pin("GND", 2, (44, 24), "Right"),
        ),
        primitives=(
            volt.SchematicSymbolSpec.rectangle((0, 0), (34, 32)),
            volt.SchematicSymbolSpec.line((34, 8), (44, 8)),
            volt.SchematicSymbolSpec.line((34, 24), (44, 24)),
            volt.SchematicSymbolSpec.text("VIN", (17, 17)),
        ),
    )


def _indicator_led_symbol() -> volt.SchematicSymbolSpec:
    return volt.SchematicSymbolSpec(
        "volt.examples.stm32_usb_buck:IndicatorLED",
        pins=(
            volt.SchematicSymbolSpec.pin("A", 1, (0, 0), "Left"),
            volt.SchematicSymbolSpec.pin("K", 2, (24, 0), "Right"),
        ),
        primitives=(
            volt.SchematicSymbolSpec.line((0, 0), (8, 0)),
            volt.SchematicSymbolSpec.line((16, 0), (24, 0)),
            volt.SchematicSymbolSpec.line((8, -6), (8, 6)),
            volt.SchematicSymbolSpec.line((8, -6), (16, 0)),
            volt.SchematicSymbolSpec.line((8, 6), (16, 0)),
            volt.SchematicSymbolSpec.line((16, -6), (16, 6)),
            volt.SchematicSymbolSpec.text("LED", (12, -10)),
        ),
    )


def _compact_connector_1x04_symbol() -> volt.SchematicSymbolSpec:
    return volt.SchematicSymbolSpec(
        "volt.examples.stm32_usb_buck:CompactConnector1x04",
        pins=(
            volt.SchematicSymbolSpec.pin("1", 1, (0, 0), "Left"),
            volt.SchematicSymbolSpec.pin("2", 2, (0, 7), "Left"),
            volt.SchematicSymbolSpec.pin("3", 3, (0, 14), "Left"),
            volt.SchematicSymbolSpec.pin("4", 4, (0, 21), "Left"),
        ),
        primitives=(
            volt.SchematicSymbolSpec.rectangle((8, -5), (28, 26)),
            volt.SchematicSymbolSpec.text("CONN", (18, -10)),
            volt.SchematicSymbolSpec.line((0, 0), (8, 0)),
            volt.SchematicSymbolSpec.line((0, 7), (8, 7)),
            volt.SchematicSymbolSpec.line((0, 14), (8, 14)),
            volt.SchematicSymbolSpec.line((0, 21), (8, 21)),
        ),
    )


def _compact_swd_symbol() -> volt.SchematicSymbolSpec:
    pin_names = (
        "VTref",
        "SWDIO",
        "GND",
        "SWCLK",
        "GND",
        "SWO",
        "NC",
        "TDI",
        "GNDDetect",
        "nRESET",
    )
    pins = []
    primitives = [
        volt.SchematicSymbolSpec.rectangle((8, -4), (30, 49)),
        volt.SchematicSymbolSpec.text("SWD", (19, -9)),
    ]
    for index, name in enumerate(pin_names, start=1):
        y = (index - 1) * 5
        pins.append(volt.SchematicSymbolSpec.pin(name, index, (0, y), "Left"))
        primitives.append(volt.SchematicSymbolSpec.line((0, y), (8, y)))
    return volt.SchematicSymbolSpec(
        "volt.examples.stm32_usb_buck:CompactSWD10",
        pins=tuple(pins),
        primitives=tuple(primitives),
    )


def _readable_usb_protection_symbol() -> volt.SchematicSymbolSpec:
    return volt.SchematicSymbolSpec(
        "volt.examples.stm32_usb_buck:ReadableUSBLC6",
        pins=(
            volt.SchematicSymbolSpec.pin("I/O2", 3, (0, 8), "Left"),
            volt.SchematicSymbolSpec.pin("I/O1", 1, (0, 20), "Left"),
            volt.SchematicSymbolSpec.pin("I/O3", 4, (54, 8), "Right"),
            volt.SchematicSymbolSpec.pin("I/O4", 6, (54, 20), "Right"),
            volt.SchematicSymbolSpec.pin("VBUS", 5, (27, -10), "Up"),
            volt.SchematicSymbolSpec.pin("GND", 2, (27, 38), "Down"),
        ),
        primitives=(
            volt.SchematicSymbolSpec.rectangle((10, 0), (44, 30)),
            volt.SchematicSymbolSpec.line((0, 8), (10, 8)),
            volt.SchematicSymbolSpec.line((0, 20), (10, 20)),
            volt.SchematicSymbolSpec.line((44, 8), (54, 8)),
            volt.SchematicSymbolSpec.line((44, 20), (54, 20)),
            volt.SchematicSymbolSpec.line((27, -10), (27, 0)),
            volt.SchematicSymbolSpec.line((27, 30), (27, 38)),
            volt.SchematicSymbolSpec.text("ESD", (27, 16)),
        ),
    )


def _compact_stm32_symbol() -> volt.SchematicSymbolSpec:
    pin_layout = (
        ("VBAT", 1, (0, 12), "Left"),
        ("PC13", 2, (0, 24), "Left"),
        ("PH0", 5, (0, 42), "Left"),
        ("PH1", 6, (0, 50), "Left"),
        ("NRST", 7, (0, 62), "Left"),
        ("VDDA", 13, (0, 76), "Left"),
        ("VCAP_1", 31, (0, 90), "Left"),
        ("VDD", 19, (22, 0), "Up"),
        ("VDD", 32, (34, 0), "Up"),
        ("VDD", 48, (46, 0), "Up"),
        ("VDD", 64, (58, 0), "Up"),
        ("VSS", 18, (28, 112), "Down"),
        ("VSSA", 12, (42, 112), "Down"),
        ("VSS", 63, (56, 112), "Down"),
        ("PA11", 44, (84, 34), "Right"),
        ("PA12", 45, (84, 42), "Right"),
        ("PA13", 46, (84, 54), "Right"),
        ("PA14", 49, (84, 62), "Right"),
        ("PB3", 55, (84, 74), "Right"),
        ("BOOT0", 60, (84, 86), "Right"),
        ("VCAP_2", 47, (84, 98), "Right"),
    )
    primitives = [
        volt.SchematicSymbolSpec.rectangle((12, 8), (72, 104)),
        volt.SchematicSymbolSpec.text("STM32F405", (42, 52)),
    ]
    pins = []
    for name, number, at, orient in pin_layout:
        x, y = at
        pins.append(volt.SchematicSymbolSpec.pin(name, number, at, orient))
        if orient == "Left":
            primitives.append(volt.SchematicSymbolSpec.line((0, y), (12, y)))
        elif orient == "Right":
            primitives.append(volt.SchematicSymbolSpec.line((72, y), (84, y)))
        elif orient == "Up":
            primitives.append(volt.SchematicSymbolSpec.line((x, 0), (x, 8)))
        else:
            primitives.append(volt.SchematicSymbolSpec.line((x, 104), (x, 112)))
    return volt.SchematicSymbolSpec(
        "volt.examples.stm32_usb_buck:STM32F405RGTxCompact",
        pins=tuple(pins),
        primitives=tuple(primitives),
    )


def build_schematic(board: Stm32UsbBuckBoard) -> volt.Schematic:
    """Create a deterministic schematic projection for the benchmark board."""

    nets = {net.name: net for net in board.design.nets()}
    net_by_pin = {
        pin.index: net
        for net in board.design.nets()
        for pin in net.pins()
    }

    sheet = board.design.schematic(
        "STM32 USB Buck",
        title="STM32 USB Buck Reference Schematic",
        number=1,
        page_count=1,
        file=SHEET_FILE,
        **SHEET_OPTIONS,
    )

    power_region = sheet.region(
        "Power Circuitry",
        x=12,
        y=12,
        w=92,
        h=176,
        style={"border": "dashed"},
    )
    mcu_region = sheet.region(
        "STM32 Microcontroller",
        x=106,
        y=12,
        w=92,
        h=176,
        style={"border": "dashed"},
    )
    connectors_region = sheet.region(
        "Connectors and USB",
        x=200,
        y=12,
        w=85,
        h=146,
        style={"border": "dashed"},
    )

    _author_power_region(power_region, board, nets)
    _author_mcu_region(mcu_region, board, nets, net_by_pin)
    _author_connectors_region(connectors_region, board, nets, net_by_pin)
    return sheet


def _author_power_region(
    region: volt.SchematicRegion,
    board: Stm32UsbBuckBoard,
    nets: dict[str, volt.Net],
) -> None:
    pwr = board.modules["PWR"]
    with region.drawing(unit=20) as drawing:
        _rail_legend(drawing, nets, (18, 22), ("+12V", "+5V", "+3V3", "VDDA", "GND"))

        vin = drawing.place(
            board.components["VIN_SRC"],
            at=(12, 38),
            symbol=_external_supply_symbol(),
            reference_label=_display_reference(board.components["VIN_SRC"]),
        )
        pwr_j = drawing.place(
            pwr.component("J"),
            at=(12, 92),
            symbol=_compact_connector_1x04_symbol(),
            reference_label=_display_reference(pwr.component("J")),
        )
        u5 = drawing.place(
            pwr.component("U5"),
            at=(34, 44),
            symbol=lib.AP1117_15.schematic_symbol,
            reference_label=_display_reference(pwr.component("U5")),
        )
        u3v3 = drawing.place(
            pwr.component("U3V3"),
            at=(34, 94),
            symbol=lib.AP1117_15.schematic_symbol,
            reference_label=_display_reference(pwr.component("U3V3")),
        )
        cin = drawing.C(
            pwr.component("CIN"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("CIN")),
        ).at((60, 118)).down()
        c5v = drawing.C(
            pwr.component("C5V"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("C5V")),
        ).at((78, 74)).down()
        c3v3 = drawing.C(
            pwr.component("C3V3"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("C3V3")),
        ).at((84, 126)).down()
        cvdda = drawing.C(
            pwr.component("CVDDA"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("CVDDA")),
        ).at((66, 136)).down()

        u5.label("AP1117-5.0", name="value", loc="bottom", ofst=10, orient="Right")
        u3v3.label("AP1117-3.3", name="value", loc="bottom", ofst=10, orient="Right")
        cin.label("4.7 uF", name="value", loc="top", ofst=8, orient="Right")
        cvdda.label("100 nF", name="value", loc="left", ofst=10, orient="Right")

        _rail(drawing, nets["+12V"], vin.OUT, orient="Right")
        _rail(drawing, nets["GND"], vin.GND, orient="Down")

        pwr_in = nets["PWR/IN_12V"]
        pwr_5v = nets["PWR/OUT_5V"]
        pwr_3v3 = nets["PWR/OUT_3V3"]
        pwr_vdda = nets["PWR/VDDA"]
        pwr_gnd = nets["PWR/GND"]

        drawing.connect(pwr_j[1], u5.VI, net=pwr_in, shape="-|-", k=24)
        drawing.connect(cin.start, u5.VI, net=pwr_in, shape="|-")
        drawing.junction(pwr_in, at=(60, 54))

        drawing.off_page("+5V", net=pwr_5v, at=u5.VO, orient="Left")
        drawing.off_page("+5V", net=pwr_5v, at=u3v3.VI, orient="Left")
        drawing.connect(c5v.start, u5.VO, net=pwr_5v, shape="-|")

        drawing.connect(u3v3.VO, c3v3.start, net=pwr_3v3, shape="-")
        drawing.off_page("+3V3", net=pwr_3v3, at=u3v3.VO, orient="Left")

        drawing.off_page("VDDA", net=pwr_vdda, at=cvdda.start, orient="Left")
        drawing.connect(cvdda.start, (66, 120), net=pwr_vdda, shape="-")
        drawing.net_label(nets["+12V"], at=(18, 170))

        ground_bus = (54, 160)
        drawing.ground("GND", net=pwr_gnd, at=ground_bus, orient="Down")
        for anchor in (
            pwr_j[2],
            pwr_j[3],
            pwr_j[4],
            u5.GND,
            u3v3.GND,
            cin.end,
            c5v.end,
            c3v3.end,
            cvdda.end,
        ):
            drawing.connect(anchor, ground_bus, net=pwr_gnd, shape="|-")
        drawing.junction(pwr_gnd, at=(54, 160))


def _author_connectors_region(
    region: volt.SchematicRegion,
    board: Stm32UsbBuckBoard,
    nets: dict[str, volt.Net],
    net_by_pin: dict[int, volt.Net],
) -> None:
    usb = board.modules["USB"]
    with region.drawing(unit=20) as drawing:
        _rail_legend(drawing, nets, (18, 20), ("+5V", "+3V3", "GND"))

        usb_j = drawing.place(
            usb.component("J1"),
            at=(18, 42),
            symbol=lib.USB_B_MICRO.schematic_symbol,
            reference_label=_display_reference(usb.component("J1")),
        )
        usb_esd = drawing.place(
            usb.component("U1"),
            at=(29, 42),
            symbol=_readable_usb_protection_symbol(),
            reference_label=_display_reference(usb.component("U1")),
        )
        swd = drawing.place(
            board.components["J2"],
            at=(16, 82),
            symbol=_compact_swd_symbol(),
            reference_label=_display_reference(board.components["J2"]),
        )
        gpio = drawing.place(
            board.components["J3"],
            at=(50, 88),
            symbol=_compact_connector_1x04_symbol(),
            reference_label=_display_reference(board.components["J3"]),
        )

        usb_j.label("USB Micro-B", name="value", loc="bottom", ofst=12, orient="Right")

        drawing.connect(usb_j.VBUS, usb_esd.VBUS, net=nets["USB/VBUS"], shape="-|", k=18)
        drawing.connect(usb_j["D+"], usb_esd["I/O1"], net=nets["USB/USB_DP"], shape="-|", k=18)
        drawing.wire(nets["USB/USB_DM"]).at(usb_j["D-"]).via((24, 50)).to(
            usb_esd["I/O2"]
        ).orthogonal()
        _connect_to_off_page(drawing, net_by_pin, usb_esd["I/O4"], "USB D+")
        _connect_to_off_page(drawing, net_by_pin, usb_esd["I/O3"], "USB D-")

        usb_gnd = nets["USB/GND"]
        for anchor in (usb_j.GND, usb_j.Shield, usb_esd.GND):
            drawing.connect(anchor, (44, 72), net=usb_gnd, shape="|-")
        drawing.ground("GND", net=usb_gnd, at=(44, 72), orient="Down")
        drawing.junction(usb_gnd, at=(44, 72))

        for anchor in (swd.VTref, gpio[1]):
            _rail(drawing, net_by_pin[anchor.pin.index], anchor, orient="Up")
        for anchor in (swd[3], swd[5], swd[9], gpio[4]):
            _rail(drawing, net_by_pin[anchor.pin.index], anchor, orient="Down")
        for name in ("SWDIO", "SWCLK", "SWO", "nRESET"):
            display = "NRST" if name == "nRESET" else name
            _connect_to_off_page(
                drawing,
                net_by_pin,
                swd[name],
                display,
            )
        _connect_to_off_page(drawing, net_by_pin, swd.TDI, "BOOT0")
        _connect_to_off_page(drawing, net_by_pin, gpio[2], "BOOT0")
        _mark_no_connects(drawing, (usb_j, swd, gpio), net_by_pin)


def _author_mcu_region(
    region: volt.SchematicRegion,
    board: Stm32UsbBuckBoard,
    nets: dict[str, volt.Net],
    net_by_pin: dict[int, volt.Net],
) -> None:
    support = board.modules["SUPPORT"]
    led = board.modules["LED_STATUS"]
    with region.drawing(unit=20) as drawing:
        _rail_legend(drawing, nets, (18, 20), ("+3V3", "VDDA", "GND"))

        stm32 = drawing.place(
            board.components["U1"],
            at=(4, 34),
            symbol=_compact_stm32_symbol(),
            reference_label=_display_reference(board.components["U1"]),
        )
        cvdd = drawing.C(
            support.component("CVDD"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(support.component("CVDD")),
        ).at((14, 30)).down()
        cvcap1 = drawing.C(
            support.component("CVCAP1"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(support.component("CVCAP1")),
        ).at((30, 30)).down()
        cvcap2 = drawing.C(
            support.component("CVCAP2"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(support.component("CVCAP2")),
        ).at((46, 30)).down()
        rreset = drawing.R(
            support.component("RRESET"),
            symbol=TWO_TERMINAL_RESISTOR,
            reference_label=_display_reference(support.component("RRESET")),
        ).at((8, 138)).right()
        rboot = drawing.R(
            support.component("RBOOT"),
            symbol=TWO_TERMINAL_RESISTOR,
            reference_label=_display_reference(support.component("RBOOT")),
        ).at((8, 154)).right()
        swboot = drawing.place(
            support.component("SWBOOT"),
            at=(42, 140),
            symbol=lib.SPDT_SWITCH.schematic_symbol,
            reference_label=_display_reference(support.component("SWBOOT")),
        )
        crystal = drawing.place(
            support.component("Y1"),
            at=(20, 112),
            symbol=lib.CRYSTAL_GND24.schematic_symbol,
            reference_label=_display_reference(support.component("Y1")),
        )
        chsein = drawing.C(
            support.component("CHSEIN"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(support.component("CHSEIN")),
        ).at((60, 108)).down()
        chseout = drawing.C(
            support.component("CHSEOUT"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(support.component("CHSEOUT")),
        ).at((76, 108)).down()
        led_r = drawing.R(
            led.component("R"),
            symbol=TWO_TERMINAL_RESISTOR,
            reference_label=_display_reference(led.component("R")),
        ).at((44, 18)).right()
        led_d = drawing.LED(
            led.component("D"),
            symbol=_indicator_led_symbol(),
            reference_label=_display_reference(led.component("D")),
        ).at((62, 18)).right(1.2)

        stm32.label("STM32F405RGT6", name="value", loc="bottom", ofst=8, orient="Right")
        rreset.label("10 kOhm", name="value", loc="top", ofst=8, orient="Right")
        crystal.label("8 MHz", name="value", loc="bottom", ofst=22, orient="Right")

        for anchor in stm32.pins("VDD"):
            _rail(drawing, net_by_pin[anchor.pin.index], anchor, orient="Up")
        for anchor in (stm32.VBAT, stm32.VDDA):
            net = net_by_pin[anchor.pin.index]
            rail = drawing.power(
                _display_net_name(net.name),
                net=net,
                at=anchor.right(12),
                orient="Right",
            )
            drawing.connect(anchor, rail, net=net, shape="-")
        for anchor in stm32.pins("VSS"):
            _rail(drawing, net_by_pin[anchor.pin.index], anchor, orient="Down")
        vssa = drawing.ground(
            "GND",
            net=net_by_pin[stm32.VSSA.pin.index],
            at=stm32.VSSA.right(12),
            orient="Right",
        )
        drawing.connect(stm32.VSSA, vssa, net=net_by_pin[stm32.VSSA.pin.index], shape="-")

        for name, pin_name in (
            ("USB D-", "PA11"),
            ("USB D+", "PA12"),
            ("SWDIO", "PA13"),
            ("SWCLK", "PA14"),
            ("SWO", "PB3"),
            ("BOOT0", "BOOT0"),
            ("NRST", "NRST"),
            ("HSE IN", "PH0"),
            ("HSE OUT", "PH1"),
            ("VCAP1", "VCAP_1"),
            ("VCAP2", "VCAP_2"),
            ("LED", "PC13"),
        ):
            _connect_to_off_page(
                drawing,
                net_by_pin,
                stm32[pin_name],
                name,
            )

        support_vdd = nets["SUPPORT/VDD"]
        support_gnd = nets["SUPPORT/GND"]
        support_reset = nets["SUPPORT/NRST"]
        support_boot = nets["SUPPORT/BOOT0"]
        support_hse_in = nets["SUPPORT/HSE_IN"]
        support_hse_out = nets["SUPPORT/HSE_OUT"]

        for anchor in (cvdd.start, rreset.start, swboot.A):
            drawing.off_page("+3V3", net=support_vdd, at=anchor, orient="Right")

        for anchor in (
            cvdd.end,
            cvcap1.end,
            cvcap2.end,
            rboot.end,
            swboot.B,
            crystal[2],
            crystal[4],
            chsein.end,
            chseout.end,
        ):
            drawing.ground("GND", net=support_gnd, at=anchor, orient="Down")

        drawing.off_page("NRST", net=support_reset, at=rreset.end, orient="Right")
        drawing.connect(rboot.start, swboot.C, net=support_boot, shape="-")
        drawing.off_page("BOOT0", net=support_boot, at=(88, 150), orient="Left")

        for anchor in (crystal[1], chsein.start):
            drawing.off_page("HSE IN", net=support_hse_in, at=anchor, orient="Right")
        for anchor in (crystal[3], chseout.start):
            drawing.off_page("HSE OUT", net=support_hse_out, at=anchor, orient="Left")
        drawing.off_page("VCAP1", net=nets["SUPPORT/VCAP_1"], at=cvcap1.start, orient="Right")
        drawing.off_page("VCAP2", net=nets["SUPPORT/VCAP_2"], at=cvcap2.start, orient="Right")

        led_supply = nets["LED_STATUS/SUPPLY"]
        led_signal = nets["LED_STATUS/SIGNAL"]
        led_gnd = nets["LED_STATUS/GND"]
        led_supply_port = drawing.off_page(
            "+3V3",
            net=led_supply,
            at=led_r.start.up(12),
            orient="Right",
        )
        drawing.connect(led_r.start, led_supply_port, net=led_supply, shape="-")
        drawing.connect(led_r.end, led_d.start, net=led_signal, shape="-")
        drawing.off_page("LED", net=led_signal, at=(76, 18), orient="Left")
        led_ground = drawing.ground(
            "GND",
            net=led_gnd,
            at=led_d.end.down(12).left(6),
            orient="Left",
        )
        drawing.connect(led_d.end, led_ground, net=led_gnd, shape="-")


def _rail_legend(
    drawing: volt.SchematicDrawing,
    nets: dict[str, volt.Net],
    at: tuple[float, float],
    names: tuple[str, ...],
) -> None:
    x, y = at
    for index, name in enumerate(names):
        point = (x + index * 14, y)
        if name == "GND":
            drawing.ground("GND", net=nets[name], at=point, orient="Down")
        else:
            drawing.power(name, net=nets[name], at=point, orient="Up")


def _rail(
    drawing: volt.SchematicDrawing,
    net: volt.Net,
    at: volt.SchematicPinAnchor,
    *,
    orient: str = "Up",
) -> None:
    name = _display_net_name(net.name)
    if name == "GND":
        drawing.ground("GND", net=net, at=at, orient=orient)
    elif name == net.name and (name.startswith("+") or name == "VDDA"):
        drawing.power(name, net=net, at=at, orient=orient)
    else:
        drawing.off_page(name, net=net, at=at, orient=orient)


def _connect_to_off_page(
    drawing: volt.SchematicDrawing,
    net_by_pin: dict[int, volt.Net],
    anchor: volt.SchematicPinAnchor,
    name: str,
    *,
    distance: float = 10,
    label: bool = False,
) -> None:
    net = net_by_pin[anchor.pin.index]
    if anchor.orientation == "Left":
        port = drawing.off_page(name, net=net, at=anchor.right(distance), orient="Right")
    elif anchor.orientation == "Right":
        port = drawing.off_page(name, net=net, at=anchor.left(distance), orient="Left")
    elif anchor.orientation == "Up":
        port = drawing.off_page(name, net=net, at=anchor.down(distance), orient="Right")
    else:
        port = drawing.off_page(name, net=net, at=anchor.up(distance), orient="Right")
    drawing.connect(anchor, port, net=net, shape="-")
    if label:
        drawing.net_label(net, at=anchor.right(6))


def _mark_no_connects(
    drawing: volt.SchematicDrawing,
    placements: tuple[volt.PlacedSchematicElement, ...],
    net_by_pin: dict[int, volt.Net],
) -> None:
    for placement in placements:
        for anchor in placement.pin_anchors():
            if anchor.pin.index not in net_by_pin:
                drawing.no_connect(anchor, reason="intentionally unused")


def _display_net_name(name: str) -> str:
    local_name = name.rsplit("/", 1)[-1]
    return {
        "IN_12V": "+12V",
        "OUT_5V": "+5V",
        "OUT_3V3": "+3V3",
        "VDD": "+3V3",
        "SUPPLY": "+3V3",
        "VBUS": "+5V",
        "GNDDetect": "GND",
        "SIGNAL": "LED",
    }.get(local_name, local_name)
