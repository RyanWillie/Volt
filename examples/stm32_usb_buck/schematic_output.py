"""Schematic projection for the STM32 USB buck benchmark."""

from __future__ import annotations

import volt
from volt.libraries import stm32_usb_buck as lib

from .stm32_board import Stm32UsbBuckBoard

SHEET_OPTIONS = {
    "size": (594, 420),
    "orientation": "landscape",
    "revision": "A",
    "date": "2026-05-18",
    "project": "Volt STM32 USB Buck",
    "margins": (16, 14, 16, 14),
    "coordinate_zones": (16, 10),
    "grid": {"spacing": 5, "visible": False},
}
SHEET_FILE = "examples/stm32_usb_buck/schematic_output.py"
DISPLAY_REFERENCES = {
    "VIN_SRC": "J1",
    "PWR/J": "J2",
    "PWR/U5": "U2",
    "PWR/U3V3": "U3",
    "PWR/CIN": "C1",
    "PWR/C5V": "C2",
    "PWR/C3V3": "C3",
    "PWR/CVDDA": "C4",
    "U1": "U1",
    "SUPPORT/CVDD": "C5",
    "SUPPORT/CVCAP1": "C6",
    "SUPPORT/CVCAP2": "C7",
    "SUPPORT/RRESET": "R1",
    "SUPPORT/RBOOT": "R2",
    "SUPPORT/SWBOOT": "SW1",
    "SUPPORT/Y1": "Y1",
    "SUPPORT/CHSEIN": "C8",
    "SUPPORT/CHSEOUT": "C9",
    "LED_STATUS/R": "R3",
    "LED_STATUS/D": "D1",
    "USB/J1": "J3",
    "USB/U1": "U4",
    "J2": "J4",
    "J3": "J5",
}


def _two_terminal_pins() -> tuple[volt.SchematicSymbolPinSpec, volt.SchematicSymbolPinSpec]:
    return (
        volt.SchematicSymbolSpec.pin("1", 1, (0, 0), "Left"),
        volt.SchematicSymbolSpec.pin("2", 2, (20, 0), "Right"),
    )


TWO_TERMINAL_CAPACITOR = volt.SchematicSymbolSpec(
    "volt.examples.stm32_usb_buck:PlainCapacitor",
    pins=_two_terminal_pins(),
    primitives=(
        volt.SchematicSymbolSpec.line((0, 0), (8, 0)),
        volt.SchematicSymbolSpec.line((8, -5), (8, 5)),
        volt.SchematicSymbolSpec.line((12, -5), (12, 5)),
        volt.SchematicSymbolSpec.line((12, 0), (20, 0)),
    ),
)
TWO_TERMINAL_RESISTOR = volt.SchematicSymbolSpec(
    "volt.examples.stm32_usb_buck:PlainResistor",
    pins=_two_terminal_pins(),
    primitives=(
        volt.SchematicSymbolSpec.line((0, 0), (4, 0)),
        volt.SchematicSymbolSpec.rectangle((4, -3), (16, 3)),
        volt.SchematicSymbolSpec.line((16, 0), (20, 0)),
    ),
)


def _display_reference(component: volt.Component) -> str:
    try:
        return DISPLAY_REFERENCES[component.reference]
    except KeyError as error:
        raise ValueError(f"No schematic display reference for {component.reference!r}") from error


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
            volt.SchematicSymbolSpec.text("1 +12V", (4, 9)),
            volt.SchematicSymbolSpec.text("2 GND", (4, 25)),
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
    pin_labels = tuple(
        volt.SchematicSymbolSpec.text(str(index), (12, (index - 1) * 7 + 1))
        for index in range(1, 5)
    )
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
            *pin_labels,
        ),
    )


