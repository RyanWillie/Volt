"""Schematic projection for the STM32 USB buck benchmark."""

from __future__ import annotations

import volt

from .schematic_connectors import _author_connectors_region
from .schematic_mcu import _author_mcu_region
from .schematic_power import _author_power_region
from .schematic_symbols import DISPLAY_REFERENCES
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
