"""Schematic projection for the STM32 USB buck benchmark."""

from __future__ import annotations

import volt
from volt.libraries import stm32_usb_buck as lib

from .stm32_board import Stm32UsbBuckBoard

# Use the generic two-terminal symbols so drawing.C/R can orient the passives.
TWO_TERMINAL_CAPACITOR = "capacitor"
TWO_TERMINAL_RESISTOR = "resistor"
SHEET_OPTIONS = {
    "size": (594, 420),
    "orientation": "landscape",
    "revision": "A",
    "date": "2026-05-18",
    "project": "Volt STM32 USB Buck",
    "margins": (16, 14, 16, 14),
    "coordinate_zones": (16, 10),
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
        x=18,
        y=18,
        w=158,
        h=350,
        style={"border": "dashed"},
    )
    mcu_region = sheet.region(
        "STM32 Microcontroller",
        x=190,
        y=18,
        w=210,
        h=350,
        style={"border": "dashed"},
    )
    connectors_region = sheet.region(
        "Connectors and USB",
        x=414,
        y=18,
        w=162,
        h=284,
        style={"border": "dashed"},
    )

    _author_power_region(power_region, board, nets)
    _author_mcu_region(mcu_region, board, nets)
    _author_connectors_region(connectors_region, board, nets)
    return sheet