def _compact_swd_symbol() -> volt.SchematicSymbolSpec:
    pin_rows = (
        ("VTref", "1 VTref"),
        ("SWDIO", "2 SWDIO"),
        ("GND", "3 GND"),
        ("SWCLK", "4 SWCLK"),
        ("GND", "5 GND"),
        ("SWO", "6 SWO"),
        ("NC", "7 NC"),
        ("TDI", "8 BOOT0"),
        ("GNDDetect", "9 GNDDET"),
        ("nRESET", "10 NRST"),
    )
    pins = []
    primitives = [
        volt.SchematicSymbolSpec.rectangle((8, -4), (58, 49)),
        volt.SchematicSymbolSpec.text("SWD", (33, -9)),
    ]
    for index, (name, label) in enumerate(pin_rows, start=1):
        y = (index - 1) * 5
        pins.append(volt.SchematicSymbolSpec.pin(name, index, (0, y), "Left"))
        primitives.append(volt.SchematicSymbolSpec.line((0, y), (8, y)))
        primitives.append(volt.SchematicSymbolSpec.text(label, (10, y + 1)))
    return volt.SchematicSymbolSpec(
        "volt.examples.stm32_usb_buck:CompactSWD10",
        pins=tuple(pins),
        primitives=tuple(primitives),
    )


def _readable_usb_micro_b_symbol() -> volt.SchematicSymbolSpec:
    pin_rows = (
        ("VBUS", 1, "1 VBUS"),
        ("D-", 2, "2 D-"),
        ("D+", 3, "3 D+"),
        ("ID", 4, "4 ID"),
        ("GND", 5, "5 GND"),
        ("Shield", 6, "6 SHLD"),
    )
    pins = []
    primitives = [
        volt.SchematicSymbolSpec.rectangle((10, -5), (48, 45)),
        volt.SchematicSymbolSpec.text("USB", (29, -10)),
    ]
    for index, (name, number, label) in enumerate(pin_rows):
        y = index * 8
        pins.append(volt.SchematicSymbolSpec.pin(name, number, (0, y), "Left"))
        primitives.append(volt.SchematicSymbolSpec.line((0, y), (10, y)))
        primitives.append(volt.SchematicSymbolSpec.text(label, (12, y + 1)))
    return volt.SchematicSymbolSpec(
        "volt.examples.stm32_usb_buck:ReadableUSBMicroB",
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
            volt.SchematicSymbolSpec.text("D-", (12, 9)),
            volt.SchematicSymbolSpec.text("D+", (12, 21)),
            volt.SchematicSymbolSpec.text("D-", (36, 9)),
            volt.SchematicSymbolSpec.text("D+", (36, 21)),
        ),
    )


def _vertical_hse_crystal_symbol() -> volt.SchematicSymbolSpec:
    return volt.SchematicSymbolSpec(
        "volt.examples.stm32_usb_buck:VerticalHSECrystal",
        pins=(
            volt.SchematicSymbolSpec.pin("1", 1, (46, 0), "Right"),
            volt.SchematicSymbolSpec.pin("3", 3, (46, 16), "Right"),
            volt.SchematicSymbolSpec.pin("GND", 2, (18, 34), "Down"),
            volt.SchematicSymbolSpec.pin("GND", 4, (30, 34), "Down"),
        ),
        primitives=(
            volt.SchematicSymbolSpec.line((34, 0), (46, 0)),
            volt.SchematicSymbolSpec.line((34, 16), (46, 16)),
            volt.SchematicSymbolSpec.line((18, 20), (18, 34)),
            volt.SchematicSymbolSpec.line((30, 20), (30, 34)),
            volt.SchematicSymbolSpec.line((16, 2), (16, 18)),
            volt.SchematicSymbolSpec.line((30, 2), (30, 18)),
            volt.SchematicSymbolSpec.rectangle((12, 4), (34, 16)),
            volt.SchematicSymbolSpec.text("XTAL", (23, -7)),
        ),
    )


