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


def _compact_connector_1x04_symbol(
    *, side: str = "left", pin_labels: bool = True
) -> volt.SchematicSymbolSpec:
    return volt.SchematicSymbolSpec.block(
        f"volt.examples.stm32_usb_buck:CompactConnector1x04:{side}:labels-{pin_labels}",
        pins=(
            volt.SchematicSymbolSpec.block_pin("1", 1, side=side, slot=1),
            volt.SchematicSymbolSpec.block_pin("2", 2, side=side, slot=2),
            volt.SchematicSymbolSpec.block_pin("3", 3, side=side, slot=3),
            volt.SchematicSymbolSpec.block_pin("4", 4, side=side, slot=4),
        ),
        width=20,
        height=26,
        lead_length=8,
        pin_pitch=7,
        pin_label_offset=3,
        side_layouts=(volt.SchematicSymbolSpec.side_layout(side, pad=0),),
        center_label="CONN",
        pin_labels=pin_labels,
        pin_numbers=False,
    )


def _compact_swd_symbol() -> volt.SchematicSymbolSpec:
    pin_rows = (
        ("VTref", 1, "VTref"),
        ("SWDIO", 2, "SWDIO"),
        ("GND", 3, "GND"),
        ("SWCLK", 4, "SWCLK"),
        ("GND", 5, "GND"),
        ("SWO", 6, "SWO"),
        ("NC", 7, "NC"),
        ("TDI", 8, "BOOT0"),
        ("GNDDetect", 9, "GNDDET"),
        ("nRESET", 10, "NRST"),
    )
    return volt.SchematicSymbolSpec.block(
        "volt.examples.stm32_usb_buck:CompactSWD10",
        pins=(
            volt.SchematicSymbolSpec.block_pin(name, number, side="left", slot=number, label=label)
            for name, number, label in pin_rows
        ),
        width=50,
        height=67,
        lead_length=8,
        pin_pitch=7,
        pin_label_offset=3,
        side_layouts=(volt.SchematicSymbolSpec.side_layout("left", pad=0),),
        pin_numbers=True,
        pin_number_offset=1,
        center_label="SWD",
    )


def _readable_usb_micro_b_symbol() -> volt.SchematicSymbolSpec:
    pin_rows = (
        ("VBUS", 1, "VBUS"),
        ("D-", 2, "D-"),
        ("D+", 3, "D+"),
        ("ID", 4, "ID"),
        ("GND", 5, "GND"),
        ("Shield", 6, "SHLD"),
    )
    return volt.SchematicSymbolSpec.block(
        "volt.examples.stm32_usb_buck:ReadableUSBMicroB",
        pins=(
            volt.SchematicSymbolSpec.block_pin(name, number, side="right", slot=number, label=label)
            for name, number, label in pin_rows
        ),
        width=38,
        height=45,
        lead_length=10,
        pin_pitch=8,
        pin_label_offset=3,
        side_layouts=(volt.SchematicSymbolSpec.side_layout("right", pad=0),),
        pin_numbers=True,
        pin_number_offset=1,
        center_label="USB",
    )


