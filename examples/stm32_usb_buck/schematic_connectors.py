"""Connector-region authoring for the STM32 USB buck schematic."""

from __future__ import annotations

import volt

from .schematic_symbols import (
    _compact_connector_1x04_symbol,
    _compact_swd_symbol,
    _display_reference,
    _readable_usb_micro_b_symbol,
    _readable_usb_protection_symbol,
)
from .stm32_board import Stm32UsbBuckBoard


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