def _author_power_region(
    region: volt.SchematicRegion,
    board: Stm32UsbBuckBoard,
    nets: dict[str, volt.Net],
) -> None:
    pwr = board.modules["PWR"]
    with region.drawing(unit=20) as drawing:
        vin = drawing.place(
            board.components["VIN_SRC"],
            at=(14, 42),
            symbol=_external_supply_symbol(),
            reference_label=_display_reference(board.components["VIN_SRC"]),
        )
        pwr_j = drawing.place(
            pwr.component("J"),
            at=(14, 138),
            symbol=_compact_connector_1x04_symbol(),
            reference_label=_display_reference(pwr.component("J")),
        )
        u5 = drawing.place(
            pwr.component("U5"),
            at=(46, 48),
            symbol=lib.AP1117_15.schematic_symbol,
            reference_label=_display_reference(pwr.component("U5")),
        )
        u3v3 = drawing.place(
            pwr.component("U3V3"),
            at=(46, 118),
            symbol=lib.AP1117_15.schematic_symbol,
            reference_label=_display_reference(pwr.component("U3V3")),
        )
        cin = drawing.C(
            pwr.component("CIN"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("CIN")),
        ).at((82, 164)).down()
        c5v = drawing.C(
            pwr.component("C5V"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("C5V")),
        ).at((92, 86)).down()
        c3v3 = drawing.C(
            pwr.component("C3V3"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("C3V3")),
        ).at((94, 156)).down()
        cvdda = drawing.C(
            pwr.component("CVDDA"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("CVDDA")),
        ).at((108, 156)).down()

        u5.label("AP1117-5.0", name="value", loc="bottom", ofst=10, orient="Right")
        u3v3.label("AP1117-3.3", name="value", loc="bottom", ofst=10, orient="Right")
        cin.label("4.7 uF", name="value", loc="top", ofst=8, orient="Right")
        cvdda.label("100 nF", name="value", loc="left", ofst=10, orient="Right")

        drawing.power("+12V", net=nets["+12V"], at=vin.OUT, orient="Right")
        drawing.ground("GND", net=nets["GND"], at=vin.GND, orient="Down")

        pwr_in = nets["PWR/IN_12V"]
        pwr_5v = nets["PWR/OUT_5V"]
        pwr_3v3 = nets["PWR/OUT_3V3"]
        pwr_vdda = nets["PWR/VDDA"]
        pwr_gnd = nets["PWR/GND"]

        drawing.connect(pwr_j[1], u5.VI, net=pwr_in, shape="-|-", k=24)
        drawing.connect(cin.start, u5.VI, net=pwr_in, shape="|-")
        drawing.junction(pwr_in, at=(60, 54))

        pwr_5v_port = drawing.power("+5V", net=pwr_5v, at=(112, 48), orient="Up")
        drawing.wire(pwr_5v).at(u5.VO).via((112, 58)).via((112, 102)).via(
            (36, 102)
        ).via((36, 128)).to(u3v3.VI).orthogonal()
        drawing.connect(u5.VO, pwr_5v_port, net=pwr_5v, shape="-|")
        drawing.connect(c5v.start, u5.VO, net=pwr_5v, shape="-|")

        drawing.connect(u3v3.VO, c3v3.start, net=pwr_3v3, shape="-|")
        drawing.power("+3V3", net=pwr_3v3, at=u3v3.VO.right(14), orient="Right")

        drawing.power("VDDA", net=pwr_vdda, at=cvdda.start.right(10), orient="Right")
        drawing.connect(cvdda.start, (108, 140), net=pwr_vdda, shape="-")
        drawing.net_label(nets["+12V"], at=(18, 222))

        ground_bus = (60, 214)
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
) -> None:
    usb = board.modules["USB"]
    with region.drawing(unit=20) as drawing:
        usb_j = drawing.place(
            usb.component("J1"),
            at=(20, 44),
            symbol=lib.USB_B_MICRO.schematic_symbol,
            reference_label=_display_reference(usb.component("J1")),
        )
        usb_esd = drawing.place(
            usb.component("U1"),
            at=(56, 46),
            symbol=_readable_usb_protection_symbol(),
            reference_label=_display_reference(usb.component("U1")),
        )
        swd = drawing.place(
            board.components["J2"],
            at=(52, 138),
            symbol=_compact_swd_symbol(),
            reference_label=_display_reference(board.components["J2"]),
        )
        gpio = drawing.place(
            board.components["J3"],
            at=(112, 170),
            symbol=_compact_connector_1x04_symbol(),
            reference_label=_display_reference(board.components["J3"]),
        )

        usb_j.label("USB Micro-B", name="value", loc="bottom", ofst=12, orient="Right")

        drawing.connect(usb_j.VBUS, usb_esd.VBUS, net=nets["USB/VBUS"], shape="-|", k=18)
        drawing.connect(usb_j["D+"], usb_esd["I/O1"], net=nets["USB/USB_DP"], shape="-|", k=18)
        drawing.wire(nets["USB/USB_DM"]).at(usb_j["D-"]).via((48, 58)).to(
            usb_esd["I/O2"]
        ).orthogonal()
        drawing.signal_stubs(
            (
                (nets["USB/MCU_USB_DP"], usb_esd["I/O4"], "USB D+"),
                (nets["USB/MCU_USB_DM"], usb_esd["I/O3"], "USB D-"),
            ),
            length=10,
            orient="Right",
        )

        usb_gnd = nets["USB/GND"]
        for anchor in (usb_j.GND, usb_j.Shield, usb_esd.GND):
            drawing.connect(anchor, (76, 94), net=usb_gnd, shape="|-")
        drawing.ground("GND", net=usb_gnd, at=(76, 94), orient="Down")
        drawing.junction(usb_gnd, at=(76, 94))

        debug_power = drawing.power("+3V3", net=nets["+3V3"], at=(142, 128), orient="Up")
        for anchor in (swd.VTref, gpio[1]):
            drawing.connect(anchor, debug_power, net=nets["+3V3"], shape="-|")
        debug_ground = drawing.ground("GND", net=nets["GND"], at=(92, 252), orient="Down")
        for anchor in (swd[3], swd[5], swd[9], gpio[4]):
            drawing.connect(anchor, debug_ground, net=nets["GND"], shape="-|")
        drawing.junction(nets["GND"], at=(92, 252))
        drawing.signal_stubs(
            (
                (nets["SWDIO"], swd.SWDIO, "SWDIO"),
                (nets["SWCLK"], swd.SWCLK, "SWCLK"),
                (nets["SWO"], swd.SWO, "SWO"),
                (nets["NRST"], swd.nRESET, "NRST"),
                (nets["BOOT0"], swd.TDI, "BOOT0"),
                (nets["BOOT0"], gpio[2], "BOOT0"),
            ),
            length=12,
            orient="Right",
        )
        drawing.no_connect(usb_j.ID, reason="USB ID not used")
        drawing.no_connect(swd.NC, reason="reserved debug pin not populated")
        drawing.no_connect(gpio[3], reason="reserved GPIO header pin not populated")