def _readable_usb_protection_symbol() -> volt.SchematicSymbolSpec:
    return volt.SchematicSymbolSpec.block(
        "volt.examples.stm32_usb_buck:ReadableUSBLC6",
        pins=(
            volt.SchematicSymbolSpec.block_pin("I/O2", 3, side="left", slot=1, label="D-"),
            volt.SchematicSymbolSpec.block_pin("I/O1", 1, side="left", slot=2, label="D+"),
            volt.SchematicSymbolSpec.block_pin("I/O3", 4, side="right", slot=1, label="D-"),
            volt.SchematicSymbolSpec.block_pin("I/O4", 6, side="right", slot=2, label="D+"),
            volt.SchematicSymbolSpec.block_pin("VBUS", 5, side="top", slot=2),
            volt.SchematicSymbolSpec.block_pin("GND", 2, side="bottom", slot=2),
        ),
        width=34,
        height=30,
        lead_length=10,
        pin_pitch=12,
        pin_label_offset=3,
        side_layouts=(
            volt.SchematicSymbolSpec.side_layout("top", pad=17, pin_pitch=1),
            volt.SchematicSymbolSpec.side_layout("bottom", pad=17, pin_pitch=1),
        ),
        center_label="ESD",
        pin_numbers=False,
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
        ("VBAT", 1, "left", 2),
        ("PC13", 2, "left", 4),
        ("PH0", 5, "left", 7),
        ("PH1", 6, "left", 9),
        ("NRST", 7, "left", 13),
        ("VDDA", 13, "left", 16),
        ("VCAP_1", 31, "left", 18, "VCAP1"),
        ("VDD", 19, "top", 1),
        ("VDD", 32, "top", 2),
        ("VDD", 48, "top", 3),
        ("VDD", 64, "top", 4),
        ("VSS", 18, "bottom", 1),
        ("VSSA", 12, "bottom", 2),
        ("VSS", 63, "bottom", 3),
        ("PA11", 44, "right", 5),
        ("PA12", 45, "right", 7),
        ("PA13", 46, "right", 9),
        ("PA14", 49, "right", 11),
        ("PB3", 55, "right", 12),
        ("BOOT0", 60, "right", 16),
        ("VCAP_2", 47, "right", 18, "VCAP2"),
    )
    pins = []
    for name, number, side, slot, *label in pin_layout:
        pins.append(
            volt.SchematicSymbolSpec.block_pin(
                name,
                number,
                side=side,
                slot=slot,
                label=label[0] if label else name,
            )
        )
    return volt.SchematicSymbolSpec.block(
        "volt.examples.stm32_usb_buck:STM32F405RGTxCompact",
        pins=tuple(pins),
        width=68,
        height=152,
        lead_length=14,
        pin_pitch=8,
        pin_label_offset=3,
        pin_number_offset=1,
        side_layouts=(
            volt.SchematicSymbolSpec.side_layout("top", pad=14, pin_pitch=12),
            volt.SchematicSymbolSpec.side_layout("bottom", pad=20, pin_pitch=16),
        ),
        center_label="STM32F405",
        pin_numbers=True,
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
        w=558,
        h=116,
        style={"border": "dashed"},
    )
    mcu_region = sheet.region(
        "STM32 Microcontroller",
        x=18,
        y=140,
        w=346,
        h=266,
        title="STM32 MCU",
        style={"border": "dashed"},
    )
    connectors_region = sheet.region(
        "Connectors and USB",
        x=370,
        y=140,
        w=208,
        h=216,
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
        drawing.move(dx=12, dy=42)
        power_root = drawing.here
        input_root = power_root
        input_connector_root = power_root.right(58).up(2)
        regulator_5v_root = power_root.right(150).up(4)
        regulator_3v3_root = power_root.right(318).up(4)
        analog_root = power_root.right(496).down(8)

        vin = drawing.place(
            board.components["VIN_SRC"],
            at=input_root,
            symbol=_external_supply_symbol(),
            reference_label=_display_reference(board.components["VIN_SRC"]),
        )
        pwr_j = drawing.place(
            pwr.component("J"),
            at=input_connector_root,
            symbol=_compact_connector_1x04_symbol(pin_labels=False),
            reference_label=_display_reference(pwr.component("J")),
        )

        u5 = drawing.place(
            pwr.component("U5"),
            at=regulator_5v_root,
            symbol=lib.AP1117_15.schematic_symbol,
            reference_label=_display_reference(pwr.component("U5")),
        )
        u3v3 = drawing.place(
            pwr.component("U3V3"),
            at=regulator_3v3_root,
            symbol=lib.AP1117_15.schematic_symbol,
            reference_label=_display_reference(pwr.component("U3V3")),
        )

        cin = drawing.two_terminal(
            pwr.component("CIN"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("CIN")),
        ).at(u5.VI.left(24)).down()
        c5v = drawing.two_terminal(
            pwr.component("C5V"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("C5V")),
        ).at(u5.VO.right(24)).down()
        c3v3 = drawing.two_terminal(
            pwr.component("C3V3"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("C3V3")),
        ).at(u3v3.VO.right(24)).down()
        cvdda = drawing.two_terminal(
            pwr.component("CVDDA"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("CVDDA")),
        ).at(analog_root).down()

        vin.label_value(loc="bottom", ofst=10, orient="Right")
        u5.label_value(loc="bottom", ofst=10, orient="Right")
        u3v3.label_value(loc="bottom", ofst=16, orient="Right")
        cin.label_value(loc="bottom", ofst=16, orient="Right")
        c5v.label_value(loc="bottom", ofst=10, orient="Right")
        c3v3.label_value(loc="bottom", ofst=10, orient="Right")
        cvdda.label_value(loc="bottom", ofst=10, orient="Right")

        drawing.power_stub("+12V", at=vin.OUT, net=nets["+12V"], side="Up", length=20)
        drawing.ground_stub("GND", at=vin.GND, net=nets["GND"], side="Down", length=24)

        pwr_in = nets["PWR/IN_12V"]
        pwr_5v = nets["PWR/OUT_5V"]
        pwr_3v3 = nets["PWR/OUT_3V3"]
        pwr_vdda = nets["PWR/VDDA"]
        pwr_gnd = nets["PWR/GND"]

        drawing.signal_tag(
            pwr_in,
            at=pwr_j[1],
            side="Up",
            length=10,
            label="+12V",
        )
        drawing.connect(cin.start, u5.VI, net=pwr_in, shape="-")
        drawing.net_label(pwr_in, at=cin.start.up(20), label="+12V")

        drawing.connect(u5.VO, c5v.start, net=pwr_5v, shape="-")
        drawing.connect(c5v.start, u3v3.VI, net=pwr_5v, shape="-")
        drawing.power_stub("+5V", at=c5v.start, net=pwr_5v, side="Up", length=22)

        drawing.connect(u3v3.VO, c3v3.start, net=pwr_3v3, shape="-")
        drawing.power_stub("+3V3", at=c3v3.start, net=pwr_3v3, side="Up", length=22)
        drawing.power_stub("VDDA", at=cvdda.start, net=pwr_vdda, side="Up", length=20)

        connector_ground = drawing.ground(
            "GND",
            net=pwr_gnd,
            at=pwr_j[3].down(30),
            orient="Down",
        )
        for anchor in (pwr_j[2], pwr_j[3], pwr_j[4]):
            drawing.connect(anchor, connector_ground, net=pwr_gnd, shape="|-")
        drawing.junction(pwr_gnd, at=connector_ground)

        regulator_5v_ground = drawing.ground(
            "GND",
            net=pwr_gnd,
            at=u5.GND.down(24),
            orient="Down",
        )
        for anchor in (cin.end, u5.GND, c5v.end):
            drawing.connect(anchor, regulator_5v_ground, net=pwr_gnd, shape="|-")
        drawing.junction(pwr_gnd, at=regulator_5v_ground)

        regulator_3v3_ground = drawing.ground(
            "GND",
            net=pwr_gnd,
            at=u3v3.GND.down(24),
            orient="Down",
        )
        for anchor in (u3v3.GND, c3v3.end):
            drawing.connect(anchor, regulator_3v3_ground, net=pwr_gnd, shape="|-")
        vdda_ground = drawing.ground("GND", net=pwr_gnd, at=cvdda.end.down(18), orient="Down")
        drawing.connect(cvdda.end, vdda_ground, net=pwr_gnd, shape="-")


def _author_connectors_region(
    region: volt.SchematicRegion,
    board: Stm32UsbBuckBoard,
    nets: dict[str, volt.Net],
) -> None:
    usb = board.modules["USB"]
    with region.drawing(unit=20) as drawing:
        drawing.move(dx=22, dy=34)
        connector_root = drawing.here

        swd = drawing.place(
            board.components["J2"],
            at=connector_root.right(72),
            symbol=_compact_swd_symbol(),
            reference_label=_display_reference(board.components["J2"]),
        )
        gpio = drawing.place(
            board.components["J3"],
            at=connector_root.right(156).down(78),
            symbol=_compact_connector_1x04_symbol(),
            reference_label=_display_reference(board.components["J3"]),
        )
        usb_j = drawing.place(
            usb.component("J1"),
            at=connector_root.down(98),
            symbol=_readable_usb_micro_b_symbol(),
            reference_label=_display_reference(usb.component("J1")),
        )
        usb_esd = drawing.place(
            usb.component("U1"),
            at=usb_j.VBUS.right(16).down(2),
            symbol=_readable_usb_protection_symbol(),
            reference_label=_display_reference(usb.component("U1")),
        )

        usb_j.label_value(loc="bottom", ofst=12, orient="Right")
        usb_esd.label_value(loc="bottom", ofst=12, orient="Right")
        swd.label_value(loc="bottom", ofst=10, orient="Right")
        gpio.label_value(loc="bottom", ofst=10, orient="Right")

        drawing.connect(usb_j.VBUS, usb_esd.VBUS, net=nets["USB/VBUS"], shape="-|")
        drawing.connect(usb_j["D+"], usb_esd["I/O1"], net=nets["USB/USB_DP"], shape="-|")
        drawing.connect(usb_j["D-"], usb_esd["I/O2"], net=nets["USB/USB_DM"], shape="-|")
        drawing.signal_tags(
            (
                (nets["USB/MCU_USB_DP"], usb_esd["I/O4"], "USB D+"),
                (nets["USB/MCU_USB_DM"], usb_esd["I/O3"], "USB D-"),
            ),
            length=8,
        )

        usb_gnd = nets["USB/GND"]
        usb_ground = drawing.ground(
            "GND",
            net=usb_gnd,
            at=usb_esd.GND.down(32),
            orient="Down",
        )
        for anchor in (usb_j.GND, usb_j.Shield, usb_esd.GND):
            drawing.connect(anchor, usb_ground, net=usb_gnd, shape="|-")
        drawing.junction(usb_gnd, at=usb_ground)

        for anchor in (swd.VTref, gpio[1]):
            drawing.power_stub("+3V3", at=anchor, net=nets["+3V3"], side="Up", length=24)
        for anchor in (swd[3], swd[5], swd[9]):
            drawing.ground_stub("GND", at=anchor, net=nets["GND"], side="Left", length=52)
        drawing.ground_stub("GND", at=gpio[4], net=nets["GND"], side="Down", length=18)
        drawing.signal_tags(
            (
                (nets["SWDIO"], swd.SWDIO, "SWDIO"),
                (nets["SWCLK"], swd.SWCLK, "SWCLK"),
                (nets["SWO"], swd.SWO, "SWO"),
                (nets["NRST"], swd.nRESET, "NRST"),
                (nets["BOOT0"], swd.TDI, "BOOT0"),
            ),
            length=20,
        )
        drawing.signal_tag(
            nets["BOOT0"],
            at=gpio[2],
            side="Left",
            length=10,
            label="BOOT0",
        )
        drawing.no_connect(usb_j.ID, reason="USB ID not used")
        drawing.no_connect(swd.NC, orient="Left", offset=8, reason="reserved debug pin not populated")
        drawing.no_connect(gpio[3], orient="Left", offset=34, reason="reserved GPIO header pin not populated")


def _author_mcu_region(
    region: volt.SchematicRegion,
    board: Stm32UsbBuckBoard,
    nets: dict[str, volt.Net],
) -> None:
    support = board.modules["SUPPORT"]
    led = board.modules["LED_STATUS"]
    with region.drawing(unit=20) as drawing:
        drawing.move(dx=104, dy=64)
        stm32 = drawing.place(
            board.components["U1"],
            symbol=_compact_stm32_symbol(),
            reference_label=_display_reference(board.components["U1"]),
        )

        decoupling_start = stm32.VBAT.left(66).up(50)
        cvdd_anchor, cvcap1_anchor, cvcap2_anchor = drawing.stack(
            count=3,
            direction="Right",
            pitch=30,
            at=decoupling_start,
        )
        cvdd = drawing.two_terminal(
            support.component("CVDD"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(support.component("CVDD")),
        ).at(cvdd_anchor).down()
        cvcap1 = drawing.two_terminal(
            support.component("CVCAP1"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(support.component("CVCAP1")),
        ).at(cvcap1_anchor).down()
        cvcap2 = drawing.two_terminal(
            support.component("CVCAP2"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(support.component("CVCAP2")),
        ).at(cvcap2_anchor).down()

        rreset = drawing.two_terminal(
            support.component("RRESET"),
            symbol=TWO_TERMINAL_RESISTOR,
            reference_label=_display_reference(support.component("RRESET")),
        ).at(stm32.NRST.left(76).down(38)).down(1.1)

        drawing.move_from(stm32.BOOT0.right(42).down(18), direction="Right")
        swboot = drawing.place(
            support.component("SWBOOT"),
            orient="Right",
            symbol=lib.SPDT_SWITCH.schematic_symbol,
            reference_label=_display_reference(support.component("SWBOOT")),
        )
        rboot = drawing.two_terminal(
            support.component("RBOOT"),
            symbol=TWO_TERMINAL_RESISTOR,
            reference_label=_display_reference(support.component("RBOOT")),
        ).at(swboot.C.right(8)).down(1.1)

        drawing.move_from(stm32.PH0.left(70).up(2), direction="Right")
        crystal = drawing.place(
            support.component("Y1"),
            symbol=_vertical_hse_crystal_symbol(),
            reference_label=_display_reference(support.component("Y1")),
        )
        chsein = drawing.two_terminal(
            support.component("CHSEIN"),
            symbol=TWO_TERMINAL_CAPACITOR,
        ).at(crystal[1].left(66)).down()
        chseout = drawing.two_terminal(
            support.component("CHSEOUT"),
            symbol=TWO_TERMINAL_CAPACITOR,
        ).at(crystal[3].left(54)).down()

        led_r = drawing.two_terminal(
            led.component("R"),
            symbol=TWO_TERMINAL_RESISTOR,
            reference_label=_display_reference(led.component("R")),
        ).at(stm32.PA11.right(60).up(44)).right()
        led_d = drawing.two_terminal(
            led.component("D"),
            symbol=_indicator_led_symbol(),
            reference_label=_display_reference(led.component("D")),
        ).at(led_r.end.right(6)).right(1.2)

        stm32.label_value(loc="bottom", ofst=12, orient="Right")
        cvdd.label_value(loc="bottom", ofst=8, orient="Right")
        cvcap1.label_value(loc="bottom", ofst=8, orient="Right")
        cvcap2.label_value(loc="bottom", ofst=8, orient="Right")
        rreset.label_value(loc="bottom", ofst=10, orient="Right")
        rboot.label_value(loc="bottom", ofst=10, orient="Right")
        swboot.label_value(loc="bottom", ofst=10, orient="Right")
        crystal.label_value(loc="bottom", ofst=22, orient="Right")
        chsein.label(
            _display_reference(support.component("CHSEIN")),
            name="reference",
            loc="top",
            ofst=8,
        )
        chsein.label_value(loc="bottom", ofst=8, orient="Right")
        chseout.label(
            _display_reference(support.component("CHSEOUT")),
            name="reference",
            loc="top",
            ofst=8,
        )
        chseout.label_value(loc="bottom", ofst=8, orient="Right")
        led_r.label_value(loc="bottom", ofst=10, orient="Right")
        led_d.label_value(loc="bottom", ofst=8, orient="Right")

        mcu_3v3 = nets["+3V3"]
        mcu_power = drawing.power("+3V3", net=mcu_3v3, at=stm32.center.up(98), orient="Up")
        for anchor in (*stm32.pins("VDD"), stm32.VBAT):
            drawing.connect(anchor, mcu_power, net=mcu_3v3, shape="|-")
        mcu_vdda = drawing.power("VDDA", net=nets["VDDA"], at=stm32.VDDA.left(16), orient="Left")
        drawing.connect(stm32.VDDA, mcu_vdda, net=nets["VDDA"], shape="-")

        mcu_ground = drawing.ground(
            "GND",
            net=nets["GND"],
            at=stm32.VSSA.down(20),
            orient="Down",
        )
        for anchor in (*stm32.pins("VSS"), stm32.VSSA):
            drawing.connect(anchor, mcu_ground, net=nets["GND"], shape="|-")
        drawing.junction(nets["GND"], at=mcu_ground)

        drawing.signal_tags(
            (
                (nets["MCU_USB_DM"], stm32.PA11, "USB D-"),
                (nets["MCU_USB_DP"], stm32.PA12, "USB D+"),
                (nets["SWDIO"], stm32.PA13, "SWDIO"),
                (nets["SWCLK"], stm32.PA14, "SWCLK"),
                (nets["SWO"], stm32.PB3, "SWO"),
                (nets["BOOT0"], stm32.BOOT0, "BOOT0"),
            ),
            length=10,
        )
        for net, anchor, label, tag_length, tag_orient in (
            (nets["STATUS_LED"], stm32.PC13, "LED", 16, None),
            (nets["HSE_IN"], stm32.PH0, "HSE IN", 6, None),
            (nets["HSE_OUT"], stm32.PH1, "HSE OUT", 6, None),
            (nets["VCAP_1"], stm32.VCAP_1, "VCAP1", 16, None),
        ):
            drawing.signal_tag(
                net,
                at=anchor,
                side="Left",
                length=tag_length,
                label=label,
                orient=tag_orient,
            )
        drawing.signal_tag(
            nets["NRST"],
            at=stm32.NRST,
            side="Left",
            length=8,
            label="NRST",
        )
        drawing.signal_tag(
            nets["VCAP_2"],
            at=stm32.VCAP_2,
            side="Right",
            length=10,
            label="VCAP2",
        )

        support_vdd = nets["SUPPORT/VDD"]
        support_gnd = nets["SUPPORT/GND"]
        support_reset = nets["SUPPORT/NRST"]
        support_boot = nets["SUPPORT/BOOT0"]
        support_hse_in = nets["SUPPORT/HSE_IN"]
        support_hse_out = nets["SUPPORT/HSE_OUT"]

        drawing.power_stub("+3V3", at=cvdd.start, net=support_vdd, side="Up", length=16)
        drawing.power_stub(
            "+3V3",
            at=rreset.start,
            net=support_vdd,
            side="Up",
            length=6,
        )
        drawing.power_stub("+3V3", at=swboot.A, net=support_vdd, side="Left", length=12, orient="Up")

        decoupling_ground = drawing.ground("GND", net=support_gnd, at=cvcap1.end.down(18), orient="Down")
        for cap in (cvdd, cvcap1, cvcap2):
            drawing.connect(cap.end, decoupling_ground, net=support_gnd, shape="|-")
        drawing.junction(support_gnd, at=decoupling_ground)

        oscillator_ground = drawing.ground("GND", net=support_gnd, at=crystal[4].down(22), orient="Down")
        for anchor in (crystal[2], crystal[4], chsein.end, chseout.end):
            drawing.connect(anchor, oscillator_ground, net=support_gnd, shape="|-")
        drawing.junction(support_gnd, at=oscillator_ground)

        drawing.ground_stub("GND", at=rboot.end, net=support_gnd, side="Down", length=6, orient="Down")
        drawing.ground_stub("GND", at=swboot.B, net=support_gnd, side="Down", length=6, orient="Down")

        drawing.signal_tag(
            support_reset,
            at=rreset.end,
            side="Right",
            length=10,
            label="NRST",
        )
        drawing.connect(swboot.C, rboot.start, net=support_boot, shape="-")
        drawing.signal_tag(
            support_boot,
            at=rboot.start,
            side="Up",
            length=10,
            label="BOOT0",
        )

        drawing.connect(crystal[1], chsein.start, net=support_hse_in, shape="-")
        drawing.connect(crystal[3], chseout.start, net=support_hse_out, shape="-")
        drawing.net_label(
            support_hse_in,
            at=crystal[1].right(8).up(8),
            label="HSE IN",
            orient="Right",
        )
        drawing.net_label(
            support_hse_out,
            at=crystal[3].right(8).down(8),
            label="HSE OUT",
            orient="Right",
        )
        for cap, net, label in (
            (cvcap1, nets["SUPPORT/VCAP_1"], "VCAP1"),
            (cvcap2, nets["SUPPORT/VCAP_2"], "VCAP2"),
        ):
            stub_anchor = cap.start.up(12)
            drawing.connect(cap.start, stub_anchor, net=net, shape="-")
            drawing.signal_tag(
                net,
                at=stub_anchor,
                side="Right",
                length=12,
                label=label,
            )

        led_supply = nets["LED_STATUS/SUPPLY"]
        led_signal = nets["LED_STATUS/SIGNAL"]
        led_gnd = nets["LED_STATUS/GND"]
        drawing.power_stub("+3V3", at=led_r.start, net=led_supply, side="Up", length=20)
        drawing.connect(led_r.end, led_d.start, net=led_signal, shape="-")
        drawing.ground_stub("GND", at=led_d.end, net=led_gnd, side="Down", length=28)