def _compact_stm32_symbol() -> volt.SchematicSymbolSpec:
    pin_layout = (
        ("VBAT", 1, (0, 16), "Left"),
        ("PC13", 2, (0, 32), "Left"),
        ("PH0", 5, (0, 54), "Left"),
        ("PH1", 6, (0, 70), "Left"),
        ("NRST", 7, (0, 104), "Left"),
        ("VDDA", 13, (0, 126), "Left"),
        ("VCAP_1", 31, (0, 142), "Left"),
        ("VDD", 19, (28, 0), "Up"),
        ("VDD", 32, (40, 0), "Up"),
        ("VDD", 48, (52, 0), "Up"),
        ("VDD", 64, (64, 0), "Up"),
        ("VSS", 18, (34, 156), "Down"),
        ("VSSA", 12, (50, 156), "Down"),
        ("VSS", 63, (66, 156), "Down"),
        ("PA11", 44, (92, 42), "Right"),
        ("PA12", 45, (92, 54), "Right"),
        ("PA13", 46, (92, 74), "Right"),
        ("PA14", 49, (92, 86), "Right"),
        ("PB3", 55, (92, 98), "Right"),
        ("BOOT0", 60, (92, 126), "Right"),
        ("VCAP_2", 47, (92, 142), "Right"),
    )
    primitives = [
        volt.SchematicSymbolSpec.rectangle((14, 8), (78, 148)),
        volt.SchematicSymbolSpec.text("STM32F405", (46, 20)),
    ]
    pins = []
    for name, number, at, orient in pin_layout:
        x, y = at
        pins.append(volt.SchematicSymbolSpec.pin(name, number, at, orient))
        if orient == "Left":
            primitives.append(volt.SchematicSymbolSpec.line((0, y), (14, y)))
            primitives.append(
                volt.SchematicSymbolSpec.text(f"{name.replace('_', '')} {number}", (16, y + 1))
            )
        elif orient == "Right":
            primitives.append(volt.SchematicSymbolSpec.line((78, y), (92, y)))
            primitives.append(
                volt.SchematicSymbolSpec.text(f"{name.replace('_', '')} {number}", (48, y + 1))
            )
        elif orient == "Up":
            primitives.append(volt.SchematicSymbolSpec.line((x, 0), (x, 8)))
        else:
            primitives.append(volt.SchematicSymbolSpec.line((x, 148), (x, 156)))
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
        w=220,
        h=350,
        title="STM32 MCU",
        style={"border": "dashed"},
    )
    connectors_region = sheet.region(
        "Connectors and USB",
        x=424,
        y=18,
        w=152,
        h=350,
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
        with drawing.frame((8, 26)):
            vin = drawing.place(
                board.components["VIN_SRC"],
                at=(0, 0),
                symbol=_external_supply_symbol(),
                reference_label=_display_reference(board.components["VIN_SRC"]),
            )
        with drawing.frame((8, 58)):
            pwr_j = drawing.place(
                pwr.component("J"),
                at=(0, 0),
                symbol=_compact_connector_1x04_symbol(),
                reference_label=_display_reference(pwr.component("J")),
            )

        with drawing.frame((76, 48)):
            u5 = drawing.place(
                pwr.component("U5"),
                at=(0, 0),
                symbol=lib.AP1117_15.schematic_symbol,
                reference_label=_display_reference(pwr.component("U5")),
            )
        with drawing.frame((76, 142)):
            u3v3 = drawing.place(
                pwr.component("U3V3"),
                at=(0, 0),
                symbol=lib.AP1117_15.schematic_symbol,
                reference_label=_display_reference(pwr.component("U3V3")),
            )

        with drawing.frame((0, 0)):
            cin = drawing.C(
                pwr.component("CIN"),
                symbol=TWO_TERMINAL_CAPACITOR,
                reference_label=_display_reference(pwr.component("CIN")),
            ).at((58, 58)).down()
            c5v = drawing.C(
                pwr.component("C5V"),
                symbol=TWO_TERMINAL_CAPACITOR,
                reference_label=_display_reference(pwr.component("C5V")),
            ).at((138, 58)).down()
            c3v3 = drawing.C(
                pwr.component("C3V3"),
                symbol=TWO_TERMINAL_CAPACITOR,
                reference_label=_display_reference(pwr.component("C3V3")),
            ).at((138, 152)).down()
            cvdda = drawing.C(
                pwr.component("CVDDA"),
                symbol=TWO_TERMINAL_CAPACITOR,
                reference_label=_display_reference(pwr.component("CVDDA")),
            ).at((104, 214)).down()

        vin.label_value(loc="bottom", ofst=10, orient="Right")
        u5.label_value(loc="bottom", ofst=10, orient="Right")
        u3v3.label_value(loc="bottom", ofst=16, orient="Right")
        cin.label_value(loc="bottom", ofst=16, orient="Right")
        c5v.label_value(loc="bottom", ofst=10, orient="Right")
        c3v3.label_value(loc="bottom", ofst=10, orient="Right")
        cvdda.label_value(loc="bottom", ofst=10, orient="Right")

        input_supply = drawing.power("+12V", net=nets["+12V"], at=vin.OUT.up(28), orient="Up")
        drawing.connect(vin.OUT, input_supply, net=nets["+12V"], shape="-")
        input_ground = drawing.ground("GND", net=nets["GND"], at=vin.GND.down(40), orient="Down")
        drawing.connect(vin.GND, input_ground, net=nets["GND"], shape="-")

        pwr_in = nets["PWR/IN_12V"]
        pwr_5v = nets["PWR/OUT_5V"]
        pwr_3v3 = nets["PWR/OUT_3V3"]
        pwr_vdda = nets["PWR/VDDA"]
        pwr_gnd = nets["PWR/GND"]

        drawing.connect(pwr_j[1], cin.start, net=pwr_in, shape="-")
        drawing.connect(cin.start, u5.VI, net=pwr_in, shape="-")
        drawing.net_label(pwr_in, at=cin.start.up(20), label="+12V")

        drawing.connect(u5.VO, c5v.start, net=pwr_5v, shape="-")
        drawing.power("+5V", net=pwr_5v, at=c5v.start.up(30), orient="Up")
        u3v3_input = drawing.power("+5V", net=pwr_5v, at=u3v3.VI.left(18), orient="Left")
        drawing.connect(u3v3.VI, u3v3_input, net=pwr_5v, shape="-")

        drawing.connect(u3v3.VO, c3v3.start, net=pwr_3v3, shape="-")
        drawing.power("+3V3", net=pwr_3v3, at=c3v3.start.up(30), orient="Up")
        vdda_port = drawing.power("VDDA", net=pwr_vdda, at=cvdda.start.up(20), orient="Up")
        drawing.connect(cvdda.start, vdda_port, net=pwr_vdda, shape="-")

        input_ground_bus = drawing.ground("GND", net=pwr_gnd, at=(66, 114), orient="Down")
        for anchor in (pwr_j[2], pwr_j[3], pwr_j[4], cin.end, u5.GND, c5v.end):
            drawing.connect(anchor, input_ground_bus, net=pwr_gnd, shape="|-")
        drawing.junction(pwr_gnd, at=(66, 114))

        output_ground_bus = drawing.ground("GND", net=pwr_gnd, at=(132, 196), orient="Down")
        for anchor in (u3v3.GND, c3v3.end):
            drawing.connect(anchor, output_ground_bus, net=pwr_gnd, shape="|-")
        vdda_ground = drawing.ground("GND", net=pwr_gnd, at=cvdda.end.down(18), orient="Down")
        drawing.connect(cvdda.end, vdda_ground, net=pwr_gnd, shape="-")


def _author_connectors_region(
    region: volt.SchematicRegion,
    board: Stm32UsbBuckBoard,
    nets: dict[str, volt.Net],
) -> None:
    usb = board.modules["USB"]
    with region.drawing(unit=20) as drawing:
        with drawing.frame((12, 42)):
            usb_j = drawing.place(
                usb.component("J1"),
                at=(0, 0),
                symbol=_readable_usb_micro_b_symbol(),
                reference_label=_display_reference(usb.component("J1")),
            )
            usb_esd = drawing.place(
                usb.component("U1"),
                at=(54, 2),
                symbol=_readable_usb_protection_symbol(),
                reference_label=_display_reference(usb.component("U1")),
            )

        with drawing.frame((16, 156)):
            swd = drawing.place(
                board.components["J2"],
                at=(0, 0),
                symbol=_compact_swd_symbol(),
                reference_label=_display_reference(board.components["J2"]),
            )

        with drawing.frame((102, 226)):
            gpio = drawing.place(
                board.components["J3"],
                at=(0, 0),
                symbol=_compact_connector_1x04_symbol(),
                reference_label=_display_reference(board.components["J3"]),
            )

        usb_j.label("USB Micro-B", name="value", loc="bottom", ofst=12, orient="Right")
        usb_esd.label("USBLC6-4SC6", name="value", loc="bottom", ofst=12, orient="Right")
        swd.label("SWD 10-pin", name="value", loc="bottom", ofst=10, orient="Right")
        gpio.label("GPIO 1x4", name="value", loc="bottom", ofst=10, orient="Right")

        drawing.connect(usb_j.VBUS, usb_esd.VBUS, net=nets["USB/VBUS"], shape="-|", k=24)
        drawing.connect(usb_j["D+"], usb_esd["I/O1"], net=nets["USB/USB_DP"], shape="-|", k=24)
        drawing.connect(usb_j["D-"], usb_esd["I/O2"], net=nets["USB/USB_DM"], shape="-|", k=24)
        drawing.signal_stubs(
            (
                (nets["USB/MCU_USB_DP"], usb_esd["I/O4"], "USB D+"),
                (nets["USB/MCU_USB_DM"], usb_esd["I/O3"], "USB D-"),
            ),
            length=10,
            orient="Right",
        )

        usb_gnd = nets["USB/GND"]
        usb_ground_point = (92, 108)
        for anchor in (usb_j.GND, usb_j.Shield, usb_esd.GND):
            drawing.connect(anchor, usb_ground_point, net=usb_gnd, shape="|-")
        drawing.ground("GND", net=usb_gnd, at=usb_ground_point, orient="Down")
        drawing.junction(usb_gnd, at=usb_ground_point)

        for anchor in (swd.VTref, gpio[1]):
            power = drawing.power("+3V3", net=nets["+3V3"], at=anchor.up(24), orient="Up")
            drawing.connect(anchor, power, net=nets["+3V3"], shape="-")
        debug_ground = drawing.ground("GND", net=nets["GND"], at=swd[9].right(36).down(30), orient="Down")
        for anchor in (swd[3], swd[5], swd[9]):
            drawing.connect(anchor, debug_ground, net=nets["GND"], shape="-|")
        gpio_ground = drawing.ground("GND", net=nets["GND"], at=gpio[4].down(18), orient="Down")
        drawing.connect(gpio[4], gpio_ground, net=nets["GND"], shape="-")
        drawing.signal_stubs(
            (
                (nets["SWDIO"], swd.SWDIO, "SWDIO"),
                (nets["SWCLK"], swd.SWCLK, "SWCLK"),
                (nets["SWO"], swd.SWO, "SWO"),
                (nets["NRST"], swd.nRESET, "NRST"),
                (nets["BOOT0"], swd.TDI, "BOOT0"),
            ),
            length=12,
            orient="Right",
        )
        drawing.signal_stub(
            nets["BOOT0"],
            at=gpio[2],
            side="Left",
            length=10,
            label="BOOT0",
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
        with drawing.frame((82, 100)):
            stm32 = drawing.place(
                board.components["U1"],
                at=(0, 0),
                symbol=_compact_stm32_symbol(),
                reference_label=_display_reference(board.components["U1"]),
            )

        with drawing.frame((26, 54)):
            cvdd_anchor, cvcap1_anchor, cvcap2_anchor = drawing.stack(
                count=3, direction="Right", pitch=30
            )
            cvdd = drawing.C(
                support.component("CVDD"),
                symbol=TWO_TERMINAL_CAPACITOR,
                reference_label=_display_reference(support.component("CVDD")),
            ).at(cvdd_anchor).down()
            cvcap1 = drawing.C(
                support.component("CVCAP1"),
                symbol=TWO_TERMINAL_CAPACITOR,
                reference_label=_display_reference(support.component("CVCAP1")),
            ).at(cvcap1_anchor).down()
            cvcap2 = drawing.C(
                support.component("CVCAP2"),
                symbol=TWO_TERMINAL_CAPACITOR,
                reference_label=_display_reference(support.component("CVCAP2")),
            ).at(cvcap2_anchor).down()

        with drawing.frame((56, 176)):
            rreset = drawing.R(
                support.component("RRESET"),
                symbol=TWO_TERMINAL_RESISTOR,
                reference_label=_display_reference(support.component("RRESET")),
            ).at((0, 0)).down(1.4)

        with drawing.frame((158, 270)):
            rboot = drawing.R(
                support.component("RBOOT"),
                symbol=TWO_TERMINAL_RESISTOR,
                reference_label=_display_reference(support.component("RBOOT")),
            ).at((42, 10)).down(1.1)
            swboot = drawing.place(
                support.component("SWBOOT"),
                at=(0, 0),
                orient="Right",
                symbol=lib.SPDT_SWITCH.schematic_symbol,
                reference_label=_display_reference(support.component("SWBOOT")),
            )

        with drawing.frame((12, 154)):
            crystal = drawing.place(
                support.component("Y1"),
                at=(0, 0),
                symbol=_vertical_hse_crystal_symbol(),
                reference_label=_display_reference(support.component("Y1")),
            )
            chsein = drawing.C(
                support.component("CHSEIN"),
                symbol=TWO_TERMINAL_CAPACITOR,
                reference_label=_display_reference(support.component("CHSEIN")),
            ).at(crystal[1].left(18)).down()
            chseout = drawing.C(
                support.component("CHSEOUT"),
                symbol=TWO_TERMINAL_CAPACITOR,
                reference_label=_display_reference(support.component("CHSEOUT")),
            ).at(crystal[3].left(18)).down()

        with drawing.frame((128, 40)):
            led_r = drawing.R(
                led.component("R"),
                symbol=TWO_TERMINAL_RESISTOR,
                reference_label=_display_reference(led.component("R")),
            ).at((0, 0)).right()
            led_d = drawing.LED(
                led.component("D"),
                symbol=_indicator_led_symbol(),
                reference_label=_display_reference(led.component("D")),
            ).at(led_r.end.right(6)).right(1.2)

        stm32.label_value(loc="bottom", ofst=22, orient="Right")
        cvdd.label_value(loc="bottom", ofst=8, orient="Right")
        cvcap1.label_value(loc="bottom", ofst=8, orient="Right")
        cvcap2.label_value(loc="bottom", ofst=8, orient="Right")
        rreset.label_value(loc="bottom", ofst=10, orient="Right")
        rboot.label_value(loc="bottom", ofst=10, orient="Right")
        swboot.label_value(loc="bottom", ofst=10, orient="Right")
        crystal.label_value(loc="bottom", ofst=22, orient="Right")
        chsein.label_value(loc="bottom", ofst=8, orient="Right")
        chseout.label_value(loc="bottom", ofst=8, orient="Right")
        led_r.label_value(loc="bottom", ofst=10, orient="Right")
        led_d.label_value(loc="bottom", ofst=8, orient="Right")

        mcu_3v3 = nets["+3V3"]
        mcu_power = drawing.power("+3V3", net=mcu_3v3, at=stm32.center.up(98), orient="Up")
        for anchor in (*stm32.pins("VDD"), stm32.VBAT):
            drawing.connect(anchor, mcu_power, net=mcu_3v3, shape="|-")
        mcu_vdda = drawing.power("VDDA", net=nets["VDDA"], at=stm32.VDDA.left(16), orient="Left")
        drawing.connect(stm32.VDDA, mcu_vdda, net=nets["VDDA"], shape="-")

        mcu_ground = drawing.ground("GND", net=nets["GND"], at=(128, 286), orient="Down")
        for anchor in (*stm32.pins("VSS"), stm32.VSSA):
            drawing.connect(anchor, mcu_ground, net=nets["GND"], shape="|-")
        drawing.junction(nets["GND"], at=(128, 286))

        drawing.signal_stubs(
            (
                (nets["MCU_USB_DM"], stm32.PA11, "USB D-"),
                (nets["MCU_USB_DP"], stm32.PA12, "USB D+"),
                (nets["SWDIO"], stm32.PA13, "SWDIO"),
                (nets["SWCLK"], stm32.PA14, "SWCLK"),
                (nets["SWO"], stm32.PB3, "SWO"),
                (nets["BOOT0"], stm32.BOOT0, "BOOT0"),
            ),
            length=10,
            orient="Right",
        )
        for net, anchor, label in (
            (nets["STATUS_LED"], stm32.PC13, "LED"),
            (nets["HSE_IN"], stm32.PH0, "HSE IN"),
            (nets["HSE_OUT"], stm32.PH1, "HSE OUT"),
            (nets["VCAP_1"], stm32.VCAP_1, "VCAP1"),
        ):
            drawing.signal_stub(net, at=anchor, side="Left", length=16, label=label, orient="Right")
        drawing.signal_stub(
            nets["NRST"],
            at=stm32.NRST,
            side="Left",
            length=8,
            label="NRST",
            orient="Right",
        )
        drawing.signal_stub(nets["VCAP_2"], at=stm32.VCAP_2, side="Right", length=10, label="VCAP2")

        support_vdd = nets["SUPPORT/VDD"]
        support_gnd = nets["SUPPORT/GND"]
        support_reset = nets["SUPPORT/NRST"]
        support_boot = nets["SUPPORT/BOOT0"]
        support_hse_in = nets["SUPPORT/HSE_IN"]
        support_hse_out = nets["SUPPORT/HSE_OUT"]

        support_vdd_port = drawing.power("+3V3", net=support_vdd, at=cvdd.start.up(16), orient="Up")
        drawing.connect(cvdd.start, support_vdd_port, net=support_vdd, shape="-")
        for anchor in (rreset.start, swboot.A):
            rail = anchor.up(16)
            drawing.connect(anchor, rail, net=support_vdd, shape="-")

        decoupling_ground = drawing.ground("GND", net=support_gnd, at=cvcap1.end.down(18), orient="Down")
        for cap in (cvdd, cvcap1, cvcap2):
            drawing.connect(cap.end, decoupling_ground, net=support_gnd, shape="|-")
        drawing.junction(support_gnd, at=decoupling_ground)

        oscillator_ground = drawing.ground("GND", net=support_gnd, at=crystal[4].down(22), orient="Down")
        for anchor in (crystal[2], crystal[4], chsein.end, chseout.end):
            drawing.connect(anchor, oscillator_ground, net=support_gnd, shape="|-")
        drawing.junction(support_gnd, at=oscillator_ground)

        rboot_ground = rboot.end.down(14)
        drawing.connect(rboot.end, rboot_ground, net=support_gnd, shape="-")
        switch_ground = swboot.B.down(14)
        drawing.connect(swboot.B, switch_ground, net=support_gnd, shape="-")

        drawing.signal_stub(
            support_reset,
            at=rreset.end,
            side="Left",
            length=10,
            label="NRST",
            orient="Right",
        )
        drawing.connect(swboot.C, rboot.start, net=support_boot, shape="-")

        drawing.connect(crystal[1], chsein.start, net=support_hse_in, shape="-")
        drawing.connect(crystal[3], chseout.start, net=support_hse_out, shape="-")
        for cap, net, label in (
            (cvcap1, nets["SUPPORT/VCAP_1"], "VCAP1"),
            (cvcap2, nets["SUPPORT/VCAP_2"], "VCAP2"),
        ):
            stub_anchor = cap.start.up(12)
            drawing.connect(cap.start, stub_anchor, net=net, shape="-")
            drawing.signal_stub(
                net,
                at=stub_anchor,
                side="Right",
                length=12,
                orient="Right",
                label=label,
            )

        led_supply = nets["LED_STATUS/SUPPLY"]
        led_signal = nets["LED_STATUS/SIGNAL"]
        led_gnd = nets["LED_STATUS/GND"]
        led_supply_port = drawing.power(
            "+3V3",
            net=led_supply,
            at=led_r.start.up(28),
            orient="Up",
        )
        drawing.connect(led_r.start, led_supply_port, net=led_supply, shape="-")
        drawing.connect(led_r.end, led_d.start, net=led_signal, shape="-")
        led_ground = drawing.ground(
            "GND",
            net=led_gnd,
            at=led_d.end.down(28),
            orient="Down",
        )
        drawing.connect(led_d.end, led_ground, net=led_gnd, shape="-")