def _author_mcu_region(
    region: volt.SchematicRegion,
    board: Stm32UsbBuckBoard,
    nets: dict[str, volt.Net],
) -> None:
    support = board.modules["SUPPORT"]
    led = board.modules["LED_STATUS"]
    with region.drawing(unit=20) as drawing:
        stm32 = drawing.place(
            board.components["U1"],
            at=(56, 86),
            symbol=_compact_stm32_symbol(),
            reference_label=_display_reference(board.components["U1"]),
        )
        cvdd = drawing.C(
            support.component("CVDD"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(support.component("CVDD")),
        ).at((28, 36)).down()
        cvcap1 = drawing.C(
            support.component("CVCAP1"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(support.component("CVCAP1")),
        ).at((56, 36)).down()
        cvcap2 = drawing.C(
            support.component("CVCAP2"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(support.component("CVCAP2")),
        ).at((84, 36)).down()
        rreset = drawing.R(
            support.component("RRESET"),
            symbol=TWO_TERMINAL_RESISTOR,
            reference_label=_display_reference(support.component("RRESET")),
        ).at((28, 294)).right()
        rboot = drawing.R(
            support.component("RBOOT"),
            symbol=TWO_TERMINAL_RESISTOR,
            reference_label=_display_reference(support.component("RBOOT")),
        ).at((28, 324)).right()
        swboot = drawing.place(
            support.component("SWBOOT"),
            at=(82, 306),
            symbol=lib.SPDT_SWITCH.schematic_symbol,
            reference_label=_display_reference(support.component("SWBOOT")),
        )
        crystal = drawing.place(
            support.component("Y1"),
            at=(36, 236),
            symbol=lib.CRYSTAL_GND24.schematic_symbol,
            reference_label=_display_reference(support.component("Y1")),
        )
        chsein = drawing.C(
            support.component("CHSEIN"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(support.component("CHSEIN")),
        ).at((92, 232)).down()
        chseout = drawing.C(
            support.component("CHSEOUT"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(support.component("CHSEOUT")),
        ).at((122, 232)).down()
        led_r = drawing.R(
            led.component("R"),
            symbol=TWO_TERMINAL_RESISTOR,
            reference_label=_display_reference(led.component("R")),
        ).at((126, 46)).right()
        led_d = drawing.LED(
            led.component("D"),
            symbol=_indicator_led_symbol(),
            reference_label=_display_reference(led.component("D")),
        ).at((152, 46)).right(1.2)

        stm32.label("STM32F405RGT6", name="value", loc="bottom", ofst=8, orient="Right")
        rreset.label("10 kOhm", name="value", loc="top", ofst=8, orient="Right")
        crystal.label("8 MHz", name="value", loc="bottom", ofst=22, orient="Right")

        mcu_3v3 = nets["+3V3"]
        mcu_power = drawing.power("+3V3", net=mcu_3v3, at=(98, 76), orient="Up")
        for anchor in (*stm32.pins("VDD"), stm32.VBAT):
            drawing.connect(anchor, mcu_power, net=mcu_3v3, shape="|-")
        mcu_vdda = drawing.power("VDDA", net=nets["VDDA"], at=stm32.VDDA.left(16), orient="Left")
        drawing.connect(stm32.VDDA, mcu_vdda, net=nets["VDDA"], shape="-")

        mcu_ground = drawing.ground("GND", net=nets["GND"], at=(98, 222), orient="Down")
        for anchor in (*stm32.pins("VSS"), stm32.VSSA):
            drawing.connect(anchor, mcu_ground, net=nets["GND"], shape="|-")
        drawing.junction(nets["GND"], at=(98, 222))

        drawing.signal_stubs(
            (
                (nets["MCU_USB_DM"], stm32.PA11, "USB D-"),
                (nets["MCU_USB_DP"], stm32.PA12, "USB D+"),
                (nets["SWDIO"], stm32.PA13, "SWDIO"),
                (nets["SWCLK"], stm32.PA14, "SWCLK"),
                (nets["SWO"], stm32.PB3, "SWO"),
                (nets["BOOT0"], stm32.BOOT0, "BOOT0"),
                (nets["NRST"], stm32.NRST, "NRST"),
                (nets["HSE_IN"], stm32.PH0, "HSE IN"),
                (nets["HSE_OUT"], stm32.PH1, "HSE OUT"),
                (nets["VCAP_1"], stm32.VCAP_1, "VCAP1"),
                (nets["VCAP_2"], stm32.VCAP_2, "VCAP2"),
                (nets["STATUS_LED"], stm32.PC13, "LED"),
            ),
            length=14,
            orient="Right",
        )

        support_vdd = nets["SUPPORT/VDD"]
        support_gnd = nets["SUPPORT/GND"]
        support_reset = nets["SUPPORT/NRST"]
        support_boot = nets["SUPPORT/BOOT0"]
        support_hse_in = nets["SUPPORT/HSE_IN"]
        support_hse_out = nets["SUPPORT/HSE_OUT"]

        drawing.signal_stub(
            support_vdd,
            at=cvdd.start,
            side="Up",
            length=8,
            orient="Right",
            label="+3V3",
        )
        for anchor in (rreset.start, swboot.A):
            rail = drawing.power("+3V3", net=support_vdd, at=anchor.up(10), orient="Up")
            drawing.connect(anchor, rail, net=support_vdd, shape="-")

        support_ground = drawing.ground("GND", net=support_gnd, at=(18, 330), orient="Down")
        for anchor in (
            cvdd.end,
            cvcap1.end,
            swboot.B,
            crystal[2],
            crystal[4],
            chsein.end,
        ):
            drawing.connect(anchor, support_ground, net=support_gnd, shape="-|")
        drawing.ground("GND", net=support_gnd, at=cvcap2.end, orient="Down")
        drawing.ground("GND", net=support_gnd, at=rboot.end, orient="Down")
        drawing.ground("GND", net=support_gnd, at=chseout.end, orient="Down")
        drawing.junction(support_gnd, at=(18, 330))

        drawing.signal_stub(
            support_reset,
            at=rreset.end,
            side="Right",
            length=12,
            orient="Right",
            label="NRST",
        )
        drawing.wire(support_boot).at(rboot.start).via((28, 314)).via((126, 314)).to(
            swboot.C
        ).orthogonal()
        drawing.signal_stub(
            support_boot,
            at=rboot.start,
            side="Left",
            length=12,
            orient="Right",
            label="BOOT0",
        )

        drawing.signal_stub(
            support_hse_in,
            at=crystal[1],
            side="Left",
            length=12,
            orient="Right",
            label="HSE IN",
        )
        drawing.signal_stub(
            support_hse_out,
            at=crystal[3],
            side="Right",
            length=12,
            orient="Right",
            label="HSE OUT",
        )
        drawing.signal_stub(
            support_hse_in,
            at=chsein.start,
            side="Right",
            length=10,
            orient="Right",
            label="HSE IN",
        )
        drawing.signal_stub(
            support_hse_out,
            at=chseout.start,
            side="Right",
            length=10,
            orient="Right",
            label="HSE OUT",
        )
        drawing.signal_stub(
            nets["SUPPORT/VCAP_1"],
            at=cvcap1.start,
            side="Right",
            length=10,
            orient="Right",
            label="VCAP1",
        )
        drawing.signal_stub(
            nets["SUPPORT/VCAP_2"],
            at=cvcap2.start,
            side="Right",
            length=10,
            orient="Right",
            label="VCAP2",
        )

        led_supply = nets["LED_STATUS/SUPPLY"]
        led_signal = nets["LED_STATUS/SIGNAL"]
        led_gnd = nets["LED_STATUS/GND"]
        led_supply_port = drawing.power(
            "+3V3",
            net=led_supply,
            at=led_r.start.up(12),
            orient="Up",
        )
        drawing.connect(led_r.start, led_supply_port, net=led_supply, shape="-")
        drawing.connect(led_r.end, led_d.start, net=led_signal, shape="-")
        drawing.signal_stub(
            led_signal,
            at=led_r.end,
            side="Up",
            length=10,
            orient="Right",
            label="LED",
        )
        led_ground = drawing.ground(
            "GND",
            net=led_gnd,
            at=led_d.end.down(12).left(6),
            orient="Left",
        )
        drawing.connect(led_d.end, led_ground, net=led_gnd, shape="-")
