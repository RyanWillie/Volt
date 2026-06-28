"""Symbol definitions and display references for the STM32 USB buck schematic."""

from __future__ import annotations

import volt


DISPLAY_REFERENCES = {
    "VIN_SRC": "J1",
    "PWR/J": "J2",
    "PWR/F1": "F1",
    "PWR/FB1": "FB1",
    "PWR/U5": "U2",
    "PWR/L1": "L1",
    "PWR/DSW": "D1",
    "PWR/U3V3": "U3",
    "PWR/CIN": "C1",
    "PWR/CBST": "C2",
    "PWR/C5V": "C3",
    "PWR/REN_TOP": "R1",
    "PWR/REN_BOT": "R2",
    "PWR/RFB_TOP": "R3",
    "PWR/RFB_BOT": "R4",
    "PWR/C3V3": "C4",
    "PWR/FBVDDA": "FB2",
    "PWR/CVDDA": "C5",
    "U1": "U1",
    "SUPPORT/CVDD": "C6",
    "SUPPORT/CVCAP1": "C7",
    "SUPPORT/CVCAP2": "C8",
    "SUPPORT/RRESET": "R5",
    "SUPPORT/RBOOT": "R6",
    "SUPPORT/SWBOOT": "SW1",
    "SUPPORT/Y1": "Y1",
    "SUPPORT/CHSEIN": "C9",
    "SUPPORT/CHSEOUT": "C10",
    "LED_STATUS/R": "R7",
    "LED_STATUS/D": "D2",
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
